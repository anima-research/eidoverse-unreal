#include "EidoverseMotion.h"
#include "Dom/JsonObject.h"

namespace EidoMotion
{
namespace
{
    constexpr double Pi = 3.14159265358979323846;

    bool Number(const TSharedPtr<FJsonObject>& O, const TCHAR* Key, double& Out)
    {
        return O && O->TryGetNumberField(Key, Out);
    }

    double NumberOr(const TSharedPtr<FJsonObject>& O, const TCHAR* Key, double Default)
    {
        double V;
        return Number(O, Key, V) ? V : Default;
    }

    bool Vec(const TSharedPtr<FJsonObject>& O, const TCHAR* Key, FVec3& Out)
    {
        const TArray<TSharedPtr<FJsonValue>>* A = nullptr;
        if (!O || !O->TryGetArrayField(Key, A) || A->Num() != 3) return false;
        double V[3];
        for (int32 I = 0; I < 3; ++I)
            if (!(*A)[I]->TryGetNumber(V[I])) return false;
        Out = {V[0], V[1], V[2]};
        return true;
    }

    FVec3 VecOr(const TSharedPtr<FJsonObject>& O, const TCHAR* Key, const FVec3& Default)
    {
        FVec3 V;
        return Vec(O, Key, V) ? V : Default;
    }

    // motioneval axisOf: [x,y,z] or "x"/"-y"/... strings.
    FVec3 AxisOf(const TSharedPtr<FJsonObject>& M, const FVec3& Default)
    {
        FVec3 V;
        if (Vec(M, TEXT("axis"), V)) return V;
        FString S;
        if (M->TryGetStringField(TEXT("axis"), S))
        {
            S = S.ToLower();
            if (S == TEXT("x")) return {1, 0, 0};
            if (S == TEXT("y")) return {0, 1, 0};
            if (S == TEXT("z")) return {0, 0, 1};
            if (S == TEXT("-x")) return {-1, 0, 0};
            if (S == TEXT("-y")) return {0, -1, 0};
            if (S == TEXT("-z")) return {0, 0, -1};
        }
        return Default;
    }

    // motioneval ampOf: amp ?? amplitude ?? default.
    double AmpOf(const TSharedPtr<FJsonObject>& M, double Default)
    {
        double V;
        if (Number(M, TEXT("amp"), V) || Number(M, TEXT("amplitude"), V))
            return FMath::IsFinite(V) ? V : Default;
        return Default;
    }

    FQuat4 YawQuat(double Yaw) { return AxisAngle({0, 1, 0}, Yaw); }

    // Rotate by Theta about local Axis at local Pivot, composed on the base pose.
    void RotateAtPivot(const FBase& Base, const FVec3& Axis, const FVec3& Pivot, double Theta, FResult& R)
    {
        const FQuat4 Q = AxisAngle(Axis, Theta);
        const FQuat4 Qy = YawQuat(Base.Yaw);
        R.Quat = Mul(Qy, Q);
        const FVec3 Rp = Apply(Q, Pivot);
        const FVec3 Shift = Apply(Qy, {Pivot.X - Rp.X, Pivot.Y - Rp.Y, Pivot.Z - Rp.Z});
        R.Pos = {Base.Pos.X + Shift.X, Base.Pos.Y + Shift.Y, Base.Pos.Z + Shift.Z};
    }

    bool ReadPoints(const TSharedPtr<FJsonObject>& M, TArray<FVec3>& Out)
    {
        const TArray<TSharedPtr<FJsonValue>>* A = nullptr;
        if (!M->TryGetArrayField(TEXT("points"), A)) return false;
        for (const auto& V : *A)
        {
            const TArray<TSharedPtr<FJsonValue>>* P = nullptr;
            if (!V->TryGetArray(P) || P->Num() < 3) return false;
            Out.Add({(*P)[0]->AsNumber(), (*P)[1]->AsNumber(), (*P)[2]->AsNumber()});
        }
        return true;
    }

