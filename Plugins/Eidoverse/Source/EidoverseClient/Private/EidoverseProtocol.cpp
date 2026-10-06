// Protocol parity with the browser's net.js: server error text, the join window of
// chat and history paging, whispers, world reset/fork/moderation messages, log
// narration, restore-where-you-were, the reconnect verb queue and close codes.
#include "EidoverseBridge.h"
#include "EidoverseUI.h"
#include "EidoverseScene.h"
#include "EidoverseAvatar.h"
#include "EidoversePanel.h"
#include "EidoverseClips.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonSerializer.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "IWebSocket.h"

namespace ProtoWire
{
    FString PStr(const TSharedPtr<FJsonObject>& O, const TCHAR* K) { FString S; if (O) O->TryGetStringField(K, S); return S; }
    double PNum(const TSharedPtr<FJsonObject>& O, const TCHAR* K, double D) { double N; return O && O->TryGetNumberField(K, N) && FMath::IsFinite(N) ? N : D; }
    TSharedPtr<FJsonObject> PObj(const TSharedPtr<FJsonObject>& O, const TCHAR* K) { const TSharedPtr<FJsonObject>* V = nullptr; return O && O->TryGetObjectField(K, V) ? *V : nullptr; }
    const TArray<TSharedPtr<FJsonValue>>& PArr(const TSharedPtr<FJsonObject>& O, const TCHAR* K)
    { static const TArray<TSharedPtr<FJsonValue>> Empty; const TArray<TSharedPtr<FJsonValue>>* A = nullptr; return O && O->TryGetArrayField(K, A) ? *A : Empty; }
}
using namespace ProtoWire;

void AEidoverseBridge::Narrate(const FString& Text)
{
    if (Text.IsEmpty()) return;
    FEidoverseChatLine Line; Line.Sender = TEXT("*"); Line.Text = Text.Left(4000);
    ChatHistory.Add(Line); if (ChatHistory.Num() > 150) ChatHistory.RemoveAt(0, ChatHistory.Num() - 150);
    if (ChatPanel) ChatPanel->RefreshHistory();
    if (HUD) HUD->SystemLine(Text.Left(4000));
    UE_LOG(LogTemp, Display, TEXT("EIDOVERSE * %s"), *Text.Left(300));
}

void AEidoverseBridge::ToastText(const FString& Text, bool bWarn)
{
    if (HUD) HUD->Toast(Text.Left(300), bWarn);
    else Status(Text.Left(300));
}

// ---- snapshot extras --------------------------------------------------------------

