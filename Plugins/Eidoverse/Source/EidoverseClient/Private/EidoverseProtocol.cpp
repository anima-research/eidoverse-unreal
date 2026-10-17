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
            // Synthetic entries from a folded snapshot carry a negative seq: not positions in history.
            if (Seq >= 0) { if (RenderedChatSeqs.Contains(Seq) || Seq <= ChatTrimmedThrough) continue; RenderedChatSeqs.Add(Seq); }
            AddChatLine(PStr(M, TEXT("actor")), PStr(M, TEXT("text")).Left(4000), false, FString(), true, Seq);
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
        const FString ReqId = PStr(J, TEXT("reqId"));
        const auto& Entries = PArr(J, TEXT("entries"));
        const double Oldest = PNum(J, TEXT("oldestSeq"), -1);
        auto OldestOf = [&]() -> int64
        {
            if (Oldest >= 0) return int64(Oldest);
            const TSharedPtr<FJsonObject> First = Entries.Num() ? Entries[0]->AsObject() : nullptr;
            return First ? int64(PNum(First, TEXT("seq"), -1)) : -1;
        };
        if (!ChatResyncReqId.IsEmpty() && ReqId == ChatResyncReqId)
        {
            // The resync after a re-join: newest first, pages of 40, until a short page.
            ChatResyncReqId.Empty();
            if (Entries.IsEmpty()) return true;
            InsertChatHistory(Entries);
            if (Entries.Num() < 40 || ++ChatResyncPages >= 25) return true;
            ChatResyncBefore = OldestOf();
            if (ChatResyncBefore > ChatResyncAfter) SendResyncPage();
            return true;
        }
        if (ReqId != HistoryReqId) return true;
        // A trim that passed the cursor while this page was in flight has restarted the walk (NoteChatTrimmed
        // is the only other writer of the cursor). The page's lines still slot in, but where the walk stands,
        // and whether it has reached the beginning, now come from the restart.
        if (ChatPageCursor == ChatPageFrom)
        {
            if (Entries.Num()) ChatPageCursor = OldestOf();
            bool bMore = true; J->TryGetBoolField(TEXT("hasMore"), bMore);
            if (Entries.IsEmpty() || !bMore) bHistoryDone = true;
        }
        const int32 Added = InsertChatHistory(Entries);
        // Nothing new on this page (every line already on screen): walk on, up to 25 pages per scroll.
        if (!Added && !bHistoryDone && ++ChatSkipPages < 25) { SendHistoryPage(); return true; }
        bHistoryPending = false;
        if (bHistoryDone)
        {
            const FString Beginning = TEXT("\u2014 the beginning of this world \u2014");
            if (ChatHistory.IsEmpty() || ChatHistory[0].Text != Beginning)
            {
                FEidoverseChatLine N; N.Sender = TEXT("*"); N.Text = Beginning; ChatHistory.Insert(N, 0);
                if (ChatPanel) ChatPanel->RefreshHistory();
                if (HUD) HUD->AddTopNote(Beginning);
            }
        }
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
        const FString Behavior = PStr(J, TEXT("behavior"));
        if (!Behavior.IsEmpty()) Narrate(FString::Printf(TEXT("script %s \u2014 %s"), *Behavior, *PStr(J, TEXT("status"))));
        if (Events.IsEmpty()) { Narrate(Behavior.IsEmpty() ? TEXT("flight recorder is empty \u2014 nothing has bounced recently") : TEXT("(no console lines)")); return true; }
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
    bHistoryPending = true; ChatSkipPages = 0;
    SendHistoryPage();
    return true;
}

void AEidoverseBridge::SendHistoryPage()
{
    // The next page is everything below the cursor; from now when the walk has not begun (or restarted).
    HistoryRequestedAt = FPlatformTime::Seconds();
    HistoryReqId = FString::Printf(TEXT("h%d"), ++HistoryRequests);
    ChatPageFrom = ChatPageCursor;
    auto J = MakeShared<FJsonObject>();
    J->SetStringField(TEXT("type"), TEXT("history")); J->SetStringField(TEXT("reqId"), HistoryReqId);
    if (ChatPageCursor >= 0) J->SetNumberField(TEXT("before"), double(ChatPageCursor));
    J->SetNumberField(TEXT("limit"), 40);
    J->SetArrayField(TEXT("verbs"), {MakeShared<FJsonValueString>(TEXT("say"))});
    Send(J);
}

void AEidoverseBridge::SendResyncPage()
{
    if (!bJoined || !Socket || !Socket->IsConnected() || ChatResyncAfter < 0) return;
    ChatResyncReqId = FString::Printf(TEXT("r%d"), ++HistoryRequests);
    auto J = MakeShared<FJsonObject>();
    J->SetStringField(TEXT("type"), TEXT("history")); J->SetStringField(TEXT("reqId"), ChatResyncReqId);
    J->SetNumberField(TEXT("after"), double(ChatResyncAfter));
    if (ChatResyncBefore >= 0) J->SetNumberField(TEXT("before"), double(ChatResyncBefore));
    J->SetNumberField(TEXT("limit"), 40);
    J->SetArrayField(TEXT("verbs"), {MakeShared<FJsonValueString>(TEXT("say"))});
    Send(J);
}