    FResult EvalPath(const TSharedPtr<FJsonObject>& M, double T, const FBase& Base)
    {
        FResult R;
        TArray<FVec3> Pts;
        if (!ReadPoints(M, Pts) || Pts.Num() < 2) { R.Why = TEXT("path needs >= 2 points"); return R; }
        TArray<double> Len{0.0};
        for (int32 I = 1; I < Pts.Num(); ++I)
        {
            const FVec3 &A = Pts[I - 1], &B = Pts[I];
            Len.Add(Len[I - 1] + FMath::Sqrt(FMath::Square(B.X - A.X) + FMath::Square(B.Y - A.Y) + FMath::Square(B.Z - A.Z)));
        }
        double Total = Len.Last();
        if (Total == 0) Total = 1;  // JS: `|| 1`
        double Speed, Duration;
        if (!Number(M, TEXT("speed"), Speed))
            Speed = Number(M, TEXT("duration"), Duration) && Duration != 0 ? Total / Duration : 1;
        double S = Speed * T;
        FString Loop = TEXT("loop");
        M->TryGetStringField(TEXT("loop"), Loop);
        if (Loop == TEXT("loop")) S = FMath::Fmod(S, Total);
        else if (Loop == TEXT("pingpong")) { S = FMath::Fmod(S, 2 * Total); if (S > Total) S = 2 * Total - S; }
        else S = FMath::Min(S, Total);
        int32 I = 1;
        while (I < Len.Num() - 1 && Len[I] < S) ++I;
        double Seg = Len[I] - Len[I - 1];
        if (Seg == 0) Seg = 1;
        const double F = (S - Len[I - 1]) / Seg;
        const FVec3 &A = Pts[I - 1], &B = Pts[I];
        R.bOk = true;
        R.Pos = {A.X + (B.X - A.X) * F, A.Y + (B.Y - A.Y) * F, A.Z + (B.Z - A.Z) * F};
        bool bFace = true;
        M->TryGetBoolField(TEXT("face"), bFace);
        if (bFace)
        {
            R.Yaw = FMath::Atan2(B.X - A.X, B.Z - A.Z);
            R.Quat = YawQuat(R.Yaw);
            R.bRot = true;
        }
        else
        {
            R.Yaw = Base.Yaw;
            R.Quat = YawQuat(Base.Yaw);
            R.bRot = false;
        }
        return R;
    }