void AEidoverseBridge::ReceiveSnapshotExtras(const TSharedPtr<FJsonObject>& J, double Now)
{
    // The server's `you` is authoritative (a suffixed name when two people share one).
    const FString You = PStr(J, TEXT("you"));
    if (!You.IsEmpty() && You != ParticipantName)
    {
        ParticipantName = You;
        ToastText(FString::Printf(TEXT("you're appearing as “%s”"), *You));
    }
    MySub = PStr(J, TEXT("yourSub"));
    // The join window of chat: the last messages, and how many there were (deduped by
    // seq, so a reconnect does not repeat what is already on screen).
    bHistoryPending = false;
    const TSharedPtr<FJsonObject> State = PObj(J, TEXT("state"));
    const auto& Recent = PArr(State, TEXT("recentChat"));
    const double Total = PNum(State, TEXT("chatTotal"), -1);
    if (Recent.Num() > 0)
    {
        const double FirstTs = PNum(Recent[0]->AsObject(), TEXT("ts"), 0), LastTs = PNum(Recent.Last()->AsObject(), TEXT("ts"), 0);
        if (Total > Recent.Num())
        {
            FString Span = (Recent.Num() > 1 && LastTs - FirstTs < 120000) ? FString::Printf(TEXT(" — the last %.0fs of talking"), (LastTs - FirstTs) / 1000) : FString();
            Narrate(FString::Printf(TEXT("showing %d of %.0f messages%s. Scroll up for earlier."), Recent.Num(), Total, *Span));
        }
        else if (Total < 0) Narrate(TEXT("scroll up for earlier messages"));
        for (const auto& V : Recent)
        {
            const TSharedPtr<FJsonObject> M = V->AsObject(); if (!M) continue;
            const int64 Seq = int64(PNum(M, TEXT("seq"), -1));
            if (Seq >= 0) { if (RenderedChatSeqs.Contains(Seq)) continue; RenderedChatSeqs.Add(Seq); OldestChatSeq = OldestChatSeq < 0 ? Seq : FMath::Min(OldestChatSeq, Seq); }
            AddChatLine(PStr(M, TEXT("actor")), PStr(M, TEXT("text")).Left(4000), false, FString(), true);
        }
    }
    else if (Total == 0) bHistoryDone = true;
    // Folded mounts: am I seated somewhere already (a reconnect mid-ride)?
    RemoteMounts.Reset();
    if (const TSharedPtr<FJsonObject> Mounts = PObj(State, TEXT("mounts")))
        for (const auto& Pair : Mounts->Values)
            if (const TSharedPtr<FJsonObject> M = Pair.Value ? Pair.Value->AsObject() : nullptr)
                RemoteMounts.Add(FString(Pair.Key), M);
    if (const TSharedPtr<FJsonObject>* Mine = RemoteMounts.Find(AssignedIdentity))
    {
        auto Args = MakeShared<FJsonObject>(*Mine->Get());
        Args->SetStringField(TEXT("id"), AssignedIdentity);
        ReceiveSeatLog(TEXT("mount"), Args);
    }
    // Wake where you fell asleep, unless this body already moved this session.
    PendingRestore = PObj(J, TEXT("restore"));
    if (PendingRestore && bBodyMovedThisSession) PendingRestore.Reset();
    // Rights, as the web narrates them.
    if (!bWorldOpen && !WorldRole.IsEmpty() && WorldRole != TEXT("owner"))
        Narrate(FString::Printf(TEXT("this world has an owner — you are a %s%s here"), *WorldRole, bCanGenerate ? TEXT(" +gen") : TEXT("")));
    bEmptyWorldNoticePending = true;
    // Anything queued while reconnecting goes out now, in order.
    if (!QueuedMessages.IsEmpty())
    {
        TArray<TSharedRef<FJsonObject>> Out = MoveTemp(QueuedMessages); QueuedMessages.Reset();
        for (const auto& M : Out) Send(M);
        Narrate(FString::Printf(TEXT("sent %d queued action%s"), Out.Num(), Out.Num() == 1 ? TEXT("") : TEXT("s")));
    }
}

bool AEidoverseBridge::ApplyRestore()
{
    if (!PendingRestore) return false;
    auto* Explorer = Cast<AEidoverseExplorer>(UGameplayStatics::GetPlayerPawn(this, 0));
    if (!Explorer) return false;
    const TSharedPtr<FJsonObject> R = PendingRestore; PendingRestore.Reset();
    const auto& P = PArr(R, TEXT("p"));
    FVector Wire(0);
    for (int32 I = 0; I < 3 && I < P.Num(); ++I) { double N = 0; if (P[I]->TryGetNumber(N) && FMath::IsFinite(N) && FMath::Abs(N) < 1e5) Wire[I] = N; }
    const FString Clip = PStr(R, TEXT("clip"));
    if (Clip == TEXT("ragdoll")) Wire.Y = 0;
    const double Yaw = PNum(R, TEXT("yaw"), 0);
    const double Half = Explorer->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
    const FQuat Facing = FromWireRotation(FQuat(FVector::YAxisVector, Yaw));
    Explorer->SetActorLocationAndRotation(OriginCentimeters + FromWirePosition(Wire) + FVector(0, 0, Half + 2), FRotator(0, Facing.Rotator().Yaw, 0), false, nullptr, ETeleportType::TeleportPhysics);
    if (auto* PC = Cast<APlayerController>(Explorer->GetController())) PC->SetControlRotation(FRotator(-10, Facing.Rotator().Yaw, 0));
    if (Clip == TEXT("sit") || Clip == TEXT("sitchair")) Explorer->Posture = TEXT("sit");
    else if (Clip == TEXT("lie")) Explorer->Posture = TEXT("lie");
    bool bFolded = false; if (R->TryGetBoolField(TEXT("wingsFolded"), bFolded)) bWingsFolded = bFolded;
    if (Clip != TEXT("ragdoll")) if (const TSharedPtr<FJsonObject> Held = PObj(R, TEXT("pose"))) SetSelfHeldPose(Held, false);
    UE_LOG(LogTemp, Display, TEXT("EIDOVERSE restored pose at %s (%s)"), *Wire.ToString(), *Clip);
    return true;
}

