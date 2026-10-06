#include "EidoverseRagdoll.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>

// Bit-faithful to the JS: no fused multiply-add contraction (JS never fuses), and
// every helper below spells its arithmetic in three.js's exact operation order.
#if defined(__clang__)
#pragma clang fp contract(off)
#endif

namespace EidoRagdoll
{
namespace
{
    // libm sin and cos one call each, as JS evaluates them. Out of line so the optimizer
    // cannot fuse a sin/cos pair into __sincos_stret, which rounds differently.
    FORCENOINLINE double JsSin(double X) { return std::sin(X); }
    FORCENOINLINE double JsCos(double X) { return std::cos(X); }

    // ---------------------------------------------------------------- three.js math
    struct FV3
    {
        double X = 0, Y = 0, Z = 0;
        FV3() = default;
        FV3(double InX, double InY, double InZ) : X(InX), Y(InY), Z(InZ) {}
    };
    struct FQ
    {
        double X = 0, Y = 0, Z = 0, W = 1;
        FQ() = default;
        FQ(double InX, double InY, double InZ, double InW) : X(InX), Y(InY), Z(InZ), W(InW) {}
    };
    struct FM4 { double E[16] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 }; };   // column-major, three's layout

    FV3 Add(const FV3& A, const FV3& B) { return FV3(A.X + B.X, A.Y + B.Y, A.Z + B.Z); }
    FV3 Sub(const FV3& A, const FV3& B) { return FV3(A.X - B.X, A.Y - B.Y, A.Z - B.Z); }
    FV3 Mul(const FV3& A, double S) { return FV3(A.X * S, A.Y * S, A.Z * S); }
    FV3 DivS(const FV3& A, double S) { return Mul(A, 1 / S); }                         // divideScalar
    void AddScaled(FV3& A, const FV3& V, double S) { A.X += V.X * S; A.Y += V.Y * S; A.Z += V.Z * S; }
    double Dot(const FV3& A, const FV3& B) { return A.X * B.X + A.Y * B.Y + A.Z * B.Z; }
    double LenSq(const FV3& A) { return A.X * A.X + A.Y * A.Y + A.Z * A.Z; }
    double Len(const FV3& A) { return std::sqrt(A.X * A.X + A.Y * A.Y + A.Z * A.Z); }
    FV3 Norm(const FV3& A) { const double L = Len(A); return DivS(A, L != 0 ? L : 1); }   // normalize: / (length() || 1)
    FV3 Cross(const FV3& A, const FV3& B) { return FV3(A.Y * B.Z - A.Z * B.Y, A.Z * B.X - A.X * B.Z, A.X * B.Y - A.Y * B.X); }
    double DistSq(const FV3& A, const FV3& B) { const double Dx = A.X - B.X, Dy = A.Y - B.Y, Dz = A.Z - B.Z; return Dx * Dx + Dy * Dy + Dz * Dz; }
    double Dist(const FV3& A, const FV3& B) { return std::sqrt(DistSq(A, B)); }
    FV3 Lerp(const FV3& A, const FV3& B, double T) { return FV3(A.X + (B.X - A.X) * T, A.Y + (B.Y - A.Y) * T, A.Z + (B.Z - A.Z) * T); }
    double Clamp(double V, double Lo, double Hi) { return FMath::Max(Lo, FMath::Min(Hi, V)); }   // MathUtils.clamp
    double JsMax(double A, double B) { return (std::isnan(A) || std::isnan(B)) ? std::numeric_limits<double>::quiet_NaN() : (A > B ? A : B); }
    double JsMin(double A, double B) { return (std::isnan(A) || std::isnan(B)) ? std::numeric_limits<double>::quiet_NaN() : (A < B ? A : B); }

