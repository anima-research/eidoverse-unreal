#include "EidoverseBridge.h"
#include "EidoverseAvatar.h"
#include "Dom/JsonObject.h"
#include "Engine/SkeletalMesh.h"
#include "GameFramework/Pawn.h"
#include "Kismet/GameplayStatics.h"

namespace ReachWire
{
TSharedPtr<FJsonObject> Obj(const TSharedPtr<FJsonObject>& O,const TCHAR* K){const TSharedPtr<FJsonObject>* V=nullptr;return O&&O->TryGetObjectField(K,V)?*V:nullptr;}
FString Str(const TSharedPtr<FJsonObject>& O,const TCHAR* K){FString V;if(O)O->TryGetStringField(K,V);return V;}
// Anatomical seeds only, not calibrated skin contact. Never use these to
// attest touch; the owning solver must measure its own hand-to-surface gap.
FName ContactBone(const FString& P)
{
    static const TMap<FString,FName> Bones={
        {TEXT("hand_l"),TEXT("leftHand")},{TEXT("hand_r"),TEXT("rightHand")},
        {TEXT("shoulder_l"),TEXT("leftUpperArm")},{TEXT("shoulder_r"),TEXT("rightUpperArm")},
        {TEXT("forearm_l"),TEXT("leftLowerArm")},{TEXT("forearm_r"),TEXT("rightLowerArm")},
        {TEXT("wrist_l"),TEXT("leftHand")},{TEXT("wrist_r"),TEXT("rightHand")},
        {TEXT("arm_l"),TEXT("leftUpperArm")},{TEXT("arm_r"),TEXT("rightUpperArm")},
        {TEXT("upper_back"),TEXT("chest")},{TEXT("back"),TEXT("spine")},
        {TEXT("head_top"),TEXT("head")},{TEXT("neck_back"),TEXT("neck")},
        {TEXT("knee_l"),TEXT("leftLowerLeg")},{TEXT("knee_r"),TEXT("rightLowerLeg")},
        {TEXT("foot_l"),TEXT("leftFoot")},{TEXT("foot_r"),TEXT("rightFoot")},
        {TEXT("eye_l"),TEXT("head")},{TEXT("eye_r"),TEXT("head")},
        {TEXT("cheek_l"),TEXT("head")},{TEXT("cheek_r"),TEXT("head")},
        {TEXT("chest_front"),TEXT("chest")},{TEXT("waist_l"),TEXT("spine")},{TEXT("waist_r"),TEXT("spine")},
        {TEXT("hip_l"),TEXT("leftUpperLeg")},{TEXT("hip_r"),TEXT("rightUpperLeg")}};
    const auto* Bone=Bones.Find(P);return Bone?*Bone:NAME_None;
}
}
bool UEidoverseAvatarMesh::HumanBonePosition(FName Human,FVector& World) const
{
    if(!Avatar||!Avatar->Mesh)return false;const auto* Name=Avatar->Humanoid.Find(Human);if(!Name)return false;
    int I=GetBoneIndex(*Name);if(!BoneSpaceTransforms.IsValidIndex(I))return false;
    FTransform T=BoneSpaceTransforms[I];const auto& Ref=Avatar->Mesh->GetRefSkeleton();
    for(I=Ref.GetParentIndex(I);I>=0;I=Ref.GetParentIndex(I))T=T*BoneSpaceTransforms[I];
    World=GetComponentTransform().TransformPosition(T.GetLocation());return !World.ContainsNaN();
}
bool UEidoverseAvatarMesh::SolveReach(FName Limb,const FVector& WorldTarget)
{
    if(!Avatar||!Avatar->Mesh||WorldTarget.ContainsNaN())return false;
    const bool Left=Limb==TEXT("leftHand")||Limb==TEXT("leftFoot"),Hand=Limb==TEXT("leftHand")||Limb==TEXT("rightHand");
    if(!Hand&&Limb!=TEXT("leftFoot")&&Limb!=TEXT("rightFoot"))return false;
    FName Upper=Hand?(Left?TEXT("leftUpperArm"):TEXT("rightUpperArm")):(Left?TEXT("leftUpperLeg"):TEXT("rightUpperLeg"));
    FName Lower=Hand?(Left?TEXT("leftLowerArm"):TEXT("rightLowerArm")):(Left?TEXT("leftLowerLeg"):TEXT("rightLowerLeg"));
    int Idx[3];FName Names[]={Upper,Lower,Limb};for(int N=0;N<3;++N){auto* B=Avatar->Humanoid.Find(Names[N]);if(!B)return false;Idx[N]=GetBoneIndex(*B);if(!BoneSpaceTransforms.IsValidIndex(Idx[N]))return false;}
    const auto& Ref=Avatar->Mesh->GetRefSkeleton();
    auto Component=[&](int I){FTransform T=BoneSpaceTransforms[I];for(I=Ref.GetParentIndex(I);I>=0;I=Ref.GetParentIndex(I))T=T*BoneSpaceTransforms[I];return T;};
    auto Rotate=[&](int I,const FQuat& Delta){FQuat Parent=Ref.GetParentIndex(I)>=0?Component(Ref.GetParentIndex(I)).GetRotation():FQuat::Identity;BoneSpaceTransforms[I].SetRotation((Parent.Inverse()*Delta*Parent*BoneSpaceTransforms[I].GetRotation()).GetNormalized());};
    FVector A=Component(Idx[0]).GetLocation(),B=Component(Idx[1]).GetLocation(),C=Component(Idx[2]).GetLocation(),Target=GetComponentTransform().InverseTransformPosition(WorldTarget);
    const double L1=(B-A).Size(),L2=(C-B).Size();if(L1<.1||L2<.1)return false;
    FVector Direction=(Target-A).GetSafeNormal();if(Direction.IsNearlyZero())return false;
    const double D=FMath::Clamp((Target-A).Size(),FMath::Abs(L1-L2)+.01,L1+L2-.01),Along=(L1*L1-L2*L2+D*D)/(2*D);
    FVector Pole=(B-A)-Direction*FVector::DotProduct(B-A,Direction);if(!Pole.Normalize()){Pole=FVector::CrossProduct(Direction,FVector::UpVector).GetSafeNormal();if(Pole.IsNearlyZero())Pole=FVector::RightVector;}
    const FVector Elbow=A+Direction*Along+Pole*FMath::Sqrt(FMath::Max(0.,L1*L1-Along*Along));
    Rotate(Idx[0],FQuat::FindBetweenVectors(B-A,Elbow-A));B=Component(Idx[1]).GetLocation();C=Component(Idx[2]).GetLocation();
    Rotate(Idx[1],FQuat::FindBetweenVectors(C-B,A+Direction*D-B));MarkRefreshTransformDirty();return true;
}
bool AEidoverseBridge::ReachTo(const FString& Participant,const FString& Point,bool Left)
{
    if(!bJoined||bSpectator||(!Peers.Contains(Participant)&&Participant!=AssignedIdentity)||ReachWire::ContactBone(Point).IsNone())
    {InteractionStatus=TEXT("Reach requires a present participant and a canonical contact point (e.g. shoulder_l, hand_r).");return false;}
    auto T=MakeShared<FJsonObject>(),Entry=MakeShared<FJsonObject>();T->SetStringField(TEXT("who"),Participant);T->SetStringField(TEXT("point"),Point);Entry->SetObjectField(TEXT("t"),T);
    // reached:true is attested by TickReaches only when my own solved hand arrives.
    if(!LocalReach)LocalReach=MakeShared<FJsonObject>();LocalReach->SetObjectField(Left?TEXT("leftHand"):TEXT("rightHand"),Entry);
    return true;
}
void UEidoverseAvatarMesh::ApplyGesture(const FString& Gesture,double Time)
{
    if(!Avatar)return;
    auto Reset=[&](FName H){if(auto* B=Avatar->Humanoid.Find(H)){int I=GetBoneIndex(*B);if(Reference.IsValidIndex(I))BoneSpaceTransforms[I]=Reference[I];}};
    if(Gesture==TEXT("wave")||Gesture==TEXT("cheer"))
    {Reset(TEXT("rightUpperArm"));Reset(TEXT("rightLowerArm"));Reset(TEXT("rightHand"));RotateBone(TEXT("rightUpperArm"),FVector::ForwardVector,-60);RotateBone(TEXT("rightLowerArm"),FVector::RightVector,-45);RotateBone(TEXT("rightHand"),FVector::ForwardVector,FMath::Sin(Time*9)*22);}
    if(Gesture==TEXT("cheer"))
    {Reset(TEXT("leftUpperArm"));Reset(TEXT("leftLowerArm"));RotateBone(TEXT("leftUpperArm"),FVector::ForwardVector,60);RotateBone(TEXT("leftLowerArm"),FVector::RightVector,-45);}
    if(Gesture==TEXT("point")){Reset(TEXT("rightUpperArm"));Reset(TEXT("rightLowerArm"));RotateBone(TEXT("rightUpperArm"),FVector::ForwardVector,65);RotateBone(TEXT("rightUpperArm"),FVector::RightVector,-85);}
    MarkRefreshTransformDirty();
}
void AEidoverseBridge::ClearReach(){LocalReach.Reset();InteractionStatus=TEXT("Reach released.");}
namespace ReachWire
{
    // A limb's target in world space, or false (the target body is missing).
    bool ResolveTarget(AEidoverseBridge& B, const TSharedPtr<FJsonObject>& T, const FEidoversePeer& Owner, FVector& Target)
    {
        const FString Who = Str(T, TEXT("who"));
        if (!Who.IsEmpty())
        {
            const FEidoversePeer* TargetPeer = Who == B.AssignedIdentity ? &B.GetSelfBody() : B.FindPeer(Who);
            const FName Bone = ContactBone(Str(T, TEXT("point")));
            if (Bone.IsNone() || !TargetPeer || !TargetPeer->AvatarMesh.IsValid() || !TargetPeer->AvatarMesh->HumanBonePosition(Bone, Target)) return false;
            double Standoff = .02; T->TryGetNumberField(TEXT("standoff"), Standoff);
            const AActor* TA = TargetPeer->AvatarMesh->GetOwner();
            if (FMath::IsFinite(Standoff) && TA) Target += TA->GetActorForwardVector() * FMath::Clamp(Standoff, 0., .2) * 100;
            return true;
        }
        const TArray<TSharedPtr<FJsonValue>>* A = nullptr; if (!T->TryGetArrayField(TEXT("p"), A) || A->Num() != 3) return false;
        double V[3]; for (int I = 0; I < 3; ++I) if (!(*A)[I]->TryGetNumber(V[I]) || !FMath::IsFinite(V[I]) || FMath::Abs(V[I]) > 1e6) return false;
        Target = AEidoverseBridge::FromWirePosition(FVector(V[0], V[1], V[2]));
        const FString Space = Str(T, TEXT("space"));
        if (Space.IsEmpty() || Space == TEXT("world")) Target += B.OriginCentimeters;
        else if (Space == TEXT("self")) { if (!Owner.Actor.IsValid()) return false; Target = Owner.Actor->GetActorTransform().TransformPosition(Target); }
        else
        {
            const FEidoversePeer* Other = Space == B.AssignedIdentity ? &B.GetSelfBody() : B.FindPeer(Space);
            if (!Other || !Other->AvatarMesh.IsValid()) return false;
            Target = Other->AvatarMesh->GetOwner()->GetActorTransform().TransformPosition(Target);
        }
        return true;
    }
}

