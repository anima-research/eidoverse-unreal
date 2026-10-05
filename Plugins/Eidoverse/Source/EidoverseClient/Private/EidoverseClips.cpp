#include "EidoverseClips.h"
#include "Async/Async.h"
#include "Dom/JsonObject.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Tasks/Task.h"

namespace
{
    // VRM 1.0 humanoid hierarchy (three-vrm VRMHumanBoneParentMap): the parent used to
    // turn a clip's local rotations into rest-relative deltas.
    const TMap<FName, FName>& HumanParents()
    {
        static const TMap<FName, FName> Map = []
        {
            TMap<FName, FName> M;
            auto Add = [&M](const TCHAR* Child, const TCHAR* Parent) { M.Add(FName(Child), FName(Parent)); };
            Add(TEXT("spine"), TEXT("hips")); Add(TEXT("chest"), TEXT("spine")); Add(TEXT("upperChest"), TEXT("chest"));
            Add(TEXT("neck"), TEXT("upperChest")); Add(TEXT("head"), TEXT("neck"));
            Add(TEXT("leftEye"), TEXT("head")); Add(TEXT("rightEye"), TEXT("head")); Add(TEXT("jaw"), TEXT("head"));
            for (const TCHAR* Side : {TEXT("left"), TEXT("right")})
            {
                const FString S(Side);
                auto B = [&S](const TCHAR* Name) { return S + Name; };
                Add(*B(TEXT("UpperLeg")), TEXT("hips")); Add(*B(TEXT("LowerLeg")), *B(TEXT("UpperLeg")));
                Add(*B(TEXT("Foot")), *B(TEXT("LowerLeg"))); Add(*B(TEXT("Toes")), *B(TEXT("Foot")));
                Add(*B(TEXT("Shoulder")), TEXT("upperChest")); Add(*B(TEXT("UpperArm")), *B(TEXT("Shoulder")));
                Add(*B(TEXT("LowerArm")), *B(TEXT("UpperArm"))); Add(*B(TEXT("Hand")), *B(TEXT("LowerArm")));
                Add(*B(TEXT("ThumbMetacarpal")), *B(TEXT("Hand"))); Add(*B(TEXT("ThumbProximal")), *B(TEXT("ThumbMetacarpal")));
                Add(*B(TEXT("ThumbDistal")), *B(TEXT("ThumbProximal")));
                for (const TCHAR* Finger : {TEXT("Index"), TEXT("Middle"), TEXT("Ring"), TEXT("Little")})
                {
                    const FString F = S + Finger;
                    Add(*(F + TEXT("Proximal")), *B(TEXT("Hand")));
                    Add(*(F + TEXT("Intermediate")), *(F + TEXT("Proximal")));
                    Add(*(F + TEXT("Distal")), *(F + TEXT("Intermediate")));
                }
            }
            return M;
        }();
        return Map;
    }

    struct FGlb
    {
        TSharedPtr<FJsonObject> Json;
        const uint8* Bin = nullptr; int64 BinSize = 0;
    };

