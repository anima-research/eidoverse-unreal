// Particles (client/lib/emitters.js over the upstream eidoverse/particles.js): a
// closed-form sprite law per particle, seeded with mulberry32 so every client draws
// the same field. Camera-facing quads on one instanced component per emitter.
#include "EidoverseScene.h"
#include "EidoverseSettings.h"
#include "EidoverseBridge.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonSerializer.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/Texture2D.h"
#include "Kismet/GameplayStatics.h"

struct FEidoPreset { int32 Count; float Size, Life; FVector Gravity; float Area, Up, Spread; uint32 Color; bool bAdditive; float Grow, Wobble, Opacity, Twinkle, Swirl; bool bFlame; float Converge, Rotate; };

namespace
{
    const TMap<FString, FEidoPreset>& Presets()
    {
        static const TMap<FString, FEidoPreset> P = {
            {TEXT("sparks"), {220, .06f, .9f, {0, -4, 0}, .15f, 2.2f, 1.6f, 0xffd27f, true, -.8f, 0, 1, 0, 0, false, 0, 0}},
            {TEXT("embers"), {140, .05f, 2.6f, {0, .5f, 0}, .4f, .7f, .5f, 0xff7a30, true, -.5f, .25f, 1, .4f, 0, false, 0, 0}},
            {TEXT("smoke"), {70, .7f, 4.5f, {0, .45f, 0}, .3f, .5f, .3f, 0x8a8a8a, false, 1.8f, .35f, .35f, 0, 0, false, 0, 0}},
            {TEXT("dust"), {160, .04f, 6, {0, .02f, 0}, 3, .05f, .2f, 0xbcb2a0, false, .2f, .5f, .4f, .3f, 0, false, 0, 0}},
            {TEXT("snow"), {320, .05f, 8, {0, -.7f, 0}, 6, 0, .1f, 0xffffff, false, 0, .55f, .9f, 0, 0, false, 0, 0}},
            {TEXT("magic"), {180, .08f, 2.4f, {0, .6f, 0}, .5f, .4f, .6f, 0x9b5cff, true, -.3f, .4f, 1, .7f, 1.4f, false, 0, 0}},
            {TEXT("stars"), {420, .07f, 3, {0, 0, 0}, 22, 0, 0, 0xffffff, true, 0, 0, 1, 1, 0, false, 0, 0}},
            {TEXT("muzzle"), {48, .45f, .22f, {0, 0, 0}, .05f, .2f, 2.4f, 0xfff0c0, true, -1.4f, 0, 1, 0, 0, false, 0, 0}},
            {TEXT("fire"), {150, .5f, .55f, {0, .5f, 0}, .1f, .55f, .18f, 0xffffff, true, 0, .15f, 1, .25f, 0, true, .85f, .35f}},
        };
        return P;
    }
    // mulberry32, as emitters.js seeds Math.random for the build.
    struct FMulberry { uint32 S; double Next() { S += 0x6D2B79F5u; uint32 T = S; T = (T ^ (T >> 15)) * (T | 1u); T ^= T + (T ^ (T >> 7)) * (T | 61u); return double(T ^ (T >> 14)) / 4294967296.0; } };
    FLinearColor Hex(uint32 C) { return FLinearColor::FromSRGBColor(FColor((C >> 16) & 255, (C >> 8) & 255, C & 255)); }
}

struct FEidoParticleEmitter
{
    FEidoPreset P;
    FVector Origin = FVector::ZeroVector, Area = FVector::ZeroVector;
    float Speed = 1;
    int32 Count = 0;
    TArray<FVector> Emit, Vel, Seed;
    TWeakObjectPtr<UInstancedStaticMeshComponent> Ism;
    TWeakObjectPtr<UMaterialInstanceDynamic> Material;
    FString Texture;
    bool bTextureApplied = false;
};

