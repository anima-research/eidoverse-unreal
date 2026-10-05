// Seats, gaze, speaking and agent animations: the browser's localbody.js (declared
// sockets, mount/dismount verbs), colliders.js findSeat (furniture), controller.js
// (head pitch) and avatar.js (who a body looks at, mouth while speaking, `anim`).
#include "EidoverseBridge.h"
#include "EidoverseUI.h"
#include "EidoverseScene.h"
#include "EidoverseAvatar.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"

namespace SeatWire
{
    FString SeatStr(const TSharedPtr<FJsonObject>& O, const TCHAR* K) { FString S; if (O) O->TryGetStringField(K, S); return S; }
    double SeatNum(const TSharedPtr<FJsonObject>& O, const TCHAR* K, double D) { double N; return O && O->TryGetNumberField(K, N) && FMath::IsFinite(N) ? N : D; }
    bool SeatVec(const TSharedPtr<FJsonObject>& O, const TCHAR* K, FVector& V)
    {
        const TArray<TSharedPtr<FJsonValue>>* A = nullptr;
        if (!O || !O->TryGetArrayField(K, A) || A->Num() != 3) return false;
        double N[3]; for (int32 I = 0; I < 3; ++I) if (!(*A)[I]->TryGetNumber(N[I]) || !FMath::IsFinite(N[I]) || FMath::Abs(N[I]) > 1e5) return false;
        V = FVector(N[0], N[1], N[2]); return true;
    }
    TSharedPtr<FJsonObject> SeatObj(const TSharedPtr<FJsonObject>& O, const TCHAR* K) { const TSharedPtr<FJsonObject>* V = nullptr; return O && O->TryGetObjectField(K, V) ? *V : nullptr; }
    // A socket's world transform: model-local pos through the parent's transform
    // (scale included), yaw = parent yaw + socket yaw (world.js mountTransform).
    bool SocketWorld(const AActor* Parent, const TSharedPtr<FJsonObject>& Sock, const FVector* OffsetWire, FTransform& Out)
    {
        if (!Parent) return false;
        FVector Pos(0, .5, 0);
        if (OffsetWire) Pos = *OffsetWire; else SeatVec(Sock, TEXT("pos"), Pos);
        const FVector World = Parent->GetActorTransform().TransformPosition(AEidoverseBridge::FromWirePosition(Pos));
        const FQuat Yaw = FRotator(0, Parent->GetActorRotation().Yaw, 0).Quaternion()
            * AEidoverseBridge::FromWireRotation(FQuat(FVector::YAxisVector, SeatNum(Sock, TEXT("yaw"), 0)));
        Out = FTransform(Yaw, World);
        return true;
    }
}
using namespace SeatWire;

float AEidoverseBridge::SelfHeadPitch() const
{
    // controller.js: clamp(camPitch - 0.32, -0.45, 0.55), camPitch + looking down;
    // first person keeps the head level (the camera is the head).
    const auto* Explorer = Cast<AEidoverseExplorer>(UGameplayStatics::GetPlayerPawn(this, 0));
    const auto* PC = UGameplayStatics::GetPlayerController(this, 0);
    if (!Explorer || !PC || Explorer->bFirstPerson) return 0;
    const float CamPitch = -FMath::DegreesToRadians(FRotator::NormalizeAxis(PC->GetControlRotation().Pitch));
    return FMath::Clamp(CamPitch - .32f, -.45f, .55f);
}

// ---- seats ------------------------------------------------------------------------

bool AEidoverseBridge::MountedSeatTransform(FTransform& Out, FString& Pose) const
{
    if (SeatTo.IsEmpty() || !DynamicScene) return false;
    const AActor* Parent = DynamicScene->EntityActor(SeatTo);
    const TSharedPtr<FJsonObject> Sock = SeatObj(SeatObj(DynamicScene->Interaction(SeatTo), TEXT("sockets")), *SeatSlot);
    Pose = SeatStr(Sock, TEXT("pose"));
    if (Pose.IsEmpty()) Pose = TEXT("sitchair");
    return SocketWorld(Parent, Sock, bSeatOffset ? &SeatOffsetWire : nullptr, Out);
}

