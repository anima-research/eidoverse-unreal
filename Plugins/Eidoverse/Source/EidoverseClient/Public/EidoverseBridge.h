#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "EidoverseBridge.generated.h"

class IWebSocket;
class FJsonObject;
class UStaticMesh;
class UTextRenderComponent;
class UStaticMeshComponent;
class UEidoverseAvatarAsset;
class UEidoverseAvatarMesh;
class SEidoversePanel;
class SWidget;
class IHttpRequest;
class USceneCaptureComponent2D;
class UTextureRenderTarget2D;
class AEidoverseScene;
class APawn;
class APostProcessVolume;
class UEidoverseHUD;

struct FEidoverseSuspendedActor
{
    TWeakObjectPtr<AActor> Actor;
    bool bHidden=false,bTick=false,bCollision=false,bPostProcess=false,bWaterPostTag=false;
};

USTRUCT(BlueprintType)
struct FEidoverseChatLine
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadOnly, Category="Eidoverse") FString Sender;
    UPROPERTY(BlueprintReadOnly, Category="Eidoverse") FString Text;
    UPROPERTY(BlueprintReadOnly, Category="Eidoverse") bool bPrivate=false;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FEidoverseChat, const FString&, Sender, const FString&, Text);

USTRUCT(BlueprintType)
struct FEidoverseAvatarChoice
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadOnly, Category="Eidoverse") FString Name;
    UPROPERTY(BlueprintReadOnly, Category="Eidoverse") FString Path;
};

struct FEidoverseSample
{
    double Time=0;
    FVector Position=FVector::ZeroVector;
    FQuat Orientation=FQuat::Identity;
};
struct FEidoversePeer
{
    TWeakObjectPtr<AActor> Actor;
    TWeakObjectPtr<UStaticMeshComponent> Mesh;
    TWeakObjectPtr<UTextRenderComponent> Label;
    // Screen-space name pill + speech bubble (UEidoverseNameplate), riding the Label anchor.
    TWeakObjectPtr<class UWidgetComponent> Plate;
    TArray<FEidoverseSample> Samples;
    FString Mode;
    FString AvatarPath;
    TWeakObjectPtr<UEidoverseAvatarAsset> AvatarAsset;
    TWeakObjectPtr<UEidoverseAvatarMesh> AvatarMesh;
    float Speed=0;
    FVector VelocityMeters=FVector::ZeroVector;
    bool bPawnAnchor=false;
    bool bSubmarine=false;
    TSharedPtr<FJsonObject> Reach;
    FString LastEmote,ActiveEmote;
    double EmoteUntil=0;
    // Clip state from the pose (clip, clipTime/clipTimeSlot/clipRate), web-identical playback.
    FString Clip;
    float ClipTime=-1,ClipRate=0;
    FString ClipTimeSlot;
    bool bClipSample=false;   // a new packet carried clipTime for this frame
    bool bEmotePending=false; // ActiveEmote should start playing
    // Head pitch (pose-wire pitch, radians, + looks down), held pose fingerprint, and
    // when this body's mouth stops moving (chat lines), as avatar.js tracks them.
    float Pitch=0;
    FString HeldPoseKey;
    TSharedPtr<FJsonObject> HeldPose;
    bool bHeldPoseDirty=false;
    double SpeakingUntil=0;
};

struct FEidoverseObjectSim
{
    FVector Velocity=FVector::ZeroVector;
    FVector Extent=FVector(20),CenterOffset=FVector::ZeroVector;
    FQuat RestRotation=FQuat::Identity;
    float Settling=0,Age=0,GroundGrace=0;
    double SentAt=0;
    bool bBall=false;
};

