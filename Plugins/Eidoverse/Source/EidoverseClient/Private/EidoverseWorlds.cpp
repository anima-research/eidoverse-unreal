#include "EidoverseBridge.h"
#include "EidoverseScene.h"
#include "EidoverseSettings.h"
#include "WaterOpticsController.h"
#include "Dom/JsonObject.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "Engine/PostProcessVolume.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/SkyAtmosphereComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "HAL/IConsoleManager.h"
#include "RenderUtils.h"

bool AEidoverseBridge::IsWorldName(const FString& Name)
{
    if(Name.IsEmpty()||Name.Len()>64)return false;
    for(TCHAR C:Name)if(!((C>='a'&&C<='z')||(C>='A'&&C<='Z')||(C>='0'&&C<='9')||C=='_'||C=='-'))return false;
    return true;
}
bool AEidoverseBridge::SwitchWorld(const FString& Destination)
{
    if(!IsWorldName(Destination)){WorldRenderStatus=TEXT("Invalid world name");return false;}
    SetShareSceneScreenshots(false);Disconnect();WorldName=Destination;Connect();return true;
}
void AEidoverseBridge::JoinLocalCommons()
{
    // Compiled-in development preset: survives new PIE sessions without storing
    // credentials or connection changes in the level. This door is a non-secret
    // fixture value accepted only by the isolated loopback Commons server.
    CloseChat();SetShareSceneScreenshots(false);Disconnect();
    Endpoint=TEXT("ws://127.0.0.1:19002/ws");WorldName=TEXT("commons");
    AvatarLibraryBase=TEXT("http://127.0.0.1:19002/library/");
    ParticipantName=TEXT("unreal-local-preview");AvatarPath.Empty();
    JoinToken=TEXT("commons-local-door");IdentityToken.Empty();bLocalCommonsPreset=true;
    bRenderDynamicWorld=true;bLoadRemoteAvatars=true;bSpectator=false;bReconnect=true;
    OriginCentimeters=FVector::ZeroVector;RetryDelay=1;
    Connect();
}
void AEidoverseBridge::BeginDynamicScene()
{
    EndDynamicScene();auto* PC=UGameplayStatics::GetPlayerController(this,0);if(!PC)return;
    if(auto* V=IConsoleManager::Get().FindConsoleVariable(TEXT("r.OneFrameThreadLag"))){PreviousFrameLag=V->GetInt();V->SetWithCurrentPriority(0,NAME_None,ECVF_SetByConsole,ECVF_SetByCode);}
    PrototypePawn=PC->GetPawn();
    // Runtime only: suspend the prototype without modifying saved levels. Keep
    // controller/game framework actors alive; save every changed flag exactly.
    for(TActorIterator<AActor> It(GetWorld());It;++It)
    {
        AActor* A=*It;if(A==this||A==PC||(!A->GetRootComponent()&&!A->IsA<AWaterOpticsController>())||A->IsA<AEidoverseScene>())continue;
        FEidoverseSuspendedActor S;S.Actor=A;S.bHidden=A->IsHidden();S.bTick=A->IsActorTickEnabled();S.bCollision=A->GetActorEnableCollision();
        if(auto* PP=Cast<APostProcessVolume>(A)){S.bPostProcess=PP->bEnabled;PP->bEnabled=false;}
        // Registries (water optics air boxes/lights, ocean post-process) must not
        // adopt the suspended prototype's actors while the server world is shown.
        S.bWaterPostTag=A->Tags.Remove(TEXT("WaterOpticsPostProcess"))>0;A->Tags.AddUnique(TEXT("EidoverseSuspended"));
        SuspendedActors.Add(S);A->SetActorHiddenInGame(true);A->SetActorTickEnabled(false);A->SetActorEnableCollision(false);
    }
    DynamicScene=GetWorld()->SpawnActor<AEidoverseScene>();DynamicScene->SetFlags(RF_Transient);
    auto* Explorer=GetWorld()->SpawnActor<AEidoverseExplorer>(OriginCentimeters+FVector(0,0,300),FRotator::ZeroRotator);
    ExplorerPawn=Explorer;PC->Possess(Explorer);Explorer->GetCharacterMovement()->DisableMovement();
    auto* LightActor=GetWorld()->SpawnActor<AActor>();LightActor->SetFlags(RF_Transient);DynamicEnvironment.Add(LightActor);
    auto* Sun=NewObject<UDirectionalLightComponent>(LightActor);LightActor->AddInstanceComponent(Sun);LightActor->SetRootComponent(Sun);Sun->SetIntensity(3.2f);Sun->LightSourceAngle=2.f;Sun->ShadowBias=.25f;Sun->SetAtmosphereSunLight(true);Sun->SetWorldRotation(FRotator(-48,-35,0));Sun->RegisterComponent();
    auto* SkyActor=GetWorld()->SpawnActor<AActor>();SkyActor->SetFlags(RF_Transient);DynamicEnvironment.Add(SkyActor);
    auto* Sky=NewObject<USkyAtmosphereComponent>(SkyActor);SkyActor->AddInstanceComponent(Sky);SkyActor->SetRootComponent(Sky);Sky->RegisterComponent();
    auto* Fill=NewObject<USkyLightComponent>(SkyActor);SkyActor->AddInstanceComponent(Fill);Fill->SetupAttachment(Sky);Fill->SetIntensity(.8);Fill->bLowerHemisphereIsBlack=false;Fill->bRealTimeCapture=true;Fill->RegisterComponent();
    DynamicScene->Sun=Sun;DynamicScene->SkyFill=Fill;
    auto* PP=GetWorld()->SpawnActor<APostProcessVolume>();DynamicEnvironment.Add(PP);PP->bUnbound=true;PP->Priority=100;
    PP->Settings.bOverride_AutoExposureMinBrightness=true;PP->Settings.bOverride_AutoExposureMaxBrightness=true;PP->Settings.AutoExposureMinBrightness=1;PP->Settings.AutoExposureMaxBrightness=1;
    DynamicScene->ScenePost=PP;
    // Runtime GLBs now get six-axis surface-cache cards. Procedural terrain,
    // structures and skinned avatars still need hit lighting; software Lumen
    // cannot trace these reliably without baked distance fields.
    const auto* HardwareLumen=IConsoleManager::Get().FindConsoleVariable(TEXT("r.Lumen.HardwareRayTracing"));
    const bool bLumen=bUseLumenLighting&&EidoSettings::Lumen()&&IsRayTracingEnabled(GMaxRHIShaderPlatform)&&HardwareLumen&&HardwareLumen->GetInt()!=0;
    PP->Settings.bOverride_DynamicGlobalIlluminationMethod=true;PP->Settings.DynamicGlobalIlluminationMethod=bLumen?EDynamicGlobalIlluminationMethod::Lumen:EDynamicGlobalIlluminationMethod::ScreenSpace;
    PP->Settings.bOverride_ReflectionMethod=true;PP->Settings.ReflectionMethod=bLumen?EReflectionMethod::Lumen:EReflectionMethod::ScreenSpace;
    PP->Settings.bOverride_LumenRayLightingMode=true;PP->Settings.LumenRayLightingMode=ELumenRayLightingModeOverride::HitLighting;
    WorldLightingStatus=bLumen?TEXT("Lumen · hardware ray tracing · hit lighting"):TEXT("Screen-space fallback · hardware Lumen unavailable or disabled");
    UE_LOG(LogTemp,Display,TEXT("EIDOVERSE lighting: %s"),*WorldLightingStatus);
    PP->Settings.bOverride_MotionBlurAmount=true;PP->Settings.MotionBlurAmount=0;
    PP->Settings.bOverride_ScreenSpaceReflectionQuality=true;PP->Settings.ScreenSpaceReflectionQuality=50;
    bSceneDirty=true;bSceneWaiting=false;bSceneMoving=false;LastSceneRequest=-100;WorldRenderStatus=TEXT("Loading server-authored scene…");
}
void AEidoverseBridge::EndDynamicScene()
{
    WorldLightingStatus.Empty();
    if(PreviousFrameLag>=0){if(auto* V=IConsoleManager::Get().FindConsoleVariable(TEXT("r.OneFrameThreadLag")))V->SetWithCurrentPriority(PreviousFrameLag,NAME_None,ECVF_SetByConsole,ECVF_SetByCode);PreviousFrameLag=-1;}
    ClearInteractions();
    if(DynamicScene){DynamicScene->Destroy();DynamicScene=nullptr;}
    for(auto& A:DynamicEnvironment)if(IsValid(A))A->Destroy();DynamicEnvironment.Empty();
    if(ExplorerPawn.IsValid())
    {if(auto* PC=UGameplayStatics::GetPlayerController(this,0))if(PrototypePawn.IsValid())PC->Possess(PrototypePawn.Get());ExplorerPawn->Destroy();}
    ExplorerPawn.Reset();PrototypePawn.Reset();
    for(auto& S:SuspendedActors)if(auto* A=S.Actor.Get())
    {A->SetActorHiddenInGame(S.bHidden);A->SetActorTickEnabled(S.bTick);A->SetActorEnableCollision(S.bCollision);if(auto* PP=Cast<APostProcessVolume>(A))PP->bEnabled=S.bPostProcess;
     A->Tags.Remove(TEXT("EidoverseSuspended"));if(S.bWaterPostTag)A->Tags.AddUnique(TEXT("WaterOpticsPostProcess"));}
    SuspendedActors.Empty();bSceneDirty=bSceneMoving=bSceneWaiting=false;bDeterministicScenePhysics=false;SceneRequestInterval=.3;
}
void AEidoverseBridge::TickDynamicScene(double Now)
{
    if(!DynamicScene||!bSceneSupported)return;
    if(bSceneWaiting&&Now-LastSceneRequest>5){bSceneWaiting=false;bSceneDirty=true;WorldRenderStatus=TEXT("Scene response delayed; retrying");}
    if(!bSceneWaiting&&(bSceneDirty||bSceneMoving)&&Now-LastSceneRequest>=SceneRequestInterval)
    {
        auto J=MakeShared<FJsonObject>();J->SetStringField(TEXT("type"),TEXT("render-scene"));J->SetNumberField(TEXT("version"),1);
        J->SetBoolField(TEXT("reset"),LastSceneRequest<0);Send(J);LastSceneRequest=Now;bSceneWaiting=true;bSceneDirty=false;
    }
    if(DynamicScene)WorldRenderStatus=DynamicScene->Diagnostic+FString::Printf(TEXT(" · %d models loaded / %d failed"),DynamicScene->LoadedModels,DynamicScene->FailedModels);
}
void AEidoverseBridge::ReceiveDynamicScene(const TSharedPtr<FJsonObject>& Message)
{
    FString Name;if(!DynamicScene||!Message->TryGetStringField(TEXT("world"),Name)||Name!=WorldName)return;
    bSceneWaiting=false;Message->TryGetBoolField(TEXT("moving"),bSceneMoving);
    {
        // Keep the folded sky/grass args for the owner panels (sent only when changed).
        const TSharedPtr<FJsonObject>* Env=nullptr;
        if(Message->TryGetObjectField(TEXT("environment"),Env))
        {
            const TSharedPtr<FJsonObject>* O=nullptr;
            WorldSky=(*Env)->TryGetObjectField(TEXT("sky"),O)?*O:nullptr;
            WorldGrass=(*Env)->TryGetObjectField(TEXT("grass"),O)?*O:nullptr;
            DynamicScene->SetSkyArgs(WorldSky);
        }
    }DynamicScene->Apply(Message,OriginCentimeters,AvatarLibraryBase);
    if(Message->HasField(TEXT("error")))
    {
        // A refusal stops polling, except terrain_pending (render-scene.ts): that one says moving:true and an
        // interval, and the scene arrives once the grid loads. The explorer stays parked until then.
        if(Message->GetStringField(TEXT("error"))==TEXT("terrain_pending"))
        {
            double Interval=300;if(Message->TryGetNumberField(TEXT("updateIntervalMs"),Interval)&&FMath::IsFinite(Interval))SceneRequestInterval=FMath::Clamp(Interval/1000.,.066,2.);
        }
        else bSceneMoving=false;
        return;
    }
    if(bEmptyWorldNoticePending)
    {
        bEmptyWorldNoticePending=false;
        if(DynamicScene->GetItems().Num()==0)
        {
            Narrate(FString::Printf(TEXT("\u201c%s\u201d is empty \u2014 nothing has ever been built here."),*WorldName));
            Narrate(TEXT("press B to start building, or ? for the controls."));
            ToastText(FString::Printf(TEXT("\u201c%s\u201d is an empty world \u2014 press B to build in it"),*WorldName));
        }
    }
    double Interval=300;if(Message->TryGetNumberField(TEXT("updateIntervalMs"),Interval)&&FMath::IsFinite(Interval))SceneRequestInterval=FMath::Clamp(Interval/1000.,.066,.3);
    const TSharedPtr<FJsonObject>* Sim=nullptr;
    if(Message->TryGetObjectField(TEXT("simulation"),Sim))(*Sim)->TryGetBoolField(TEXT("enabled"),bDeterministicScenePhysics);
    if(auto* Explorer=Cast<AEidoverseExplorer>(ExplorerPawn.Get()))if(Explorer->GetCharacterMovement()->MovementMode==MOVE_None)
    {
        const TSharedPtr<FJsonObject>* Env=nullptr;const TSharedPtr<FJsonObject>* T=nullptr;const TArray<TSharedPtr<FJsonValue>>* H=nullptr;
        double Ground=0;
        if(Message->TryGetObjectField(TEXT("environment"),Env)&&(*Env)->TryGetObjectField(TEXT("terrain"),T)&&(*T)->TryGetArrayField(TEXT("heights"),H)&&!H->IsEmpty())Ground=(*H)[H->Num()/2]->AsNumber()*100;
        // An authored environment spawn (wire metres, feet) wins over the terrain centre.
        FVector Spawn;
        Explorer->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
        // Wake where you fell asleep (snapshot restore), else the world's spawn.
        if(ApplyRestore()){}
        else if(DynamicScene->EnvironmentSpawn(Spawn))Explorer->SetActorLocation(OriginCentimeters+FromWirePosition(Spawn)+FVector(0,0,90));
        else Explorer->SetActorLocation(OriginCentimeters+FVector(0,0,Ground+100));
        Explorer->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
    }
}
