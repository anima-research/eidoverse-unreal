// Taking someone's limp body in hand (bodydrag.js, dragger half). In mouselook the
// crosshair aims: LMB within 0.3 m of a joint of a body whose newest clip is 'ragdoll'
// (frontmost, 0.4 m tie window; its root within 5 m of my feet) grabs that joint into
// a local ragdoll on their avatar. While held, the joint follows the aim at a depth the
// wheel sets (1-30 m), the sim steps here and {pose, p, yaw} streams to the owner every
// 66 ms; their own stream is ignored. Release hands the sim over ({end, sim}); P nails
// the joint where it is ({end, pinAt}). The owner can refuse or break free ({end}).
#include "EidoverseBridge.h"
#include "EidoverseUI.h"
#include "EidoverseScene.h"
#include "EidoverseAvatar.h"
#include "EidoverseRagdoll.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "DrawDebugHelpers.h"
#include "EidoverseVoice.h"

namespace
{
    TSharedPtr<FJsonValue> V3(const FVector& V) { return MakeShared<FJsonValueArray>(TArray<TSharedPtr<FJsonValue>>{MakeShared<FJsonValueNumber>(V.X), MakeShared<FJsonValueNumber>(V.Y), MakeShared<FJsonValueNumber>(V.Z)}); }
    TSharedRef<FJsonObject> PoseOf(const TMap<FName, FQuat>& Pose)
    {
        auto J = MakeShared<FJsonObject>();
        for (const auto& P : Pose) J->SetArrayField(P.Key.ToString(), {MakeShared<FJsonValueNumber>(P.Value.X), MakeShared<FJsonValueNumber>(P.Value.Y), MakeShared<FJsonValueNumber>(P.Value.Z), MakeShared<FJsonValueNumber>(P.Value.W)});
        return J;
    }
    TSharedRef<FJsonObject> SimOf(const EidoRagdoll::FSnapshot& S)
    {
        auto J = MakeShared<FJsonObject>();
        TArray<TSharedPtr<FJsonValue>> Names, P, V, H;
        for (const FName N : S.J) Names.Add(MakeShared<FJsonValueString>(N.ToString()));
        for (const double X : S.P) P.Add(MakeShared<FJsonValueNumber>(X));
        for (const double X : S.V) V.Add(MakeShared<FJsonValueNumber>(X));
        for (const double X : S.H) H.Add(MakeShared<FJsonValueNumber>(X));
        J->SetArrayField(TEXT("j"), Names); J->SetArrayField(TEXT("p"), P); J->SetArrayField(TEXT("v"), V);
        if (H.Num()) J->SetArrayField(TEXT("h"), H);
        return J;
    }
}