// Presence/chat plus an opt-in projection of the server's canonical world fold.
UCLASS(BlueprintType)
class EIDOVERSECLIENT_API AEidoverseBridge : public AActor
{
    GENERATED_BODY()
public:
    AEidoverseBridge();
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    virtual void Tick(float DeltaSeconds) override;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Eidoverse") FString Endpoint=TEXT("ws://127.0.0.1:8993/ws");
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Eidoverse") FString WorldName=TEXT("underwater-prototype");
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Eidoverse") FString ParticipantName=TEXT("unreal-diver");
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Eidoverse") FString AvatarPath=TEXT("eidoverse/assets/vrms/claude.vrm");
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Eidoverse") bool bAutoConnect=false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Eidoverse") bool bSpectator=false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Eidoverse") bool bReconnect=true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Eidoverse|World") bool bRenderDynamicWorld=false;
    // Hardware Lumen avoids relying on absent runtime-mesh distance fields.
    // Disable before joining to use the cheaper screen-space fallback.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Eidoverse|World") bool bUseLumenLighting=true;
    UPROPERTY(Transient, BlueprintReadOnly, Category="Eidoverse|World") FString WorldLightingStatus;
    UPROPERTY(Transient, BlueprintReadOnly, Category="Eidoverse|World") FString WorldRenderStatus;
    UFUNCTION(BlueprintCallable, Category="Eidoverse|World") bool SwitchWorld(const FString& Destination);
    UFUNCTION(BlueprintCallable, Category="Eidoverse|World") void JoinLocalCommons();
    static bool IsWorldName(const FString& Name);
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Eidoverse|UI") bool bShowChatUI=true;
    UPROPERTY(Transient, BlueprintReadOnly, Category="Eidoverse|UI") TArray<FEidoverseChatLine> ChatHistory;
    UPROPERTY(Transient, BlueprintReadOnly, Category="Eidoverse|UI") FString ChatSendStatus;
    // The UMG shell (UEidoverseHUD). Set a Widget Blueprint subclass to redesign it.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Eidoverse|UI") TSubclassOf<UEidoverseHUD> HUDClass;
    UPROPERTY(Transient, BlueprintReadOnly, Category="Eidoverse|UI") TObjectPtr<UEidoverseHUD> HUD;
    UFUNCTION(BlueprintPure, Category="Eidoverse") TArray<FString> GetParticipants() const;
    // Queue a gesture by name (server-validated vocabulary: /defs emotes).
    UFUNCTION(BlueprintCallable, Category="Eidoverse") bool Emote(const FString& Name);
    // Presence-only "composing" signal (never logged). To: whisper recipient, or empty.
    UFUNCTION(BlueprintCallable, Category="Eidoverse") bool SendTyping(const FString& To);

    // Rights (snapshot yourRights, live your-rights): visitor | builder | owner.
    // An ownerless ("open") world makes everyone a builder. The server still decides.
    UPROPERTY(Transient, BlueprintReadOnly, Category="Eidoverse|Rights") FString WorldRole;
    UPROPERTY(Transient, BlueprintReadOnly, Category="Eidoverse|Rights") bool bWorldOpen=false;
    UPROPERTY(Transient, BlueprintReadOnly, Category="Eidoverse|Rights") bool bCanGenerate=false;
    UFUNCTION(BlueprintPure, Category="Eidoverse|Rights") bool CanBuild() const { return bJoined&&!bSpectator&&(bWorldOpen||WorldRole==TEXT("builder")||WorldRole==TEXT("owner")); }
    UFUNCTION(BlueprintPure, Category="Eidoverse|Rights") bool IsOwner() const { return bJoined&&!bSpectator&&(WorldRole==TEXT("owner")); }
    // Authoring verbs (sky, terrain, grass, spawn, place, remove, comp, ...). Refusals arrive as 'error'.
    bool SendVerb(const FString& Verb,const TSharedRef<FJsonObject>& Args);
    UFUNCTION(BlueprintCallable, Category="Eidoverse") bool SendVerbJson(const FString& Verb,const FString& ArgsJson);
    // Avatars offered by the server (snapshot 'avatars'); switching re-joins in place (web mybody.js).
    UPROPERTY(Transient, BlueprintReadOnly, Category="Eidoverse|Avatars") TArray<FEidoverseAvatarChoice> AvatarChoices;
    UFUNCTION(BlueprintCallable, Category="Eidoverse|Avatars") bool ChangeAvatar(const FString& Path);
    // Folded world environment args as last projected (render-scene environment.sky / grass).
    TSharedPtr<FJsonObject> WorldSky, WorldGrass;
    AEidoverseScene* GetScene() const { return DynamicScene; }
    // One entity's authored component bag ({type:'inspect'}, docs/NATIVE-WORLD-RENDERING.md).
    UFUNCTION(BlueprintCallable, Category="Eidoverse") bool RequestInspect(const FString& Id);
    UPROPERTY(Transient, BlueprintReadOnly, Category="Eidoverse") FString InspectedId;
    UPROPERTY(Transient, BlueprintReadOnly, Category="Eidoverse") int32 InspectSerial=0;
    // The last reply's comp bag as JSON (empty until one arrives; "{}" when none).
    UFUNCTION(BlueprintPure, Category="Eidoverse") FString GetInspectedComponents() const;
    TSharedPtr<FJsonObject> InspectReply;
    // http(s)://host[:port] of the configured endpoint, for /defs and /library.
    UFUNCTION(BlueprintPure, Category="Eidoverse") FString HttpBase() const;
    // General-purpose client (AEidoverseGameMode): show the front door while offline.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Eidoverse|UI") bool bFrontDoor=false;
    static const TCHAR* ProductionEndpoint;
    // Join from the front door: remembers server/world/name (never credentials).
    // The public server requires a Discord session; other servers use the configured tokens.
    UFUNCTION(BlueprintCallable, Category="Eidoverse") bool JoinFromDoor(const FString& Server, const FString& World, const FString& Name);
    UFUNCTION(BlueprintCallable, Category="Eidoverse") void LoadClientSettings();
    UFUNCTION(BlueprintCallable, Category="Eidoverse") void SaveClientSettings() const;
    UFUNCTION(BlueprintCallable, Category="Eidoverse|UI") void OpenChat();
    UFUNCTION(BlueprintCallable, Category="Eidoverse|UI") void CloseChat();
    // Session-only consent. Scene captures exclude Slate/HUD/chat and never use the desktop.
    UPROPERTY(Transient, BlueprintReadOnly, Category="Eidoverse|Capture") bool bShareSceneScreenshots=false;
    UPROPERTY(Transient, BlueprintReadOnly, Category="Eidoverse|Capture") FString CaptureStatus=TEXT("Scene sharing off");
    UFUNCTION(BlueprintCallable, Category="Eidoverse|Capture") void SetShareSceneScreenshots(bool bEnabled);
    void AddChatLine(const FString& Sender,const FString& Text,bool bPrivate=false);