bool UEidoverseAvatarMesh::SolveReachWeighted(FName Limb, const FVector& WorldTarget, float Weight)
{
    if (Weight <= .001f || !Avatar) return false;
    const bool Left = Limb == TEXT("leftHand") || Limb == TEXT("leftFoot"), Hand = Limb == TEXT("leftHand") || Limb == TEXT("rightHand");
    const FName Upper = Hand ? (Left ? TEXT("leftUpperArm") : TEXT("rightUpperArm")) : (Left ? TEXT("leftUpperLeg") : TEXT("rightUpperLeg"));
    const FName Lower = Hand ? (Left ? TEXT("leftLowerArm") : TEXT("rightLowerArm")) : (Left ? TEXT("leftLowerLeg") : TEXT("rightLowerLeg"));
    int32 Idx[2];
    for (int32 N = 0; N < 2; ++N) { const auto* B = Avatar->Humanoid.Find(N ? Lower : Upper); if (!B) return false; Idx[N] = GetBoneIndex(*B); if (!BoneSpaceTransforms.IsValidIndex(Idx[N])) return false; }
    const FQuat Before[2] = {BoneSpaceTransforms[Idx[0]].GetRotation(), BoneSpaceTransforms[Idx[1]].GetRotation()};
    if (!SolveReach(Limb, WorldTarget)) return false;
    if (Weight < .999f)
        for (int32 N = 0; N < 2; ++N) BoneSpaceTransforms[Idx[N]].SetRotation(FQuat::Slerp(Before[N], BoneSpaceTransforms[Idx[N]].GetRotation(), Weight).GetNormalized());
    MarkRefreshTransformDirty();
    return true;
}

