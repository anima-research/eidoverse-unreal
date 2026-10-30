// Slice D's hook (Plugins/Eidoverse/Bridge/SLICES.md): seats from bridge/scene, holding, and my body's track at the authority (Bridge/src/bodies.ts).
#include "EidoverseBridge.h"
#include "EidoverseScene.h"
#include "Dom/JsonObject.h"
#include "IWebSocket.h"
#include "Dom/JsonValue.h"
#include "GameFramework/Pawn.h"
#include "Kismet/GameplayStatics.h"

namespace
{
    FString BStr(const TSharedPtr<FJsonObject>& O, const TCHAR* K) { FString S; if (O) O->TryGetStringField(K, S); return S; }
    TSharedPtr<FJsonObject> BObj(const TSharedPtr<FJsonObject>& O, const TCHAR* K) { const TSharedPtr<FJsonObject>* V = nullptr; return O && O->TryGetObjectField(K, V) ? *V : nullptr; }
    bool BOk(const TSharedPtr<FJsonObject>& R) { bool B = false; return R && R->TryGetBoolField(TEXT("ok"), B) && B; }
    FString BCode(const TSharedPtr<FJsonObject>& R) { return BStr(BObj(R, TEXT("refused")), TEXT("code")); }
    bool BVec(const TSharedPtr<FJsonObject>& O, const TCHAR* K, FVector& V)
    {
        const TArray<TSharedPtr<FJsonValue>>* A = nullptr;
        if (!O || !O->TryGetArrayField(K, A) || A->Num() != 3) return false;
        double N[3]; for (int32 I = 0; I < 3; ++I) if (!(*A)[I]->TryGetNumber(N[I]) || !FMath::IsFinite(N[I])) return false;
        V = FVector(N[0], N[1], N[2]); return true;
    }
    TArray<TSharedPtr<FJsonValue>> BArr(const FVector& V)
    {
        auto R3 = [](double X) { return FMath::RoundToDouble(X * 1000) / 1000; };
        return {MakeShared<FJsonValueNumber>(R3(V.X)), MakeShared<FJsonValueNumber>(R3(V.Y)), MakeShared<FJsonValueNumber>(R3(V.Z))};
    }
    constexpr double TrackEvery = .2, TrackChunk = 3.5;   // thingphysics.js TRACK_MS, CHUNK (mover.js TRACK.cap - 0.5)
}

void AEidoverseBridge::OnBodiesMsg(const FString& Type,const TSharedPtr<FJsonObject>& J)
{
    // bridge/ridden reaches its asker through PendingReplies (OnBridgeMessage); nothing else of slice D's comes on its own
    UE_LOG(LogTemp,Verbose,TEXT("EIDOVERSE bridge message not handled: %s"),*Type);
}

void AEidoverseBridge::ReceiveSceneSeats(const TSharedPtr<FJsonObject>& Scene)
{
    const TArray<TSharedPtr<FJsonValue>>* Seats = nullptr;
    if (!Scene || !Scene->TryGetArrayField(TEXT("seats"), Seats)) return;
    SceneSeats.Reset(); RemoteMounts.Reset();
    for (const auto& V : *Seats)
    {
        const TSharedPtr<FJsonObject> O = V->AsObject(); if (!O) continue;
        FEidoSeat S{BStr(O, TEXT("seat")), BStr(O, TEXT("host")), BStr(O, TEXT("name")), BStr(O, TEXT("action")), BStr(O, TEXT("pose")), BStr(O, TEXT("occupant")), BStr(O, TEXT("thing"))};
        if (S.Seat.IsEmpty() || S.Host.IsEmpty() || SceneSeats.Num() >= 512) continue;
        // someone else's seat pins their body to it each frame (EidoverseBridge.cpp Tick, MountWorldTransform)
        if (!S.Occupant.IsEmpty() && S.Occupant != AssignedIdentity)
        { auto M = MakeShared<FJsonObject>(); M->SetStringField(TEXT("to"), S.Host); M->SetStringField(TEXT("slot"), S.Name); RemoteMounts.Add(S.Occupant, M); }
        SceneSeats.Add(S);
    }
    ++SceneSeatsSerial;
    // a seat the authority put me in (someone seated me, or a re-join while sitting): sit there
    if (SeatTo.IsEmpty() && !AssignedIdentity.IsEmpty())
        for (const FEidoSeat& S : SceneSeats) if (S.Occupant == AssignedIdentity)
        { SeatTo = S.Host; SeatSlot = S.Name; SeatEntity = S.Seat; SeatAnim = S.Pose; bSeatConfirmed = true; bChairSeat = false; bSeatOffset = false; break; }
}