    bool ReadGlb(const TArray<uint8>& B, FGlb& Out, FString& Error)
    {
        auto Word = [&B](int64 At) { uint32 V; FMemory::Memcpy(&V, B.GetData() + At, 4); return V; };
        if (B.Num() < 28 || Word(0) != 0x46546c67 || Word(4) != 2 || Word(8) > uint32(B.Num())) { Error = TEXT("not a GLB"); return false; }
        const uint32 JsonLength = Word(12);
        if (Word(16) != 0x4E4F534A || 20 + int64(JsonLength) > B.Num()) { Error = TEXT("bad JSON chunk"); return false; }
        FString Text;
        FUTF8ToTCHAR Conv(reinterpret_cast<const ANSICHAR*>(B.GetData() + 20), int32(JsonLength));
        Text = FString(Conv.Length(), Conv.Get());
        if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Out.Json) || !Out.Json) { Error = TEXT("bad JSON"); return false; }
        const int64 BinAt = 20 + JsonLength;
        if (BinAt + 8 <= B.Num() && Word(BinAt + 4) == 0x004E4942)
        {
            Out.BinSize = FMath::Min<int64>(Word(BinAt), B.Num() - BinAt - 8);
            Out.Bin = B.GetData() + BinAt + 8;
        }
        return true;
    }

    // Float accessor as rows of N components.
    bool ReadFloats(const FGlb& G, int32 AccessorIndex, int32 Components, TArray<float>& Out)
    {
        const TArray<TSharedPtr<FJsonValue>>* Accessors; const TArray<TSharedPtr<FJsonValue>>* Views;
        if (!G.Json->TryGetArrayField(TEXT("accessors"), Accessors) || !G.Json->TryGetArrayField(TEXT("bufferViews"), Views)) return false;
        if (!Accessors->IsValidIndex(AccessorIndex)) return false;
        const TSharedPtr<FJsonObject> A = (*Accessors)[AccessorIndex]->AsObject();
        int32 Count = 0, ComponentType = 0, View = -1, AOffset = 0;
        A->TryGetNumberField(TEXT("count"), Count); A->TryGetNumberField(TEXT("componentType"), ComponentType);
        A->TryGetNumberField(TEXT("bufferView"), View); A->TryGetNumberField(TEXT("byteOffset"), AOffset);
        if (ComponentType != 5126 || !Views->IsValidIndex(View) || Count <= 0 || Count > 1000000) return false;   // float only
        const TSharedPtr<FJsonObject> V = (*Views)[View]->AsObject();
        int32 VOffset = 0, Stride = 0; V->TryGetNumberField(TEXT("byteOffset"), VOffset); V->TryGetNumberField(TEXT("byteStride"), Stride);
        if (Stride == 0) Stride = Components * 4;
        const int64 Start = int64(VOffset) + AOffset;
        if (!G.Bin || Start < 0 || Start + int64(Stride) * (Count - 1) + Components * 4 > G.BinSize) return false;
        Out.SetNumUninitialized(Count * Components);
        for (int32 I = 0; I < Count; ++I) FMemory::Memcpy(Out.GetData() + I * Components, G.Bin + Start + int64(I) * Stride, Components * 4);
        return true;
    }

    FTransform NodeLocal(const TSharedPtr<FJsonObject>& N)
    {
        const TArray<TSharedPtr<FJsonValue>>* M;
        if (N->TryGetArrayField(TEXT("matrix"), M) && M->Num() == 16)
        {
            FMatrix Mat; for (int32 I = 0; I < 16; ++I) Mat.M[I / 4][I % 4] = (*M)[I]->AsNumber();   // column-major glTF == UE row-vector rows
            return FTransform(Mat);
        }
        FVector T(0), S(1); FQuat R = FQuat::Identity; const TArray<TSharedPtr<FJsonValue>>* A;
        if (N->TryGetArrayField(TEXT("translation"), A) && A->Num() == 3) T = FVector((*A)[0]->AsNumber(), (*A)[1]->AsNumber(), (*A)[2]->AsNumber());
        if (N->TryGetArrayField(TEXT("rotation"), A) && A->Num() == 4) R = FQuat((*A)[0]->AsNumber(), (*A)[1]->AsNumber(), (*A)[2]->AsNumber(), (*A)[3]->AsNumber()).GetNormalized();
        if (N->TryGetArrayField(TEXT("scale"), A) && A->Num() == 3) S = FVector((*A)[0]->AsNumber(), (*A)[1]->AsNumber(), (*A)[2]->AsNumber());
        return FTransform(R, T, S);
    }

    int32 KeyBefore(const TArray<float>& Times, float T)
    {
        if (Times.Num() < 2 || T <= Times[0]) return 0;
        if (T >= Times.Last()) return Times.Num() - 1;
        int32 Lo = 0, Hi = Times.Num() - 1;
        while (Hi - Lo > 1) { const int32 Mid = (Lo + Hi) / 2; (Times[Mid] <= T ? Lo : Hi) = Mid; }
        return Lo;
    }
}