bool AEidoverseBridge::TryMountNearest(AEidoverseExplorer* Explorer)
{
    if (!DynamicScene || AssignedIdentity.IsEmpty()) return false;
    const FVector Feet = Explorer->GetActorLocation();
    FString BestId, BestSlot; double Best = 350;
    for (const auto& Pair : DynamicScene->GetItems())
    {
        const TSharedPtr<FJsonObject> Sockets = SeatObj(Pair.Value.Interact, TEXT("sockets"));
        if (!Sockets || !Pair.Value.Actor.IsValid()) continue;
        for (const auto& Slot : Sockets->Values)
        {
            FTransform W; const TSharedPtr<FJsonObject> Sock = Slot.Value ? Slot.Value->AsObject() : nullptr;
            if (!SocketWorld(Pair.Value.Actor.Get(), Sock, nullptr, W)) continue;
            const double D = FVector::Dist2D(W.GetLocation(), Feet);
            if (D < Best) { Best = D; BestId = Pair.Key; BestSlot = Slot.Key; }
        }
    }
    if (BestId.IsEmpty()) return false;
    auto A = MakeShared<FJsonObject>();
    A->SetStringField(TEXT("id"), AssignedIdentity); A->SetStringField(TEXT("to"), BestId); A->SetStringField(TEXT("slot"), BestSlot);
    if (!SendInteractionVerb(TEXT("mount"), A)) return false;
    // Seated at once, as the browser's echo would do a moment later; an unconfirmed
    // mount lets go after a few seconds (refused, or the seat is taken).
    SeatTo = BestId; SeatSlot = BestSlot; bSeatConfirmed = false; bSeatOffset = false; SeatRequestedAt = FPlatformTime::Seconds();
    if (HUD) HUD->Hint(FString::Printf(TEXT("you sit on %s (%s) — X or move to get off"), *BestId.Left(8), *BestSlot), 4);
    return true;
}

bool AEidoverseBridge::TrySitOnFurniture(AEidoverseExplorer* Explorer)
{
    if (!DynamicScene) return false;
    const double Half = Explorer->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
    const FVector Feet = Explorer->GetActorLocation() - FVector(0, 0, Half);
    double Best = 120; FTransform Seat; bool bFound = false;
    for (const auto& Pair : DynamicScene->GetItems())
    {
        const AActor* A = Pair.Value.Actor.Get();
        if (!A || !Pair.Value.bCollide || FVector::Dist2D(A->GetActorLocation(), Feet) > 800) continue;
        const FBox Box = A->CalculateComponentsBoundingBoxInLocalSpace(false);
        if (!Box.IsValid) continue;
        const FTransform T = A->GetActorTransform();
        const FVector Size = Box.GetSize() * T.GetScale3D().GetAbs();
        if (Size.X > 300 || Size.Y > 300 || Size.Z > 400) continue;   // not furniture (a house, a hill)
        const FVector Top = T.TransformPosition(FVector(Box.GetCenter().X, Box.GetCenter().Y, Box.Max.Z));
        const double Rise = Top.Z - Feet.Z;
        if (Rise < 25 || Rise > 85) continue;                          // not seat height
        const double D = FVector::Dist2D(Top, Feet);
        if (D > Best) continue;
        Best = D; bFound = true;
        Seat = FTransform(FRotator(0, A->GetActorRotation().Yaw, 0).Quaternion(), Top);
    }
    if (!bFound) return false;
    bChairSeat = true; ChairSeat = Seat;
    return true;
}