    bool Finite(const FVec3& V) { return FMath::IsFinite(V.X) && FMath::IsFinite(V.Y) && FMath::IsFinite(V.Z); }
    bool Finite(const FQuat4& Q) { return FMath::IsFinite(Q.X) && FMath::IsFinite(Q.Y) && FMath::IsFinite(Q.Z) && FMath::IsFinite(Q.W); }
}

FQuat4 AxisAngle(const FVec3& Axis, double Angle)
{
    double N = FMath::Sqrt(Axis.X * Axis.X + Axis.Y * Axis.Y + Axis.Z * Axis.Z);
    if (N == 0) N = 1;
    const double S = FMath::Sin(Angle / 2);
    return {Axis.X / N * S, Axis.Y / N * S, Axis.Z / N * S, FMath::Cos(Angle / 2)};
}

FQuat4 Mul(const FQuat4& A, const FQuat4& B)
{
    return {
        A.W * B.X + A.X * B.W + A.Y * B.Z - A.Z * B.Y,
        A.W * B.Y - A.X * B.Z + A.Y * B.W + A.Z * B.X,
        A.W * B.Z + A.X * B.Y - A.Y * B.X + A.Z * B.W,
        A.W * B.W - A.X * B.X - A.Y * B.Y - A.Z * B.Z,
    };
}

FVec3 Apply(const FQuat4& Q, const FVec3& V)
{
    const double Cx = Q.Y * V.Z - Q.Z * V.Y + Q.W * V.X;
    const double Cy = Q.Z * V.X - Q.X * V.Z + Q.W * V.Y;
    const double Cz = Q.X * V.Y - Q.Y * V.X + Q.W * V.Z;
    return {V.X + 2 * (Q.Y * Cz - Q.Z * Cy), V.Y + 2 * (Q.Z * Cx - Q.X * Cz), V.Z + 2 * (Q.X * Cy - Q.Y * Cx)};
}

double YawOf(const FQuat4& Q)
{
    const FVec3 F = Apply(Q, {0, 0, 1});
    return FMath::Atan2(F.X, F.Z);
}

bool ReadBase(const TSharedPtr<FJsonObject>& Json, FBase& Out)
{
    if (!Vec(Json, TEXT("pos"), Out.Pos)) return false;
    Out.Yaw = NumberOr(Json, TEXT("yaw"), 0);
    return Finite(Out.Pos) && FMath::IsFinite(Out.Yaw);
}

FResult EvalWholeMotion(const FBase& Base, const TSharedPtr<FJsonObject>& M, double NowMs)
{
    FResult R;
    FString Type;
    if (!M || !M->TryGetStringField(TEXT("type"), Type)) { R.Why = TEXT("no motion type"); return R; }
    FString Part;
    if (M->TryGetStringField(TEXT("part"), Part))
    {
        // Animates one named node of the model; the root transform is untouched.
        R.bOk = true; R.Pos = Base.Pos; R.Yaw = Base.Yaw; R.Quat = YawQuat(Base.Yaw); R.bRot = false;
        return R;
    }
    const double T = FMath::Max(0.0, (NowMs - NumberOr(M, TEXT("t0"), NowMs)) / 1000);
    bool bYawKnown = false;
    if (Type == TEXT("pendulum"))
    {
        const double W0 = 2 * Pi / NumberOr(M, TEXT("period"), 3.5);
        const double Theta = AmpOf(M, 0) * FMath::Exp(-NumberOr(M, TEXT("damp"), 0) * T) * FMath::Cos(W0 * T + NumberOr(M, TEXT("phase"), 0));
        RotateAtPivot(Base, AxisOf(M, {1, 0, 0}), VecOr(M, TEXT("pivot"), {0, 2, 0}), Theta, R);
        R.bOk = true; R.bRot = true;
    }
    else if (Type == TEXT("spin"))
    {
        double Rate;
        if (!Number(M, TEXT("degPerSec"), Rate)) Rate = NumberOr(M, TEXT("rpm"), 6) * 6;
        RotateAtPivot(Base, AxisOf(M, {0, 1, 0}), VecOr(M, TEXT("pivot"), {0, 0, 0}), NumberOr(M, TEXT("phase"), 0) + Rate * Pi / 180 * T, R);
        R.bOk = true; R.bRot = true;
    }
    else if (Type == TEXT("orbit"))
    {
        const FVec3 C = VecOr(M, TEXT("center"), Base.Pos);
        const double Rad = NumberOr(M, TEXT("radius"), 3);
        const double A = NumberOr(M, TEXT("phase"), 0) + NumberOr(M, TEXT("degPerSec"), 12) * Pi / 180 * T;
        R.bOk = true;
        R.Pos = {C.X + Rad * FMath::Sin(A), C.Y, C.Z + Rad * FMath::Cos(A)};
        bool bFace = true;
        M->TryGetBoolField(TEXT("face"), bFace);
        if (bFace) { R.Yaw = A + Pi / 2; R.bRot = true; }
        else { R.Yaw = Base.Yaw; R.bRot = false; }
        R.Quat = YawQuat(R.Yaw);
        bYawKnown = true;
    }
    else if (Type == TEXT("bob"))
    {
        const double Off = FMath::Sin(2 * Pi / NumberOr(M, TEXT("period"), 4) * T + NumberOr(M, TEXT("phase"), 0)) * AmpOf(M, 0.3);
        const FVec3 Ax = AxisOf(M, {0, 1, 0});
        double N = FMath::Sqrt(Ax.X * Ax.X + Ax.Y * Ax.Y + Ax.Z * Ax.Z);
        if (N == 0) N = 1;
        R.bOk = true; R.bRot = false; R.Yaw = Base.Yaw; R.Quat = YawQuat(Base.Yaw);
        R.Pos = {Base.Pos.X + Ax.X / N * Off, Base.Pos.Y + Ax.Y / N * Off, Base.Pos.Z + Ax.Z / N * Off};
        bYawKnown = true;
    }
    else if (Type == TEXT("path"))
    {
        R = EvalPath(M, T, Base);
        bYawKnown = true;
    }
    else
    {
        R.Why = FString::Printf(TEXT("motion type \"%s\" not supported here"), *Type);
        return R;
    }
    if (!R.bOk) return R;
    if (!bYawKnown) R.Yaw = YawOf(R.Quat);
    if (!Finite(R.Pos) || !FMath::IsFinite(R.Yaw) || !Finite(R.Quat))
    {
        R.bOk = false;
        R.Why = FString::Printf(TEXT("motion \"%s\" produced a non-finite transform"), *Type);
    }
    return R;
}
}