bool FEidoClip::Parse(const TArray<uint8>& Bytes, FString& Error)
{
    FGlb G;
    if (!ReadGlb(Bytes, G, Error)) return false;
    const TSharedPtr<FJsonObject>* Ext; const TSharedPtr<FJsonObject>* Vrma; const TSharedPtr<FJsonObject>* Humanoid; const TSharedPtr<FJsonObject>* HumanBones;
    if (!G.Json->TryGetObjectField(TEXT("extensions"), Ext) || !(*Ext)->TryGetObjectField(TEXT("VRMC_vrm_animation"), Vrma) ||
        !(*Vrma)->TryGetObjectField(TEXT("humanoid"), Humanoid) || !(*Humanoid)->TryGetObjectField(TEXT("humanBones"), HumanBones))
    { Error = TEXT("not a VRM animation"); return false; }
    const TArray<TSharedPtr<FJsonValue>>* Nodes; const TArray<TSharedPtr<FJsonValue>>* Animations;
    if (!G.Json->TryGetArrayField(TEXT("nodes"), Nodes) || !G.Json->TryGetArrayField(TEXT("animations"), Animations) || Animations->IsEmpty())
    { Error = TEXT("no animation"); return false; }

    // Rest pose: world transforms of every node (glTF axes).
    const int32 NodeCount = Nodes->Num();
    TArray<int32> Parent; Parent.Init(-1, NodeCount);
    for (int32 I = 0; I < NodeCount; ++I)
    {
        const TArray<TSharedPtr<FJsonValue>>* Children;
        if ((*Nodes)[I]->AsObject()->TryGetArrayField(TEXT("children"), Children))
            for (const auto& C : *Children) { const int32 Ci = int32(C->AsNumber()); if (Ci >= 0 && Ci < NodeCount) Parent[Ci] = I; }
    }
    TArray<FTransform> World; World.SetNum(NodeCount);
    TArray<bool> Done; Done.Init(false, NodeCount);
    TFunction<const FTransform&(int32)> WorldOf = [&](int32 I) -> const FTransform&
    {
        if (!Done[I]) { const FTransform L = NodeLocal((*Nodes)[I]->AsObject()); World[I] = Parent[I] >= 0 ? L * WorldOf(Parent[I]) : L; Done[I] = true; }
        return World[I];
    };
    TMap<FName, int32> BoneNode; TMap<int32, FName> NodeBone;
    for (const auto& Pair : (*HumanBones)->Values)
    {
        double NodeIndex = -1; const TSharedPtr<FJsonObject> O = Pair.Value->AsObject();
        if (O && O->TryGetNumberField(TEXT("node"), NodeIndex) && NodeIndex >= 0 && NodeIndex < NodeCount)
        { BoneNode.Add(FName(FString(Pair.Key)), int32(NodeIndex)); NodeBone.Add(int32(NodeIndex), FName(FString(Pair.Key))); }
    }
    const int32* HipsNode = BoneNode.Find(FName(TEXT("hips")));
    if (!HipsNode) { Error = TEXT("no hips"); return false; }
    const FTransform HipsParentWorld = Parent[*HipsNode] >= 0 ? WorldOf(Parent[*HipsNode]) : FTransform::Identity;
    RestHipsHeight = FMath::Max(0.01, WorldOf(*HipsNode).GetLocation().Y);
    auto ParentRestRotation = [&](FName Bone) -> FQuat
    {
        FName At = Bone;
        while (const FName* P = HumanParents().Find(At))
        {
            if (const int32* N = BoneNode.Find(*P)) return WorldOf(*N).GetRotation();
            At = *P;
        }
        return HipsParentWorld.GetRotation();
    };

    const TSharedPtr<FJsonObject> Anim = (*Animations)[0]->AsObject();
    const TArray<TSharedPtr<FJsonValue>>* Channels; const TArray<TSharedPtr<FJsonValue>>* Samplers;
    if (!Anim->TryGetArrayField(TEXT("channels"), Channels) || !Anim->TryGetArrayField(TEXT("samplers"), Samplers)) { Error = TEXT("no channels"); return false; }
    Duration = 0;
    for (const auto& ChannelValue : *Channels)
    {
        const TSharedPtr<FJsonObject> C = ChannelValue->AsObject(); const TSharedPtr<FJsonObject>* Target;
        int32 SamplerIndex = -1; C->TryGetNumberField(TEXT("sampler"), SamplerIndex);
        if (!C->TryGetObjectField(TEXT("target"), Target) || !Samplers->IsValidIndex(SamplerIndex)) continue;
        int32 Node = -1; FString Path; (*Target)->TryGetNumberField(TEXT("node"), Node); (*Target)->TryGetStringField(TEXT("path"), Path);
        const FName* Bone = NodeBone.Find(Node);
        if (!Bone) continue;   // expressions / lookAt tracks: not used yet
        const TSharedPtr<FJsonObject> S = (*Samplers)[SamplerIndex]->AsObject();
        int32 Input = -1, Output = -1; FString Interp = TEXT("LINEAR");
        S->TryGetNumberField(TEXT("input"), Input); S->TryGetNumberField(TEXT("output"), Output); S->TryGetStringField(TEXT("interpolation"), Interp);
        TArray<float> Times, Values;
        const int32 Components = Path == TEXT("rotation") ? 4 : Path == TEXT("translation") ? 3 : 0;
        if (!Components || !ReadFloats(G, Input, 1, Times) || !ReadFloats(G, Output, Components, Values)) continue;
        const bool bCubic = Interp == TEXT("CUBICSPLINE");
        const int32 Keys = Times.Num();
        if (Values.Num() != Keys * Components * (bCubic ? 3 : 1)) continue;
        auto ValueAt = [&](int32 K, int32 C) { return Values[(bCubic ? K * 3 + 1 : K) * Components + C]; };
        Duration = FMath::Max(Duration, Times.Last());
        if (Path == TEXT("rotation"))
        {
            const FQuat P = ParentRestRotation(*Bone);
            const FQuat Winv = WorldOf(Node).GetRotation().Inverse();
            FEidoClipTrack& Track = Bones.Add(*Bone);
            Track.Times = Times; Track.Rotations.SetNum(Keys);
            for (int32 K = 0; K < Keys; ++K)
            {
                const FQuat Q = FQuat(ValueAt(K, 0), ValueAt(K, 1), ValueAt(K, 2), ValueAt(K, 3)).GetNormalized();
                Track.Rotations[K] = (P * Q * Winv).GetNormalized();   // three-vrm: premultiply(parentWorldRest).multiply(inverse(worldRest))
            }
        }
        else if (*Bone == FName(TEXT("hips")))
        {
            HipsTimes = Times; HipsPositions.SetNum(Keys);
            for (int32 K = 0; K < Keys; ++K)
                HipsPositions[K] = HipsParentWorld.TransformPosition(FVector(ValueAt(K, 0), ValueAt(K, 1), ValueAt(K, 2)));
        }
    }
    if (Bones.IsEmpty() || Duration <= 0) { Error = TEXT("no humanoid tracks"); return false; }

    // Jump: start where the hips rise back through rest height after the dip.
    Takeoff = 0;
    if (HipsPositions.Num() > 2)
    {
        int32 Bottom = 0;
        for (int32 K = 1; K < HipsPositions.Num(); ++K) if (HipsTimes[K] < Duration * .6f && HipsPositions[K].Y < HipsPositions[Bottom].Y) Bottom = K;
        for (int32 K = Bottom + 1; K < HipsPositions.Num(); ++K) if (HipsPositions[K].Y >= RestHipsHeight) { Takeoff = HipsTimes[K]; break; }
    }
    return true;
}

