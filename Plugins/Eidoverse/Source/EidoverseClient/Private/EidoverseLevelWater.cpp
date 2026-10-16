// The world's one water level, natively (client/lib/water.js): a translucent sheet at the level over
// every place the ground is lower, built from the server's analysis on the terrain grid (render-scene
// environment.water: wet vertices as [index, kind, flowX, flowZ, waveSpeed, toShoreX, toShoreZ, shore]).
// Waves are shared/waves.js surfaceOffset, in M_EidoverseLevelWater (WPO + analytic normal) and here
// for gameplay (wading, swimming, floating), on the shared wave clock.
#include "EidoverseScene.h"
#include "EidoverseBridge.h"
#include "ProceduralMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialParameterCollection.h"
#include "Kismet/KismetMaterialLibrary.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

namespace
{
    constexpr double WavePeriod = 600;
    double Cyc(double W) { const double T = 2 * UE_DOUBLE_PI; return T * FMath::Max(1.0, FMath::RoundToDouble(W * WavePeriod / T)) / WavePeriod; }
    double Sm(double A, double B, double X) { const double T = FMath::Clamp((X - A) / (B - A), 0.0, 1.0); return T * T * (3 - 2 * T); }
}

double AEidoverseScene::SurfaceOffset(const FLevelWaterSample& W, double X, double Z, double T)
{
    if (W.Kind < 0) return 0;
    if (W.Kind == 2 && !W.ToShore.IsNearlyZero())
    {
        const double Sx = W.ToShore.X, Sz = W.ToShore.Y, O = X * Sx + Z * Sz, Oc = Z * Sx - X * Sz;
        const double P1 = O * .2 - T * Cyc(FMath::Sqrt(9.8 * .2)) + FMath::Sin(Oc * .045) * 1.4;
        const double P2 = O * .33 + Oc * .12 - T * Cyc(FMath::Sqrt(9.8 * .33));
        const double Amp = .2 + (.6 - .2) * Sm(-1.0, 5.0, W.Depth);
        auto Crest = [](double P) { return FMath::Sin(P) + .18 * FMath::Sin(2 * P); };
        return Amp * Crest(P1) + Amp * .45 * Crest(P2);
    }
    if (W.Kind == 1 && !W.Flow.IsNearlyZero()) return .025 * FMath::Sin((X * W.Flow.X + Z * W.Flow.Y - T * W.WaveSpeed) * 1.4);
    return .012 * FMath::Sin(X * .9 + Z * .4 - T * Cyc(.8));
}

double AEidoverseScene::LevelWaveTime() const
{
    const double Ms = ServerNowMs();
    return FMath::Fmod(FMath::Fmod(Ms, WavePeriod * 1000) + WavePeriod * 1000, WavePeriod * 1000) / 1000;
}

bool AEidoverseScene::LevelWaterSample(double X, double Z, FLevelWaterSample& Out) const
{
    const FLevelWater& L = LevelWater;
    if (!L.bOn || L.N < 1) return false;
    const int32 n = L.N + 1; const double Cell = L.Size / L.N;
    const double U = FMath::Clamp((X + L.Size / 2) / Cell, 0.0, double(L.N)), V = FMath::Clamp((Z + L.Size / 2) / Cell, 0.0, double(L.N));
    const int32 I = FMath::Min(L.N - 1, FMath::FloorToInt(U)), J = FMath::Min(L.N - 1, FMath::FloorToInt(V));
    const double Fu = U - I, Fv = V - J;
    const int32 Ks[4] = {J * n + I, J * n + I + 1, (J + 1) * n + I, (J + 1) * n + I + 1};
    const double Ws[4] = {(1 - Fu) * (1 - Fv), Fu * (1 - Fv), (1 - Fu) * Fv, Fu * Fv};
    double D = 0; for (int32 C = 0; C < 4; ++C) D += (L.Level - L.Heights[Ks[C]]) * Ws[C];
    if (!(D > 0)) return false;
    int32 Top = -1; for (int32 C = 0; C < 4; ++C) if (L.Kind[Ks[C]] >= 0 && (Top < 0 || Ws[C] > Ws[Top])) Top = C;
    if (Top < 0) return false;
    Out = FLevelWaterSample(); Out.Kind = L.Kind[Ks[Top]]; Out.Depth = D;
    double W = 0; FVector2D Dir(0);
    for (int32 C = 0; C < 4; ++C)
    {
        const int32 K = Ks[C]; if (L.Kind[K] != Out.Kind) continue;
        W += Ws[C]; Out.Shore += L.Shore[K] * Ws[C]; Out.WaveSpeed += L.WaveSpeed[K] * Ws[C]; Dir += L.Dir[K] * Ws[C];
    }
    if (W > 0) { Out.Shore /= W; Out.WaveSpeed /= W; }
    Dir = Dir.GetSafeNormal();
    if (Out.Kind == 1) Out.Flow = Dir; else if (Out.Kind == 2) Out.ToShore = Dir;
    return true;
}