// ---- messages -------------------------------------------------------------------------

bool AEidoverseBridge::ReceiveProtocol(const FString& Type, const TSharedPtr<FJsonObject>& J, double Now)
{
    if (Type == TEXT("error"))
    {
        // Verbatim, every time (web net.js): a refusal is information.
        const FString Text = PStr(J, TEXT("error")).Left(300);
        LastErrorText = Text; LastErrorAt = Now;
        if (!Text.IsEmpty()) { ToastText(Text, true); Narrate(Text); InteractionStatus = Text; }
        OnVerbRefused.Broadcast(Text);
        return true;
    }
    if (!bJoined) return false;
    if (Type == TEXT("history"))
    {
        if (PStr(J, TEXT("reqId")) != HistoryReqId) return true;
        bHistoryPending = false;
        const auto& Entries = PArr(J, TEXT("entries"));
        TArray<TPair<FString, FString>> Older;
        for (const auto& V : Entries)
        {
            const TSharedPtr<FJsonObject> E = V->AsObject(); if (!E || PStr(E, TEXT("verb")) != TEXT("say")) continue;
            const int64 Seq = int64(PNum(E, TEXT("seq"), -1));
            if (Seq >= 0) { if (RenderedChatSeqs.Contains(Seq)) continue; RenderedChatSeqs.Add(Seq); OldestChatSeq = OldestChatSeq < 0 ? Seq : FMath::Min(OldestChatSeq, Seq); }
            Older.Add({PStr(E, TEXT("actor")), PStr(PObj(E, TEXT("args")), TEXT("text")).Left(4000)});
        }
        const double Oldest = PNum(J, TEXT("oldestSeq"), -1);
        if (Oldest >= 0) OldestChatSeq = OldestChatSeq < 0 ? int64(Oldest) : FMath::Min(OldestChatSeq, int64(Oldest));
        bool bMore = true; J->TryGetBoolField(TEXT("hasMore"), bMore);
        if (Older.IsEmpty() || !bMore) bHistoryDone = true;
        TArray<FEidoverseChatLine> Head;
        if (bHistoryDone) { FEidoverseChatLine N; N.Sender = TEXT("*"); N.Text = TEXT("\u2014 the beginning of this world \u2014"); Head.Add(N); }
        for (const auto& P : Older) { FEidoverseChatLine L; L.Sender = P.Key; L.Text = P.Value; Head.Add(L); }
        ChatHistory.Insert(Head, 0);
        if (ChatPanel) ChatPanel->RefreshHistory();
        if (HUD) HUD->PrependChatLines(Older, bHistoryDone ? TEXT("\u2014 the beginning of this world \u2014") : FString());
        return true;
    }
    if (Type == TEXT("world-reset"))
    {
        const FString By = PStr(J, TEXT("by")), World = PStr(J, TEXT("world"));
        ToastText(FString::Printf(TEXT("%s reset “%s” to zero — reloading…"), *By, *World), true);
        Narrate(FString::Printf(TEXT("%s reset “%s” to zero"), *By, *World));
        ResetReconnectAt = Now + 1.8;
        return true;
    }
    if (Type == TEXT("world-forked"))
    {
        const FString From = PStr(J, TEXT("from")), To = PStr(J, TEXT("to"));
        Narrate(FString::Printf(TEXT("world copied — “%s” → “%s” — visit with /go %s"), *From, *To, *To));
        ToastText(FString::Printf(TEXT("world copied to “%s”"), *To));
        return true;
    }
    if (Type == TEXT("mod"))
    {
        TArray<FString> Lines; PStr(J, TEXT("text")).ParseIntoArrayLines(Lines);
        for (const FString& L : Lines) Narrate(L.Left(400));
        return true;
    }
    if (Type == TEXT("defs-updated"))
    {
        if (HUD) HUD->ReloadDefs();
        if (ClipLibrary) ClipLibrary->Configure(FString()), ClipLibrary->Configure(HttpBase());
        return true;
    }
    if (Type == TEXT("debug"))
    {
        if (PStr(J, TEXT("reqId")) != DebugReqId) return true;
        const auto& Events = PArr(J, TEXT("events"));
        if (Events.IsEmpty()) { Narrate(TEXT("flight recorder is empty — nothing has bounced recently")); return true; }
        for (const auto& V : Events)
        {
            const TSharedPtr<FJsonObject> E = V->AsObject(); if (!E) continue;
            const FDateTime T = FDateTime::FromUnixTimestamp(int64(PNum(E, TEXT("ts"), 0) / 1000));
            FString Line = FString::Printf(TEXT("%02d:%02d:%02d [%s]"), T.GetHour(), T.GetMinute(), T.GetSecond(), *PStr(E, TEXT("kind")));
            for (const auto& F : E->Values)
            {
                if (F.Key == TEXT("ts") || F.Key == TEXT("kind")) continue;
                FString V2; if (!F.Value->TryGetString(V2)) { auto W = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&V2); FJsonSerializer::Serialize(F.Value, FString(), W); }
                Line += FString::Printf(TEXT(" %s=%s"), *F.Key, *V2.Left(80));
            }
            Narrate(Line.Left(400));
        }
        return true;
    }
    if (Type == TEXT("drag"))
    {
        // Someone dragging a thing in build mode: show it moving live (build.js).
        const FString Id = PStr(J, TEXT("id"));
        if (Id.IsEmpty() || PStr(J, TEXT("by")) == AssignedIdentity || !DynamicScene) return true;
        const auto& Pos = PArr(J, TEXT("pos"));
        if (Pos.Num() != 3) return true;
        const FVector W(Pos[0]->AsNumber(), Pos[1]->AsNumber(), Pos[2]->AsNumber());
        if (W.ContainsNaN()) return true;
        AActor* A = DynamicScene->EntityActor(Id); if (!A) return true;
        const FQuat Q = FromWireRotation(FQuat(FVector::YAxisVector, PNum(J, TEXT("yaw"), 0)));
        DynamicScene->HoldRemote(Id, FTransform(Q, OriginCentimeters + FromWirePosition(W), A->GetActorScale3D()), .5);
        return true;
    }
    if (Type == TEXT("puppet")) { ReceivePuppet(J); return true; }
    if (Type == TEXT("geom")) { if (DynamicScene) { const TSharedPtr<FJsonObject>* G = nullptr; if (J->TryGetObjectField(TEXT("geom"), G)) DynamicScene->SetGeom(*G); } return true; }
    if (Type == TEXT("bodydrag")) { ReceiveBodyDrag(J); return true; }
    return false;
}