const AEidoverseBridge::FEidoSeat* AEidoverseBridge::FindSceneSeat(const FString& Host,const FString& Name) const
{
    for (const FEidoSeat& S : SceneSeats) if (S.Host == Host && (Name.IsEmpty() || S.Name == Name)) return &S;
    return nullptr;
}

FString AEidoverseBridge::GetSeatOccupant(const FString& Host) const
{
    const FEidoSeat* S = FindSceneSeat(Host, FString());
    return S ? S->Occupant : FString();
}

TSharedPtr<FJsonObject> AEidoverseBridge::SeatWirePose() const
{
    // net.js sendPose: {to: <socket id>, pose?} rides every frame while seated; its absence stands the body up
    const FString To = Helm.bActive ? Helm.Seat : !SeatTo.IsEmpty() && bSeatConfirmed ? SeatEntity : FString();
    if (To.IsEmpty()) return nullptr;
    auto Seat = MakeShared<FJsonObject>(); Seat->SetStringField(TEXT("to"), To);
    if (!SeatAnim.IsEmpty()) Seat->SetStringField(TEXT("pose"), SeatAnim);
    return Seat;
}

FString AEidoverseBridge::BridgeRide(const FString& Type,const TSharedRef<FJsonObject>& Fields,TFunction<void(const TSharedPtr<FJsonObject>&)> Done,bool bQuiet)
{
    if (!bUseBridge || !bJoined || bSpectator || !Socket || !Socket->IsConnected()) return FString();
    const FString Req = FString::Printf(TEXT("r%d"), ++RequestSerial);
    Fields->SetStringField(TEXT("type"), Type); Fields->SetStringField(TEXT("req"), Req);
    Send(Fields);
    PendingReplies.Add(Req, [this, Done, bQuiet](const TSharedPtr<FJsonObject>& R) { if (!BOk(R) && !bQuiet) ShowRefusal(R); if (Done) Done(R); });
    return Req;
}

bool AEidoverseBridge::BodyRequest(const FString& Verb,const TSharedRef<FJsonObject>& Args,TFunction<void(const TSharedPtr<FJsonObject>&)> Done)
{
    if (!bUseBridge || !bJoined || bSpectator || FPlatformTime::Seconds() - LastInteraction < .25) { InteractionStatus = TEXT("Join as a participant, and wait briefly between actions."); return false; }
    LastInteraction = FPlatformTime::Seconds();
    InteractionStatus = TEXT("Action requested; awaiting server.");
    return !Request(Verb, Args, [this, Verb, Done](const TSharedPtr<FJsonObject>& R)
    {
        if (BOk(R)) InteractionStatus = TEXT("Server confirmed ") + Verb; else ShowRefusal(R);
        if (Done) Done(R);
    }).IsEmpty();
}

bool AEidoverseBridge::GrabEntity(const FString& Id)
{
    if (Id.IsEmpty() || Id.Len() > 128) return false;
    auto A = MakeShared<FJsonObject>(); A->SetStringField(TEXT("id"), Id);
    return BodyRequest(TEXT("grab"), A, [this, Id](const TSharedPtr<FJsonObject>& R) { if (BOk(R)) HeldItem = Id; });
}

bool AEidoverseBridge::LetGoOfEntity(bool bThrow,FVector V)
{
    if (HeldItem.IsEmpty()) return false;
    const FString Id = HeldItem;
    auto A = MakeShared<FJsonObject>(); A->SetStringField(TEXT("id"), Id);
    if (bThrow) A->SetArrayField(TEXT("v"), BArr(V.GetClampedToMaxSize(20)));
    return BodyRequest(bThrow ? TEXT("throw") : TEXT("put"), A, [this, Id](const TSharedPtr<FJsonObject>& R) { if (BOk(R) && HeldItem == Id) HeldItem.Empty(); });
}

void AEidoverseBridge::ReceiveHolds(const TSharedPtr<FJsonObject>& J)
{
    const TSharedPtr<FJsonObject> Holds = BObj(J, TEXT("holds"));
    if (!HeldItem.IsEmpty() && (!Holds || !Holds->HasField(HeldItem))) HeldItem.Empty();
}

FVector AEidoverseBridge::MyFeetWire() const
{
    // SendPose's body point: the explorer's feet (its capsule's 88 cm under the actor)
    const APawn* Pawn = UGameplayStatics::GetPlayerPawn(this, 0);
    return Pawn ? ToWirePosition(Pawn->GetActorLocation() - OriginCentimeters - FVector(0, 0, 88)) : FVector::ZeroVector;
}