void AEidoverseScene::TickParticles(float Dt)
{
    FVector Eye = SceneOrigin; FRotator View;
    APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0);
    if (PC) PC->GetPlayerViewPoint(Eye, View);
    const FVector CamRight = FRotationMatrix(View).GetUnitAxis(EAxis::Y), CamUp = FRotationMatrix(View).GetUnitAxis(EAxis::Z);
    const double T = GetWorld()->GetTimeSeconds();
    for (auto& Pair : Items)
    {
        FEidoverseSceneItem& E = Pair.Value;
        if (!E.Actor.IsValid()) continue;
        FString Key;
        if (E.Particles) { auto W = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Key); FJsonSerializer::Serialize(E.Particles.ToSharedRef(), W); }
        if (Key != E.EmitterKey)
        {
            if (E.Emitter && E.Emitter->Ism.IsValid()) E.Emitter->Ism->DestroyComponent();
            E.Emitter.Reset(); E.EmitterKey = Key;
            FString Preset; if (E.Particles) E.Particles->TryGetStringField(TEXT("preset"), Preset);
            const FEidoPreset* Base = Presets().Find(Preset);
            if (!Base) continue;
            auto Em = MakeShared<FEidoParticleEmitter>();
            Em->P = *Base;
            auto Num = [&E](const TCHAR* K, float D) { double V; return E.Particles->TryGetNumberField(K, V) && FMath::IsFinite(V) ? float(V) : D; };
            Em->P.Size = Num(TEXT("size"), Base->Size); Em->P.Opacity = Num(TEXT("opacity"), Base->Opacity);
            Em->P.Life = FMath::Max(.05f, Num(TEXT("lifetime"), Base->Life)); Em->Speed = Num(TEXT("speed"), 1);
            Em->Count = FMath::Clamp(int32(Num(TEXT("count"), Base->Count)), 1, 600);
            // resolvedCount: the emitter's own quality and my particle tier, whichever is lower.
            {
                FString Q; E.Particles->TryGetStringField(TEXT("quality"), Q);
                const float Own = Q == TEXT("med") ? .5f : Q == TEXT("low") ? .25f : 1.f;
                Em->Count = FMath::Max(1, FMath::RoundToInt(Em->Count * FMath::Min(Own, EidoSettings::ParticleScale())));
            }
            const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
            if (E.Particles->TryGetArrayField(TEXT("area"), Arr) && Arr->Num() == 3) Em->Area = FVector((*Arr)[0]->AsNumber(), (*Arr)[1]->AsNumber(), (*Arr)[2]->AsNumber());
            else Em->Area = FVector(Num(TEXT("area"), Base->Area));
            if (E.Particles->TryGetArrayField(TEXT("origin"), Arr) && Arr->Num() == 3) Em->Origin = FVector((*Arr)[0]->AsNumber(), (*Arr)[1]->AsNumber(), (*Arr)[2]->AsNumber());
            E.Particles->TryGetStringField(TEXT("texture"), Em->Texture);
            // The build: 9 draws per particle in particles.js order, from the emitter's seed.
            double SeedV = 0; E.Particles->TryGetNumberField(TEXT("seed"), SeedV);
            FMulberry R{uint32(int64(SeedV))};
            auto Rand = [&R](double A, double B) { return A + R.Next() * (B - A); };
            for (int32 I = 0; I < Em->Count; ++I)
            {
                const double Th = Rand(0, 2 * PI), Ph = FMath::Acos(Rand(-1, 1)), Rr = FMath::Pow(R.Next(), 1. / 3);
                Em->Emit.Add(FVector(FMath::Sin(Ph) * FMath::Cos(Th) * Rr * Em->Area.X, FMath::Cos(Ph) * Rr * .6 * Em->Area.Y, FMath::Sin(Ph) * FMath::Sin(Th) * Rr * Em->Area.Z));
                const double Vx = Rand(-1, 1) * Em->P.Spread, Vy = Em->P.Up + Rand(-.2, .2) * Em->P.Spread, Vz = Rand(-1, 1) * Em->P.Spread;
                Em->Vel.Add(FVector(Vx, Vy, Vz));
                const double S0 = R.Next(), S1 = Rand(.5, 1.5) * Em->P.Wobble, S2 = Rand(.7, 1.3);
                Em->Seed.Add(FVector(S0, S1, S2));
            }
            auto* Ism = NewObject<UInstancedStaticMeshComponent>(E.Actor.Get());
            E.Actor->AddInstanceComponent(Ism); Ism->SetupAttachment(E.Actor->GetRootComponent());
            Ism->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Plane.Plane")));
            Ism->SetCollisionEnabled(ECollisionEnabled::NoCollision); Ism->SetCastShadow(false); Ism->NumCustomDataFloats = 4;
            Ism->SetTranslucentSortPriority(10); Ism->bAffectDistanceFieldLighting = false; Ism->SetVisibleInRayTracing(false);
            if (UMaterialInterface* M = LoadObject<UMaterialInterface>(nullptr, Em->P.bAdditive ? TEXT("/Eidoverse/FX/M_EidoverseParticleAdd.M_EidoverseParticleAdd") : TEXT("/Eidoverse/FX/M_EidoverseParticleAlpha.M_EidoverseParticleAlpha")))
            { auto* MI = UMaterialInstanceDynamic::Create(M, Ism); Ism->SetMaterial(0, MI); Em->Material = MI; }
            Ism->RegisterComponent();
            TArray<FTransform> Init; Init.Init(FTransform(FQuat::Identity, FVector::ZeroVector, FVector::ZeroVector), Em->Count);
            Ism->AddInstances(Init, false);
            Em->Ism = Ism;
            E.Emitter = Em;
        }
        FEidoParticleEmitter* Em = E.Emitter.Get();
        if (!Em || !Em->Ism.IsValid()) continue;
        UInstancedStaticMeshComponent* Ism = Em->Ism.Get();
        if (FVector::DistSquared(Eye, E.Actor->GetActorLocation()) > 15000. * 15000.) { Ism->SetVisibility(false); continue; }
        Ism->SetVisibility(true);
        if (!Em->bTextureApplied && !Em->Texture.IsEmpty() && Em->Material.IsValid())
        {
            bool bPending = false;
            if (UTexture2D* Tex = LibraryTexture(Em->Texture, false, bPending)) { Em->Material->SetTextureParameterValue(TEXT("Sprite"), Tex); Em->Material->SetScalarParameterValue(TEXT("UseSprite"), 1); Em->bTextureApplied = true; }
            else if (!bPending) Em->bTextureApplied = true;   // failed: the preset's own soft dot
        }
        const FEidoPreset& P = Em->P;
        const float Cyc = Em->Speed / P.Life;
        const FLinearColor Base = Hex(P.Color);
        const FTransform& ActorT = E.Actor->GetActorTransform();
        const FQuat Facing = ActorT.GetRotation().Inverse() * FRotationMatrix::MakeFromYZ(CamRight, CamUp).ToQuat();
        TArray<FTransform> Xf; Xf.SetNum(Em->Count);
        for (int32 I = 0; I < Em->Count; ++I)
        {
            const FVector& Sd = Em->Seed[I];
            const double TL = FMath::Frac(T * Cyc + Sd.X), Age = TL * P.Life;
            const FVector Grav = P.Gravity * (.5 * Age * Age);
            const double A2 = T * 1.2 + Sd.X * 6.2832;
            const FVector Drift(FMath::Sin(A2) * Sd.Y, FMath::Cos(A2 * .7) * Sd.Y * .4, FMath::Cos(A2) * Sd.Y);
            const double Ang = Age * P.Swirl;
            const FVector Swirl(FMath::Cos(Ang) * P.Swirl * .15, 0, FMath::Sin(Ang) * P.Swirl * .15);
            FVector Off = Em->Emit[I] + Em->Vel[I] * Age + Grav + Drift + Swirl;
            if (P.bFlame || P.Converge > 0) { const double Conv = 1 - TL * (P.Converge > 0 ? P.Converge : .85); Off.X *= Conv; Off.Z *= Conv; }
            double Sz = P.Size * Sd.Z;
            if (P.bFlame) Sz *= FMath::Sqrt(FMath::SmoothStep(0., .12, TL)) * (1 - TL * .55);
            Sz = FMath::Max(.005, Sz * (1 + P.Grow * TL));
            FQuat Q = Facing;
            if (P.Rotate > 0) Q = Q * FQuat(FVector(0, 0, 1), Sd.X * 6.2832 + T * P.Rotate * (Sd.X - .5) * 2);
            // Plane mesh: 1 m in local XY facing +Z; turn it so +Z faces along the view.
            Xf[I] = FTransform(Q * FQuat(FVector(0, 1, 0), PI / 2), AEidoverseBridge::FromWirePosition(Em->Origin + Off), FVector(Sz));
            const double Fade = FMath::Clamp(TL / .12, 0., 1.) * FMath::Clamp((1 - TL) / .35, 0., 1.);
            const double Tw = .5 + .5 * FMath::Sin(T * 7 + Sd.X * 40);
            const float Alpha = float(P.Opacity * Fade * FMath::Lerp(1., Tw, double(P.Twinkle)));
            FLinearColor C = Base;
            if (P.bFlame)
            {
                FLinearColor Ramp = FMath::Lerp(FLinearColor(1, .97f, .88f), FLinearColor(1, .78f, .25f), float(FMath::SmoothStep(0., .3, TL)));
                Ramp = FMath::Lerp(Ramp, FLinearColor(1, .42f, .1f), float(FMath::SmoothStep(.3, .65, TL)));
                Ramp = FMath::Lerp(Ramp, FLinearColor(.55f, .12f, .04f), float(FMath::SmoothStep(.65, 1., TL)));
                C = Ramp * Base;
            }
            Ism->SetCustomData(I, {Alpha, C.R, C.G, C.B}, false);
        }
        Ism->BatchUpdateInstancesTransforms(0, Xf, false, true, true);
    }
}
