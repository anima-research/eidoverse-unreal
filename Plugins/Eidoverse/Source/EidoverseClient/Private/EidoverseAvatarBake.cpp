// A skeleton that hangs under a transformed node that is not a joint -- an "Armature" carrying scale 1.8 and a
// quarter turn, say (antra1.vrm) -- is posed by that transform in glTF and in three.js: a joint's world matrix
// includes every ancestor, so the body is skinned as (ancestors x joint) x inverseBind x v. glTFRuntime builds the
// reference skeleton from the inverse bind matrices alone and drops the ancestors, so such a body came out at its
// raw size (antra1: 1.0 m instead of 1.66 m) and unturned.
//
// Bake the difference in before parsing, per skin: D = rootJointWorld x rootInverseBind (identity for a skeleton
// whose parents are plain, i.e. nearly every file, which is left byte-for-byte alone). Skinned vertices become D v
// (normals by D's inverse transpose, tangents and morph deltas by its linear part) and every inverse bind matrix
// IBM D^-1, so the imported skeleton and mesh are already in the scene's frame and nothing downstream (clips, hips
// height, springs, gaze, plates, rigid parts) needs to know. All or nothing: any accessor this can't rewrite
// exactly (quantized, sparse, shared across skins with different parents) leaves the file as it came.
#include "EidoverseAvatar.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace
{
    using FJsonArray = TArray<TSharedPtr<FJsonValue>>;
    const FJsonArray& Arr(const TSharedPtr<FJsonObject>& J, const TCHAR* K) { static const FJsonArray E; const FJsonArray* A = nullptr; return J && J->TryGetArrayField(K, A) ? *A : E; }
    TSharedPtr<FJsonObject> At(const FJsonArray& A, int32 I) { return A.IsValidIndex(I) && A[I].IsValid() ? A[I]->AsObject() : nullptr; }
    int32 Int(const TSharedPtr<FJsonObject>& J, const TCHAR* K, int32 Def = -1) { double V; return J && J->TryGetNumberField(K, V) && V >= 0 && V == FMath::FloorToDouble(V) && V < 1e9 ? int32(V) : Def; }
    uint32 Word(const TArray<uint8>& B, int32 O) { return O >= 0 && O + 4 <= B.Num() ? uint32(B[O]) | uint32(B[O + 1]) << 8 | uint32(B[O + 2]) << 16 | uint32(B[O + 3]) << 24 : 0; }

    // 4x4, column vectors (glTF's convention): M[row][col]
    struct FM4
    {
        double M[4][4];
        static FM4 Identity() { FM4 R; for (int r = 0; r < 4; ++r) for (int c = 0; c < 4; ++c) R.M[r][c] = r == c; return R; }
        FM4 operator*(const FM4& B) const { FM4 R; for (int r = 0; r < 4; ++r) for (int c = 0; c < 4; ++c) { double S = 0; for (int k = 0; k < 4; ++k) S += M[r][k] * B.M[k][c]; R.M[r][c] = S; } return R; }
        double Det3() const { return M[0][0] * (M[1][1] * M[2][2] - M[1][2] * M[2][1]) - M[0][1] * (M[1][0] * M[2][2] - M[1][2] * M[2][0]) + M[0][2] * (M[1][0] * M[2][1] - M[1][1] * M[2][0]); }
        // affine inverse (the bottom row is 0 0 0 1 for every node and bind matrix this touches)
        FM4 Inverse() const
        {
            const double D = Det3(); FM4 R = Identity();
            R.M[0][0] = (M[1][1] * M[2][2] - M[1][2] * M[2][1]) / D; R.M[0][1] = (M[0][2] * M[2][1] - M[0][1] * M[2][2]) / D; R.M[0][2] = (M[0][1] * M[1][2] - M[0][2] * M[1][1]) / D;
            R.M[1][0] = (M[1][2] * M[2][0] - M[1][0] * M[2][2]) / D; R.M[1][1] = (M[0][0] * M[2][2] - M[0][2] * M[2][0]) / D; R.M[1][2] = (M[0][2] * M[1][0] - M[0][0] * M[1][2]) / D;
            R.M[2][0] = (M[1][0] * M[2][1] - M[1][1] * M[2][0]) / D; R.M[2][1] = (M[0][1] * M[2][0] - M[0][0] * M[2][1]) / D; R.M[2][2] = (M[0][0] * M[1][1] - M[0][1] * M[1][0]) / D;
            for (int r = 0; r < 3; ++r) R.M[r][3] = -(R.M[r][0] * M[0][3] + R.M[r][1] * M[1][3] + R.M[r][2] * M[2][3]);
            return R;
        }
        FM4 InverseTranspose3() const { const FM4 I = Inverse(); FM4 R = Identity(); for (int r = 0; r < 3; ++r) for (int c = 0; c < 3; ++c) R.M[r][c] = I.M[c][r]; return R; }
        void Apply(const float* In, float* Out, bool bPoint) const { for (int r = 0; r < 3; ++r) Out[r] = float(M[r][0] * In[0] + M[r][1] * In[1] + M[r][2] * In[2] + (bPoint ? M[r][3] : 0)); }
        bool NearIdentity() const { for (int r = 0; r < 3; ++r) for (int c = 0; c < 4; ++c) if (FMath::Abs(M[r][c] - (r == c)) > (c == 3 ? 1e-4 : 1e-5)) return false; return true; }
    };
    FM4 FromColumnMajor(const float* A) { FM4 R; for (int r = 0; r < 4; ++r) for (int c = 0; c < 4; ++c) R.M[r][c] = A[c * 4 + r]; return R; }
    FM4 NodeLocal(const TSharedPtr<FJsonObject>& N)
    {
        const FJsonArray& Mx = Arr(N, TEXT("matrix"));
        if (Mx.Num() == 16) { float A[16]; for (int i = 0; i < 16; ++i) A[i] = float(Mx[i]->AsNumber()); return FromColumnMajor(A); }
        const FJsonArray& T = Arr(N, TEXT("translation")); const FJsonArray& Q = Arr(N, TEXT("rotation")); const FJsonArray& S = Arr(N, TEXT("scale"));
        const double tx = T.Num() == 3 ? T[0]->AsNumber() : 0, ty = T.Num() == 3 ? T[1]->AsNumber() : 0, tz = T.Num() == 3 ? T[2]->AsNumber() : 0;
        double x = 0, y = 0, z = 0, w = 1; if (Q.Num() == 4) { x = Q[0]->AsNumber(); y = Q[1]->AsNumber(); z = Q[2]->AsNumber(); w = Q[3]->AsNumber(); }
        const double L = FMath::Sqrt(x * x + y * y + z * z + w * w); if (L > 0) { x /= L; y /= L; z /= L; w /= L; }
        const double sx = S.Num() == 3 ? S[0]->AsNumber() : 1, sy = S.Num() == 3 ? S[1]->AsNumber() : 1, sz = S.Num() == 3 ? S[2]->AsNumber() : 1;
        FM4 R = FM4::Identity();
        const double Rm[3][3] = {{1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)}, {2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)}, {2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)}};
        const double Sv[3] = {sx, sy, sz};
        for (int r = 0; r < 3; ++r) { for (int c = 0; c < 3; ++c) R.M[r][c] = Rm[r][c] * Sv[c]; }
        R.M[0][3] = tx; R.M[1][3] = ty; R.M[2][3] = tz;
        return R;
    }

    // A float accessor, tightly read (VEC3 = 3, VEC4 = 4, MAT4 = 16); false for anything this can't rewrite exactly
    bool ReadFloats(const TSharedPtr<FJsonObject>& Doc, const TArray<uint8>& Bin, int32 Index, int32 Comps, TArray<float>& Out)
    {
        const TSharedPtr<FJsonObject> A = At(Arr(Doc, TEXT("accessors")), Index); if (!A) return false;
        bool bNorm = false; A->TryGetBoolField(TEXT("normalized"), bNorm);
        FString Type; A->TryGetStringField(TEXT("type"), Type);
        const int32 Want = Type == TEXT("VEC3") ? 3 : Type == TEXT("VEC4") ? 4 : Type == TEXT("MAT4") ? 16 : 0;
        if (Int(A, TEXT("componentType")) != 5126 || bNorm || A->HasField(TEXT("sparse")) || Want != Comps) return false;
        const TSharedPtr<FJsonObject> V = At(Arr(Doc, TEXT("bufferViews")), Int(A, TEXT("bufferView"))); if (!V || Int(V, TEXT("buffer"), 0) != 0) return false;
        const int32 Count = Int(A, TEXT("count")), Stride = FMath::Max(Int(V, TEXT("byteStride"), 0), Comps * 4);
        const int64 Base = int64(Int(V, TEXT("byteOffset"), 0)) + Int(A, TEXT("byteOffset"), 0);
        if (Count < 0 || Base < 0 || (Count > 0 && Base + int64(Count - 1) * Stride + Comps * 4 > FMath::Min<int64>(Bin.Num(), int64(Int(V, TEXT("byteOffset"), 0)) + Int(V, TEXT("byteLength"), 0)))) return false;
        Out.SetNumUninitialized(Count * Comps);
        for (int32 i = 0; i < Count; ++i) FMemory::Memcpy(&Out[i * Comps], Bin.GetData() + Base + int64(i) * Stride, Comps * 4);
        for (float F : Out) if (!FMath::IsFinite(F)) return false;
        return true;
    }
}