bool AEidoverseBridge::IsBodyTracked(float Within) const
{
    return bBodyArrived && !bBodyMoving && FVector::Dist(BodyTrackedAt, MyFeetWire()) <= Within;
}

void AEidoverseBridge::TickBodyTrack(double Now)
{
    if (!bUseBridge) return;
    if (PendingUnrideAt > 0 && Now >= PendingUnrideAt) SendUnride();   // a refused walk-off, again
    // the scene's seats follow the frames: ask for the scene again while they don't say what the authority told me yet
    if (Now >= NextSeatPollAt)
    {
        bool bStale = bSeatConfirmed && !SeatTo.IsEmpty() && GetSeatOccupant(SeatTo) != AssignedIdentity;
        if (SeatTo.IsEmpty()) for (const FEidoSeat& S : SceneSeats) bStale |= S.Occupant == AssignedIdentity;
        if (bStale) { bSceneDirty = true; NextSeatPollAt = Now + 1; }
    }
    // thingphysics.js track: arrive once, then walk the authority's body toward mine in steps it accepts; a seat holds it
    if (!bUseBridge || !bBridgeReady || bSpectator || bBodyMoving || Now < BodyTrackAt || !SeatTo.IsEmpty() || !Socket || !Socket->IsConnected()) return;
    if (!UGameplayStatics::GetPlayerPawn(this, 0)) return;
    const FVector To = MyFeetWire();
    const double D = FVector::Dist(To, BodyTrackedAt);
    if (bBodyArrived && D < .05) return;
    const bool bTeleport = bBodyArrived && D > 3 * TrackChunk && HeldItem.IsEmpty() && Now - BodyTeleportAt > 3;
    const FVector Step = !bBodyArrived || bTeleport || D <= TrackChunk ? To : BodyTrackedAt + (To - BodyTrackedAt) * (TrackChunk / D);
    auto A = MakeShared<FJsonObject>(); A->SetArrayField(TEXT("to"), BArr(Step));
    if (bTeleport) { A->SetBoolField(TEXT("teleport"), true); BodyTeleportAt = Now; }
    bBodyMoving = true; BodyTrackAt = Now + TrackEvery;
    const FString Id = Request(TEXT("move"), A, [this, Step, bTeleport](const TSharedPtr<FJsonObject>& R)
    {
        bBodyMoving = false; const double At = FPlatformTime::Seconds();
        FVector Where;
        if (BOk(R)) { bBodyArrived = true; BodyTrackedAt = BVec(R, TEXT("at"), Where) ? Where : Step; return; }
        if (BVec(BObj(R, TEXT("refused")), TEXT("at"), Where)) { BodyTrackedAt = Where; bBodyArrived = true; }
        // lost (faster than a body walks, or through something solid): re-arrive as a teleport, at most once in 3 s
        const FString Code = BCode(R);
        if (!bTeleport && (Code == TEXT("too-fast") || Code == TEXT("solid")) && HeldItem.IsEmpty() && At - BodyTeleportAt > 3)
        {
            BodyTeleportAt = At;
            auto T = MakeShared<FJsonObject>(); T->SetArrayField(TEXT("to"), BArr(MyFeetWire())); T->SetBoolField(TEXT("teleport"), true);
            bBodyMoving = true;
            const FVector Dest = MyFeetWire();
            Request(TEXT("move"), T, [this, Dest](const TSharedPtr<FJsonObject>& R2) { bBodyMoving = false; FVector W; if (BOk(R2)) { bBodyArrived = true; BodyTrackedAt = BVec(R2, TEXT("at"), W) ? W : Dest; } else BodyTrackAt = FPlatformTime::Seconds() + 2; });
            return;
        }
        BodyTrackAt = At + 2;   // a refused step waits before the next
    });
    if (Id.IsEmpty()) bBodyMoving = false;
}

void AEidoverseBridge::SendUnride()
{
    // standing up: the pose already stopped naming the seat; walk off to the landing (bodies.ts unride → move), retried briefly
    PendingUnrideAt = 0;
    auto A = MakeShared<FJsonObject>(); A->SetArrayField(TEXT("pos"), BArr(PendingUnridePos));
    const FVector Land = PendingUnridePos; const bool bLast = PendingUnrideTries >= 5;
    if (BridgeRide(TEXT("bridge/unride"), A, [this, Land](const TSharedPtr<FJsonObject>& R)
        {
            if (BOk(R)) { PendingUnrideTries = 0; bBodyArrived = true; BodyTrackedAt = Land; return; }
            if (++PendingUnrideTries < 6) PendingUnrideAt = FPlatformTime::Seconds() + .3;
        }, !bLast).IsEmpty()) PendingUnrideTries = 0;
}
