#pragma once
#include "CoreMinimal.h"

class FJsonObject;

// C++ port of client/lib/motioneval.js (eidoverse-worlds): whole-entity motion
// closed forms, evaluated in WIRE space (metres, Y-up, right-handed, quaternion
// [x,y,z,w]). Convert results with AEidoverseBridge::FromWire*.
//
// Must reproduce shared/conformance/motion.json; see Eidoverse.MotionConformance.
// When motioneval.js changes, port the change and refresh the vectors copy in
// Plugins/Eidoverse/Resources/Conformance/.
namespace EidoMotion
{
    struct FVec3 { double X = 0, Y = 0, Z = 0; };
    struct FQuat4 { double X = 0, Y = 0, Z = 0, W = 1; };

    struct FBase
    {
        FVec3 Pos;
        double Yaw = 0;
    };

    struct FResult
    {
        bool bOk = false;
        FString Why;
        FVec3 Pos;
        double Yaw = 0;
        FQuat4 Quat;
        // false: the motion leaves the entity's authored rotation alone.
        bool bRot = false;
    };

    FQuat4 AxisAngle(const FVec3& Axis, double Angle);
    FQuat4 Mul(const FQuat4& A, const FQuat4& B);
    FVec3 Apply(const FQuat4& Q, const FVec3& V);
    double YawOf(const FQuat4& Q);

    // Motion carries an explicit t0 (render-scene always resolves it).
    FResult EvalWholeMotion(const FBase& Base, const TSharedPtr<FJsonObject>& Motion, double NowMs);

    bool ReadBase(const TSharedPtr<FJsonObject>& Json, FBase& Out);
}