bool FEidoClip::SampleRotation(FName Bone, float Time, FQuat& Out) const
{
    const FEidoClipTrack* T = Bones.Find(Bone);
    if (!T || T->Times.IsEmpty()) return false;
    const int32 K = KeyBefore(T->Times, Time);
    if (K + 1 >= T->Times.Num() || Time <= T->Times[K]) { Out = T->Rotations[K]; return true; }
    const float Span = T->Times[K + 1] - T->Times[K];
    const float A = Span > 0 ? (Time - T->Times[K]) / Span : 0;
    Out = FQuat::Slerp(T->Rotations[K], T->Rotations[K + 1], A).GetNormalized();
    return true;
}

bool FEidoClip::SampleHips(float Time, FVector& Out) const
{
    if (HipsTimes.IsEmpty()) return false;
    const int32 K = KeyBefore(HipsTimes, Time);
    if (K + 1 >= HipsTimes.Num() || Time <= HipsTimes[K]) { Out = HipsPositions[K]; return true; }
    const float Span = HipsTimes[K + 1] - HipsTimes[K];
    Out = FMath::Lerp(HipsPositions[K], HipsPositions[K + 1], Span > 0 ? (Time - HipsTimes[K]) / Span : 0.f);
    return true;
}

// ---- shared/clipdefs.js + avatar.js rules ----------------------------------

FString EidoClipRules::FileForSlot(const FString& Slot)
{
    static const TMap<FString, FString> Files = {
        {TEXT("sit"), TEXT("sitting_on_ground")}, {TEXT("lie"), TEXT("sit_laying_on_ground")},
        {TEXT("climb"), TEXT("climbLedge")}, {TEXT("fly"), TEXT("fallIdle")}, {TEXT("soar"), TEXT("fallIdle")},
        {TEXT("sitchair"), TEXT("sitting_normal_chair")}};
    const FString* F = Files.Find(Slot);
    return F ? *F : Slot;
}