void AEidoverseBridge::NarrateLogEntry(const TSharedPtr<FJsonObject>& Entry)
{
    const FString Verb = PStr(Entry, TEXT("verb")), Actor = PStr(Entry, TEXT("actor"));
    const TSharedPtr<FJsonObject> A = PObj(Entry, TEXT("args"));
    const FString Who = Actor == TEXT("world") ? TEXT("the world") : Actor;
    const FString Id = PStr(A, TEXT("id")), Reason = PStr(A, TEXT("reason"));
    const FString Tail = Reason.IsEmpty() ? FString() : TEXT(" — ") + Reason.Left(200);
    if (Verb == TEXT("grant"))
    {
        FString Role = PStr(A, TEXT("role")); bool bGen = false, bFly = false;
        FString Flags;
        if (A && A->TryGetBoolField(TEXT("gen"), bGen)) Flags += bGen ? TEXT(" +gen") : TEXT(" -gen");
        if (A && A->TryGetBoolField(TEXT("fly"), bFly)) Flags += bFly ? TEXT(" +fly") : TEXT(" -fly");
        Narrate(FString::Printf(TEXT("%s made %s %s%s"), *Who, *Id, Role.IsEmpty() ? TEXT("") : *Role, *Flags));
    }
    else if (Verb == TEXT("ban")) Narrate(FString::Printf(TEXT("%s banned %s%s"), *Who, *Id, *Tail));
    else if (Verb == TEXT("unban")) Narrate(FString::Printf(TEXT("%s lifted the ban on %s"), *Who, *Id));
    else if (Verb == TEXT("kick")) Narrate(FString::Printf(TEXT("%s removed %s%s"), *Who, *Id, *Tail));
    else if (Verb == TEXT("asset")) ToastText(FString::Printf(TEXT("%s joined the world's palette"), *PStr(A, TEXT("name"))));
    else if (Verb == TEXT("force")) ReceiveForce(A);
}