void AEidoverseBridge::StandUp(AEidoverseExplorer* Explorer, bool bSendDismount)
{
    FTransform Seat; FString Pose; bool bHave = false;
    if (!SeatTo.IsEmpty()) bHave = MountedSeatTransform(Seat, Pose);
    else if (bChairSeat) { Seat = ChairSeat; bHave = true; }
    if (!SeatTo.IsEmpty() && bSendDismount)
    {
        // My landing spot stamped: 0.7 m ahead of the seat (localbody.js dismountMe).
        FTransform Land = bHave ? Seat : Explorer->GetActorTransform();
        const FVector Off = ToWirePosition(Land.GetLocation() + Land.GetRotation().GetForwardVector() * 70 - OriginCentimeters);
        const FVector F = ToWirePosition(Land.GetRotation().GetForwardVector());
        auto A = MakeShared<FJsonObject>();
        A->SetStringField(TEXT("id"), AssignedIdentity);
        A->SetArrayField(TEXT("pos"), {MakeShared<FJsonValueNumber>(Off.X), MakeShared<FJsonValueNumber>(0), MakeShared<FJsonValueNumber>(Off.Z)});
        A->SetNumberField(TEXT("yaw"), FMath::Atan2(F.X, F.Z));
        LastInteraction = 0;   // getting up is never rate limited away
        SendInteractionVerb(TEXT("dismount"), A);
    }
    SeatTo.Empty(); SeatSlot.Empty(); SeatClip.Empty(); bChairSeat = false; bSeatConfirmed = false; bSeatOffset = false;
    Explorer->Posture.Empty();
    if (Explorer->bSeated)
    {
        // Step off forward, standing, before collision comes back.
        const double Half = Explorer->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
        const FVector Ahead = bHave ? Seat.GetLocation() + Seat.GetRotation().GetForwardVector() * 70 : Explorer->GetActorLocation() - FVector(0, 0, Half);
        FHitResult Hit; FCollisionQueryParams Q(SCENE_QUERY_STAT(EidoverseStandUp), false, Explorer);
        FVector Ground = Ahead;
        if (GetWorld()->LineTraceSingleByChannel(Hit, Ahead + FVector(0, 0, 120), Ahead - FVector(0, 0, 400), ECC_Visibility, Q)) Ground = Hit.ImpactPoint;
        Explorer->SetActorLocation(Ground + FVector(0, 0, Half + 2), false, nullptr, ETeleportType::TeleportPhysics);
        Explorer->SetSeated(false);
    }
}

void AEidoverseBridge::ReceiveSeatLog(const FString& Verb, const TSharedPtr<FJsonObject>& Args)
{
    if (SeatStr(Args, TEXT("id")) != AssignedIdentity || AssignedIdentity.IsEmpty()) return;
    auto* Explorer = Cast<AEidoverseExplorer>(UGameplayStatics::GetPlayerPawn(this, 0));
    if (Verb == TEXT("mount"))
    {
        // Confirms my request, or someone (an agent, a ride) seated me.
        bChairSeat = false;
        SeatTo = SeatStr(Args, TEXT("to")); SeatSlot = SeatStr(Args, TEXT("slot")); bSeatConfirmed = !SeatTo.IsEmpty();
        bSeatOffset = SeatVec(Args, TEXT("offset"), SeatOffsetWire);
    }
    else if (Verb == TEXT("dismount") && !SeatTo.IsEmpty() && bSeatConfirmed && Explorer)
        StandUp(Explorer, false);
}

void AEidoverseBridge::TickSeating(AEidoverseExplorer* Explorer, double Now)
{
    Explorer->bSeatHandled = bJoined && !bSpectator;
    const bool bPressed = Explorer->bSitPressed; Explorer->bSitPressed = false;
    if (!Explorer->bSeatHandled) { if (Explorer->bSeated) StandUp(Explorer, false); return; }
    if (!SeatTo.IsEmpty() && !bSeatConfirmed && Now - SeatRequestedAt > 4)
    {
        if (HUD) HUD->Hint(TEXT("couldn't sit there"), 3);
        StandUp(Explorer, false);
        return;
    }
    const bool bSeatedNow = !SeatTo.IsEmpty() || bChairSeat;
    if (bSeatedNow && (bPressed || Explorer->bWantMove)) { StandUp(Explorer, true); return; }
    if (!bSeatedNow && bPressed)
    {
        if (!Explorer->Posture.IsEmpty()) Explorer->Posture.Empty();
        else if (!TryMountNearest(Explorer) && !TrySitOnFurniture(Explorer)) Explorer->Posture = TEXT("sit");
    }
    FTransform Seat; FString Pose;
    if (!SeatTo.IsEmpty() && MountedSeatTransform(Seat, Pose)) SeatClip = Pose;
    else if (bChairSeat) { Seat = ChairSeat; SeatClip = TEXT("sitchair"); }
    else { SeatClip.Empty(); if (Explorer->bSeated) Explorer->SetSeated(false); return; }   // parent still loading: stand where we are
    // The body rides the seat: root at the socket, yaw only (mounted bodies render upright).
    Explorer->SetSeated(true);
    Explorer->Posture = SeatClip == TEXT("sit") || SeatClip == TEXT("lie") ? SeatClip : TEXT("sitchair");
    const double Half = Explorer->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
    Explorer->SetActorLocationAndRotation(Seat.GetLocation() + FVector(0, 0, Half), Seat.GetRotation(), false, nullptr, ETeleportType::TeleportPhysics);
}

