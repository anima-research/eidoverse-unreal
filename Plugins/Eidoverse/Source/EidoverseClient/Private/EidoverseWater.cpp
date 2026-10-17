#include "EidoverseWater.h"
#include "Dom/JsonObject.h"

namespace EidoWater
{
namespace
{
    // JS Number.isFinite: only real finite numbers, never strings or nulls.
    bool FiniteNumber(const TSharedPtr<FJsonValue>& V, double& Out)
    {
        if (!V.IsValid() || V->Type != EJson::Number) return false;
        Out = V->AsNumber();
        return FMath::IsFinite(Out);
    }

    double FiniteOr(const TSharedPtr<FJsonObject>& O, const TCHAR* Key, double Default)
    {
        double V;
        return O && FiniteNumber(O->TryGetField(Key), V) ? V : Default;
    }

    // vector(v, fallback): exactly three finite numbers, otherwise the whole fallback.
    FVec3 Vector(const TArray<TSharedPtr<FJsonValue>>* A, const FVec3& Fallback)
    {
        double X, Y, Z;
        if (A && A->Num() == 3 && FiniteNumber((*A)[0], X) && FiniteNumber((*A)[1], Y) && FiniteNumber((*A)[2], Z))
            return {X, Y, Z};
        return Fallback;
    }

    FVec3 VectorField(const TSharedPtr<FJsonObject>& O, const TCHAR* Key, const FVec3& Fallback)
    {
        const TArray<TSharedPtr<FJsonValue>>* A = nullptr;
        return O && O->TryGetArrayField(Key, A) ? Vector(A, Fallback) : Fallback;
    }
}

FParams ReadParams(const TSharedPtr<FJsonObject>& Data)
{
    FParams W;
    W.Center = VectorField(Data, TEXT("center"), {0, 0, 0});
    const FVec3 S = VectorField(Data, TEXT("size"), {200, 200, 200});
    auto Clamp = [](double V) { return FMath::Max(.01, FMath::Min(100000.0, FMath::Abs(V))); };
    W.Size = {Clamp(S.X), Clamp(S.Y), Clamp(S.Z)};
    const FVec3 A = VectorField(Data, TEXT("absorption"), {.16, .055, .025});
    W.Absorption = {FMath::Max(0.0, A.X), FMath::Max(0.0, A.Y), FMath::Max(0.0, A.Z)};
    const FVec3 C = VectorField(Data, TEXT("scatter"), {.015, .12, .18});
    W.Scatter = {FMath::Max(0.0, C.X), FMath::Max(0.0, C.Y), FMath::Max(0.0, C.Z)};
    W.Speed = FMath::Max(.1, FMath::Min(20.0, FiniteOr(Data, TEXT("speed"), 2.4)));

    const TArray<TSharedPtr<FJsonValue>>* Waves = nullptr;
    if (Data && Data->TryGetArrayField(TEXT("waves"), Waves))
    {
        for (int32 I = 0; I < FMath::Min(Waves->Num(), MaxWaves); ++I)
        {
            // `w?.x` on null/non-objects reads undefined -> defaults.
            const TSharedPtr<FJsonObject> O = (*Waves)[I].IsValid() && (*Waves)[I]->Type == EJson::Object ? (*Waves)[I]->AsObject() : nullptr;
            FWave Wave;
            Wave.Amplitude = FMath::Max(0.0, FMath::Min(5.0, FiniteOr(O, TEXT("amplitude"), 0)));
            Wave.Wavelength = FMath::Max(.1, FiniteOr(O, TEXT("wavelength"), 10));
            // vector([d[0], 0, d[1]], [1,0,0]) then normalize in x/z (`|| 1`).
            const TArray<TSharedPtr<FJsonValue>>* D = nullptr;
            FVec3 Dir{1, 0, 0};
            if (O && O->TryGetArrayField(TEXT("direction"), D))
            {
                double Dx, Dz;
                const bool bX = D->Num() > 0 && FiniteNumber((*D)[0], Dx);
                const bool bZ = D->Num() > 1 && FiniteNumber((*D)[1], Dz);
                if (bX && bZ) Dir = {Dx, 0, Dz};
            }
            double Len = FMath::Sqrt(Dir.X * Dir.X + Dir.Z * Dir.Z);
            if (Len == 0) Len = 1;
            Wave.DirX = Dir.X / Len;
            Wave.DirZ = Dir.Z / Len;
            Wave.Phase = FiniteOr(O, TEXT("phase"), 0);
            W.Waves.Add(Wave);
        }
    }
    W.UpdateBounds();
    return W;
}

double WaveHeight(const FParams& W, double X, double Z, double Time)
{
    double Height = W.Top;
    for (const FWave& Wave : W.Waves)
    {
        const double K = UE_DOUBLE_PI * 2 / Wave.Wavelength;
        Height += Wave.Amplitude * FMath::Sin(K * (Wave.DirX * X + Wave.DirZ * Z) - FMath::Sqrt(9.81 * K) * Time + Wave.Phase);
    }
    return Height;
}

bool InVolume(const FParams& W, const FVec3& P, double Time)
{
    return FMath::Abs(P.X - W.Center.X) <= W.Size.X / 2 && FMath::Abs(P.Z - W.Center.Z) <= W.Size.Z / 2
        && P.Y >= W.Bottom && P.Y <= WaveHeight(W, P.X, P.Z, Time);
}

FSwimResult SwimStep(const FVec3& Pos, const FVec3& Velocity, const FSwimInput& Input, const FParams& W, double Dt, double Time)
{
    Dt = FMath::Max(0.0, FMath::Min(.1, FMath::IsFinite(Dt) ? Dt : 0.0));
    const double Speed = W.Speed * (Input.bFast ? 1.65 : 1);
    const FVec3& D = Input.Direction;
    const double Length = FMath::Max(1.0, FMath::Sqrt(D.X * D.X + D.Y * D.Y + D.Z * D.Z));
    FVec3 Target{D.X / Length * Speed, D.Y / Length * Speed, D.Z / Length * Speed};
    if (!Input.bDive && !Input.bRise) Target.Y += .16;
    const double Blend = 1 - FMath::Exp(-5 * Dt);
    FSwimResult R;
    R.Velocity = {Velocity.X + (Target.X - Velocity.X) * Blend, Velocity.Y + (Target.Y - Velocity.Y) * Blend, Velocity.Z + (Target.Z - Velocity.Z) * Blend};
    R.Pos = {Pos.X + R.Velocity.X * Dt, Pos.Y + R.Velocity.Y * Dt, Pos.Z + R.Velocity.Z * Dt};
    // The root floats a body-length fraction under the surface: 1.1 m for a 100% body, scaled with its size
    // so a small body's eyes still clear the water (shared/water.js swimStep, input.stature).
    const double Stature = FMath::IsFinite(Input.Stature) && Input.Stature > 0 ? Input.Stature : 1.0;
    const double Surface = WaveHeight(W, R.Pos.X, R.Pos.Z, Time) - 1.1 * Stature;
    if (!Input.bDive && R.Pos.Y >= Surface - .12)
    {
        R.Pos.Y = Surface;
        R.Velocity.Y = 0;
        R.bSurface = true;
    }
    return R;
}
}