    FV3 ApplyQ(const FV3& V, const FQ& Q)   // Vector3.applyQuaternion (r186)
    {
        const double Tx = 2 * (Q.Y * V.Z - Q.Z * V.Y);
        const double Ty = 2 * (Q.Z * V.X - Q.X * V.Z);
        const double Tz = 2 * (Q.X * V.Y - Q.Y * V.X);
        return FV3(V.X + Q.W * Tx + Q.Y * Tz - Q.Z * Ty,
                   V.Y + Q.W * Ty + Q.Z * Tx - Q.X * Tz,
                   V.Z + Q.W * Tz + Q.X * Ty - Q.Y * Tx);
    }
    FQ QMul(const FQ& A, const FQ& B)   // multiplyQuaternions(a, b) = a * b
    {
        return FQ(A.X * B.W + A.W * B.X + A.Y * B.Z - A.Z * B.Y,
                  A.Y * B.W + A.W * B.Y + A.Z * B.X - A.X * B.Z,
                  A.Z * B.W + A.W * B.Z + A.X * B.Y - A.Y * B.X,
                  A.W * B.W - A.X * B.X - A.Y * B.Y - A.Z * B.Z);
    }
    FQ Conj(const FQ& Q) { return FQ(Q.X * -1, Q.Y * -1, Q.Z * -1, Q.W); }   // invert() == conjugate()
    double QLenSq(const FQ& Q) { return Q.X * Q.X + Q.Y * Q.Y + Q.Z * Q.Z + Q.W * Q.W; }
    FQ QNorm(const FQ& Q)
    {
        double L = std::sqrt(Q.X * Q.X + Q.Y * Q.Y + Q.Z * Q.Z + Q.W * Q.W);
        if (L == 0) return FQ(0, 0, 0, 1);
        L = 1 / L;
        return FQ(Q.X * L, Q.Y * L, Q.Z * L, Q.W * L);
    }
    FQ AxisAngle(const FV3& Axis, double Angle)
    {
        const double Half = Angle / 2, S = JsSin(Half);
        return FQ(Axis.X * S, Axis.Y * S, Axis.Z * S, JsCos(Half));
    }
    FQ FromUnitVectors(const FV3& From, const FV3& To)
    {
        double R = Dot(From, To) + 1;
        FQ Q;
        if (R < 1e-8)
        {
            R = 0;
            if (std::fabs(From.X) > std::fabs(From.Z)) Q = FQ(-From.Y, From.X, 0, R);
            else Q = FQ(0, -From.Z, From.Y, R);
        }
        else Q = FQ(From.Y * To.Z - From.Z * To.Y, From.Z * To.X - From.X * To.Z, From.X * To.Y - From.Y * To.X, R);
        return QNorm(Q);
    }
    FQ YawQuat(double Yaw)   // setFromEuler(0, yaw, 0, 'XYZ'), operation for operation
    {
        const double C1 = JsCos(0.0), C2 = JsCos(Yaw / 2), C3 = JsCos(0.0);
        const double S1 = JsSin(0.0), S2 = JsSin(Yaw / 2), S3 = JsSin(0.0);
        return FQ(S1 * C2 * C3 + C1 * S2 * S3, C1 * S2 * C3 - S1 * C2 * S3, C1 * C2 * S3 + S1 * S2 * C3, C1 * C2 * C3 - S1 * S2 * S3);
    }
    FM4 MakeBasis(const FV3& Xa, const FV3& Ya, const FV3& Za)
    {
        FM4 M;
        M.E[0] = Xa.X; M.E[1] = Xa.Y; M.E[2] = Xa.Z; M.E[3] = 0;
        M.E[4] = Ya.X; M.E[5] = Ya.Y; M.E[6] = Ya.Z; M.E[7] = 0;
        M.E[8] = Za.X; M.E[9] = Za.Y; M.E[10] = Za.Z; M.E[11] = 0;
        M.E[12] = 0; M.E[13] = 0; M.E[14] = 0; M.E[15] = 1;
        return M;
    }
    FQ FromRotationMatrix(const FM4& M)
    {
        const double* Te = M.E;
        const double M11 = Te[0], M12 = Te[4], M13 = Te[8];
        const double M21 = Te[1], M22 = Te[5], M23 = Te[9];
        const double M31 = Te[2], M32 = Te[6], M33 = Te[10];
        const double Trace = M11 + M22 + M33;
        FQ Q;
        if (Trace > 0)
        {
            const double S = 0.5 / std::sqrt(Trace + 1.0);
            Q.W = 0.25 / S; Q.X = (M32 - M23) * S; Q.Y = (M13 - M31) * S; Q.Z = (M21 - M12) * S;
        }
        else if (M11 > M22 && M11 > M33)
        {
            const double S = 2.0 * std::sqrt(1.0 + M11 - M22 - M33);
            Q.W = (M32 - M23) / S; Q.X = 0.25 * S; Q.Y = (M12 + M21) / S; Q.Z = (M13 + M31) / S;
        }
        else if (M22 > M33)
        {
            const double S = 2.0 * std::sqrt(1.0 + M22 - M11 - M33);
            Q.W = (M13 - M31) / S; Q.X = (M12 + M21) / S; Q.Y = 0.25 * S; Q.Z = (M23 + M32) / S;
        }
        else
        {
            const double S = 2.0 * std::sqrt(1.0 + M33 - M11 - M22);
            Q.W = (M21 - M12) / S; Q.X = (M13 + M31) / S; Q.Y = (M23 + M32) / S; Q.Z = 0.25 * S;
        }
        return Q;
    }
    FM4 Compose(const FV3& P, const FQ& Q)   // Matrix4.compose with scale (1,1,1)
    {
        const double X = Q.X, Y = Q.Y, Z = Q.Z, W = Q.W;
        const double X2 = X + X, Y2 = Y + Y, Z2 = Z + Z;
        const double XX = X * X2, XY = X * Y2, XZ = X * Z2;
        const double YY = Y * Y2, YZ = Y * Z2, ZZ = Z * Z2;
        const double WX = W * X2, WY = W * Y2, WZ = W * Z2;
        const double Sx = 1, Sy = 1, Sz = 1;
        FM4 M;
        M.E[0] = (1 - (YY + ZZ)) * Sx; M.E[1] = (XY + WZ) * Sx; M.E[2] = (XZ - WY) * Sx; M.E[3] = 0;
        M.E[4] = (XY - WZ) * Sy; M.E[5] = (1 - (XX + ZZ)) * Sy; M.E[6] = (YZ + WX) * Sy; M.E[7] = 0;
        M.E[8] = (XZ + WY) * Sz; M.E[9] = (YZ - WX) * Sz; M.E[10] = (1 - (XX + YY)) * Sz; M.E[11] = 0;
        M.E[12] = P.X; M.E[13] = P.Y; M.E[14] = P.Z; M.E[15] = 1;
        return M;
    }
    FM4 MMul(const FM4& A, const FM4& B)   // multiplyMatrices(a, b)
    {
        const double* Ae = A.E; const double* Be = B.E;
        const double A11 = Ae[0], A12 = Ae[4], A13 = Ae[8], A14 = Ae[12];
        const double A21 = Ae[1], A22 = Ae[5], A23 = Ae[9], A24 = Ae[13];
        const double A31 = Ae[2], A32 = Ae[6], A33 = Ae[10], A34 = Ae[14];
        const double A41 = Ae[3], A42 = Ae[7], A43 = Ae[11], A44 = Ae[15];
        const double B11 = Be[0], B12 = Be[4], B13 = Be[8], B14 = Be[12];
        const double B21 = Be[1], B22 = Be[5], B23 = Be[9], B24 = Be[13];
        const double B31 = Be[2], B32 = Be[6], B33 = Be[10], B34 = Be[14];
        const double B41 = Be[3], B42 = Be[7], B43 = Be[11], B44 = Be[15];
        FM4 M; double* Te = M.E;
        Te[0] = A11 * B11 + A12 * B21 + A13 * B31 + A14 * B41;
        Te[4] = A11 * B12 + A12 * B22 + A13 * B32 + A14 * B42;
        Te[8] = A11 * B13 + A12 * B23 + A13 * B33 + A14 * B43;
        Te[12] = A11 * B14 + A12 * B24 + A13 * B34 + A14 * B44;
        Te[1] = A21 * B11 + A22 * B21 + A23 * B31 + A24 * B41;
        Te[5] = A21 * B12 + A22 * B22 + A23 * B32 + A24 * B42;
        Te[9] = A21 * B13 + A22 * B23 + A23 * B33 + A24 * B43;
        Te[13] = A21 * B14 + A22 * B24 + A23 * B34 + A24 * B44;
        Te[2] = A31 * B11 + A32 * B21 + A33 * B31 + A34 * B41;
        Te[6] = A31 * B12 + A32 * B22 + A33 * B32 + A34 * B42;
        Te[10] = A31 * B13 + A32 * B23 + A33 * B33 + A34 * B43;
        Te[14] = A31 * B14 + A32 * B24 + A33 * B34 + A34 * B44;
        Te[3] = A41 * B11 + A42 * B21 + A43 * B31 + A44 * B41;
        Te[7] = A41 * B12 + A42 * B22 + A43 * B32 + A44 * B42;
        Te[11] = A41 * B13 + A42 * B23 + A43 * B33 + A44 * B43;
        Te[15] = A41 * B14 + A42 * B24 + A43 * B34 + A44 * B44;
        return M;
    }
    FQ DecomposeQuat(const FM4& M)   // Matrix4.decompose's quaternion
    {
        const double* Te = M.E;
        const double Det = Te[0] * (Te[5] * Te[10] - Te[9] * Te[6]) - Te[4] * (Te[1] * Te[10] - Te[9] * Te[2]) + Te[8] * (Te[1] * Te[6] - Te[5] * Te[2]);
        if (Det == 0) return FQ();
        double Sx = Len(FV3(Te[0], Te[1], Te[2]));
        const double Sy = Len(FV3(Te[4], Te[5], Te[6]));
        const double Sz = Len(FV3(Te[8], Te[9], Te[10]));
        if (Det < 0) Sx = -Sx;
        FM4 R = M;
        const double InvSX = 1 / Sx, InvSY = 1 / Sy, InvSZ = 1 / Sz;
        R.E[0] *= InvSX; R.E[1] *= InvSX; R.E[2] *= InvSX;
        R.E[4] *= InvSY; R.E[5] *= InvSY; R.E[6] *= InvSY;
        R.E[8] *= InvSZ; R.E[9] *= InvSZ; R.E[10] *= InvSZ;
        return FromRotationMatrix(R);
    }

    FV3 ToV3(const FVector& V) { return FV3(V.X, V.Y, V.Z); }
    FVector ToVec(const FV3& V) { return FVector(V.X, V.Y, V.Z); }

    // Number.prototype.toFixed(4), then unary + : round half away from zero on the
    // exact binary value (printf rounds exact ties to even, toFixed picks the larger n).
    double Fixed4(double V)
    {
        if (!std::isfinite(V) || std::fabs(V) >= 1e15) return V;
        char Exact[1200];
        std::snprintf(Exact, sizeof(Exact), "%.1100f", std::fabs(V));   // the exact decimal value of the double
        const char* DotPos = std::strchr(Exact, '.');
        long long Units = std::strtoll(Exact, nullptr, 10) * 10000;
        for (int32 I = 1; I <= 4; ++I) Units += (long long)(DotPos[I] - '0') * (I == 1 ? 1000 : I == 2 ? 100 : I == 3 ? 10 : 1);
        if (DotPos[5] >= '5') ++Units;                                    // >= half a unit: the larger n
        char Out[64];
        std::snprintf(Out, sizeof(Out), "%lld.%04lld", Units / 10000, Units % 10000);
        const double R = std::strtod(Out, nullptr);
        return V < 0 ? -R : R;
    }