// ---- gaze, speaking, agent animations ---------------------------------------------

void AEidoverseBridge::NoteSpeaker(const FString& Sender, const FString& Text)
{
    // avatar.js: speakUntil = now + min(bubble, 1000 + 38 ms per character).
    const double Len = Text.Len();
    const double Bubble = FMath::Clamp(2500 + Len * 45, 5000., 22000.);
    const double Until = FPlatformTime::Seconds() + FMath::Min(Bubble, 1000 + Len * 38) / 1000;
    if (Sender == AssignedIdentity && !Sender.IsEmpty())
    {
        SelfSpeakingUntil = Until;
        if (auto* M = SelfBody.AvatarMesh.Get()) M->NoteSpeaking(Until);
    }
    else if (auto* P = Peers.Find(Sender))
    {
        P->SpeakingUntil = Until;
        if (auto* M = P->AvatarMesh.Get()) M->NoteSpeaking(Until);
    }
}

void AEidoverseBridge::TickGaze(double Now)
{
    if (Now < NextGazeAt) return;
    NextGazeAt = Now + .25;
    // Every body: the latest speaker (not itself), else the nearest body within 9 m.
    struct FBody { UEidoverseAvatarMesh* Mesh; FVector Root; double SpeakingUntil; };
    TArray<FBody> Bodies;
    for (auto& Pair : Peers) if (auto* M = Pair.Value.AvatarMesh.Get()) if (M->IsVisible()) Bodies.Add({M, M->GetComponentLocation(), Pair.Value.SpeakingUntil});
    if (auto* M = SelfBody.AvatarMesh.Get()) Bodies.Add({M, M->GetComponentLocation(), SelfSpeakingUntil});
    for (const FBody& Me : Bodies)
    {
        const FBody* Speaker = nullptr; const FBody* Nearest = nullptr; double Best = 900. * 900.;
        for (const FBody& Other : Bodies)
        {
            if (&Other == &Me) continue;
            if (Other.SpeakingUntil > Now && (!Speaker || Other.SpeakingUntil > Speaker->SpeakingUntil)) Speaker = &Other;
            const double D = FVector::DistSquared(Other.Root, Me.Root);
            if (D < Best) { Best = D; Nearest = &Other; }
        }
        const FBody* Target = Speaker ? Speaker : Nearest;
        Me.Mesh->SetGazeTarget(Target ? Target->Root + FVector(0, 0, 150) : FVector::ZeroVector, Target != nullptr);
    }
}

void AEidoverseBridge::ReceiveAgentAnimation(const TSharedPtr<FJsonObject>& J)
{
    const FString Id = SeatStr(J, TEXT("id"));
    UEidoverseAvatarMesh* M = nullptr;
    if (!Id.IsEmpty() && Id == AssignedIdentity) M = SelfBody.AvatarMesh.Get();
    else if (auto* P = Peers.Find(Id)) M = P->AvatarMesh.Get();
    if (!M) return;
    bool bLoop = false, bReplace = false; J->TryGetBoolField(TEXT("loop"), bLoop); J->TryGetBoolField(TEXT("replace"), bReplace);
    if (const TSharedPtr<FJsonObject> Tracks = SeatObj(J, TEXT("tracks")))
        M->PlayAgentAnimation(Tracks, float(SeatNum(J, TEXT("dur"), 2)), bLoop, bReplace);
}
