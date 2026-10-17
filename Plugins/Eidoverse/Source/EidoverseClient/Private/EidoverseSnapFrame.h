// Optional framing for a remote snapshot (eidoverse-worlds shared/snapframe.js + client/lib/net_snap.js
// and fp_view.js). The server's `snap` message may carry a nested `frame` {dist?, height?, pitch?},
// normalised on both ends; an absent field keeps the fixed default framing.
//
//   dist   third/selfie: metres between the camera and the body, measured flat.  [0.5, 30]
//   height third/selfie: camera height above the body's feet.                    [0, 30]
//   pitch  first: gaze tilt in degrees, + looks up, - looks down.                 [-85, 85]
#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

namespace EidoSnapFrame
{
    struct FSnapFrame
    {
        TOptional<double> Dist, Height, Pitch;
        bool IsEmpty() const { return !Dist.IsSet() && !Height.IsSet() && !Pitch.IsSet(); }
    };

    inline constexpr double DistMin = .5, DistMax = 30, HeightMin = 0, HeightMax = 30, PitchMin = -85, PitchMax = 85;

    /** snapframe.js num(): finite numbers and non-blank numeric strings (HTTP query values) count.
     *  Booleans, arrays, objects and nulls are dropped BEFORE conversion (false must not become 0). */
    inline TOptional<double> Number(const TSharedPtr<FJsonValue>& V)
    {
        if (!V.IsValid()) return {};
        if (V->Type == EJson::Number)
        {
            const double N = V->AsNumber();
            return FMath::IsFinite(N) ? TOptional<double>(N) : TOptional<double>();
        }
        if (V->Type != EJson::String) return {};
        const FString S = V->AsString().TrimStartAndEnd();
        if (S.IsEmpty()) return {};
        // A plain decimal: sign, digits, one point, an exponent. Anything else (letters, "Infinity") drops.
        int32 I = 0, Digits = 0;
        if (I < S.Len() && (S[I] == '+' || S[I] == '-')) ++I;
        while (I < S.Len() && FChar::IsDigit(S[I])) { ++I; ++Digits; }
        if (I < S.Len() && S[I] == '.') { ++I; while (I < S.Len() && FChar::IsDigit(S[I])) { ++I; ++Digits; } }
        if (!Digits) return {};
        if (I < S.Len() && (S[I] == 'e' || S[I] == 'E'))
        {
            ++I; if (I < S.Len() && (S[I] == '+' || S[I] == '-')) ++I;
            int32 Exp = 0; while (I < S.Len() && FChar::IsDigit(S[I])) { ++I; ++Exp; }
            if (!Exp) return {};
        }
        if (I != S.Len()) return {};
        const double N = FCString::Atod(*S);
        return FMath::IsFinite(N) ? TOptional<double>(N) : TOptional<double>();
    }

    /** normalizeSnapFrame(raw): only the fields given as finite numbers, each clamped to its range. */
    inline FSnapFrame Normalize(const TSharedPtr<FJsonObject>& Raw)
    {
        FSnapFrame F;
        if (!Raw.IsValid()) return F;
        if (const auto D = Number(Raw->TryGetField(TEXT("dist")))) F.Dist = FMath::Clamp(*D, DistMin, DistMax);
        if (const auto H = Number(Raw->TryGetField(TEXT("height")))) F.Height = FMath::Clamp(*H, HeightMin, HeightMax);
        if (const auto P = Number(Raw->TryGetField(TEXT("pitch")))) F.Pitch = FMath::Clamp(*P, PitchMin, PitchMax);
        return F;
    }

    /** net_snap.js's third/selfie cameras for a humanoid, in UE units (cm, Z up). Feet is the body's root on
     *  the ground, Forward its facing (flattened here: the browser frames by the root's yaw only).
     *  third: behind at dist (3.4 m) and height (2.1 m), aimed 1.2 m up and 4 m ahead at the default
     *         distance, the look-ahead scaling with the distance so a near camera keeps the body in frame;
     *  selfie: in front at dist (2.6 m) and height (1.6 m), aimed at the chest (1.25 m). */
    inline bool BodyCamera(const FString& View, const FSnapFrame& F, const FVector& Feet, const FVector& Forward, FVector& OutEye, FQuat& OutRotation)
    {
        FVector Fwd(Forward.X, Forward.Y, 0);
        Fwd = Fwd.IsNearlyZero() ? FVector::ForwardVector : Fwd.GetSafeNormal();
        FVector Aim;
        if (View == TEXT("third"))
        {
            const double Dist = F.Dist.Get(3.4);
            OutEye = Feet + FVector(0, 0, F.Height.Get(2.1) * 100) - Fwd * (Dist * 100);
            Aim = Feet + FVector(0, 0, 120) + Fwd * (400 * Dist / 3.4);
        }
        else if (View == TEXT("selfie"))
        {
            OutEye = Feet + FVector(0, 0, F.Height.Get(1.6) * 100) + Fwd * (F.Dist.Get(2.6) * 100);
            Aim = Feet + FVector(0, 0, 125);
        }
        else return false;
        OutRotation = (Aim - OutEye).Rotation().Quaternion();
        return true;
    }

    /** fp_view.js composeFirstPerson with an asked-for pitch: the same eye and heading, the gaze tilted. */
    inline FQuat FirstPersonGaze(double PitchDegrees, const FQuat& Facing)
    {
        return FRotator(PitchDegrees, Facing.Rotator().Yaw, 0).Quaternion();
    }
}