    // ---------------------------------------------------------------- topology (rigmeasure.js / joints.js)
    enum EJ : int32 { Hips, Spine, Chest, Neck, Head, LUA, LLA, LH, RUA, RLA, RH, LUL, LLL, LF, RUL, RLL, RF, NJ };
    const TCHAR* const JointNames[NJ] = {
        TEXT("hips"), TEXT("spine"), TEXT("chest"), TEXT("neck"), TEXT("head"),
        TEXT("leftUpperArm"), TEXT("leftLowerArm"), TEXT("leftHand"),
        TEXT("rightUpperArm"), TEXT("rightLowerArm"), TEXT("rightHand"),
        TEXT("leftUpperLeg"), TEXT("leftLowerLeg"), TEXT("leftFoot"),
        TEXT("rightUpperLeg"), TEXT("rightLowerLeg"), TEXT("rightFoot"),
    };
    constexpr int32 NSeg = 12;
    const int32 Segments[NSeg][2] = {
        { Hips, Spine }, { Spine, Chest }, { Chest, Neck }, { Neck, Head },
        { LUA, LLA }, { LLA, LH }, { RUA, RLA }, { RLA, RH },
        { LUL, LLL }, { LLL, LF }, { RUL, RLL }, { RLL, RF },
    };
    constexpr int32 NBones = NSeg + 4;
    const int32 Bones[NBones][2] = {
        { Hips, Spine }, { Spine, Chest }, { Chest, Neck }, { Neck, Head },
        { LUA, LLA }, { LLA, LH }, { RUA, RLA }, { RLA, RH },
        { LUL, LLL }, { LLL, LF }, { RUL, RLL }, { RLL, RF },
        { Hips, LUL }, { Hips, RUL }, { Chest, LUA }, { Chest, RUA },
    };
    constexpr int32 NBraces = 4;
    const int32 Braces[NBraces][2] = { { LUA, RUA }, { LUL, RUL }, { Hips, Chest }, { Spine, Neck } };
    const double Mass[NJ] = { 12, 8, 10, 2, 5, 2.5, 1.5, 0.5, 2.5, 1.5, 0.5, 7, 4, 1.2, 7, 4, 1.2 };
    const double RadiusFrac[NJ] = { 1.0, 0.95, 1.0, 0.5, 0.75, 0.5, 0.35, 0.3, 0.5, 0.35, 0.3, 0.55, 0.4, 0.35, 0.55, 0.4, 0.35 };
    struct FFlexRow { int32 A, B, C; double MaxDeg; };
    const FFlexRow FlexRows[3] = { { Hips, Spine, Chest, 25 }, { Spine, Chest, Neck, 25 }, { Chest, Neck, Head, 45 } };
    struct FBehindRow { int32 B, C; double Deg; };
    const FBehindRow BehindRows[4] = { { LUL, LLL, 30 }, { RUL, RLL, 30 }, { LUA, LLA, 65 }, { RUA, RLA, 65 } };
    struct FConeRow { int32 B, C; double Half, Tilt; };
    const FConeRow ConeRows[4] = { { LUA, LLA, 85, 0 }, { RUA, RLA, 85, 0 }, { LUL, LLL, 55, 25 }, { RUL, RLL, 55, 25 } };
    struct FHingeRow { int32 A, B, C; double Dir, MaxFlex, Side; };
    const FHingeRow HingeRows[4] = {
        { LUA, LLA, LH, +1, 145, 12 }, { RUA, RLA, RH, +1, 145, 12 },
        { LUL, LLL, LF, -1, 150, 6 }, { RUL, RLL, RF, -1, 150, 6 },
    };
    // TWIST (degrees; -1 = absent -> 180) and TWIST_PARENT (-1 = body frame)
    const double TwistDeg[NJ] = { -1, 25, 25, 45, -1, 75, 80, -1, 75, 80, -1, 40, 25, -1, 40, 25, -1 };
    const int32 TwistParent[NJ] = { -1, Hips, Spine, Chest, -1, Chest, LUA, -1, Chest, RUA, -1, Hips, LUL, -1, Hips, RUL, -1 };

    // The VRM humanoid chain the internal rig is built from (nearest PRESENT ancestor wins).
    struct FChainRow { const TCHAR* Bone; const TCHAR* Parent; };
    const FChainRow Chain[] = {
        { TEXT("hips"), nullptr }, { TEXT("spine"), TEXT("hips") }, { TEXT("chest"), TEXT("spine") },
        { TEXT("upperChest"), TEXT("chest") }, { TEXT("neck"), TEXT("upperChest") }, { TEXT("head"), TEXT("neck") },
        { TEXT("leftShoulder"), TEXT("upperChest") }, { TEXT("leftUpperArm"), TEXT("leftShoulder") },
        { TEXT("leftLowerArm"), TEXT("leftUpperArm") }, { TEXT("leftHand"), TEXT("leftLowerArm") },
        { TEXT("rightShoulder"), TEXT("upperChest") }, { TEXT("rightUpperArm"), TEXT("rightShoulder") },
        { TEXT("rightLowerArm"), TEXT("rightUpperArm") }, { TEXT("rightHand"), TEXT("rightLowerArm") },
        { TEXT("leftUpperLeg"), TEXT("hips") }, { TEXT("leftLowerLeg"), TEXT("leftUpperLeg") }, { TEXT("leftFoot"), TEXT("leftLowerLeg") },
        { TEXT("rightUpperLeg"), TEXT("hips") }, { TEXT("rightLowerLeg"), TEXT("rightUpperLeg") }, { TEXT("rightFoot"), TEXT("rightLowerLeg") },
    };

    constexpr double FixedDt = 1.0 / 60;
    constexpr int32 MaxFrames = 4;
    constexpr double PinJump = 0.12;
    constexpr double ImpulseCap = 8;
    const double Pi = 3.141592653589793;
    const double D2R = 3.141592653589793 / 180;

    void ClosestParams(const FV3& P1, const FV3& Q1, const FV3& P2, const FV3& Q2, double& OutS, double& OutT)
    {
        const FV3 A = Sub(Q1, P1), B = Sub(Q2, P2), C = Sub(P1, P2);
        const double AA = Dot(A, A), E = Dot(B, B), F = Dot(B, C);
        double S, T;
        if (AA < 1e-9 && E < 1e-9) { OutS = 0; OutT = 0; return; }
        if (AA < 1e-9) { S = 0; T = Clamp(F / E, 0, 1); }
        else
        {
            const double Cc = Dot(A, C);
            if (E < 1e-9) { T = 0; S = Clamp(-Cc / AA, 0, 1); }
            else
            {
                const double Bb = Dot(A, B), Den = AA * E - Bb * Bb;
                S = Den > 1e-9 ? Clamp((Bb * F - Cc * E) / Den, 0, 1) : 0;
                T = (Bb * S + F) / E;
                if (T < 0) { T = 0; S = Clamp(-Cc / AA, 0, 1); }
                else if (T > 1) { T = 1; S = Clamp((Bb - Cc) / AA, 0, 1); }
            }
        }
        OutS = S; OutT = T;
    }

    double TorsoRadius(const bool* Has, const FV3* Rest)
    {
        auto D = [&](int32 A, int32 B) { return (Has[A] && Has[B]) ? Len(Sub(Rest[A], Rest[B])) : 0.0; };
        const double ShoulderW = D(LUA, RUA), HipW = D(LUL, RUL);
        double SpineLen = D(Hips, Head);
        if (SpineLen == 0) SpineLen = 0.5;
        double W = JsMax(ShoulderW, HipW) * 0.42;
        if (W == 0 || W != W) W = SpineLen * 0.26;
        return JsMin(0.25, JsMax(0.05, W));
    }

    // Twist half of a swing-twist decomposition, signed, radians.
    double TwistAbout(const FQ& Q, const FV3& Axis)
    {
        const FV3 T3(Q.X, Q.Y, Q.Z);
        const double Along = Dot(T3, Axis);
        FQ Tw(Axis.X * Along, Axis.Y * Along, Axis.Z * Along, Q.W);
        if (QLenSq(Tw) < 1e-12) return 0;
        Tw = QNorm(Tw);
        double A = 2 * std::acos(Clamp(Tw.W, -1, 1));
        if (A > Pi) A -= Pi * 2;
        return Along < 0 ? -A : A;
    }
}