void AEidoverseBridge::TickReaches()
{
    const double Now = FPlatformTime::Seconds();
    const float Dt = LastReachTick > 0 ? FMath::Clamp(float(Now - LastReachTick), 0.f, .1f) : 1.f / 60;
    LastReachTick = Now;
    static const TCHAR* Limbs[] = {TEXT("leftHand"), TEXT("rightHand"), TEXT("leftFoot"), TEXT("rightFoot")};
    auto Solve = [&](FEidoversePeer& P, const TSharedPtr<FJsonObject>& Reach, bool bSelf)
    {
        UEidoverseAvatarMesh* M = P.AvatarMesh.Get();
        if (!M || P.bSubmarine || M->IsLimp()) return;
        for (const TCHAR* Limb : Limbs)
        {
            const FName L(Limb);
            const TSharedPtr<FJsonObject> Entry = ReachWire::Obj(Reach, Limb), T = ReachWire::Obj(Entry, TEXT("t"));
            FVector Target; const bool bHas = T && ReachWire::ResolveTarget(*this, T, P, Target);
            if (bHas) P.ReachLastTarget.Add(L, Target);
            float& W = P.ReachWeight.FindOrAdd(L);
            W += ((bHas ? 1.f : 0.f) - W) * FMath::Min(1.f, 12 * Dt);
            if (W < .01f) { W = 0; P.ReachLastTarget.Remove(L); continue; }
            const FVector* Goal = P.ReachLastTarget.Find(L);
            if (!Goal || !M->SolveReachWeighted(L, *Goal, W)) continue;
            // My own hand attests touch only when it truly arrives (reachwire.js TOUCH_GAP 0.05 m).
            if (bSelf && Entry && bHas)
            {
                FVector HandAt; const bool bReached = W > .5f && M->HumanBonePosition(L, HandAt) && FVector::Dist(HandAt, *Goal) <= 5;
                Entry->SetBoolField(TEXT("reached"), bReached);
            }
        }
    };
    for (auto& Pair : Peers) Solve(Pair.Value, Pair.Value.Reach, false);
    // VR grips (xrbody.js): hands to their root-relative positions unless an emote plays.
    for (auto& Pair : Peers)
    {
        FEidoversePeer& P = Pair.Value; UEidoverseAvatarMesh* M = P.AvatarMesh.Get();
        if (!M) continue;
        const TArray<TSharedPtr<FJsonValue>>* H = nullptr;
        const bool bXR = P.XR && P.XR->TryGetArrayField(TEXT("h"), H) && H->Num() == 4;
        FVector4f Curls(0, 0, 0, 0);
        if (bXR)
        {
            const TArray<TSharedPtr<FJsonValue>>* C = nullptr;
            if (P.XR->TryGetArrayField(TEXT("c"), C) && C->Num() == 4) Curls = FVector4f((*C)[0]->AsNumber(), (*C)[1]->AsNumber(), (*C)[2]->AsNumber(), (*C)[3]->AsNumber());
            M->SetXR(true, FQuat((*H)[0]->AsNumber(), (*H)[1]->AsNumber(), (*H)[2]->AsNumber(), (*H)[3]->AsNumber()).GetNormalized(), Curls);
            if (M->HasOneShot() || !P.Actor.IsValid()) continue;
            for (const TPair<const TCHAR*, const TCHAR*>& S : {TPair<const TCHAR*, const TCHAR*>{TEXT("l"), TEXT("leftHand")}, {TEXT("r"), TEXT("rightHand")}})
            {
                const TArray<TSharedPtr<FJsonValue>>* G = nullptr;
                if (!P.XR->TryGetArrayField(S.Key, G) || G->Num() != 7) continue;
                const FVector Local((*G)[0]->AsNumber(), (*G)[1]->AsNumber(), (*G)[2]->AsNumber());
                M->SolveReachWeighted(FName(S.Value), P.Actor->GetActorTransform().TransformPosition(FromWirePosition(Local)), 1);
            }
        }
        else M->SetXR(false, FQuat::Identity, Curls);
    }
    if (LocalReach || !SelfBody.ReachWeight.IsEmpty()) Solve(SelfBody, LocalReach, true);
    // Narrate reaches aimed at me (reachnet.js diffReach), 10 s quiet per (who, limb).
    for (auto& Pair : Peers)
    {
        FEidoversePeer& P = Pair.Value;
        TMap<FString, FString> Now2;
        for (const TCHAR* Limb : Limbs)
        {
            const TSharedPtr<FJsonObject> Entry = ReachWire::Obj(P.Reach, Limb), T = ReachWire::Obj(Entry, TEXT("t"));
            if (!T || (ReachWire::Str(T, TEXT("who")) != AssignedIdentity && ReachWire::Str(T, TEXT("space")) != AssignedIdentity)) continue;
            bool bReached = false; Entry->TryGetBoolField(TEXT("reached"), bReached);
            Now2.Add(Limb, bReached ? TEXT("touch") : TEXT("reach"));
        }
        if (!P.bReachBaseline) { P.bReachBaseline = true; P.ReachSeen = Now2; continue; }
        auto LimbName = [](const FString& L) { return L == TEXT("leftHand") ? TEXT("left hand") : L == TEXT("rightHand") ? TEXT("right hand") : L == TEXT("leftFoot") ? TEXT("left foot") : TEXT("right foot"); };
        for (const auto& N : Now2)
        {
            const FString* Was = P.ReachSeen.Find(N.Key);
            if (Was && *Was == N.Value) continue;
            if (Now < P.ReachQuietUntil.FindRef(N.Key) && N.Value == TEXT("reach")) continue;
            const FString Point = ReachWire::Str(ReachWire::Obj(ReachWire::Obj(P.Reach, *N.Key), TEXT("t")), TEXT("point"));
            if (N.Value == TEXT("touch")) Narrate(FString::Printf(TEXT("%s's %s touches your %s"), *Pair.Key, LimbName(N.Key), Point.IsEmpty() ? TEXT("body") : *Point.Replace(TEXT("_"), TEXT(" "))));
            else Narrate(FString::Printf(TEXT("%s reaches toward your %s (%s)"), *Pair.Key, Point.IsEmpty() ? TEXT("body") : *Point.Replace(TEXT("_"), TEXT(" ")), LimbName(N.Key)));
            P.ReachQuietUntil.Add(N.Key, Now + 10);
        }
        for (const auto& W : P.ReachSeen) if (!Now2.Contains(W.Key)) Narrate(FString::Printf(TEXT("%s withdraws their %s"), *Pair.Key, LimbName(W.Key)));
        P.ReachSeen = Now2;
    }
}