bool AEidoverseBridge::GrabBodyAt(FVector RayOrigin, FVector RayDirection)
{
    if (HeldBody.IsValid() || bDowned || !bJoined) return false;
    const APawn* Me = UGameplayStatics::GetPlayerPawn(this, 0); if (!Me) return false;
    const FVector Dir = RayDirection.GetSafeNormal();
    const FVector MeWire = ToWirePosition(Me->GetActorLocation() - OriginCentimeters);
    struct FHit { FString Id; FName Joint; double Along, Dist; FVector At; };
    TArray<FHit> Hits;
    for (const auto& Pair : Peers)
    {
        const FEidoversePeer& P = Pair.Value;
        UEidoverseAvatarMesh* M = P.AvatarMesh.Get();
        if (!M || !P.Actor.IsValid() || P.Clip != TEXT("ragdoll")) continue;
        const FVector Root = ToWirePosition(P.Actor->GetActorLocation() - OriginCentimeters);
        if (FVector2D(Root.X - MeWire.X, Root.Z - MeWire.Z).Size() > 5 || FMath::Abs(Root.Y - MeWire.Y) > 5) continue;
        for (const FName J : EidoRagdoll::Joints())
        {
            FVector W; if (!M->HumanBonePosition(J, W)) continue;
            const double Along = FVector::DotProduct(W - RayOrigin, Dir) / 100;
            if (Along <= 0) continue;
            const double Dist = FVector::Dist(RayOrigin + Dir * Along * 100, W) / 100;
            if (Dist < .3) Hits.Add({Pair.Key, J, Along, Dist, W});
        }
    }
    if (!Hits.Num()) return false;
    // Frontmost body wins; within 0.4 m of the front, the joint nearest the ray.
    double Front = 1e9; for (const FHit& H : Hits) Front = FMath::Min(Front, H.Along);
    const FHit* Best = nullptr;
    for (const FHit& H : Hits) if (H.Along <= Front + .4 && (!Best || H.Dist < Best->Dist)) Best = &H;
    FEidoversePeer& P = Peers[Best->Id];
    UEidoverseAvatarMesh* M = P.AvatarMesh.Get();
    // Their body as it lies now, simulated here.
    EidoRagdoll::FInit Init;
    if (!M->RestWorldWire(Init.RestWorld, OriginCentimeters)) return false;
    const FVector F = ToWirePosition(P.Actor->GetActorForwardVector() * 100);
    Init.RootPosition = ToWirePosition(M->GetComponentLocation() - OriginCentimeters);
    Init.RootYaw = FMath::Atan2(F.X, F.Z);
    Init.bVrm0 = M->Avatar && M->Avatar->bVRM0;
    for (const FName B : EidoRagdoll::DrivenBones()) if (const FQuat* Q = M->LastNormalized.Find(B)) Init.StartLocal.Add(B, *Q);
    UWorld* World = GetWorld(); const FVector Origin = OriginCentimeters; TWeakObjectPtr<AActor> Them(P.Actor.Get());
    Init.HeightAt = [World, Origin, Them](double Xw, double Zw)
    {
        const FVector Top = Origin + FromWirePosition(FVector(Xw, 200, Zw)), Bottom = Origin + FromWirePosition(FVector(Xw, -200, Zw));
        FCollisionQueryParams Q(SCENE_QUERY_STAT(EidoverseHeldGround), true); if (Them.IsValid()) Q.AddIgnoredActor(Them.Get());
        if (const APawn* Me = UGameplayStatics::GetPlayerPawn(World, 0)) Q.AddIgnoredActor(Me);
        FHitResult Hit;
        return World && World->LineTraceSingleByChannel(Hit, Top, Bottom, ECC_Visibility, Q) ? (Hit.ImpactPoint.Z - Origin.Z) / 100 : 0.;
    };
    Init.ResolveColliders = [](FVector& Pt, const EidoRagdoll::FHeightFn& HeightAt, double, double) { return HeightAt(Pt.X, Pt.Z); };
    HeldBody = MakeShared<FHeldBody>();
    HeldBody->Id = Best->Id; HeldBody->Joint = Best->Joint; HeldBody->Depth = FMath::Clamp(Best->Along, 1.0, 30.0);
    HeldBody->Rd = MakeShared<EidoRagdoll::FRagdoll>(Init); HeldBody->bByApi = true;
    HeldBody->Off = ToWirePosition(Best->At - OriginCentimeters) - ToWirePosition(RayOrigin + Dir * HeldBody->Depth * 100 - OriginCentimeters);
    P.bDraggedLocal = true;
    if (auto* X = Cast<AEidoverseExplorer>(UGameplayStatics::GetPlayerPawn(this, 0))) { X->bWheelToDrag = true; X->WheelDragAccum = 0; }
    auto Grab = MakeShared<FJsonObject>(); Grab->SetStringField(TEXT("joint"), Best->Joint.ToString());
    auto Msg = MakeShared<FJsonObject>(); Msg->SetStringField(TEXT("target"), Best->Id); Msg->SetObjectField(TEXT("grab"), Grab);
    SendRaw(TEXT("bodydrag"), Msg);
    if (HUD) HUD->Hint(FString::Printf(TEXT("you take %s by the %s — wheel: nearer/further · P: nail it here · release to let go"), *Best->Id, *Best->Joint.ToString()), 4);
    return true;
}

bool AEidoverseBridge::TryGrabBody(APlayerController* PC)
{
    FVector Eye; FRotator View; PC->GetPlayerViewPoint(Eye, View);
    if (!GrabBodyAt(Eye, View.Vector())) return false;
    HeldBody->bByApi = false;   // a mouse grab ends when the button does
    return true;
}

