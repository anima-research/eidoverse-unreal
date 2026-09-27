#pragma once
#include "CoreMinimal.h"
#include "EidoverseMotion.h"

class FJsonObject;

// C++ port of shared/water.js (eidoverse-worlds): the renderer-independent water
// law shared by every client. WIRE space: metres, Y-up. Must reproduce
// shared/conformance/water.json; see Eidoverse.WaterConformance.
namespace EidoWater
{
    using EidoMotion::FVec3;

    constexpr int32 MaxWater = 4, MaxAir = 16, MaxWaves = 8;

    struct FWave
    {
        double Amplitude = 0, Wavelength = 10, DirX = 1, DirZ = 0, Phase = 0;
    };

    struct FParams
    {
        FVec3 Center, Size{200, 200, 200};
        FVec3 Absorption{.16, .055, .025}, Scatter{.015, .12, .18};
        double Speed = 2.4;
        TArray<FWave> Waves;
        // Filled by the caller after placing the volume (as client/lib/water.js does).
        double Top = 100, Bottom = -100;
        void UpdateBounds() { Top = Center.Y + Size.Y / 2; Bottom = Center.Y - Size.Y / 2; }
    };

    // waterParams(data): clamps and defaults, identical to the browser.
    FParams ReadParams(const TSharedPtr<FJsonObject>& Data);

    // waveHeight(water, x, z, t): surface height at wire x/z, seconds t.
    double WaveHeight(const FParams& W, double X, double Z, double Time);

    // Horizontal footprint and vertical wet range (bottom..wave surface).
    bool InVolume(const FParams& W, const FVec3& P, double Time);

    struct FSwimInput
    {
        FVec3 Direction;
        bool bFast = false, bDive = false, bRise = false;
    };
    struct FSwimResult
    {
        FVec3 Pos, Velocity;
        bool bSurface = false;
    };
    // swimStep(pos, velocity, input, water, dt, time).
    FSwimResult SwimStep(const FVec3& Pos, const FVec3& Velocity, const FSwimInput& Input, const FParams& W, double Dt, double Time);

    // The browser's wave clock: server epoch wrapped hourly for float precision.
    inline double WaveTime(double ServerNowMs) { return FMath::Fmod(ServerNowMs, 3600000.0) / 1000.0; }
}