double AEidoverseScene::LevelWaterSurface(double X, double Z) const
{
    FLevelWaterSample W;
    if (!LevelWater.bOn) return -1e9;
    return LevelWater.Level + (LevelWaterSample(X, Z, W) ? SurfaceOffset(W, X, Z, LevelWaveTime()) : 0.0);
}

FString AEidoverseScene::DescribeLevelWater(FVector P) const
{
    FLevelWaterSample W;
    if (!LevelWaterSample(P.X, P.Z, W)) return LevelWater.bOn ? TEXT("dry") : TEXT("no water");
    const TCHAR* Kinds[] = {TEXT("still"), TEXT("river"), TEXT("ocean")};
    return FString::Printf(TEXT("%s %.2fm deep, shore %.1fm, surface %.3f"), Kinds[FMath::Clamp(W.Kind, 0, 2)], W.Depth, W.Shore, LevelWaterSurface(P.X, P.Z));
}

void AEidoverseScene::BuildLevelWater(const TSharedPtr<FJsonObject>& Water, const TArray<TSharedPtr<FJsonValue>>& Heights, int32 N, double Size, const FVector& Origin)
{
    if (LevelWaterMesh) { LevelWaterMesh->DestroyComponent(); LevelWaterMesh = nullptr; }
    LevelWater = FLevelWater();
    double Level = 0; if (!Water || !Water->TryGetNumberField(TEXT("level"), Level) || !FMath::IsFinite(Level)) return;
    FLevelWater& L = LevelWater;
    const int32 n = N + 1, Count = n * n;
    L.bOn = true; L.Level = Level; L.Size = Size; L.N = N;
    L.Heights.SetNumUninitialized(Count); for (int32 K = 0; K < Count; ++K) { const double Hy = Heights[K]->AsNumber(); L.Heights[K] = FMath::IsFinite(Hy) ? Hy : 0; }
    L.Kind.Init(-1, Count); L.Shore.Init(0, Count); L.WaveSpeed.Init(0, Count); L.Dir.Init(FVector2D::ZeroVector, Count);
    const TArray<TSharedPtr<FJsonValue>>* Cells = nullptr;
    if (Water->TryGetArrayField(TEXT("cells"), Cells))
        for (int32 C = 0; C + 7 < Cells->Num(); C += 8)
        {
            const int32 K = int32((*Cells)[C]->AsNumber()); if (K < 0 || K >= Count) continue;
            L.Kind[K] = int8(FMath::Clamp(int32((*Cells)[C + 1]->AsNumber()), 0, 2));
            const bool bOcean = L.Kind[K] == 2;
            L.Dir[K] = bOcean ? FVector2D((*Cells)[C + 5]->AsNumber(), (*Cells)[C + 6]->AsNumber()) : FVector2D((*Cells)[C + 2]->AsNumber(), (*Cells)[C + 3]->AsNumber());
            L.WaveSpeed[K] = float((*Cells)[C + 4]->AsNumber()); L.Shore[K] = float((*Cells)[C + 7]->AsNumber());
            ++L.WetCount;
        }
    if (!L.WetCount) return;
    // The sheet: every vertex at the level, triangles only where water is (a cell with any wet corner).
    TArray<FVector> P; TArray<int32> Tris; TArray<FVector> Normals; TArray<FVector2D> UV0, UV1, UV2; TArray<FLinearColor> Colors;
    P.Reserve(Count); Normals.Init(FVector::UpVector, Count);
    for (int32 J = 0; J < n; ++J) for (int32 I = 0; I < n; ++I)
    {
        const int32 K = J * n + I;
        P.Add(AEidoverseBridge::FromWirePosition(FVector((double(I) / N - .5) * Size, Level, (double(J) / N - .5) * Size)));
        UV0.Add(FVector2D(double(I) / N, double(J) / N));
        UV1.Add(L.Dir[K]); UV2.Add(FVector2D(L.WaveSpeed[K], 0));
        const double Depth = FMath::Max(0.0, Level - L.Heights[K]);
        Colors.Add(FLinearColor(float(FMath::Min(Depth / 10, 1.0)), FMath::Min(L.Shore[K] / 20.f, 1.f), L.Kind[K] < 0 ? 0.f : L.Kind[K] / 2.f, 1));
    }
    for (int32 J = 0; J < N; ++J) for (int32 I = 0; I < N; ++I)
    {
        const int32 A = J * n + I, B = A + 1, C = A + n, D = C + 1;
        if (L.Kind[A] < 0 && L.Kind[B] < 0 && L.Kind[C] < 0 && L.Kind[D] < 0) continue;
        Tris.Append({A, C, B, B, C, D});
    }
    LevelWaterMesh = NewObject<UProceduralMeshComponent>(this); AddInstanceComponent(LevelWaterMesh);
    LevelWaterMesh->SetupAttachment(RootComponent); LevelWaterMesh->SetWorldLocation(Origin);
    LevelWaterMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision); LevelWaterMesh->SetCastShadow(false);
    LevelWaterMesh->RegisterComponent();
    LevelWaterMesh->CreateMeshSection_LinearColor(0, P, Tris, Normals, UV0, UV1, UV2, {}, Colors, {}, false);
    // Waves reach up to ~0.9 m: keep the culling bounds honest.
    LevelWaterMesh->SetBoundsScale(1.5f);
    if (UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, TEXT("/Eidoverse/FX/M_EidoverseLevelWater.M_EidoverseLevelWater")))
    {
        auto* MI = UMaterialInstanceDynamic::Create(Base, LevelWaterMesh);
        FString Hex; Water->TryGetStringField(TEXT("color"), Hex);
        FColor C = FColor(0x1d, 0x5a, 0x6e); if (Hex.StartsWith(TEXT("#")) && Hex.Len() == 7) C = FColor::FromHex(Hex);
        MI->SetVectorParameterValue(TEXT("Color"), FLinearColor::FromSRGBColor(C));
        double Opacity = .82; Water->TryGetNumberField(TEXT("opacity"), Opacity);
        MI->SetScalarParameterValue(TEXT("Opacity"), float(FMath::Clamp(Opacity, .1, 1.0)));
        LevelWaterMesh->SetMaterial(0, MI);
    }
}

void AEidoverseScene::TickLevelWater()
{
    if (!LevelWater.bOn) return;
    if (!LevelWaterCollection) LevelWaterCollection = LoadObject<UMaterialParameterCollection>(nullptr, TEXT("/Eidoverse/FX/MPC_EidoverseWater.MPC_EidoverseWater"));
    if (!LevelWaterCollection) return;
    UKismetMaterialLibrary::SetScalarParameterValue(this, LevelWaterCollection, TEXT("WaveT"), float(LevelWaveTime()));
    UKismetMaterialLibrary::SetScalarParameterValue(this, LevelWaterCollection, TEXT("OriginX"), float(SceneOrigin.X));
    UKismetMaterialLibrary::SetScalarParameterValue(this, LevelWaterCollection, TEXT("OriginY"), float(SceneOrigin.Y));
}
