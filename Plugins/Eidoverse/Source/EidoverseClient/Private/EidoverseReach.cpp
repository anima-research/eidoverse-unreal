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
    // This first-person client has no locally solved hand: never forge reached:true.
    if(!LocalReach)LocalReach=MakeShared<FJsonObject>();LocalReach->SetObjectField(Left?TEXT("leftHand"):TEXT("rightHand"),Entry);
    InteractionStatus=TEXT("Reaching toward ")+Participant+TEXT(" · touch is not attested by this client.");return true;
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
void AEidoverseBridge::TickReaches()
{
    for(auto& Pair:Peers)
    {
        auto& P=Pair.Value;if(!P.AvatarMesh.IsValid()||!P.Reach||P.bSubmarine)continue;
        for(const TCHAR* Limb:{TEXT("leftHand"),TEXT("rightHand"),TEXT("leftFoot"),TEXT("rightFoot")})
        {
            auto Entry=ReachWire::Obj(P.Reach,Limb),T=ReachWire::Obj(Entry,TEXT("t"));if(!T)continue;
            FVector Target;FString Who=ReachWire::Str(T,TEXT("who"));
            if(!Who.IsEmpty())
            {
                auto* TargetPeer=Peers.Find(Who);const FName Bone=ReachWire::ContactBone(ReachWire::Str(T,TEXT("point")));
                if(Bone.IsNone()||!TargetPeer||!TargetPeer->AvatarMesh.IsValid()||!TargetPeer->AvatarMesh->HumanBonePosition(Bone,Target))continue;
                double Standoff=0;T->TryGetNumberField(TEXT("standoff"),Standoff);if(FMath::IsFinite(Standoff))Target+=TargetPeer->Actor->GetActorForwardVector()*FMath::Clamp(Standoff,0.,.2)*100;
            }
            else
            {
                const TArray<TSharedPtr<FJsonValue>>* A=nullptr;if(!T->TryGetArrayField(TEXT("p"),A)||A->Num()!=3)continue;
                double V[3];bool Valid=true;for(int I=0;I<3;++I)if(!(*A)[I]->TryGetNumber(V[I])||!FMath::IsFinite(V[I])||FMath::Abs(V[I])>1e6)Valid=false;if(!Valid)continue;
                Target=FromWirePosition(FVector(V[0],V[1],V[2]));const FString Space=ReachWire::Str(T,TEXT("space"));
                if(Space.IsEmpty()||Space==TEXT("world"))Target+=OriginCentimeters;
                else if(Space==TEXT("self"))Target=P.Actor->GetActorTransform().TransformPosition(Target);
                else if(Space==AssignedIdentity){auto* Pawn=UGameplayStatics::GetPlayerPawn(this,0);if(!Pawn)continue;Target=Pawn->GetActorTransform().TransformPosition(Target)-FVector(0,0,88);}
                else {auto* Other=Peers.Find(Space);if(!Other||!Other->Actor.IsValid())continue;Target=Other->Actor->GetActorTransform().TransformPosition(Target);}
            }
            P.AvatarMesh->SolveReach(FName(Limb),Target);
        }
    }
}