const TArray<FName>& DrivenBones()
{
    static const TArray<FName> Names = [] { TArray<FName> N; for (int32 S = 0; S < NSeg; ++S) N.Add(JointNames[Segments[S][0]]); return N; }();
    return Names;
}
const TArray<FName>& Joints()
{
    static const TArray<FName> Names = [] { TArray<FName> N; for (int32 J = 0; J < NJ; ++J) N.Add(JointNames[J]); return N; }();
    return Names;
}

struct FRagdoll::FImpl
{
    // internal normalized rig: the three.js scene graph the drive reads (root -> pivot -> bones)
    struct FNode { FName Name; int32 Parent = -1; FV3 LocalPos; FQ LocalQ; };
    TArray<FNode> Nodes;
    int32 JointNode[NJ];
    FM4 BaseMatrix;          // root.matrixWorld * pivot.matrix
    FQ BaseQ;                // root yaw * pivot, for the local <-> world offsets
    FQ PivotRot;

    FTuning T;
    FHeightFn HeightAtFn;
    FResolveFn ResolveFn;

    bool Has[NJ] = {};
    FV3 P[NJ], Prev[NJ], Pre[NJ], Rest[NJ];
    double Iw[NJ] = {}, Radius[NJ] = {};
    struct FLink { int32 A, B; double Len; bool bBrace; };
    TArray<FLink> Links;
    FV3 FrameR, FrameU, FrameF;
    struct FFlex { int32 A, B, C; double Max; };
    TArray<FFlex> Flex;
    struct FBehind { int32 B, C; double MinFwd; };
    TArray<FBehind> Behind;
    struct FCone { int32 B, C; FV3 Axis; double Cos; };
    TArray<FCone> Cone;
    struct FHinge { int32 A, B, C; double Max, Side; FV3 N; };
    TArray<FHinge> Hinge;
    struct FCap { int32 A, B; double R; };
    TArray<FCap> Caps;
    struct FPair { int32 CA, CB; double Min; };
    TArray<FPair> Pairs;
    struct FDrive
    {
        int32 Bone, Child, TwistParentDrive; int32 ParentNode;
        FQ RestFrame, RestFrameInv, RestLocal, RestQuat, FrameW, SwingW;
        FV3 RestLocalAxis;
        double Tw = 0, Twv = 0, TwMax = 0;
    };
    TArray<FDrive> Drive;
    int32 DriveBy[NJ];

    TArray<TPair<int32, FV3>> Pins;          // insertion-ordered, like a JS Map
    TArray<TPair<int32, FV3>> PinFrom;
    double Acc = 0;
    int32 Steps = 0, FramesRun = 0;
    bool bDone = false, bHavePose = false;
    double SettledFor = 0, Elapsed = 0, MaxV = std::numeric_limits<double>::infinity();
    double RootStartY = 0, HipsOffset = 0.82, HipsLocalX = 0, HipsLocalZ = 0;
    FV3 RootPos;
    double RootYaw = 0;
    FV3 S1;                                  // _s1: the correction direction _stop reads
    FStepResult Result;

    // ---- rig
    FM4 WorldMatrix(int32 NodeIndex) const
    {
        int32 ChainIdx[64]; int32 N = 0;
        for (int32 I = NodeIndex; I >= 0 && N < 64; I = Nodes[I].Parent) ChainIdx[N++] = I;
        FM4 M = BaseMatrix;
        for (int32 K = N - 1; K >= 0; --K) M = MMul(M, Compose(Nodes[ChainIdx[K]].LocalPos, Nodes[ChainIdx[K]].LocalQ));
        return M;
    }
    FQ WorldQuat(int32 NodeIndex) const { return NodeIndex < 0 ? DecomposeQuat(BaseMatrix) : DecomposeQuat(WorldMatrix(NodeIndex)); }
    FV3 WorldPos(int32 NodeIndex) const { const FM4 M = WorldMatrix(NodeIndex); return FV3(M.E[12], M.E[13], M.E[14]); }

    void BuildRig(const FInit& Init)
    {
        RootPos = ToV3(Init.RootPosition);
        RootYaw = Init.RootYaw;
        const FQ RootQ = YawQuat(Init.RootYaw);
        const FQ PivotQ = Init.bVrm0 ? YawQuat(Pi) : FQ();
        PivotRot = PivotQ;
        BaseMatrix = MMul(Compose(RootPos, RootQ), Compose(FV3(), PivotQ));
        BaseQ = QMul(RootQ, PivotQ);
        const bool bRotated = Init.RootYaw != 0 || Init.bVrm0;
        const FQ BaseInv = Conj(BaseQ);

        auto DefaultParent = [&](FName Bone) -> FName
        {
            for (const FChainRow& Row : Chain)
            {
                if (Bone != FName(Row.Bone)) continue;
                const TCHAR* Up = Row.Parent;
                while (Up)
                {
                    if (Init.RestWorld.Contains(FName(Up))) return FName(Up);
                    const TCHAR* Next = nullptr;
                    for (const FChainRow& R2 : Chain) if (FName(R2.Bone) == FName(Up)) { Next = R2.Parent; break; }
                    Up = Next;
                }
                return NAME_None;
            }
            return NAME_None;
        };
        // nodes in chain order (parents first), only bones the caller supplied
        TMap<FName, int32> Index;
        for (const FChainRow& Row : Chain)
        {
            const FName Bone(Row.Bone);
            const FVector* RW = Init.RestWorld.Find(Bone);
            if (!RW) continue;
            const FName* Explicit = Init.Parents.Find(Bone);
            const FName ParentName = Explicit ? *Explicit : DefaultParent(Bone);
            const int32* ParentIdx = ParentName.IsNone() ? nullptr : Index.Find(ParentName);
            FNode Node;
            Node.Name = Bone;
            Node.Parent = ParentIdx ? *ParentIdx : -1;
            const FV3 Base = ParentIdx ? ToV3(Init.RestWorld[Nodes[*ParentIdx].Name]) : RootPos;
            const FV3 Offset = Sub(ToV3(*RW), Base);
            Node.LocalPos = bRotated ? ApplyQ(Offset, BaseInv) : Offset;
            if (const FQuat* SQ = Init.StartLocal.Find(Bone)) Node.LocalQ = FQ(SQ->X, SQ->Y, SQ->Z, SQ->W);
            Index.Add(Bone, Nodes.Add(Node));
        }
        for (int32 J = 0; J < NJ; ++J) { const int32* I = Index.Find(FName(JointNames[J])); JointNode[J] = I ? *I : -1; }
    }

    // ---- ragdoll.js
    void BodyFrame(const FV3* Pos)
    {
        FV3& R = FrameR; FV3& U = FrameU; FV3& F = FrameF;
        if (Has[LUL] && Has[RUL]) R = Sub(Pos[LUL], Pos[RUL]);
        else if (Has[LUA] && Has[RUA]) R = Sub(Pos[LUA], Pos[RUA]);
        else R = FV3(1, 0, 0);
        if (Has[Head] && Has[Hips]) U = Sub(Pos[Head], Pos[Hips]);
        else if (Has[Chest] && Has[Hips]) U = Sub(Pos[Chest], Pos[Hips]);
        else U = FV3(0, 1, 0);
        if (LenSq(R) < 1e-10) R = FV3(1, 0, 0);
        if (LenSq(U) < 1e-10) U = FV3(0, 1, 0);
        R = Norm(R); U = Norm(U);
        FV3 V = Cross(R, U);
        if (LenSq(V) > 1e-8) F = Norm(V);
        else if (LenSq(F) < 1e-8) F = FV3(0, 0, 1);
        V = Cross(U, F);
        if (LenSq(V) > 1e-8) R = Norm(V);
    }

    static bool BasisOf(const FV3& Dir, const FV3& Ref, FQ& Out)
    {
        FV3 Bz = Ref; AddScaled(Bz, Dir, -Dot(Ref, Dir));
        if (LenSq(Bz) < 1e-4) return false;
        Bz = Norm(Bz);
        const FV3 By = Cross(Bz, Dir);
        Out = FromRotationMatrix(MakeBasis(Dir, By, Bz));
        return true;
    }