int32 AEidoverseBridge::InsertChatHistory(const TArray<TSharedPtr<FJsonValue>>& Entries)
{
    // Slot one page into the log where each line belongs (by seq), skipping lines already on screen.
    TArray<UEidoverseHUD::FHistoricalLine> Fresh;
    for (const auto& V : Entries)
    {
        const TSharedPtr<FJsonObject> E = V.IsValid() ? V->AsObject() : nullptr;
        if (!E || PStr(E, TEXT("verb")) != TEXT("say")) continue;
        const int64 Seq = int64(PNum(E, TEXT("seq"), -1));
        if (Seq < 0 || RenderedChatSeqs.Contains(Seq)) continue;
        RenderedChatSeqs.Add(Seq);
        UEidoverseHUD::FHistoricalLine L;
        L.Sender = PStr(E, TEXT("actor")).Left(64); L.Text = PStr(PObj(E, TEXT("args")), TEXT("text")).Left(4000);
        L.Seq = Seq; L.Ts = PNum(E, TEXT("ts"), 0);
        Fresh.Add(MoveTemp(L));
        // the legacy panel's list, in seq order too
        FEidoverseChatLine C; C.Sender = Fresh.Last().Sender; C.Text = Fresh.Last().Text; C.Seq = Seq;
        int32 At = 0;
        for (int32 I = 0; I < ChatHistory.Num(); ++I) if (ChatHistory[I].Seq >= 0 && ChatHistory[I].Seq < Seq) At = I + 1;
        ChatHistory.Insert(MoveTemp(C), At);
    }
    if (Fresh.IsEmpty()) return 0;
    if (ChatPanel) ChatPanel->RefreshHistory();
    if (HUD) HUD->InsertHistoricalLines(Fresh);
    return Fresh.Num();
}

void AEidoverseBridge::NoteChatTrimmed(const TArray<int64>& Seqs)
{
    for (const int64 Seq : Seqs)
    {
        if (Seq < 0) continue;
        RenderedChatSeqs.Remove(Seq);
        ChatTrimmedThrough = FMath::Max(ChatTrimmedThrough, Seq);
        // If the backward walk had already passed it, start over from the present (skipping what is still on
        // screen) instead of stepping over the new hole. A walk that ended at the present has passed everything.
        const bool bPassed = ChatPageCursor < 0 ? bHistoryDone : Seq >= ChatPageCursor;
        if (bPassed) { ChatPageCursor = -1; bHistoryDone = false; }
    }
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

#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "GenericPlatform/GenericPlatformHttp.h"

void AEidoverseBridge::UploadFile(const FString& LocalPath, const FString& As, TFunction<void(const FString&, const FString&)> OnDone)
{
    TArray<uint8> Bytes;
    const FString Path = LocalPath.TrimStartAndEnd().TrimQuotes();
    if (!FFileHelper::LoadFileToArray(Bytes, *Path)) { OnDone(FString(), TEXT("couldn't read that file")); return; }
    if (Bytes.Num() > 64 * 1024 * 1024) { OnDone(FString(), TEXT("file is larger than 64 MB")); return; }
    const FString Base = HttpBase();
    if (Base.IsEmpty() || !bJoined) { OnDone(FString(), TEXT("join a world first")); return; }
    FString Url = Base + TEXT("/upload?name=") + FGenericPlatformHttp::UrlEncode(FPaths::GetCleanFilename(Path));
    if (!As.IsEmpty()) Url += TEXT("&as=") + As;
    if (!JoinToken.IsEmpty() && !(HasProductionSession() && Endpoint == ProductionEndpoint)) Url += TEXT("&token=") + FGenericPlatformHttp::UrlEncode(JoinToken);
    auto Request = FHttpModule::Get().CreateRequest();
    Request->SetURL(Url); Request->SetVerb(TEXT("POST")); Request->SetContent(Bytes); Request->SetTimeout(120);
    Request->SetHeader(TEXT("Content-Type"), TEXT("application/octet-stream"));
    if (HasProductionSession() && Endpoint == ProductionEndpoint) Request->SetHeader(TEXT("Cookie"), TEXT("ew_sess=") + ProductionSession);
    TWeakObjectPtr<AEidoverseBridge> Weak(this);
    Request->OnProcessRequestComplete().BindLambda([Weak, OnDone](FHttpRequestPtr, FHttpResponsePtr Response, bool bOk)
    {
        if (!Weak.IsValid()) return;
        if (!bOk || !Response.IsValid()) { OnDone(FString(), TEXT("upload failed (no response)")); return; }
        TSharedPtr<FJsonObject> J;
        FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Response->GetContentAsString()), J);
        FString P, Err;
        if (J) { J->TryGetStringField(TEXT("path"), P); J->TryGetStringField(TEXT("error"), Err); }
        if (Response->GetResponseCode() != 200 || P.IsEmpty()) { OnDone(FString(), Err.IsEmpty() ? FString::Printf(TEXT("upload refused (%d)"), Response->GetResponseCode()) : Err.Left(200)); return; }
        OnDone(P, FString());
    });
    Request->ProcessRequest();
}

bool AEidoverseBridge::GetPeerInfo(const FString& Id, FString& OutPresence, float& OutMetres) const
{
    const FEidoversePeer* P = Peers.Find(Id); if (!P) return false;
    OutPresence = P->Presence;
    const APawn* Me = UGameplayStatics::GetPlayerPawn(this, 0);
    OutMetres = Me && P->Actor.IsValid() ? float(FVector::Dist(Me->GetActorLocation(), P->Actor->GetActorLocation()) / 100) : -1.f;
    return true;
}