#if WITH_DEV_AUTOMATION_TESTS
    void ProbeLocalChatUI(bool bPrivate);
#endif
    UPROPERTY(Transient, BlueprintReadOnly, Category="Eidoverse|Login") FString LoginStatus=TEXT("Sign in to join production. Approve all accessible worlds for world switching.");
    UPROPERTY(Transient, BlueprintReadOnly, Category="Eidoverse|Login") FString LoginWorldScope;
    UPROPERTY(Transient, BlueprintReadOnly, Category="Eidoverse|Login") FString LoginName;
    UPROPERTY(Transient, BlueprintReadOnly, Category="Eidoverse|Login") bool bAuthenticating=false;
    UFUNCTION(BlueprintCallable, Category="Eidoverse|Login") void BeginProductionLogin();
    UFUNCTION(BlueprintCallable, Category="Eidoverse|Login") void OpenProductionLoginBrowser();
    UFUNCTION(BlueprintCallable, Category="Eidoverse|Login") void LogoutProduction();
    UFUNCTION(BlueprintCallable, Category="Eidoverse|Login") void JoinProduction();
    bool HasProductionSession() const{return !ProductionSession.IsEmpty()&&LoginService==TEXT("https://eidoverse.animalabs.ai");}
    static bool ParseProductionSession(const FString& Header,FString& Out);
    // Keychain-backed sign-in that survives restarts (EidoverseLogin.cpp).
    void SaveProductionSession() const;
    UFUNCTION(BlueprintCallable, Category="Eidoverse|Login") void RestoreProductionSession();
    static bool IsPairingCode(const FString& Code);
#if WITH_DEV_AUTOMATION_TESTS
    void BeginLocalLoginTest();