bool UEidoverseAvatarAsset::BakeSkeletonParents(TArray<uint8>& Glb, const TSharedPtr<FJsonObject>& Doc, FString* Note)
{
    if (!Doc || Glb.Num() < 28) return false;
    const FJsonArray& Nodes = Arr(Doc, TEXT("nodes")); const FJsonArray& Skins = Arr(Doc, TEXT("skins")); const FJsonArray& Meshes = Arr(Doc, TEXT("meshes"));
    if (Skins.IsEmpty()) return false;
    TArray<int32> Parent; Parent.Init(-1, Nodes.Num());
    for (int32 I = 0; I < Nodes.Num(); ++I) for (const auto& C : Arr(At(Nodes, I), TEXT("children"))) { const int32 Ci = int32(C->AsNumber()); if (Parent.IsValidIndex(Ci)) Parent[Ci] = I; }
    auto World = [&](int32 I) { FM4 W = NodeLocal(At(Nodes, I)); for (int32 P = Parent[I], Guard = 0; P >= 0 && Guard < 128; P = Parent[P], ++Guard) W = NodeLocal(At(Nodes, P)) * W; return W; };

    // GLB chunks (ValidateGlb has already checked the layout)
    const uint32 JsonBytes = Word(Glb, 12); const int32 BinStart = 20 + int32(JsonBytes);
    if (Word(Glb, BinStart + 4) != 0x004e4942) return false;
    TArray<uint8> Bin(Glb.GetData() + BinStart + 8, int32(Word(Glb, BinStart)));

    // per skin: its parents' transform D, and the rewrites it implies
    TMap<int32, TArray<float>> NewData;   // accessor -> rewritten floats
    TMap<int32, int32> AccessorSkin;      // accessor -> the skin whose D rewrote it (a second skin must agree)
    TArray<FString> Notes;
    for (int32 S = 0; S < Skins.Num(); ++S)
    {
        const TSharedPtr<FJsonObject> Skin = At(Skins, S); const FJsonArray& Joints = Arr(Skin, TEXT("joints"));
        const int32 IbmAccessor = Int(Skin, TEXT("inverseBindMatrices"));
        if (Joints.IsEmpty() || IbmAccessor < 0) continue;   // no bind matrices: glTFRuntime reads the nodes
        TSet<int32> JointSet; for (const auto& J : Joints) JointSet.Add(int32(J->AsNumber()));
        int32 RootSlot = INDEX_NONE;
        for (int32 K = 0; K < Joints.Num() && RootSlot == INDEX_NONE; ++K) { const int32 N = int32(Joints[K]->AsNumber()); if (Parent.IsValidIndex(N) && !JointSet.Contains(Parent[N])) RootSlot = K; }
        TArray<float> Ibm;
        if (RootSlot == INDEX_NONE || !ReadFloats(Doc, Bin, IbmAccessor, 16, Ibm) || Ibm.Num() != Joints.Num() * 16) continue;
        const FM4 D = World(int32(Joints[RootSlot]->AsNumber())) * FromColumnMajor(&Ibm[RootSlot * 16]);
        if (D.NearIdentity()) continue;
        const double Det = D.Det3(), Scale = FMath::Pow(FMath::Abs(Det), 1.0 / 3);
        if (!(Det > 0) || Scale < 1e-3 || Scale > 1e3) { if (Note) *Note = FString::Printf(TEXT("skin %d parents not baked (determinant %.3g)"), S, Det); return false; }
        const FM4 Dinv = D.Inverse(), Normal = D.InverseTranspose3();
        // inverse bind matrices: IBM' = IBM D^-1
        if (AccessorSkin.Contains(IbmAccessor)) return false;
        AccessorSkin.Add(IbmAccessor, S);
        TArray<float>& OutIbm = NewData.Add(IbmAccessor);
        OutIbm.SetNumUninitialized(Ibm.Num());
        for (int32 K = 0; K < Joints.Num(); ++K)
        {
            const FM4 R = FromColumnMajor(&Ibm[K * 16]) * Dinv;
            for (int r = 0; r < 4; ++r) for (int c = 0; c < 4; ++c) OutIbm[K * 16 + c * 4 + r] = float(R.M[r][c]);
        }
        // the skinned geometry under this skin
        auto Rewrite = [&](int32 Accessor, int32 Comps, int32 Kind) -> bool   // Kind: 0 point, 1 vector (linear), 2 normal
        {
            if (Accessor < 0) return true;
            if (const int32* Prev = AccessorSkin.Find(Accessor)) return *Prev == S;   // already rewritten by this skin
            TArray<float> F; if (!ReadFloats(Doc, Bin, Accessor, Comps, F)) return false;
            const FM4& X = Kind == 2 ? Normal : D;
            for (int32 i = 0; i + Comps <= F.Num(); i += Comps)
            {
                float O[3]; X.Apply(&F[i], O, Kind == 0);
                if (Kind == 2 || (Kind == 1 && Comps == 4)) { const float L = FMath::Sqrt(O[0] * O[0] + O[1] * O[1] + O[2] * O[2]); if (L > 1e-12f) for (float& C : O) C /= L; }
                F[i] = O[0]; F[i + 1] = O[1]; F[i + 2] = O[2];
            }
            AccessorSkin.Add(Accessor, S); NewData.Add(Accessor, MoveTemp(F));
            return true;
        };
        for (int32 N = 0; N < Nodes.Num(); ++N)
        {
            const TSharedPtr<FJsonObject> Node = At(Nodes, N);
            if (Int(Node, TEXT("skin")) != S) continue;
            for (const auto& Pv : Arr(At(Meshes, Int(Node, TEXT("mesh"))), TEXT("primitives")))
            {
                const TSharedPtr<FJsonObject> P = Pv->AsObject(); if (!P) return false;
                if (P->HasField(TEXT("extensions"))) return false;   // compressed geometry: leave the file alone
                const TSharedPtr<FJsonObject>* Attr = nullptr; if (!P->TryGetObjectField(TEXT("attributes"), Attr)) return false;
                if (!Rewrite(Int(*Attr, TEXT("POSITION")), 3, 0) || !Rewrite(Int(*Attr, TEXT("NORMAL")), 3, 2) || !Rewrite(Int(*Attr, TEXT("TANGENT")), 4, 1)) return false;
                // morph deltas: no translation
                for (const auto& Tv : Arr(P, TEXT("targets")))
                {
                    const TSharedPtr<FJsonObject> T = Tv->AsObject(); if (!T) return false;
                    if (!Rewrite(Int(T, TEXT("POSITION")), 3, 1) || !Rewrite(Int(T, TEXT("NORMAL")), 3, 2) || !Rewrite(Int(T, TEXT("TANGENT")), 3, 1)) return false;
                }
            }
        }
        Notes.Add(FString::Printf(TEXT("skin %d: x%.3f"), S, Scale));
    }
    if (NewData.IsEmpty()) return false;
    // an accessor some OTHER primitive (unskinned, or another skin's) also reads would change under it: refuse
    {
        // every (node, primitive) read of an accessor, and those by a node wearing the skin that rewrote it
        TMap<int32, int32> Users, SkinnedUses;
        for (int32 N = 0; N < Nodes.Num(); ++N)
        {
            const TSharedPtr<FJsonObject> Node = At(Nodes, N); const int32 S = Int(Node, TEXT("skin"));
            for (const auto& Pv : Arr(At(Meshes, Int(Node, TEXT("mesh"))), TEXT("primitives"))) if (const TSharedPtr<FJsonObject> P = Pv->AsObject())
            {
                auto Use = [&](int32 A) { Users.FindOrAdd(A)++; if (const int32* By = AccessorSkin.Find(A); By && *By == S) SkinnedUses.FindOrAdd(A)++; };
                const TSharedPtr<FJsonObject>* Attr = nullptr; if (P->TryGetObjectField(TEXT("attributes"), Attr)) for (const auto& Kv : (*Attr)->Values) { double V; if (Kv.Value->TryGetNumber(V)) Use(int32(V)); }
                for (const auto& Tv : Arr(P, TEXT("targets"))) if (const TSharedPtr<FJsonObject> T = Tv->AsObject()) for (const auto& Kv : T->Values) { double V; if (Kv.Value->TryGetNumber(V)) Use(int32(V)); }
            }
        }
        for (const auto& Kv : NewData)
        {
            const int32 A = Kv.Key; if (A == Int(At(Skins, AccessorSkin[A]), TEXT("inverseBindMatrices"))) continue;
            if (SkinnedUses.FindRef(A) < Users.FindRef(A)) { if (Note) *Note = TEXT("skeleton parents not baked: geometry shared with another mesh"); return false; }
        }
    }
    // apply: each rewritten accessor gets its own tightly packed view at the end of the buffer
    const FJsonArray& Accessors = Arr(Doc, TEXT("accessors"));
    FJsonArray Views = Arr(Doc, TEXT("bufferViews"));
    for (auto& Kv : NewData)
    {
        while (Bin.Num() % 4) Bin.Add(0);
        const int32 Offset = Bin.Num();
        Bin.Append(reinterpret_cast<const uint8*>(Kv.Value.GetData()), Kv.Value.Num() * 4);
        auto View = MakeShared<FJsonObject>();
        View->SetNumberField(TEXT("buffer"), 0); View->SetNumberField(TEXT("byteOffset"), Offset); View->SetNumberField(TEXT("byteLength"), Kv.Value.Num() * 4);
        Views.Add(MakeShared<FJsonValueObject>(View));
        const TSharedPtr<FJsonObject> A = At(Accessors, Kv.Key);
        A->SetNumberField(TEXT("bufferView"), Views.Num() - 1); A->RemoveField(TEXT("byteOffset"));
        FString Type; A->TryGetStringField(TEXT("type"), Type);
        if (Type == TEXT("VEC3") && A->HasField(TEXT("min")))
        {
            FVector3f Mn(TNumericLimits<float>::Max()), Mx(-TNumericLimits<float>::Max());
            for (int32 i = 0; i + 3 <= Kv.Value.Num(); i += 3) { const FVector3f P(Kv.Value[i], Kv.Value[i + 1], Kv.Value[i + 2]); Mn = FVector3f::Min(Mn, P); Mx = FVector3f::Max(Mx, P); }
            A->SetArrayField(TEXT("min"), {MakeShared<FJsonValueNumber>(Mn.X), MakeShared<FJsonValueNumber>(Mn.Y), MakeShared<FJsonValueNumber>(Mn.Z)});
            A->SetArrayField(TEXT("max"), {MakeShared<FJsonValueNumber>(Mx.X), MakeShared<FJsonValueNumber>(Mx.Y), MakeShared<FJsonValueNumber>(Mx.Z)});
        }
    }
    Doc->SetArrayField(TEXT("bufferViews"), Views);
    if (const TSharedPtr<FJsonObject> Buffer = At(Arr(Doc, TEXT("buffers")), 0)) Buffer->SetNumberField(TEXT("byteLength"), Bin.Num());
    while (Bin.Num() % 4) Bin.Add(0);
    // repack: header, JSON chunk (space padded), BIN chunk
    FString Text;
    if (!FJsonSerializer::Serialize(Doc.ToSharedRef(), TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Text))) return false;
    FTCHARToUTF8 Utf8(*Text);
    TArray<uint8> Json(reinterpret_cast<const uint8*>(Utf8.Get()), Utf8.Length());
    while (Json.Num() % 4) Json.Add(' ');
    TArray<uint8> Out; Out.Reserve(28 + Json.Num() + Bin.Num());
    auto Put = [&Out](uint32 V) { Out.Append(reinterpret_cast<const uint8*>(&V), 4); };
    Put(0x46546c67); Put(2); Put(uint32(20 + Json.Num() + 8 + Bin.Num()));
    Put(uint32(Json.Num())); Put(0x4e4f534a); Out.Append(Json);
    Put(uint32(Bin.Num())); Put(0x004e4942); Out.Append(Bin);
    Glb = MoveTemp(Out);
    if (Note) *Note = FString::Join(Notes, TEXT(", "));
    return true;
}