    double Clear(int32 J) const { return JsMax(0.02, Radius[J]); }

    void Topple(const FV3& Lean)
    {
        double Lo = std::numeric_limits<double>::infinity(), Hi = -std::numeric_limits<double>::infinity();
        for (int32 J = 0; J < NJ; ++J)
        {
            if (!Has[J]) continue;
            if (P[J].Y < Lo) Lo = P[J].Y;
            if (P[J].Y > Hi) Hi = P[J].Y;
        }
        double Span = Hi - Lo;
        if (Span == 0 || Span != Span) Span = 1;
        for (int32 J = 0; J < NJ; ++J) if (Has[J]) AddScaled(Prev[J], Lean, -(P[J].Y - Lo) / Span * FixedDt);
    }

    void BuildCapsules()
    {
        const double TorsoR = TorsoRadius(Has, Rest);
        for (int32 J = 0; J < NJ; ++J) if (Has[J]) Radius[J] = TorsoR * RadiusFrac[J];
        for (int32 B = 0; B < NBones; ++B)
        {
            const int32 A0 = Bones[B][0], A1 = Bones[B][1];
            if (Has[A0] && Has[A1]) Caps.Add({ A0, A1, (Radius[A0] + Radius[A1]) / 2 });
        }
        for (int32 I = 0; I < Caps.Num(); ++I)
        {
            for (int32 K = I + 1; K < Caps.Num(); ++K)
            {
                const FCap& A = Caps[I]; const FCap& B = Caps[K];
                if (A.A == B.A || A.A == B.B || A.B == B.A || A.B == B.B) continue;
                const double Min = A.R + B.R;
                double S, Tt;
                ClosestParams(Rest[A.A], Rest[A.B], Rest[B.A], Rest[B.B], S, Tt);
                const FV3 Ca = Lerp(Rest[A.A], Rest[A.B], S), Cb = Lerp(Rest[B.A], Rest[B.B], Tt);
                if (Dist(Ca, Cb) < Min * 1.02) continue;
                Pairs.Add({ I, K, Min });
            }
        }
    }

    void Construct(const FInit& Init)
    {
        T = Init.Tuning;
        HeightAtFn = Init.HeightAt ? Init.HeightAt : FHeightFn([](double, double) { return 0.0; });
        ResolveFn = Init.ResolveColliders;
        BuildRig(Init);

        for (int32 J = 0; J < NJ; ++J)
        {
            DriveBy[J] = -1;
            if (JointNode[J] < 0) continue;
            Has[J] = true;
            const FV3 Wp = WorldPos(JointNode[J]);
            P[J] = Wp;
            const FVector* V = Init.Seed.IsSet() ? nullptr : Init.SeedVelocity.Find(FName(JointNames[J]));
            Prev[J] = Wp;
            if (V) AddScaled(Prev[J], ToV3(*V), -FixedDt);
            Pre[J] = Wp;
        }
        if (Init.Seed.IsSet())
        {
            const FSnapshot& S = Init.Seed.GetValue();
            for (int32 I = 0; I < S.J.Num(); ++I)
            {
                int32 J = -1;
                for (int32 K = 0; K < NJ; ++K) if (S.J[I] == FName(JointNames[K])) { J = K; break; }
                if (J < 0 || !Has[J] || !S.P.IsValidIndex(I * 3 + 2) || !S.V.IsValidIndex(I * 3 + 2)) continue;
                P[J] = FV3(S.P[I * 3], S.P[I * 3 + 1] + S.Dy, S.P[I * 3 + 2]);
                Prev[J] = P[J];
                AddScaled(Prev[J], FV3(S.V[I * 3], S.V[I * 3 + 1], S.V[I * 3 + 2]), -FixedDt);
            }
        }
        for (int32 J = 0; J < NJ; ++J) if (Has[J]) Rest[J] = ToV3(Init.RestWorld[FName(JointNames[J])]);

        if (Init.Lean.IsSet()) Topple(ToV3(Init.Lean.GetValue()));

        for (int32 J = 0; J < NJ; ++J) if (Has[J]) Iw[J] = 1 / Mass[J];

        auto AddLink = [&](int32 A, int32 B, bool bBrace)
        {
            if (!Has[A] || !Has[B]) return;
            const double L = Dist(Rest[A], Rest[B]);
            if (L > 1e-5) Links.Add({ A, B, L, bBrace });
        };
        for (int32 B = 0; B < NBones; ++B)
        {
            bool bBrace = false;
            for (int32 K = 0; K < NBraces; ++K) if (Braces[K][0] == Bones[B][0] && Braces[K][1] == Bones[B][1]) bBrace = true;
            AddLink(Bones[B][0], Bones[B][1], bBrace);
        }
        for (int32 K = 0; K < NBraces; ++K) AddLink(Braces[K][0], Braces[K][1], true);

        BodyFrame(Rest);
        const FV3 R0 = FrameR, U0 = FrameU, F0 = FrameF;
        auto ToLocal = [&](const FV3& W) { return FV3(Dot(W, R0), Dot(W, U0), Dot(W, F0)); };

        for (const FFlexRow& Row : FlexRows)
        {
            if (!Has[Row.A] || !Has[Row.B] || !Has[Row.C]) continue;
            if (Dist(Rest[Row.B], Rest[Row.A]) < 0.03 || Dist(Rest[Row.C], Rest[Row.B]) < 0.03) continue;
            const FV3 A = Norm(Sub(Rest[Row.B], Rest[Row.A])), B = Norm(Sub(Rest[Row.C], Rest[Row.B]));
            const double At = std::acos(Clamp(Dot(A, B), -1, 1));
            Flex.Add({ Row.A, Row.B, Row.C, JsMin(Pi, At + Row.MaxDeg * D2R) });
        }
        for (const FBehindRow& Row : BehindRows)
            if (Has[Row.B] && Has[Row.C]) Behind.Add({ Row.B, Row.C, -JsSin(Row.Deg * D2R) });
        for (const FConeRow& Row : ConeRows)
        {
            if (!Has[Row.B] || !Has[Row.C]) continue;
            FV3 Axis = ToLocal(Norm(Sub(Rest[Row.C], Rest[Row.B])));
            if (Row.Tilt != 0)
            {
                FV3 V(0, 0, 1); AddScaled(V, Axis, -Axis.Z);
                if (LenSq(V) > 1e-8) AddScaled(Axis, Norm(V), std::tan(Row.Tilt * D2R));
            }
            Cone.Add({ Row.B, Row.C, Norm(Axis), JsCos(Row.Half * D2R) });
        }
        for (const FHingeRow& Row : HingeRows)
        {
            if (!Has[Row.A] || !Has[Row.B] || !Has[Row.C]) continue;
            FV3 A = Sub(Rest[Row.B], Rest[Row.A]);
            if (LenSq(A) < 1e-8) continue;
            A = Norm(A);
            FV3 C = Mul(F0, Row.Dir);
            AddScaled(C, A, -Dot(C, A));
            if (LenSq(C) < 1e-8) continue;
            Hinge.Add({ Row.A, Row.B, Row.C, Row.MaxFlex * D2R, JsSin(Row.Side * D2R), Norm(Cross(A, Norm(C))) });
        }
        if (Init.Seed.IsSet())
        {
            const TArray<double>& H = Init.Seed->H;
            const bool bH = H.Num() == Hinge.Num() * 3;
            for (int32 I = 0; I < Hinge.Num(); ++I)
            {
                FHinge& Hg = Hinge[I];
                if (bH)
                {
                    Hg.N = FV3(H[I * 3], H[I * 3 + 1], H[I * 3 + 2]);
                    if (LenSq(Hg.N) > 1e-8) { Hg.N = Norm(Hg.N); continue; }
                }
                FV3 A = Sub(P[Hg.B], P[Hg.A]);
                if (LenSq(A) < 1e-8) continue;
                A = Norm(A);
                FV3 C = Cross(Hg.N, A);
                AddScaled(C, A, -Dot(C, A));
                if (LenSq(C) < 1e-8) continue;
                Hg.N = Norm(Cross(A, Norm(C)));
            }
        }

        BuildCapsules();

        BodyFrame(P);
        const FQ BodyRest = FromRotationMatrix(MakeBasis(FrameR, FrameU, FrameF));
        const FV3 Axes[3] = { FrameR, FrameU, FrameF };
        for (int32 S = 0; S < NSeg; ++S)
        {
            const int32 Bone = Segments[S][0], Child = Segments[S][1];
            if (!Has[Bone] || !Has[Child]) continue;
            const FV3 Bwp = WorldPos(JointNode[Bone]);
            const FV3 V = Sub(P[Child], Bwp);
            if (LenSq(V) < 1e-8) continue;
            const FV3 Dir = Norm(V);
            int32 Ref = 0;
            for (int32 I = 1; I < 3; ++I) if (std::fabs(Dot(Dir, Axes[I])) < std::fabs(Dot(Dir, Axes[Ref]))) Ref = I;
            FQ RestFrame;
            if (!BasisOf(Dir, Axes[Ref], RestFrame)) continue;
            const int32 Tp = TwistParent[Bone];
            const int32 TpDrive = Tp >= 0 ? DriveBy[Tp] : -1;
            if (Tp >= 0 && TpDrive < 0) continue;
            const FQ ParentRest = Tp >= 0 ? Drive[TpDrive].RestFrame : BodyRest;
            FDrive D;
            D.Bone = Bone; D.Child = Child; D.TwistParentDrive = TpDrive;
            D.RestFrame = RestFrame;
            D.RestFrameInv = Conj(RestFrame);
            D.RestLocal = QMul(Conj(ParentRest), RestFrame);
            D.RestLocalAxis = ApplyQ(FV3(1, 0, 0), D.RestLocal);
            D.RestQuat = WorldQuat(JointNode[Bone]);
            D.ParentNode = Nodes[JointNode[Bone]].Parent;
            D.Tw = 0; D.Twv = 0;
            D.TwMax = (TwistDeg[Bone] >= 0 ? TwistDeg[Bone] : 180) * D2R;
            D.FrameW = RestFrame; D.SwingW = RestFrame;
            DriveBy[Bone] = Drive.Add(D);
        }

        RootStartY = RootPos.Y;
        HipsOffset = Rest[Hips].Y - RootPos.Y;
        // bodyengine._measureHipsLocal
        const double Dx = Rest[Hips].X - RootPos.X, Dz = Rest[Hips].Z - RootPos.Z;
        const double C = JsCos(RootYaw), Sn = JsSin(RootYaw);
        HipsLocalX = Dx * C - Dz * Sn;
        HipsLocalZ = Dx * Sn + Dz * C;
    }