void AEidoverseBridge::DropHeldBody(const FString& Why)
{
    if (!HeldBody.IsValid()) return;
    if (FEidoversePeer* P = Peers.Find(HeldBody->Id)) P->bDraggedLocal = false;
    if (auto* X = Cast<AEidoverseExplorer>(UGameplayStatics::GetPlayerPawn(this, 0))) { X->bWheelToDrag = false; X->WheelDragAccum = 0; }
    if (HUD && !Why.IsEmpty()) HUD->Hint(Why, 3);
    HeldBody.Reset();
}

void AEidoverseBridge::ReleaseHeldBody(bool bNail)
{
    if (!HeldBody.IsValid()) return;
    const EidoRagdoll::FStepResult& R = HeldBody->Rd->LastResult();
    auto Msg = MakeShared<FJsonObject>();
    Msg->SetStringField(TEXT("target"), HeldBody->Id); Msg->SetBoolField(TEXT("end"), true);
    Msg->SetObjectField(TEXT("pose"), PoseOf(R.Pose)); Msg->SetField(TEXT("p"), V3(R.RootPosition)); Msg->SetNumberField(TEXT("yaw"), R.RootYaw);
    Msg->SetObjectField(TEXT("sim"), SimOf(HeldBody->Rd->Snapshot()));
    FVector At;
    if (bNail && HeldBody->Rd->GetParticle(HeldBody->Joint, At))
    {
        auto Pin = MakeShared<FJsonObject>(); Pin->SetStringField(TEXT("joint"), HeldBody->Joint.ToString()); Pin->SetField(TEXT("at"), V3(At));
        Msg->SetObjectField(TEXT("pinAt"), Pin);
    }
    SendRaw(TEXT("bodydrag"), Msg);
    DropHeldBody(bNail ? FString::Printf(TEXT("you nail %s's %s in place"), *HeldBody->Id, *HeldBody->Joint.ToString()) : FString());
}

bool AEidoverseBridge::ReceiveDraggerMessage(const FString& By, const TSharedPtr<FJsonObject>& J)
{
    if (!HeldBody.IsValid() || HeldBody->Id != By) return false;
    if (J->HasField(TEXT("end")) && !J->HasField(TEXT("pinAt")))
    { DropHeldBody(By + TEXT(" broke free")); return true; }   // refused, or got up
    const TArray<TSharedPtr<FJsonValue>>* Pins = nullptr;
    if (J->TryGetArrayField(TEXT("pins"), Pins))
    {
        // The owner's other nails: my takeover sim keeps them, firm.
        for (const auto& V : *Pins)
        {
            const TSharedPtr<FJsonObject> O = V.IsValid() ? V->AsObject() : nullptr; if (!O) continue;
            FString Joint; const TArray<TSharedPtr<FJsonValue>>* A = nullptr;
            if (O->TryGetStringField(TEXT("j"), Joint) && O->TryGetArrayField(TEXT("at"), A) && A->Num() == 3)
                HeldBody->Rd->PinJoint(FName(Joint.Left(48)), FVector((*A)[0]->AsNumber(), (*A)[1]->AsNumber(), (*A)[2]->AsNumber()), true);
        }
        return true;
    }
    return false;
}