bool AEidoverseBridge::RequestEarlierChat()
{
    const double Now = FPlatformTime::Seconds();
    if (!bJoined || bHistoryDone || (bHistoryPending && Now - HistoryRequestedAt < 8) || !Socket || !Socket->IsConnected()) return false;
    bHistoryPending = true; HistoryRequestedAt = Now;
    HistoryReqId = FString::Printf(TEXT("h%d"), ++HistoryRequests);
    auto J = MakeShared<FJsonObject>();
    J->SetStringField(TEXT("type"), TEXT("history")); J->SetStringField(TEXT("reqId"), HistoryReqId);
    if (OldestChatSeq >= 0) J->SetNumberField(TEXT("before"), double(OldestChatSeq));
    J->SetNumberField(TEXT("limit"), 40);
    J->SetArrayField(TEXT("verbs"), {MakeShared<FJsonValueString>(TEXT("say"))});
    Send(J);
    return true;
}

bool AEidoverseBridge::SendRaw(const FString& Type, const TSharedRef<FJsonObject>& Fields)
{
    if (!bJoined || !Socket || !Socket->IsConnected()) return false;
    Fields->SetStringField(TEXT("type"), Type); Send(Fields); return true;
}

// The verb queue: while reconnecting, actions wait instead of vanishing (net.js).
bool AEidoverseBridge::QueueWhileReconnecting(const TSharedRef<FJsonObject>& Message, const FString& Verb)
{
    if (bJoined || !bWanted || bSpectator) return false;
    if (QueuedMessages.Num() >= 64) return false;
    QueuedMessages.Add(Message);
    Narrate(FString::Printf(TEXT("queued %s — reconnecting…"), *Verb));
    return true;
}

FString AEidoverseBridge::CloseReason(int32 Code, bool& bRetry) const
{
    const FString Recent = FPlatformTime::Seconds() - LastErrorAt < 2 ? LastErrorText : FString();
    bRetry = false;
    switch (Code)
    {
    case 4002: return FString::Printf(TEXT("“%s” is connected elsewhere — reconnect to take over"), *ParticipantName);
    case 4006: return TEXT("you were removed from this world") + (Recent.IsEmpty() ? FString() : TEXT(" — ") + Recent);
    case 4005: return FString::Printf(TEXT("“%s” is not a valid world name"), *WorldName);
    case 4003: return HasProductionSession() ? TEXT("your sign-in was refused — sign in again") : TEXT("that door key was refused");
    default: break;
    }
    if (Code >= 4000 && Code < 4100) return FString::Printf(TEXT("connection refused (%d)%s"), Code, Recent.IsEmpty() ? TEXT("") : *(TEXT(" — ") + Recent));
    bRetry = true;
    return TEXT("disconnected; retry pending");
}

// ---- presence -----------------------------------------------------------------------

FString AEidoverseBridge::CurrentPresence() const
{
    if (!ManualPresence.IsEmpty()) return ManualPresence;
    const bool bFocused = FApp::HasFocus();
    return (!bFocused || FPlatformTime::Seconds() - LastLocalInputAt >= 180) ? TEXT("away") : TEXT("present");
}

void AEidoverseBridge::SetPresence(const FString& Value)
{
    ManualPresence = (Value == TEXT("away") || Value == TEXT("busy")) ? Value : FString();
}