    bool Pinned() const { return Pins.Num() > 0; }

    void Stop(int32 J, double Side)
    {
        const double Vn = Dot(Sub(P[J], Prev[J]), S1);
        if (Vn * Side < 0) AddScaled(Prev[J], S1, Vn);
    }

    void Swing(int32 B, int32 C, const FV3& Dir, double Lb)
    {
        const double Wb = Iw[B], Wc = Iw[C], Ws = Wb + Wc;
        if (Ws <= 0) return;
        const FV3 Tv = Add(Sub(Mul(Dir, Lb), P[C]), P[B]);
        const bool bDead = LenSq(Tv) > Lb * Lb * 0.25;
        S1 = Norm(Tv);
        FV3 U = Mul(Tv, Wc / Ws);
        P[C] = Add(P[C], U);
        if (bDead) Prev[C] = P[C]; else { Prev[C] = Add(Prev[C], U); Stop(C, 1); }
        U = Mul(Tv, -Wb / Ws);
        P[B] = Add(P[B], U);
        if (bDead) Prev[B] = P[B]; else { Prev[B] = Add(Prev[B], U); Stop(B, -1); }
    }

    void Pin(double Dt)
    {
        if (!Pins.Num()) { PinFrom.Reset(); return; }
        const double K = Dt / FixedDt;
        for (const TPair<int32, FV3>& Pn : Pins)
        {
            const int32 J = Pn.Key;
            if (!Has[J]) continue;
            const FV3* From = nullptr;
            for (const TPair<int32, FV3>& F : PinFrom) if (F.Key == J) From = &F.Value;
            P[J] = Pn.Value;
            if (!T.bPinVel) continue;
            FV3 V = Sub(Pn.Value, From ? *From : Pn.Value);
            if (!From || LenSq(V) > PinJump * PinJump) Prev[J] = Pn.Value;
            else Prev[J] = Sub(Pn.Value, Mul(V, K));
        }
        PinFrom.RemoveAll([&](const TPair<int32, FV3>& F) { return !Pins.ContainsByPredicate([&](const TPair<int32, FV3>& X) { return X.Key == F.Key; }); });
    }

    void SolveLinks()
    {
        for (const FLink& L : Links)
        {
            if (L.bBrace && !T.bOnBrace) continue;
            const double Wa = Iw[L.A], Wb = Iw[L.B], Ws = Wa + Wb;
            if (Ws <= 0) continue;
            FV3 V = Sub(P[L.B], P[L.A]);
            double D = Len(V);
            if (D == 0 || D != D) D = 1e-4;
            V = Mul(V, (D - L.Len) / D / Ws);
            AddScaled(P[L.A], V, Wa);
            AddScaled(P[L.B], V, -Wb);
        }
    }

    void SolveCapsules()
    {
        if (!T.bOnCapsule) return;
        for (const FPair& Pr : Pairs)
        {
            const FCap& A = Caps[Pr.CA]; const FCap& B = Caps[Pr.CB];
            double S, Tt;
            ClosestParams(P[A.A], P[A.B], P[B.A], P[B.B], S, Tt);
            const FV3 Ca = Lerp(P[A.A], P[A.B], S), Cb = Lerp(P[B.A], P[B.B], Tt);
            FV3 N = Sub(Cb, Ca);
            const double D = Len(N);
            if (D >= Pr.Min || D < 1e-6) continue;
            N = DivS(N, D);
            const double WA0 = (1 - S) * Iw[A.A], WA1 = S * Iw[A.B];
            const double WB0 = (1 - Tt) * Iw[B.A], WB1 = Tt * Iw[B.B];
            const double Tot = WA0 + WA1 + WB0 + WB1;
            if (Tot <= 1e-9) continue;
            const double Pen = (Pr.Min - D) * T.CapsuleSoft / Tot;
            AddScaled(P[A.A], N, -Pen * WA0);
            AddScaled(P[A.B], N, -Pen * WA1);
            AddScaled(P[B.A], N, Pen * WB0);
            AddScaled(P[B.B], N, Pen * WB1);
            S1 = N;
            Stop(A.A, -1); Stop(A.B, -1);
            Stop(B.A, 1); Stop(B.B, 1);
        }
    }

    void SolveTerrain()
    {
        for (int32 J = 0; J < NJ; ++J)
        {
            if (!Has[J]) continue;
            const double G = HeightAtFn(P[J].X, P[J].Z) + Clear(J);
            if (P[J].Y < G) P[J].Y = G;
        }
    }

    void SolveWorld()
    {
        if (!T.bOnGround) return;
        for (int32 J = 0; J < NJ; ++J)
        {
            if (!Has[J]) continue;
            FV3& Pj = P[J];
            const double X0 = Pj.X, Z0 = Pj.Z;
            const double R = Clear(J);
            double Ground;
            if (ResolveFn)
            {
                FVector Probe(Pj.X, Pj.Y, Pj.Z);
                Ground = ResolveFn(Probe, HeightAtFn, R, R);
                Pj.X = Probe.X; Pj.Z = Probe.Z;
            }
            else Ground = HeightAtFn(Pj.X, Pj.Z);
            const double G = Ground + R;
            const bool bPushed = std::fabs(Pj.X - X0) > 1e-5 || std::fabs(Pj.Z - Z0) > 1e-5;
            if (Pj.Y < G)
            {
                Pj.Y = G;
                Prev[J].X += (Pj.X - Prev[J].X) * 0.4;
                Prev[J].Z += (Pj.Z - Prev[J].Z) * 0.4;
            }
            if (bPushed)
            {
                Prev[J].X += (Pj.X - Prev[J].X) * 0.3;
                Prev[J].Z += (Pj.Z - Prev[J].Z) * 0.3;
            }
        }
    }