void AEidoverseBridge::TickHeldBody(APlayerController* PC, float Dt)
{
    const bool bMouselook = !(HUD && HUD->IsPointerMode());
    const bool bBuilding = HUD && HUD->IsFrameOpen(TEXT("build"));
    if (!HeldBody.IsValid())
    {
        if (bMouselook && !bBuilding && !bDowned && PC->WasInputKeyJustPressed(EKeys::LeftMouseButton)) TryGrabBody(PC);
        return;
    }
    FEidoversePeer* P = Peers.Find(HeldBody->Id);
    UEidoverseAvatarMesh* M = P ? P->AvatarMesh.Get() : nullptr;
    if (!P || !M || !P->Actor.IsValid()) { DropHeldBody(FString()); return; }
    // Their newest clip stopped being 'ragdoll': they're up — the hand is empty.
    if (P->Samples.Num() && !P->Samples.Last().bLimp && P->Clip != TEXT("ragdoll")) { DropHeldBody(HeldBody->Id + TEXT(" broke free")); return; }
    if (PC->WasInputKeyJustPressed(EKeys::P)) { ReleaseHeldBody(true); return; }
    if (!HeldBody->bByApi && !PC->IsInputKeyDown(EKeys::LeftMouseButton)) { ReleaseHeldBody(false); return; }
    if (auto* X = Cast<AEidoverseExplorer>(PC->GetPawn())) { HeldBody->Depth = FMath::Clamp(HeldBody->Depth + X->WheelDragAccum, 1.0, 30.0); X->WheelDragAccum = 0; }
    FVector Eye; FRotator View; PC->GetPlayerViewPoint(Eye, View);
    const FVector Target = ToWirePosition(Eye + View.Vector() * HeldBody->Depth * 100 - OriginCentimeters) + HeldBody->Off;
    HeldBody->Rd->PinJoint(HeldBody->Joint, Target);
    const EidoRagdoll::FStepResult& R = HeldBody->Rd->Step(Dt);
    // Here, the body is where my sim says.
    P->Actor->SetActorLocationAndRotation(OriginCentimeters + FromWirePosition(R.RootPosition), FromWireRotation(FQuat(FVector::YAxisVector, R.RootYaw)));
    M->SetLimp(true);
    M->SetHeldPoseDirect(R.Pose);
    const double Now = FPlatformTime::Seconds();
    if (Now >= HeldBody->NextSend)
    {
        HeldBody->NextSend = Now + .066;
        auto Msg = MakeShared<FJsonObject>();
        Msg->SetStringField(TEXT("target"), HeldBody->Id); Msg->SetObjectField(TEXT("pose"), PoseOf(R.Pose));
        Msg->SetField(TEXT("p"), V3(R.RootPosition)); Msg->SetNumberField(TEXT("yaw"), R.RootYaw);
        SendRaw(TEXT("bodydrag"), Msg);
    }
}

// ---------------------------------------------------------------- F3
void AEidoverseBridge::ToggleDebugOverlay()
{
    bDebugOverlay = !bDebugOverlay;
    if (APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0)) PC->ConsoleCommand(TEXT("show Collision"));
    if (HUD) HUD->Hint(bDebugOverlay ? TEXT("debug: collision volumes and ragdoll joints (F3 to hide)") : TEXT("debug off"), 2.5f);
}

FString AEidoverseBridge::DebugStats() const
{
    const float Fps = FApp::GetDeltaTime() > 0 ? 1.f / float(FApp::GetDeltaTime()) : 0.f;
    const AEidoverseScene* S = DynamicScene.Get();
    int32 Demoted = S ? S->GetDemotedCount() : 0;
    const APawn* Me = UGameplayStatics::GetPlayerPawn(this, 0);
    const FVector P = Me ? ToWirePosition(Me->GetActorLocation() - OriginCentimeters) : FVector::ZeroVector;
    return FString::Printf(TEXT("%.0f fps · %s · %d people · %d things (%d demoted) · at %.1f %.1f %.1f · %s%s"),
        Fps, *WorldName, Peers.Num(), S ? S->GetItems().Num() : 0, Demoted, P.X, P.Y, P.Z,
        bDowned ? TEXT("limp") : IsFlying() ? TEXT("flying") : TEXT("standing"), Voice ? *(TEXT(" · ") + Voice->DescribeState()) : TEXT(""));
}

void AEidoverseBridge::TickDebugOverlay()
{
    if (!bDebugOverlay) return;
    UWorld* World = GetWorld();
    auto Draw = [this, World](const EidoRagdoll::FRagdoll& Rd)
    {
        for (const FName J : EidoRagdoll::Joints())
        {
            FVector W; if (!Rd.GetParticle(J, W)) continue;
            DrawDebugSphere(World, OriginCentimeters + FromWirePosition(W), 6, 8, FColor(0xff, 0x8f, 0xb0), false, -1, SDPG_Foreground, 1);
        }
    };
    if (Rag.IsValid()) Draw(*Rag);
    if (HeldBody.IsValid()) Draw(*HeldBody->Rd);
    if (HUD) HUD->Hint(DebugStats(), .3f);
}
