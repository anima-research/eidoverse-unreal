#include "EidoverseBridge.h"
#include "EidoverseAvatar.h"
#include "EidoverseSnapFrame.h"
#include "SpectralOcean.h"
#include "Dom/JsonObject.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Components/TextRenderComponent.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "Kismet/GameplayStatics.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Modules/ModuleManager.h"
#include "Misc/Base64.h"
#include "RenderingThread.h"
#include "TextureResource.h"

void AEidoverseBridge::SetShareSceneScreenshots(bool bEnabled)
{
    bShareSceneScreenshots=bEnabled;
    CaptureConsentEndpoint=bEnabled?Endpoint:FString();CaptureConsentWorld=bEnabled?WorldName:FString();
    CaptureStatus=bEnabled?TEXT("Scene sharing enabled · up to one 960×540 image / 2 seconds"):TEXT("Scene sharing off");
    if(!bEnabled)
    {
        if(RemoteCapture){RemoteCapture->DestroyComponent();RemoteCapture=nullptr;}
        RemoteCaptureTarget=nullptr;
    }
    AdvertiseCapture();
}
void AEidoverseBridge::AdvertiseCapture()
{
    if(!bJoined)return;
    if(bShareSceneScreenshots&&(CaptureConsentEndpoint!=Endpoint||CaptureConsentWorld!=WorldName))
    {SetShareSceneScreenshots(false);return;}
    // An older server sees no new messages unless the player explicitly opts in.
    if(!bShareSceneScreenshots&&!bCaptureAdvertised)return;
    auto J=MakeShared<FJsonObject>();J->SetStringField(TEXT("type"),TEXT("capture-capabilities"));
    if(bShareSceneScreenshots)
    {
        auto C=MakeShared<FJsonObject>();C->SetNumberField(TEXT("version"),1);
        C->SetStringField(TEXT("engine"),TEXT("unreal"));C->SetStringField(TEXT("scene"),DynamicScene?TEXT("world-projection"):TEXT("underwater-prototype"));
        J->SetObjectField(TEXT("capture"),C);
    }
    else J->SetField(TEXT("capture"),MakeShared<FJsonValueNull>());
    Send(J);bCaptureAdvertised=bShareSceneScreenshots;
}
void AEidoverseBridge::ReceiveCapture(const TSharedPtr<FJsonObject>& Message)
{
    FString Id,Follow,View;
    if(!Message->TryGetStringField(TEXT("id"),Id)||Id.IsEmpty()||Id.Len()>96)return;
    auto Reply=MakeShared<FJsonObject>();Reply->SetStringField(TEXT("type"),TEXT("snap-result"));Reply->SetStringField(TEXT("id"),Id);
    auto Fail=[&](const TCHAR* Why){Reply->SetStringField(TEXT("error"),Why);Send(Reply);};
    if(!bShareSceneScreenshots||CaptureConsentEndpoint!=Endpoint||CaptureConsentWorld!=WorldName){Fail(TEXT("scene sharing disabled"));return;}
    const double Now=FPlatformTime::Seconds();
    if(Now-LastCaptureAt<2){Fail(TEXT("renderer busy"));return;}
    // Consume quota even for invalid requests. Remote input never controls image
    // dimensions, output files, console commands, URLs, or the player's camera.
    LastCaptureAt=Now;
    if(!Message->TryGetStringField(TEXT("follow"),Follow)||Follow.Len()>64||!Message->TryGetStringField(TEXT("view"),View)
        ||(View!=TEXT("first")&&View!=TEXT("third")&&View!=TEXT("selfie"))) {Fail(TEXT("invalid framing"));return;}
    // Optional framing (shared/snapframe.js), nested as `frame` so no field of the message itself (its
    // width/height are the image's) is read as the camera's. Absent fields keep the default framing.
    const TSharedPtr<FJsonObject>* RawFrame=nullptr;
    const EidoSnapFrame::FSnapFrame Frame=EidoSnapFrame::Normalize(Message->TryGetObjectField(TEXT("frame"),RawFrame)?*RawFrame:nullptr);
    AActor* Subject=nullptr;FVector Eye,Feet;FQuat Rotation;bool bSub=false;
    if(Follow==AssignedIdentity)
    {
        auto* Pawn=UGameplayStatics::GetPlayerPawn(this,0);
        if(!Pawn||bSpectator){Fail(TEXT("participant unavailable"));return;}
        FRotator EyeRotation;Subject=Pawn;Pawn->GetActorEyesViewPoint(Eye,EyeRotation);
        Rotation=Pawn->GetViewRotation().Quaternion();bSub=Pawn->ActorHasTag(TEXT("OceanSubmarine"));
        Feet=Pawn->GetActorLocation()-FVector(0,0,Pawn->GetSimpleCollisionHalfHeight());
    }
    else
    {
        auto* P=Peers.Find(Follow);
        if(!P||!P->Actor.IsValid()||P->Samples.IsEmpty()){Fail(TEXT("participant pose unavailable"));return;}
        Subject=P->Actor.Get();Rotation=Subject->GetActorQuat();bSub=P->bSubmarine;
        Eye=Subject->GetActorLocation()+Rotation.GetUpVector()*(P->bPawnAnchor?65:160);
        // a feet-anchored proxy stands on its origin; a pawn-anchored one hangs its mesh 85 cm x size below
        Feet=Subject->GetActorLocation()-FVector(0,0,P->bPawnAnchor?85*P->BodyScale:0);
        if(P->AvatarMesh.IsValid()&&P->AvatarMesh->Avatar)
            if(const FName* Head=P->AvatarMesh->Avatar->Humanoid.Find(TEXT("head")))
                Eye=P->AvatarMesh->GetBoneLocation(*Head,EBoneSpaces::WorldSpace);
    }
    if(Eye.ContainsNaN()||Rotation.ContainsNaN()||Eye.SizeSquared()>1e16){Fail(TEXT("invalid participant pose"));return;}
    const FVector Forward=Rotation.GetForwardVector(),Up=Rotation.GetUpVector();
    if(View==TEXT("first"))
    {
        Eye+=Forward*12;
        if(Frame.Pitch.IsSet())Rotation=EidoSnapFrame::FirstPersonGaze(*Frame.Pitch,Rotation);   // same eye and heading, tilted
    }
    else if(bSub)
    {
        // A vessel is framed at its own scale; an asked-for distance or height still wins.
        if(View==TEXT("third"))Eye+=-Forward*(Frame.Dist?*Frame.Dist*100:2800)+Up*(Frame.Height?*Frame.Height*100:450);
        else
        {
            const FVector Aim=Eye-Up*30;
            Eye+=Forward*(Frame.Dist?*Frame.Dist*100:2800)+Up*(Frame.Height?*Frame.Height*100:0);Rotation=(Aim-Eye).Rotation().Quaternion();
        }
    }
    else EidoSnapFrame::BodyCamera(View,Frame,Feet,Forward,Eye,Rotation);   // net_snap.js's camera, default or framed
    if(!RemoteCapture)
    {
        RemoteCaptureTarget=NewObject<UTextureRenderTarget2D>(this);
        RemoteCaptureTarget->RenderTargetFormat=RTF_RGBA8;RemoteCaptureTarget->InitAutoFormat(960,540);
        RemoteCaptureTarget->UpdateResourceImmediate(true);
        RemoteCapture=NewObject<USceneCaptureComponent2D>(this);
        AddInstanceComponent(RemoteCapture);RemoteCapture->SetupAttachment(GetRootComponent());
        RemoteCapture->bCaptureEveryFrame=false;RemoteCapture->bCaptureOnMovement=false;
        // Material.cpp requires View.State for post-process blendables. Without
        // it one-shot captures silently omit our underwater optics material.
        RemoteCapture->bAlwaysPersistRenderingState=true;
        RemoteCapture->CaptureSource=SCS_FinalColorLDR;RemoteCapture->FOVAngle=80;
        RemoteCapture->TextureTarget=RemoteCaptureTarget;
        RemoteCapture->ShowFlags.SetTemporalAA(false);RemoteCapture->ShowFlags.SetMotionBlur(false);
        // Use this view's world-volume exposure range. The camera-cut flag below
        // forces immediate adaptation instead of inheriting the previous shot.
        RemoteCapture->RegisterComponent();
    }
    RemoteCapture->HiddenActors.Reset();RemoteCapture->HiddenComponents.Reset();
    if(View==TEXT("first"))RemoteCapture->HideActorComponents(Subject);
    for(const auto& Pair:Peers)if(Pair.Value.Label.IsValid())RemoteCapture->HiddenComponents.Add(Pair.Value.Label.Get());
    RemoteCapture->SetWorldLocationAndRotation(Eye,Rotation);
    RemoteCapture->bCameraCutThisFrame=true;
    TArray<FColor> Pixels;bool Read=false;
    auto Capture=[&]()
    {
        RemoteCapture->CaptureScene();
        FReadSurfaceDataFlags Flags(RCM_UNorm);Flags.SetLinearToGamma(false);
        Read=RemoteCaptureTarget->GameThread_GetRenderTargetResource()->ReadPixels(Pixels,Flags);
    };
    auto* Ocean=Cast<ASpectralOcean>(UGameplayStatics::GetActorOfClass(this,ASpectralOcean::StaticClass()));
    if(Ocean&&!DynamicScene)Ocean->WithCaptureView(Eye,Capture);else Capture();
    RemoteCapture->HiddenActors.Reset();RemoteCapture->HiddenComponents.Reset();
    if(!Read||Pixels.Num()!=960*540){Fail(TEXT("capture readback failed"));return;}
    for(FColor& Pixel:Pixels)Pixel.A=255;
    auto& Module=FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
    auto PNG=Module.CreateImageWrapper(EImageFormat::PNG);
    if(!PNG||!PNG->SetRaw(Pixels.GetData(),Pixels.Num()*sizeof(FColor),960,540,ERGBFormat::BGRA,8)){Fail(TEXT("PNG encoding failed"));return;}
    const auto& Bytes=PNG->GetCompressed();
    if(Bytes.IsEmpty()||Bytes.Num()>4*1024*1024){Fail(TEXT("image too large"));return;}
    Reply->SetStringField(TEXT("dataUrl"),TEXT("data:image/png;base64,")+FBase64::Encode(Bytes.GetData(),Bytes.Num()));
    Send(Reply);CaptureStatus=FString::Printf(TEXT("Shared %s view%s · 960×540 · %.0f ms"),*View,Frame.IsEmpty()?TEXT(""):TEXT(" (framed)"),(FPlatformTime::Seconds()-Now)*1000);
}