    void SolveLimits()
    {
        if (T.bOnFlex) for (const FFlex& Fl : Flex)
        {
            FV3 A = Sub(P[Fl.B], P[Fl.A]); const double La = Len(A);
            FV3 B = Sub(P[Fl.C], P[Fl.B]); const double Lb = Len(B);
            if (La < 1e-5 || Lb < 1e-5) continue;
            A = DivS(A, La); B = DivS(B, Lb);
            const double Ang = std::acos(Clamp(Dot(A, B), -1, 1));
            if (Ang <= Fl.Max) continue;
            FV3 N = Cross(A, B);
            if (LenSq(N) < 1e-10) continue;
            N = Norm(N);
            const FQ Qd = AxisAngle(N, (Fl.Max - Ang) * T.Yield);
            Swing(Fl.B, Fl.C, ApplyQ(B, Qd), Lb);
        }

        const FV3 R = FrameR, U = FrameU, F = FrameF;
        if (T.bOnCone) for (const FCone& Cn : Cone)
        {
            FV3 B = Sub(P[Cn.C], P[Cn.B]); const double Lb = Len(B);
            if (Lb < 1e-5) continue;
            B = DivS(B, Lb);
            FV3 N = Mul(R, Cn.Axis.X); AddScaled(N, U, Cn.Axis.Y); AddScaled(N, F, Cn.Axis.Z); N = Norm(N);
            const double D = Dot(B, N);
            if (D >= Cn.Cos) continue;
            const double Ang = std::acos(Clamp(D, -1, 1));
            const double Lim = std::acos(Cn.Cos);
            FV3 V = Cross(N, B);
            if (LenSq(V) < 1e-10) continue;
            V = Norm(V);
            const FQ Qd = AxisAngle(V, -(Ang - Lim) * T.Yield);
            Swing(Cn.B, Cn.C, ApplyQ(B, Qd), Lb);
        }

        if (T.bOnBehind) for (const FBehind& Bh : Behind)
        {
            FV3 B = Sub(P[Bh.C], P[Bh.B]); const double Lb = Len(B);
            if (Lb < 1e-5) continue;
            B = DivS(B, Lb);
            const double Fwd = Dot(B, FrameF);
            if (Fwd >= Bh.MinFwd) continue;
            FV3 V = Cross(B, FrameF);
            if (LenSq(V) < 1e-10) continue;
            V = Norm(V);
            const double Need = std::asin(Clamp(Bh.MinFwd, -1, 1)) - std::asin(Clamp(Fwd, -1, 1));
            const FQ Qd = AxisAngle(V, Need * T.Yield);
            Swing(Bh.B, Bh.C, ApplyQ(B, Qd), Lb);
        }

        if (T.bOnHinge) for (FHinge& H : Hinge)
        {
            FV3 A = Sub(P[H.B], P[H.A]); const double La = Len(A);
            FV3 B = Sub(P[H.C], P[H.B]); const double Lb = Len(B);
            if (La < 1e-5 || Lb < 1e-5) continue;
            A = DivS(A, La); B = DivS(B, Lb);
            FV3 N = H.N;
            AddScaled(N, A, -Dot(N, A));
            if (LenSq(N) < 1e-8)
            {
                N = Cross(A, FrameF);
                if (LenSq(N) < 1e-8) continue;
            }
            N = Norm(N);
            H.N = N;
            const FV3 C = Cross(N, A);
            bool bChanged = false;
            const double Oop = Dot(B, N);
            const double Lim2 = Clamp(Oop, -H.Side, H.Side);
            if (Oop != Lim2) { AddScaled(B, N, (Lim2 - Oop) * T.Yield); bChanged = true; }
            const double FlexC = Dot(B, C);
            if (FlexC < -0.03) { AddScaled(B, C, -FlexC * T.Yield); bChanged = true; }
            if (bChanged) B = Norm(B);
            const double Ang = std::acos(Clamp(Dot(B, A), -1, 1));
            if (Ang > H.Max)
            {
                FV3 V = Cross(A, B);
                if (LenSq(V) > 1e-10)
                {
                    const FQ Qd = AxisAngle(Norm(V), (H.Max - Ang) * T.Yield);
                    B = ApplyQ(B, Qd);
                    bChanged = true;
                }
            }
            if (bChanged) Swing(H.B, H.C, B, Lb);
        }
    }

    void Substep(double Dt, double DampS, int32 Iters)
    {
        for (int32 J = 0; J < NJ; ++J)
        {
            if (!Has[J]) continue;
            const FV3 V = Mul(Sub(P[J], Prev[J]), DampS);
            Prev[J] = P[J];
            P[J] = Add(P[J], V);
            P[J].Y += T.Gravity * Dt * Dt;
        }
        Pin(Dt);
        BodyFrame(P);
        for (int32 It = 0; It < Iters; ++It)
        {
            SolveLinks();
            SolveCapsules();
            SolveTerrain();
            SolveLimits();
        }
    }

    void Solve()
    {
        const double DampF = MaxV < T.SettleV * 4 ? T.SleepDamp : T.Damp;
        const int32 N = FMath::Max(1, T.Substeps);
        const double Sdt = FixedDt / N;
        const double Sdamp = std::pow(DampF, 1.0 / N);
        for (int32 J = 0; J < NJ; ++J) if (Has[J]) Pre[J] = P[J];
        const int32 Iters = Pinned() ? T.IterPinned : T.Iter;
        for (int32 S = 0; S < N; ++S) Substep(Sdt, Sdamp, Iters);
        SolveWorld();
        SolveLimits();
        double Moved = 0;
        for (int32 J = 0; J < NJ; ++J) if (Has[J]) Moved = JsMax(Moved, DistSq(P[J], Pre[J]));
        MaxV = std::sqrt(Moved) / FixedDt;
    }

    void SettleTick(double Dt, bool bQuiet, bool bCancel)
    {
        Elapsed += Dt;
        if (Pinned()) { SettledFor = 0; Elapsed = 0; return; }
        if (bQuiet) SettledFor += Dt;
        else if (bCancel) SettledFor = 0;
    }

    void FollowRoot()
    {
        const FV3& H = P[Hips];
        const double C = JsCos(RootYaw), S = JsSin(RootYaw);
        RootPos.X = H.X - (HipsLocalX * C + HipsLocalZ * S);
        RootPos.Z = H.Z - (-HipsLocalX * S + HipsLocalZ * C);
        const double Y = H.Y - HipsOffset;
        if (Pinned() && Y > RootStartY) RootStartY = Y;
        RootPos.Y = JsMin(RootStartY, Y);
    }

