// WHERE a nameplate hangs (plateanchor.js + avatar.js _placePlate/_liftPlateForView): anchored to the body,
// following its posture. The crown is measured ONCE at rest; the plate hangs over the LIVE HIPS at the rest
// hips→crown height (sitting lowers it for free), blends over the head when lying (a low head AND a torso gone
// over), and standing in ordinary motion is RIGID — idle/walk/run/jump hang it at the rest crown over the root,
// so a jump's crouch and tuck no longer drag it; only a posture change eases. Seen from above, each viewer
// slides the plate along its own camera's up just enough to keep it standing off the head.
// Native pills are screen-space with their bottom edge at the anchor, so the pill's half-height is 0 here.
#include "EidoverseBridge.h"
#include "EidoverseAvatar.h"
#include "Engine/SkeletalMesh.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/PlayerController.h"

namespace EidoPlate
{
    constexpr float EYE_K=1.35f,NO_EYE_K=.45f,BOUNDS_CAP_K=1.8f;
    constexpr float GAP_K=.05f,GAP_MIN=3,GAP_MAX=15;   // cm
    constexpr float LIE_START=.5f,LIE_FULL=.3f,TORSO_START=.7f,TORSO_FULL=.5f;
    constexpr float TAU=.12f,DEAD=2;                   // s, cm

    float Band(float X,float Rest,float Start,float Full)
    {
        if(!FMath::IsFinite(X)||!(Rest>0))return 0;
        const float T=FMath::Clamp((Start-X/Rest)/(Start-Full),0.f,1.f);return T*T*(3-2*T);
    }
    float LieAmount(float HeadAboveFeet,float RestHeadAboveFeet,float HeadOverHips,float RestHeadOverHips)
    {
        return FMath::Min(Band(HeadAboveFeet,RestHeadAboveFeet,LIE_START,LIE_FULL),Band(HeadOverHips,RestHeadOverHips,TORSO_START,TORSO_FULL));
    }
    float SmoothY(float Prev,float Target,float Dt,float Dead)
    {
        if(!FMath::IsFinite(Prev))return Target;
        const float Err=Target-Prev;if(FMath::Abs(Err)<=Dead)return Prev;
        const float Goal=Target-FMath::Sign(Err)*Dead;
        return Prev+(Goal-Prev)*(1-FMath::Exp(-FMath::Max(0.f,Dt)/TAU));
    }
    // plateViewLift, in UE axes (Z up): the least lift along the camera's up that keeps the plate's bottom
    // standing off the head's top by its eye-level drop. 0 for a level or upward-looking camera.
    float ViewLift(const FVector& Eye,const FVector& Up,const FVector& Fwd,const FVector& Plate,float Drop,float HeadR,float HalfH)
    {
        const FVector P=Plate-Eye;const float ZP=FVector::DotProduct(P,Fwd);
        const float Down=Drop+HeadR;const float ZH=ZP-Down*Fwd.Z;
        if(!(Fwd.Z<0)||!(ZP>.1f)||!(ZH>.1f))return 0;
        const float YP=FVector::DotProduct(P,Up),YH=YP-Down*Up.Z;
        return FMath::Max(0.f,ZP*(YH+HeadR+Drop-HalfH)/ZH-YP+HalfH);
    }
}

namespace
{
    // The rest crown (crownEstimate) in model units: heights over the mesh origin with the component's
    // rotation applied and its scale divided out, so the live scale multiplies back in.
    FEidoversePeer::FPlateRest MeasureRest(const UEidoverseAvatarMesh* M)
    {
        using namespace EidoPlate;
        FEidoversePeer::FPlateRest R;
        if(!M||!M->Avatar||!M->Avatar->Mesh)return R;
        const FTransform CT=M->GetComponentTransform();
        const float S=FMath::Max(1e-3f,float(CT.GetScale3D().Z));
        auto Up=[&](const FVector& Local){return float(CT.TransformVector(Local).Z)/S;};
        auto Y=[&](FName B,float& Out){FVector L;if(!M->HumanBoneRestPosition(B,L))return false;Out=Up(L);return true;};
        float Hips=0,Head=0,LE=0,RE=0,LF=0,RF=0;
        const bool bHips=Y(TEXT("hips"),Hips),bHead=Y(TEXT("head"),Head),bLE=Y(TEXT("leftEye"),LE),bRE=Y(TEXT("rightEye"),RE);
        const bool bLF=Y(TEXT("leftFoot"),LF),bRF=Y(TEXT("rightFoot"),RF);
        const float Feet=bLF&&bRF?FMath::Min(LF,RF):bLF?LF:bRF?RF:0;
        // the mesh's top at rest, from its reference bounds
        const FBoxSphereBounds B=M->Avatar->Mesh->GetBounds();float Top=-1e9f;
        for(int32 I=0;I<8;++I)Top=FMath::Max(Top,Up(B.Origin+B.BoxExtent*FVector(I&1?1:-1,I&2?1:-1,I&4?1:-1)));
        const bool bTop=Top>-1e8f;
        if(bHips&&bHead&&Head>Hips)
        {
            const bool bEye=bLE||bRE;const float Eye=bLE&&bRE?(LE+RE)/2:bLE?LE:RE;
            const float EyeCrown=bEye&&Eye>Head?Eye+EYE_K*(Eye-Head):Head+NO_EYE_K*(Head-Hips);
            const float Cap=Head+BOUNDS_CAP_K*(EyeCrown-Head);
            const float Crown=bTop?FMath::Max(EyeCrown,FMath::Min(Top,Cap)):EyeCrown;
            R.bValid=R.bBones=true;R.Hips=Hips;R.Head=Head;R.Crown=Crown;R.HeadSpan=Crown-Head;R.HipsToCrown=Crown-Hips;
            R.RestHeadAboveFeet=Head-Feet;R.Height=Crown-Feet;
        }
        else if(bTop){R.bValid=true;R.Crown=R.BoundsTop=Top;R.Height=Top-Feet;}
        R.BoundsTop=bTop?Top:R.Crown;
        return R;
    }
    const TSet<FString>& StandSlots(){static const TSet<FString> S={TEXT("idle"),TEXT("walk"),TEXT("run"),TEXT("jump")};return S;}
}