#endif
    // Only relative references from this explicitly configured HTTPS library are loaded.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Eidoverse|Avatars") bool bLoadRemoteAvatars=true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Eidoverse|Avatars") FString AvatarLibraryBase=TEXT("https://eidoverse.animalabs.ai/library/");
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Eidoverse|Avatars") int32 LoadedAvatarCount=0;
    // Never serialize tokens in levels, assets, or source control.
    UPROPERTY(Transient, BlueprintReadWrite, Category="Eidoverse") FString JoinToken;
    UPROPERTY(Transient, BlueprintReadWrite, Category="Eidoverse") FString IdentityToken;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Eidoverse") FVector OriginCentimeters=FVector::ZeroVector;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Eidoverse") float SurfaceZ=0;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Eidoverse") TObjectPtr<UStaticMesh> DiverProxyMesh;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Eidoverse") TObjectPtr<UStaticMesh> SubmarineProxyMesh;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Eidoverse") FString ConnectionStatus=TEXT("offline");
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Eidoverse") FString AssignedIdentity;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Eidoverse") int32 RemoteCount=0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Eidoverse") int32 SentPoseCount=0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Eidoverse") bool bJoined=false;
    UPROPERTY(BlueprintAssignable, Category="Eidoverse") FEidoverseChat OnChat;
    UFUNCTION(BlueprintCallable, Category="Eidoverse") void Connect();
    UFUNCTION(BlueprintCallable, Category="Eidoverse") void Disconnect();
    UFUNCTION(BlueprintCallable, Category="Eidoverse") bool Say(const FString& Text);
    UFUNCTION(BlueprintCallable, Category="Eidoverse") bool Whisper(const FString& To, const FString& Text);
    UFUNCTION(BlueprintCallable, Category="Eidoverse|Interaction") bool UseEntity(const FString& Id,const FString& Action);
    UFUNCTION(BlueprintCallable, Category="Eidoverse|Interaction") bool PuntEntity(const FString& Id,float Power=5);
    UFUNCTION(BlueprintCallable, Category="Eidoverse|Interaction") bool ReachTo(const FString& Participant,const FString& Point,bool bLeft=false);
    UFUNCTION(BlueprintCallable, Category="Eidoverse|Interaction") void ClearReach();
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Eidoverse|Interaction") bool bSimulateObjectPhysics=true;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Eidoverse|Interaction") FString InteractionStatus;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Eidoverse|Interaction") int32 ActivePhysicsObjects=0;
    bool HandleInteractionCommand(const FString& Text);

    // Three.js is right-handed, UE is left-handed. Facing wire +Z, wire +X
    // must appear on the LEFT (UE -Y). An axis permutation alone mirrors views.
    static FVector ToWirePosition(const FVector& P) { return FVector(-P.Y,P.Z,P.X)/100.0; }
    static FVector FromWirePosition(const FVector& P) { return FVector(P.Z,-P.X,P.Y)*100.0; }
    // Quaternion xyz is an axial vector: det(B)*B*q, not the position mapping.
    static FQuat ToWireRotation(const FQuat& Q) { return FQuat(Q.Y,-Q.Z,-Q.X,Q.W); }
    static FQuat FromWireRotation(const FQuat& Q) { return FQuat(-Q.Z,Q.X,-Q.Y,Q.W); }