    const FStepResult& StepImpl(double DtIn)
    {
        if (bDone) { Result.bActive = false; return Result; }
        double Dt = DtIn != DtIn ? 0 : DtIn;
        Dt = JsMin(0.25, JsMax(0, Dt));
        if (T.bPaused) Dt = 0;
        Acc += Dt;
        int32 N = 0;
        while (Acc >= FixedDt && N < MaxFrames) { Solve(); Acc -= FixedDt; ++N; }
        if (N == MaxFrames) Acc = 0;
        if (Pins.Num())
        {
            for (const TPair<int32, FV3>& Pn : Pins)
            {
                TPair<int32, FV3>* F = PinFrom.FindByPredicate([&](const TPair<int32, FV3>& X) { return X.Key == Pn.Key; });
                if (F) F->Value = Pn.Value; else PinFrom.Add(Pn);
            }
        }
        ++Steps;
        FramesRun += N;
        SettleTick(Dt, MaxV < T.SettleV, MaxV > T.SettleV * T.SettleReset);
        Result.bActive = true;
        Result.MaxV = MaxV;
        if (N == 0 && bHavePose) return Result;

        FollowRoot();

        const FQ QBody = FromRotationMatrix(MakeBasis(FrameR, FrameU, FrameF));
        const double DtF = JsMax(1e-4, N * FixedDt);
        TMap<FName, FQuat> Pose;
        for (FDrive& D : Drive)
        {
            FV3 B = Sub(P[D.Child], P[D.Bone]);
            if (LenSq(B) < 1e-6) continue;
            B = Norm(B);
            const FQ Qp = D.TwistParentDrive >= 0 ? Drive[D.TwistParentDrive].FrameW : QBody;
            const FV3 T2 = ApplyQ(B, Conj(Qp));
            if (Dot(T2, D.RestLocalAxis) < -0.999) continue;
            FQ Qs = FromUnitVectors(D.RestLocalAxis, T2);
            Qs = QMul(Qs, D.RestLocal);
            const FQ QL = QMul(Qp, Qs);
            const double RollRate = TwistAbout(QMul(QL, Conj(D.SwingW)), B);
            D.SwingW = QL;
            D.Twv -= RollRate * T.TwistLag / DtF;
            D.Twv *= T.TwistDamp;
            D.Twv -= D.Tw * T.TwistStiff * DtF;
            D.Tw += D.Twv * DtF;
            if (!T.bOnTwist) { D.Tw = 0; D.Twv = 0; }
            if (D.Tw > D.TwMax) { D.Tw = D.TwMax; D.Twv = 0; }
            else if (D.Tw < -D.TwMax) { D.Tw = -D.TwMax; D.Twv = 0; }
            Qs = QMul(Qs, AxisAngle(FV3(1, 0, 0), D.Tw));
            D.FrameW = QMul(Qp, Qs);
            const FQ Qd = QMul(QMul(D.FrameW, D.RestFrameInv), D.RestQuat);
            const FQ Local = QMul(Conj(WorldQuat(D.ParentNode)), Qd);
            Nodes[JointNode[D.Bone]].LocalQ = Local;
            Pose.Add(FName(JointNames[D.Bone]), FQuat(Fixed4(Local.X), Fixed4(Local.Y), Fixed4(Local.Z), Fixed4(Local.W)));
        }
        Result.Pose = MoveTemp(Pose);
        bHavePose = true;
        Result.RootPosition = ToVec(RootPos);
        Result.RootYaw = RootYaw;
        if (SettledFor >= T.SettleTime || Elapsed >= T.Deadline)
        {
            bDone = true;
            Result.bDone = true;
            Result.bSettled = SettledFor >= T.SettleTime;
        }
        return Result;
    }

    int32 JointIndex(FName Name) const
    {
        for (int32 J = 0; J < NJ; ++J) if (Name == FName(JointNames[J])) return J;
        return -1;
    }
    void UnpinIndex(int32 J)
    {
        Iw[J] = 1 / Mass[J];
        Pins.RemoveAll([J](const TPair<int32, FV3>& X) { return X.Key == J; });
    }
};

FRagdoll::FRagdoll(const FInit& Init) : Impl(MakeUnique<FImpl>())
{
    check(Init.RestWorld.Contains(FName(TEXT("hips"))));
    Impl->Construct(Init);
    Impl->Result.RootPosition = Init.RootPosition;
    Impl->Result.RootYaw = Init.RootYaw;
}
FRagdoll::~FRagdoll() = default;

const FStepResult& FRagdoll::Step(double Dt) { return Impl->StepImpl(Dt); }

void FRagdoll::Impulse(const FVector& V)
{
    if (Impl->bDone) return;
    FV3 Iv = ToV3(V);
    if (LenSq(Iv) > ImpulseCap * ImpulseCap) Iv = Mul(Norm(Iv), ImpulseCap);   // setLength
    Impl->Topple(Iv);
    Impl->SettledFor = 0;
    Impl->Elapsed = 0;
}

void FRagdoll::PinJoint(FName Joint, const FVector& Target, bool)
{
    const int32 J = Impl->JointIndex(Joint);
    if (J < 0 || !Impl->Has[J]) return;
    const FV3 Tg = ToV3(Target);
    if (!std::isfinite(Tg.X + Tg.Y + Tg.Z)) { Impl->UnpinIndex(J); return; }
    if (TPair<int32, FV3>* Existing = Impl->Pins.FindByPredicate([J](const TPair<int32, FV3>& X) { return X.Key == J; })) Existing->Value = Tg;
    else Impl->Pins.Add(TPair<int32, FV3>(J, Tg));
    Impl->Iw[J] = 0;
}
void FRagdoll::Unpin(FName Joint)
{
    const int32 J = Impl->JointIndex(Joint);
    if (J >= 0 && Impl->Has[J]) Impl->UnpinIndex(J);
}
void FRagdoll::UnpinAll()
{
    const TArray<TPair<int32, FV3>> Copy = Impl->Pins;
    for (const TPair<int32, FV3>& Pn : Copy) Impl->UnpinIndex(Pn.Key);
}
bool FRagdoll::IsPinned() const { return Impl->Pinned(); }
bool FRagdoll::IsDone() const { return Impl->bDone; }

FSnapshot FRagdoll::Snapshot() const
{
    FSnapshot S;
    for (int32 J = 0; J < NJ; ++J)
    {
        if (!Impl->Has[J]) continue;
        const FV3 V = DivS(Sub(Impl->P[J], Impl->Prev[J]), FixedDt);
        S.J.Add(FName(JointNames[J]));
        S.P.Append({ Impl->P[J].X, Impl->P[J].Y, Impl->P[J].Z });
        S.V.Append({ V.X, V.Y, V.Z });
    }
    for (const FImpl::FHinge& H : Impl->Hinge) S.H.Append({ H.N.X, H.N.Y, H.N.Z });
    return S;
}

bool FRagdoll::GetParticle(FName Joint, FVector& Out) const
{
    const int32 J = Impl->JointIndex(Joint);
    if (J < 0 || !Impl->Has[J]) return false;
    Out = ToVec(Impl->P[J]);
    return true;
}
double FRagdoll::GetMaxV() const { return Impl->MaxV; }
double FRagdoll::GetSettledFor() const { return Impl->SettledFor; }
double FRagdoll::GetElapsed() const { return Impl->Elapsed; }
FVector FRagdoll::GetRootPosition() const { return ToVec(Impl->RootPos); }
const FStepResult& FRagdoll::LastResult() const { return Impl->Result; }

TMap<FName, FQuat> FRagdoll::GetLocalRotations() const
{
    TMap<FName, FQuat> Out;
    for (const FImpl::FNode& N : Impl->Nodes) Out.Add(N.Name, FQuat(N.LocalQ.X, N.LocalQ.Y, N.LocalQ.Z, N.LocalQ.W));
    return Out;
}

TMap<FName, FVector> FRagdoll::RestPositionsAt(const FVector& RootPosition) const
{
    // the same root -> pivot -> bones chain with every local rotation identity
    const FQ RootQ = YawQuat(Impl->RootYaw);
    const FM4 Base = MMul(Compose(ToV3(RootPosition), RootQ), Compose(FV3(), Impl->PivotRot));
    TMap<FName, FVector> Out;
    for (int32 I = 0; I < Impl->Nodes.Num(); ++I)
    {
        int32 ChainIdx[64]; int32 N = 0;
        for (int32 K = I; K >= 0 && N < 64; K = Impl->Nodes[K].Parent) ChainIdx[N++] = K;
        FM4 M = Base;
        for (int32 K = N - 1; K >= 0; --K) M = MMul(M, Compose(Impl->Nodes[ChainIdx[K]].LocalPos, FQ()));
        Out.Add(Impl->Nodes[I].Name, FVector(M.E[12], M.E[13], M.E[14]));
    }
    return Out;
}
}