FString EidoClipRules::Fallback(const FString& Slot)
{
    static const TMap<FString, FString> Next = {
        {TEXT("run"), TEXT("walk")}, {TEXT("jump"), TEXT("idle")}, {TEXT("climb"), TEXT("walk")}, {TEXT("sit"), TEXT("idle")},
        {TEXT("lie"), TEXT("idle")}, {TEXT("sitchair"), TEXT("sit")}, {TEXT("fly"), TEXT("soar")}, {TEXT("soar"), TEXT("idle")},
        {TEXT("walk"), TEXT("idle")}};
    if (Slot == TEXT("idle")) return FString();
    const FString* N = Next.Find(Slot);
    return N ? *N : TEXT("idle");
}

float EidoClipRules::NaturalSpeed(const FString& Slot)
{
    return Slot == TEXT("walk") ? 1.55f : Slot == TEXT("run") ? 4.0f : 0.f;
}

float EidoClipRules::RateFor(const FString& Slot, float Speed)
{
    const float Natural = NaturalSpeed(Slot);
    return Natural > 0 && Speed > 0 ? FMath::Clamp(Speed / Natural, .6f, 1.6f) : 1.f;
}

FString EidoClipRules::SelectLocomotion(bool bJumped, float AirborneSeconds, bool bWantMove, float Speed, const FString& Posture)
{
    if (bJumped || AirborneSeconds > .04f) return TEXT("jump");
    if (bWantMove && Speed >= .05f) return Speed < 2.6f ? TEXT("walk") : TEXT("run");
    if (Posture == TEXT("sit")) return TEXT("sit");   // a chair seat is 'sitchair' (not yet: seats)
    if (Posture == TEXT("lie")) return TEXT("lie");
    return TEXT("idle");
}

// ---- library ----------------------------------------------------------------

void UEidoverseClipLibrary::Configure(const FString& HttpBase)
{
    if (HttpBase == Base) return;
    Base = HttpBase; ++Generation;
    bRosterRequested = bRosterReady = false;
    Roster.Empty(); Emotes.Empty(); Ready.Empty(); Loading.Empty(); Failed.Empty(); Waiting.Empty();
}

void UEidoverseClipLibrary::RequestRoster()
{
    if (bRosterRequested || Base.IsEmpty()) return;
    bRosterRequested = true;
    const uint32 Gen = Generation;
    TWeakObjectPtr<UEidoverseClipLibrary> Weak(this);
    TSharedRef<int32> Pending = MakeShared<int32>(2);
    auto Finish = [Weak, Gen, Pending]()
    {
        UEidoverseClipLibrary* Self = Weak.Get();
        if (!Self || Self->Generation != Gen || --(*Pending) > 0) return;
        Self->bRosterReady = true;
        UE_LOG(LogTemp, Display, TEXT("EIDOVERSE clips: roster of %d, %d emotes"), Self->Roster.Num(), Self->Emotes.Num());
        TArray<FString> Queue = MoveTemp(Self->Waiting);
        for (const FString& File : Queue) Self->Download(File);
    };
    auto Get = [](const FString& Url) { auto R = FHttpModule::Get().CreateRequest(); R->SetURL(Url); R->SetVerb(TEXT("GET")); R->SetTimeout(20); return R; };
    auto RosterRequest = Get(Base + TEXT("/animations"));
    RosterRequest->OnProcessRequestComplete().BindLambda([Weak, Gen, Finish](FHttpRequestPtr, FHttpResponsePtr Response, bool bOk)
    {
        UEidoverseClipLibrary* Self = Weak.Get();
        if (Self && Self->Generation == Gen && bOk && Response && Response->GetResponseCode() == 200 && Response->GetContent().Num() < 1 << 20)
        {
            TArray<TSharedPtr<FJsonValue>> Rows;
            if (FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Response->GetContentAsString()), Rows))
                for (const auto& Row : Rows)
                {
                    const TSharedPtr<FJsonObject> O = Row->AsObject(); FString Name, Path;
                    if (O && O->TryGetStringField(TEXT("name"), Name) && O->TryGetStringField(TEXT("path"), Path) && !Path.Contains(TEXT("..")) && Path.Len() < 512)
                        Self->Roster.Add(Name, Path);
                }
        }
        Finish();
    });
    auto Defs = Get(Base + TEXT("/defs"));
    Defs->OnProcessRequestComplete().BindLambda([Weak, Gen, Finish](FHttpRequestPtr, FHttpResponsePtr Response, bool bOk)
    {
        UEidoverseClipLibrary* Self = Weak.Get();
        TSharedPtr<FJsonObject> J; const TSharedPtr<FJsonObject>* E;
        if (Self && Self->Generation == Gen && bOk && Response && Response->GetResponseCode() == 200 && Response->GetContent().Num() < 4 << 20 &&
            FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Response->GetContentAsString()), J) && J && J->TryGetObjectField(TEXT("emotes"), E))
            for (const auto& Pair : (*E)->Values)
            {
                const TSharedPtr<FJsonObject> O = Pair.Value->AsObject(); FString Clip;
                if (O && O->TryGetStringField(TEXT("clip"), Clip)) Self->Emotes.Add(FString(Pair.Key), Clip);
            }
        Finish();
    });
    RosterRequest->ProcessRequest(); Defs->ProcessRequest();
}