private:
    // Explicit loopback preview must never inherit production environment tokens.
    bool bLocalCommonsPreset=false;
    // Intentionally not reflected, serialized, logged, or placed in URLs.
    FString ProductionSession;
    FString LoginService=TEXT("https://eidoverse.animalabs.ai");
    FString DeviceCode,PairingCode;
    double PairingExpires=0,NextAuthPoll=0;
    void BeginLogin();
    void TickProductionLogin(double Now);
    void FinishLoginAttempt(const FString& Message);
    double ProductionSessionExpires=0;
    uint32 AuthGeneration=0;
    TSharedPtr<IHttpRequest,ESPMode::ThreadSafe> AuthRequest;
    TSharedPtr<SEidoversePanel> ChatPanel;
    TSharedPtr<SWidget> ChatOverlay;
    TSharedPtr<IWebSocket> Socket;
    TMap<FString,FEidoversePeer> Peers;
    // My own body: same loader and animation as peers, riding the explorer pawn.
    FEidoversePeer SelfBody;
    // The browser's clip vocabulary and the clips themselves (EidoverseClips.h).
    UPROPERTY(Transient) TObjectPtr<class UEidoverseClipLibrary> ClipLibrary;
    FString SelfClipSlot=TEXT("idle");
    // Plays the clip a peer (or I) should show; false falls back to the procedural pose.
    bool AnimateWithClips(FEidoversePeer& P,const FString& Slot,float Speed,double Time);
    void TickSelfBody(double Time);
    // Seats (EidoverseSeats.cpp, localbody.js/controller.js): X mounts the nearest
    // declared socket within 3.5 m, else sits on furniture of seat height within
    // 1.2 m, else on the ground; moving (or X again) gets up.
    FString SeatTo,SeatSlot,SeatClip;
    bool bSeatConfirmed=false,bChairSeat=false,bSeatOffset=false;
    double SeatRequestedAt=0;
    FVector SeatOffsetWire=FVector::ZeroVector;
    FTransform ChairSeat;
    void TickSeating(class AEidoverseExplorer* Explorer,double Now);
    bool TryMountNearest(class AEidoverseExplorer* Explorer);
    bool TrySitOnFurniture(class AEidoverseExplorer* Explorer);
    void StandUp(class AEidoverseExplorer* Explorer,bool bSendDismount);
    bool MountedSeatTransform(FTransform& Out,FString& Pose) const;
    void ReceiveSeatLog(const FString& Verb,const TSharedPtr<FJsonObject>& Args);
    // Gaze, speaking and agent animations (avatar.js lookAt/speak/anim).
    double SelfSpeakingUntil=0,NextGazeAt=0;
    void TickGaze(double Now);
    void NoteSpeaker(const FString& Sender,const FString& Text);
    void ReceiveAgentAnimation(const TSharedPtr<FJsonObject>& J);
    float SelfHeadPitch() const;
    void TickCommandLine(double Now);
    bool bCommandLineJoinTried=false,bCommandLineQuitting=false;
    double CommandLineJoinedAt=0;
    UPROPERTY(Transient) TMap<FString,TObjectPtr<UEidoverseAvatarAsset>> AvatarCache;
    uint32 Generation=0;
    double RetryAt=0, RetryDelay=1, LastSend=0, LastChat=0, LastFrameTime=-1, OpenedAt=0;
    int64 LastLogSequence=-1;
    bool bWanted=false;
    FString LastExplorerMode;
    bool bRejoining=false;
    void SendJoin();
    void ReadRights(const TSharedPtr<FJsonObject>& Rights);
    void Status(const FString& Value);
    void ClearPeers();
    void Announce(const FString& Id,const FString& Avatar);
    void RequestAvatar(FEidoversePeer& Peer,const FString& Path);
    void TickAvatars(double Time);
    void Pose(const FString& Id,const TSharedPtr<FJsonObject>& Data,double Time);
    void Receive(const FString& Message);
    void Lost(int32 Code);
    void Send(const TSharedRef<FJsonObject>& Message);
    void SendPose();
    void AdvertiseCapture();
    void ReceiveCapture(const TSharedPtr<FJsonObject>& Message);
    UPROPERTY(Transient) TObjectPtr<USceneCaptureComponent2D> RemoteCapture;
    UPROPERTY(Transient) TObjectPtr<UTextureRenderTarget2D> RemoteCaptureTarget;
    double LastCaptureAt=-100;
    bool bCaptureAdvertised=false;
    FString CaptureConsentEndpoint,CaptureConsentWorld;
    UPROPERTY(Transient) TObjectPtr<AEidoverseScene> DynamicScene;
    UPROPERTY(Transient) TArray<TObjectPtr<AActor>> DynamicEnvironment;
    TArray<FEidoverseSuspendedActor> SuspendedActors;
    int32 PreviousFrameLag=-1;
    TWeakObjectPtr<APawn> PrototypePawn;
    TWeakObjectPtr<APawn> ExplorerPawn;
    void BeginDynamicScene();
    void EndDynamicScene();
    void TickDynamicScene(double Now);
    void ReceiveDynamicScene(const TSharedPtr<FJsonObject>& Message);
    bool bSceneDirty=false,bSceneMoving=false,bSceneSupported=false,bSceneWaiting=false;
    double LastSceneRequest=-100,SceneRequestInterval=.3;
    bool bDeterministicScenePhysics=false;
    TMap<FString,FEidoverseObjectSim> ObjectSims;
    TMap<FString,FVector> PendingPunts;
    TMap<FString,double> PendingPuntExpiry;
    TSharedPtr<FJsonObject> LocalReach;
    FString PendingEmote;
    double LastInteraction=0;
    bool SendInteractionVerb(const FString& Verb,const TSharedRef<FJsonObject>& Args);
    void ReceiveLease(const TSharedPtr<FJsonObject>& Message);
    void ReceiveInteraction(const TSharedPtr<FJsonObject>& Entry);
    void TickInteractions(float Dt,double Now);
    void TickReaches();
    void ClearInteractions();
    void SendLease(const FString& Op,const FString& Id,bool bPose=false);
};