bool AEidoverseBridge::PlaceNameplate(FEidoversePeer& P,float Dt,FVector& Anchor)
{
    using namespace EidoPlate;
    UEidoverseAvatarMesh* M=P.AvatarMesh.Get();
    if(P.bSubmarine||!M||!M->Avatar||!M->Avatar->Mesh)return false;
    if(P.PlateRestFor.Get()!=M->Avatar->Mesh){P.PlateRest=MeasureRest(M);P.PlateRestFor=M->Avatar->Mesh;P.PlateOff=NAN;}
    const FEidoversePeer::FPlateRest& R=P.PlateRest;
    if(!R.bValid)return false;
    const FTransform CT=M->GetComponentTransform();
    const float S=float(CT.GetScale3D().Z);
    const FVector Root=CT.GetLocation();
    const float Gap=FMath::Clamp(GAP_K*R.Height*S,GAP_MIN,GAP_MAX);
    const float Lift=P.PlateY*100*P.BodyScale;   // bodyscale.js plateLift: the wearer's own lift, grows with the body
    const float UpBy=Gap+Lift;
    FVector Hips,Head;
    const bool bBones=R.bBones&&M->HumanBonePosition(TEXT("hips"),Hips)&&M->HumanBonePosition(TEXT("head"),Head);
    float X=Root.X,Y=Root.Y,Z=Root.Z+R.BoundsTop*S+UpBy;P.PlateLie=0;
    if(bBones)
    {
        FVector F;float FeetZ=TNumericLimits<float>::Max();
        for(const TCHAR* Foot:{TEXT("leftFoot"),TEXT("rightFoot")})if(M->HumanBonePosition(Foot,F))FeetZ=FMath::Min(FeetZ,float(F.Z));
        if(FeetZ==TNumericLimits<float>::Max())FeetZ=Root.Z;
        const float Stand=FMath::Max(float(Hips.Z)+R.HipsToCrown*S,float(Head.Z)+R.HeadSpan*S)+UpBy;
        const float Lie=LieAmount(Head.Z-FeetZ,R.RestHeadAboveFeet*S,Head.Z-Hips.Z,(R.HipsToCrown-R.HeadSpan)*S);
        const float LieZ=Head.Z+R.HeadSpan*S+UpBy;
        X=FMath::Lerp(Hips.X,Head.X,Lie);Y=FMath::Lerp(Hips.Y,Head.Y,Lie);Z=FMath::Lerp(Stand,LieZ,Lie);P.PlateLie=Lie;
    }
    // standing: upright in ordinary motion, with nothing else owning the body (a gesture, a held pose, a fall)
    FString Slot=P.Clip;if(Slot.IsEmpty())Slot=P.Speed>.05f?TEXT("walk"):TEXT("idle");
    P.bPlateStanding=bBones&&StandSlots().Contains(Slot)&&P.ActiveEmote.IsEmpty()&&!P.HeldPose.IsValid()&&P.Pins.IsEmpty();
    float Target=Z-Root.Z,Dead=DEAD;
    if(P.bPlateStanding){X=Hips.X;Y=Hips.Y;P.PlateLie=0;Target=R.Crown*S+UpBy;Dead=0;}
    P.PlateOff=SmoothY(P.PlateOff,Target,Dt,Dead);
    Anchor=FVector(X,Y,Root.Z+P.PlateOff);
    // seen from above: slide along this viewer's camera up
    if(APlayerController* PC=UGameplayStatics::GetPlayerController(this,0))
    {
        FVector Eye;FRotator Rot;PC->GetPlayerViewPoint(Eye,Rot);
        const FQuat Q=Rot.Quaternion();
        const float HeadR=R.bBones?R.HeadSpan*S/2:10;
        const float D=ViewLift(Eye,Q.GetUpVector(),Q.GetForwardVector(),Anchor,UpBy,HeadR,0);
        if(D>0)Anchor+=Q.GetUpVector()*D;
    }
    return true;
}

FString AEidoverseBridge::DescribePeerPlate(const FString& Id) const
{
    const FEidoversePeer* P=Peers.Find(Id);
    if(!P||!P->PlateRest.bValid)return FString();
    const UEidoverseAvatarMesh* M=P->AvatarMesh.Get();
    const float S=M?float(M->GetComponentTransform().GetScale3D().Z):1.f;
    return FString::Printf(TEXT("%s|%.2f|%.1f|%.1f"),P->bPlateStanding?TEXT("standing"):TEXT("live"),P->PlateLie,P->PlateOff,P->PlateRest.Crown*S);
}