void UEidoverseClipLibrary::Download(const FString& File)
{
    if (File.IsEmpty() || Ready.Contains(File) || Loading.Contains(File) || Failed.Contains(File) || Base.IsEmpty()) return;
    if (!bRosterReady) { Waiting.AddUnique(File); RequestRoster(); return; }
    const FString* Path = Roster.Find(File);
    const FString Url = Base + TEXT("/library/") + (Path ? *Path : TEXT("eidoverse/assets/animations/") + File + TEXT(".vrma"));
    Loading.Add(File);
    const uint32 Gen = Generation;
    TWeakObjectPtr<UEidoverseClipLibrary> Weak(this);
    auto R = FHttpModule::Get().CreateRequest(); R->SetURL(Url); R->SetVerb(TEXT("GET")); R->SetTimeout(30);
    R->OnProcessRequestComplete().BindLambda([Weak, Gen, File](FHttpRequestPtr, FHttpResponsePtr Response, bool bOk)
    {
        UEidoverseClipLibrary* Self = Weak.Get();
        if (!Self || Self->Generation != Gen) return;
        if (!bOk || !Response || Response->GetResponseCode() != 200 || Response->GetContent().Num() > 16 << 20)
        {
            Self->Loading.Remove(File); Self->Failed.Add(File);
            UE_LOG(LogTemp, Warning, TEXT("EIDOVERSE clip %s unavailable (HTTP %d)"), *File, Response ? Response->GetResponseCode() : 0);
            return;
        }
        TArray<uint8> Bytes = Response->GetContent();
        UE::Tasks::Launch(UE_SOURCE_LOCATION, [Weak, Gen, File, Bytes = MoveTemp(Bytes)]()
        {
            TSharedPtr<FEidoClip> Clip = MakeShared<FEidoClip>(); FString Error;
            Clip->Name = File;
            const bool bOk = Clip->Parse(Bytes, Error);
            AsyncTask(ENamedThreads::GameThread, [Weak, Gen, File, Clip, bOk, Error]()
            {
                UEidoverseClipLibrary* Self = Weak.Get();
                if (!Self || Self->Generation != Gen) return;
                Self->Loading.Remove(File);
                if (bOk) Self->Ready.Add(File, Clip);
                else { Self->Failed.Add(File); UE_LOG(LogTemp, Warning, TEXT("EIDOVERSE clip %s rejected: %s"), *File, *Error); }
            });
        });
    });
    R->ProcessRequest();
}

void UEidoverseClipLibrary::Prefetch(const FString& Slot)
{
    Download(EidoClipRules::FileForSlot(Slot));
}

TSharedPtr<const FEidoClip> UEidoverseClipLibrary::Resolve(const FString& Slot, FString* OutPlayedSlot)
{
    FString At = Slot;
    for (int32 Guard = 0; Guard < 8 && !At.IsEmpty(); ++Guard)
    {
        const FString File = EidoClipRules::FileForSlot(At);
        if (const TSharedPtr<const FEidoClip>* Clip = Ready.Find(File)) { if (OutPlayedSlot) *OutPlayedSlot = At; return *Clip; }
        Download(File);
        At = EidoClipRules::Fallback(At);
    }
    return nullptr;
}

FString UEidoverseClipLibrary::EmoteClip(const FString& Emote) const
{
    const FString* Clip = Emotes.Find(Emote);
    return Clip ? *Clip : FString();
}

TSharedPtr<const FEidoClip> UEidoverseClipLibrary::Find(const FString& File)
{
    if (const TSharedPtr<const FEidoClip>* Clip = Ready.Find(File)) return *Clip;
    Download(File);
    return nullptr;
}
