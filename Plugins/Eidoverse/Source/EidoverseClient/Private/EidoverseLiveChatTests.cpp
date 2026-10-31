// Eidoverse.LiveChat: the real bridge against a scratch sequencer and two bots (Tools/eidoverse/
// run_chat_live_test.sh starts them and sets EIDO_LIVE_CHAT_*; without them the test only says how to run it).
//
//   1. scrollback: the arrival window keeps a quiet line above a hole of trimmed loud talk; walking back from
//      the present fills the hole (chat.js loadOlder), every line once, in seq order;
//   2. attention: a live mention and a whisper each reach the gate's player once; my arrival carries my
//      page session;
//   3. resync: a dropped connection held while 60 lines are said re-joins to a window missing most of them;
//      the resync after the last delivered seq slots them in, in order, once each;
//   4. travel: the server's map (GET /worlds) shows this world and who is in it; travelling to another world
//      and back keeps the same signed identity, with each world's own chat.
// Eidoverse.LivePairing: the browser-approved sign-in against the server's own pairing handler on a loopback
// fixture (tools/native-login-fixture.ts): approved, signed in, one ordinary session, no world named.
#include "EidoverseBridge.h"
#include "EidoverseAttention.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Misc/AutomationTest.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Tests/AutomationCommon.h"
#include "WebSocketsModule.h"
#include "EidoverseUI.h"
#include "EngineUtils.h"
#include "GenericPlatform/GenericPlatformHttp.h"

#if WITH_DEV_AUTOMATION_TESTS
namespace
{
    struct FLiveChat : TSharedFromThis<FLiveChat>
    {
        UWorld* World = nullptr;
        TWeakObjectPtr<AEidoverseBridge> Bridge;
        FString Control, Home;
        int32 Step = 0, Chimes = 0;
        double StepAt = 0, Last = 0;
        bool bHttpBusy = false; FString HttpBody;
        void Http(const FString& Verb, const FString& Path)
        {
            bHttpBusy = true; HttpBody.Empty();
            auto Req = FHttpModule::Get().CreateRequest();
            Req->SetVerb(Verb); Req->SetURL(Control + Path);
            Req->OnProcessRequestComplete().BindLambda([Self = AsWeak()](FHttpRequestPtr, FHttpResponsePtr R, bool bOk)
            {
                if (const TSharedPtr<FLiveChat> L = Self.Pin()) { L->HttpBody = bOk && R ? R->GetContentAsString() : FString(); L->bHttpBusy = false; }
            });
            Req->ProcessRequest();
        }
        void Close()
        {
            if (AEidoverseBridge* B = Bridge.Get()) { B->Disconnect(); B->Destroy(); }
            if (World) { GEngine->DestroyWorldContext(World); World->DestroyWorld(false); World = nullptr; }
        }
    };

    // The lines with a log position, in the order the log holds them.
    TArray<const FEidoverseChatLine*> Positioned(const AEidoverseBridge* B)
    {
        TArray<const FEidoverseChatLine*> Out;
        for (const FEidoverseChatLine& L : B->ChatHistory) if (L.Seq >= 0) Out.Add(&L);
        return Out;
    }
    int32 CountText(const AEidoverseBridge* B, const FString& Text)
    {
        int32 N = 0; for (const FEidoverseChatLine& L : B->ChatHistory) N += L.Text == Text; return N;
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEidoverseLiveChat, "Eidoverse.LiveChat", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FEidoverseLiveChat::RunTest(const FString&)
{
    const FString Url = FPlatformMisc::GetEnvironmentVariable(TEXT("EIDO_LIVE_CHAT_URL"));
    const FString WorldName = FPlatformMisc::GetEnvironmentVariable(TEXT("EIDO_LIVE_CHAT_WORLD"));
    const FString Control = FPlatformMisc::GetEnvironmentVariable(TEXT("EIDO_LIVE_CHAT_CONTROL"));
    const int32 Loud = FCString::Atoi(*FPlatformMisc::GetEnvironmentVariable(TEXT("EIDO_LIVE_CHAT_LOUD")));
    if (Url.IsEmpty() || WorldName.IsEmpty() || Control.IsEmpty() || Loud <= 0)
    {
        AddInfo(TEXT("live check skipped: run Tools/eidoverse/run_chat_live_test.sh (a scratch sequencer on loopback and two bots)"));
        return true;
    }
    if (!Url.StartsWith(TEXT("ws://127.0.0.1:")) || !Control.StartsWith(TEXT("http://127.0.0.1:"))) { AddError(TEXT("loopback servers only")); return false; }
    FModuleManager::LoadModuleChecked<FWebSocketsModule>(TEXT("WebSockets"));

    TSharedPtr<FLiveChat> S = MakeShared<FLiveChat>();
    S->Control = Control;
    S->World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("EidoverseLiveChat"));
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(S->World);
    S->World->InitializeActorsForPlay(FURL());
    FActorSpawnParameters P; P.ObjectFlags |= RF_Transient;
    AEidoverseBridge* B = S->World->SpawnActor<AEidoverseBridge>(P);
    if (!B) { AddError(TEXT("could not spawn the bridge")); S->Close(); return false; }
    B->bShowChatUI = false; B->bLoadRemoteAvatars = false; B->bReconnect = true;
    B->Endpoint = Url; B->WorldName = WorldName; B->ParticipantName = TEXT("unrealtest");
    if (!B->HasActorBegunPlay()) B->DispatchBeginPlay();
    B->AttentionGate.Play = [W = TWeakPtr<FLiveChat>(S)]() { if (auto L = W.Pin()) ++L->Chimes; return true; };
    S->Bridge = B;
    S->StepAt = S->Last = FPlatformTime::Seconds();
    B->Connect();

    ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this, S, Loud]() -> bool {
        AEidoverseBridge* B = S->Bridge.Get();
        const double Now = FPlatformTime::Seconds();
        if (!B) { AddError(TEXT("bridge gone")); S->Close(); return true; }
        B->Tick(float(FMath::Min(.1, Now - S->Last))); S->Last = Now;
        auto Next = [&]() { ++S->Step; S->StepAt = Now; };
        auto Fail = [&](const FString& Why) { AddError(Why); S->Close(); return true; };
        const double In = Now - S->StepAt;
        const FString Quiet = TEXT("quiet hello from the start of the day");
        auto Ordered = [&](const TCHAR* When)
        {
            const auto Lines = Positioned(B);
            for (int32 I = 1; I < Lines.Num(); ++I)
                if (Lines[I]->Seq <= Lines[I - 1]->Seq)
                    { AddError(FString::Printf(TEXT("%s: seq %lld (%s) after %lld (%s)"), When, Lines[I]->Seq, *Lines[I]->Text, Lines[I - 1]->Seq, *Lines[I - 1]->Text)); return false; }
            return true;
        };
        switch (S->Step)
        {
        case 0:   // joined, with the fair window
            if (!B->bJoined) return In > 20 ? Fail(TEXT("never joined ") + B->ConnectionStatus) : false;
            if (CountText(B, Quiet) != 1) return Fail(TEXT("the arrival window lost the quiet line"));
            if (CountText(B, FString::Printf(TEXT("loud 1 of %d"), Loud)) != 0) return Fail(TEXT("precondition: the window should have a trimmed hole"));
            AddInfo(FString::Printf(TEXT("window: %d lines, the quiet line above a hole"), Positioned(B).Num()));
            Next(); return false;
        case 1:   // scroll to the top until the beginning (the HUD calls this while the log is near its top)
            B->RequestEarlierChat();
            if (!B->bHistoryDone) return In > 30 ? Fail(TEXT("the walk never reached the beginning")) : false;
            for (int32 I = 1; I <= Loud; ++I)
                if (CountText(B, FString::Printf(TEXT("loud %d of %d"), I, Loud)) != 1) return Fail(FString::Printf(TEXT("scrollback: line %d of %d not shown exactly once"), I, Loud));
            if (CountText(B, Quiet) != 1 || !Ordered(TEXT("scrollback"))) return Fail(TEXT("scrollback out of order or duplicated"));
            AddInfo(FString::Printf(TEXT("scrollback filled the hole: %d lines in seq order"), Positioned(B).Num()));
            S->Http(TEXT("POST"), TEXT("/mention")); Next(); return false;
        case 2:   // a live mention chimes once
            if (S->Chimes < 1) return In > 8 ? Fail(TEXT("the live mention never reached the attention gate")) : false;
            if (In < 3.3) return false;   // past the cooldown
            S->Http(TEXT("POST"), TEXT("/whisper")); Next(); return false;
        case 3:   // a whisper to me chimes
            if (S->Chimes < 2) return In > 8 ? Fail(TEXT("the whisper never reached the attention gate")) : false;
            S->Http(TEXT("GET"), TEXT("/report")); Next(); return false;
        case 4:   // my arrival carried my page session
        {
            if (S->bHttpBusy) return In > 5 ? Fail(TEXT("no report from the bot")) : false;
            TSharedPtr<FJsonObject> R; FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(S->HttpBody), R);
            bool bSession = false;
            if (R) for (const auto& V : R->GetArrayField(TEXT("arrivals"))) if (const auto A = V->AsObject())
                bSession |= A->GetStringField(TEXT("id")) == TEXT("unrealtest") && A->GetStringField(TEXT("session")) == EidoAttention::PageSession();
            if (!bSession) return Fail(TEXT("my arrive did not carry my page session: ") + S->HttpBody.Left(300));
            TestEqual(TEXT("one chime per notice"), S->Chimes, 2);
            B->TestDropConnection(20);
            S->Http(TEXT("POST"), TEXT("/say?n=60")); Next(); return false;
        }
        case 5:   // re-joined after the hold, the resync done
        {
            if (!B->bJoined) return In > 40 ? Fail(TEXT("never re-joined")) : false;
            int32 Missing = 0; for (int32 I = 1; I <= 60; ++I) Missing += CountText(B, FString::Printf(TEXT("gap %d of 60"), I)) != 1;
            if (Missing) return In > 45 ? Fail(FString::Printf(TEXT("resync: %d of the 60 lines said while away are missing or doubled"), Missing)) : false;
            if (!Ordered(TEXT("resync"))) return Fail(TEXT("resync out of order"));
            AddInfo(FString::Printf(TEXT("resync after the drop: 60 lines slotted in, %d lines in seq order"), Positioned(B).Num()));
            S->Home = B->WorldName; B->RefreshWorlds(); Next(); return false;
        }
        case 6:   // the map: this world, with the bots and me in it
        {
            if (B->bWorldsPending || B->KnownWorlds.IsEmpty()) return In > 10 ? Fail(TEXT("no world map: ") + B->WorldsStatus) : false;
            const FEidoverseWorldInfo* Here = B->KnownWorlds.FindByPredicate([&](const FEidoverseWorldInfo& W) { return W.Name == S->Home; });
            if (!Here || !Here->People.Contains(TEXT("loudbot")) || !Here->People.Contains(TEXT("quietone")) || !Here->People.Contains(TEXT("unrealtest")))
                return Fail(TEXT("the map does not show this world's people: ") + B->WorldsStatus);
            AddInfo(TEXT("world map: ") + B->WorldsStatus);
            if (!B->TravelTo(S->Home + TEXT("-elsewhere"))) return Fail(TEXT("travel refused"));
            Next(); return false;
        }
        case 7:   // arrived elsewhere, as the same person, with that world's (empty) chat
            if (!B->bJoined || B->WorldName != S->Home + TEXT("-elsewhere")) return In > 15 ? Fail(TEXT("never arrived elsewhere: ") + B->ConnectionStatus) : false;
            if (B->AssignedIdentity != TEXT("unrealtest")) return Fail(TEXT("arrived as someone else: ") + B->AssignedIdentity);
            if (CountText(B, Quiet) != 0) return Fail(TEXT("the other world's chat came along"));
            B->RefreshWorlds(); Next(); return false;
        case 8:   // both on the map, me in the new one; then home
        {
            if (B->bWorldsPending) return In > 10 ? Fail(TEXT("no second map")) : false;
            const FEidoverseWorldInfo* There = B->KnownWorlds.FindByPredicate([&](const FEidoverseWorldInfo& W) { return W.Name == S->Home + TEXT("-elsewhere"); });
            const FEidoverseWorldInfo* Home = B->KnownWorlds.FindByPredicate([&](const FEidoverseWorldInfo& W) { return W.Name == S->Home; });
            if (!There || !Home || !There->People.Contains(TEXT("unrealtest")) || Home->People.Contains(TEXT("unrealtest")))
            { if (In < 4) return false; return Fail(TEXT("the map did not follow me: ") + B->WorldsStatus); }
            B->TravelTo(S->Home); Next(); return false;
        }
        case 9:   // home again: its chat window is back
            if (!B->bJoined || B->WorldName != S->Home || CountText(B, Quiet) != 1) return In > 15 ? Fail(TEXT("never got home")) : false;
            AddInfo(TEXT("travel: there and back as unrealtest, each world with its own chat"));
            S->Close(); return true;
        }
        return true;
    }));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEidoverseLivePairing, "Eidoverse.LivePairing", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FEidoverseLivePairing::RunTest(const FString&)
{
    if (FPlatformMisc::GetEnvironmentVariable(TEXT("EIDO_LIVE_PAIRING")) != TEXT("1"))
    {
        AddInfo(TEXT("live check skipped: run Tools/eidoverse/run_chat_live_test.sh (it starts the loopback pairing fixture)"));
        return true;
    }
    TSharedPtr<FLiveChat> S = MakeShared<FLiveChat>();
    S->Control = TEXT("http://127.0.0.1:18996");
    S->World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("EidoverseLivePairing"));
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(S->World);
    S->World->InitializeActorsForPlay(FURL());
    FActorSpawnParameters P; P.ObjectFlags |= RF_Transient;
    AEidoverseBridge* B = S->World->SpawnActor<AEidoverseBridge>(P);
    if (!B) { AddError(TEXT("could not spawn the bridge")); S->Close(); return false; }
    B->bShowChatUI = false;
    if (!B->HasActorBegunPlay()) B->DispatchBeginPlay();
    S->Bridge = B; S->StepAt = S->Last = FPlatformTime::Seconds();
    B->BeginLocalLoginTest();   // loopback only: never opens a browser or reaches production
    ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this, S]() -> bool {
        AEidoverseBridge* B = S->Bridge.Get();
        const double Now = FPlatformTime::Seconds();
        if (!B) { AddError(TEXT("bridge gone")); S->Close(); return true; }
        B->Tick(float(FMath::Min(.1, Now - S->Last))); S->Last = Now;
        const double In = Now - S->StepAt;
        auto Fail = [&](const FString& Why) { AddError(Why); S->Close(); return true; };
        switch (S->Step)
        {
        case 0:   // a pairing code to show
            if (!B->bAuthenticating || !B->LoginStatus.StartsWith(TEXT("Code: "))) return In > 10 ? Fail(TEXT("no pairing code: ") + B->LoginStatus) : false;
            if (B->LoginStatus.Contains(TEXT("water"), ESearchCase::IgnoreCase)) return Fail(TEXT("the code names a world: ") + B->LoginStatus);
            S->Http(TEXT("POST"), TEXT("/fixture/approve")); ++S->Step; S->StepAt = Now; return false;
        case 1:   // the browser's approval arrives by the next poll: signed in, as an ordinary sign-in
            if (B->bAuthenticating || B->LoginName.IsEmpty()) return In > 12 ? Fail(TEXT("never signed in: ") + B->LoginStatus) : false;
            TestEqual(TEXT("the browser's identity"), B->LoginName, FString(TEXT("Local Discord Fixture")));
            TestEqual(TEXT("signed in, no world named"), B->LoginStatus, FString(TEXT("Signed in as Local Discord Fixture.")));
            S->Http(TEXT("GET"), TEXT("/fixture/report")); ++S->Step; S->StepAt = Now; return false;
        case 2:   // exactly one session issued
        {
            if (S->bHttpBusy) return In > 5 ? Fail(TEXT("no fixture report")) : false;
            TSharedPtr<FJsonObject> R; FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(S->HttpBody), R);
            TestTrue(TEXT("one session issued"), R.IsValid() && R->GetNumberField(TEXT("issued")) == 1);
            AddInfo(TEXT("pairing: code shown, browser approval, signed in as the browser's person, one session"));
            B->LogoutProduction(); S->Close(); return true;
        }
        }
        return true;
    }));
    return true;
}
// Eidoverse.LiveV4Chat (T11, slice B: Plugins/Eidoverse/Bridge/SLICES.md): chat, narration, rights, pose.anim, capture, /defs and
// /worlds through the local bridge, in a game session against Tools/eidoverse/run_v4_chat.sh (its founder is v4_chat_bot.ts).
// Every row runs, red or green, and logs EIDOVERSE_V4CHAT <row> green|RED.
namespace
{
    struct FV4Chat : TSharedFromThis<FV4Chat>
    {
        TWeakObjectPtr<UWorld> World;
        TWeakObjectPtr<AEidoverseBridge> Bridge;
        FString Url, WorldName, Control, Http, Body, ContentType, SavedEndpoint;
        int32 Row = 0, Chimes = 0, Code = 0, Red = 0;
        double RowAt = 0, AskAt = 0;
        bool bStarted = false, bBusy = false, bAsked = false, bResult = false;
        void Get(const FString& Full)
        {
            bBusy = true; bAsked = true; Body.Empty(); Code = 0; ContentType.Empty();
            auto Req = FHttpModule::Get().CreateRequest();
            Req->SetVerb(TEXT("GET")); Req->SetURL(Full); Req->SetTimeout(60);
            Req->OnProcessRequestComplete().BindLambda([Self = AsWeak()](FHttpRequestPtr, FHttpResponsePtr R, bool bOk)
            {
                if (const TSharedPtr<FV4Chat> L = Self.Pin())
                { L->Code = bOk && R ? R->GetResponseCode() : -1; L->ContentType = bOk && R ? R->GetContentType() : FString(); L->Body = bOk && R ? R->GetContentAsString().Left(400) : FString(); L->bBusy = false; }
            });
            Req->ProcessRequest();
        }
        void Bot(const FString& Path) { Get(Control + Path); }
    };
    const FEidoverseChatLine* FindLine(const AEidoverseBridge* B, const FString& Sender, const FString& Text)
    {
        for (const FEidoverseChatLine& L : B->ChatHistory) if (L.Sender == Sender && L.Text == Text) return &L;
        return nullptr;
    }
    bool HasNarration(const AEidoverseBridge* B, const FString& Part)
    {
        for (const FEidoverseChatLine& L : B->ChatHistory) if (L.Sender == TEXT("*") && L.Text.Contains(Part)) return true;
        return false;
    }
    FString Q(const FString& S) { return FGenericPlatformHttp::UrlEncode(S); }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEidoverseLiveV4Chat, "Eidoverse.LiveV4Chat", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FEidoverseLiveV4Chat::RunTest(const FString&)
{
    TSharedPtr<FV4Chat> S = MakeShared<FV4Chat>();
    S->Url = FPlatformMisc::GetEnvironmentVariable(TEXT("EIDO_LIVE_V4_URL"));
    S->WorldName = FPlatformMisc::GetEnvironmentVariable(TEXT("EIDO_LIVE_V4_WORLD"));
    S->Control = FPlatformMisc::GetEnvironmentVariable(TEXT("EIDO_LIVE_V4_CONTROL"));
    if (S->Url.IsEmpty() || S->WorldName.IsEmpty() || S->Control.IsEmpty()) { AddInfo(TEXT("live check skipped: run Tools/eidoverse/run_v4_chat.sh (-game, a scratch server and its founder)")); return true; }
    if (!S->Url.StartsWith(TEXT("ws://127.0.0.1:")) || !S->Control.StartsWith(TEXT("http://127.0.0.1:"))) { AddError(TEXT("loopback servers only")); return false; }
    S->Http = AEidoverseBridge::HttpBaseOf(S->Url);
    for (const FWorldContext& C : GEngine->GetWorldContexts()) if (C.WorldType == EWorldType::Game && C.World()) S->World = C.World();
    if (!S->World.IsValid()) { AddError(TEXT("needs a game session (-game)")); return false; }
    S->RowAt = FPlatformTime::Seconds();
    const FString Me = TEXT("unrealv4"), Founder = TEXT("founder");

    // {row, start (once), check (until true or the row's time is up)}
    struct FRow { const TCHAR* Name; double Limit; TFunction<void(AEidoverseBridge*)> Start; TFunction<bool(AEidoverseBridge*, double)> Check; };
    TSharedRef<TArray<FRow>> Rows = MakeShared<TArray<FRow>>();
    Rows->Add({TEXT("join through the bridge"), 60, [S, Me](AEidoverseBridge* B) {
        if (B->bJoined) B->Disconnect();
        B->bLoadRemoteAvatars = false; B->bReconnect = true; B->bUseBridge = true;
        B->AttentionGate.Play = [W = TWeakPtr<FV4Chat>(S)]() { if (auto L = W.Pin()) ++L->Chimes; return true; };
        B->JoinFromDoor(S->Url, S->WorldName, Me); },
        [](AEidoverseBridge* B, double) { return B->bJoined && B->bBridgeReady; }});
    Rows->Add({TEXT("speech is arrival-only: said, and nothing from before"), 5, nullptr,
        [](AEidoverseBridge* B, double) { return HasNarration(B, TEXT("arrival-only")) && !FindLine(B, TEXT("founder"), TEXT("said before you came")); }});
    Rows->Add({TEXT("bridge/say: someone's line, named"), 15, [S](AEidoverseBridge*) { S->Bot(TEXT("/say?text=") + Q(TEXT("line one from the founder"))); },
        [Founder](AEidoverseBridge* B, double) { const auto* L = FindLine(B, Founder, TEXT("line one from the founder")); return L && L->Seq > 0; }});
    Rows->Add({TEXT("my say goes as a request and comes back"), 15, [](AEidoverseBridge* B) { B->Say(TEXT("hello from unreal")); },
        [](AEidoverseBridge* B, double) { return FindLine(B, B->AssignedIdentity, TEXT("hello from unreal")) != nullptr; }});
    Rows->Add({TEXT("a live mention chimes once"), 15, [S](AEidoverseBridge*) { S->Bot(TEXT("/say?text=") + Q(TEXT("@unrealv4 are you there"))); },
        [S, Founder](AEidoverseBridge* B, double) { return FindLine(B, Founder, TEXT("@unrealv4 are you there")) && S->Chimes == 1; }});
    Rows->Add({TEXT("a spoken utterance's two says are one line"), 15, [S](AEidoverseBridge*) { S->Bot(TEXT("/say2?utt=3&a=first%20half&b=second%20half")); },
        [Founder](AEidoverseBridge* B, double) { return FindLine(B, Founder, TEXT("first half second half")) != nullptr; }});
    Rows->Add({TEXT("my spoken say is admitted (no voiced)"), 15, [](AEidoverseBridge* B) { B->SaySpoken(TEXT("testing out loud")); },
        [](AEidoverseBridge* B, double) { return FindLine(B, B->AssignedIdentity, TEXT("\U0001F399 testing out loud")) != nullptr; }});
    Rows->Add({TEXT("bridge/narration: a role grant is narrated"), 15, [S](AEidoverseBridge*) { S->Bot(TEXT("/grant?role=moderator")); },
        [](AEidoverseBridge* B, double) { return HasNarration(B, TEXT("founder made unrealv4 moderator")); }});
    Rows->Add({TEXT("rights: a moderator's permissions[] build"), 10, nullptr, [](AEidoverseBridge* B, double) { return B->CanBuild(); }});
    Rows->Add({TEXT("rights: a custom role with build builds"), 15, [S](AEidoverseBridge*) { S->Bot(TEXT("/grant?role=crew")); },
        [](AEidoverseBridge* B, double) { return HasNarration(B, TEXT("founder made unrealv4 crew")) && B->WorldRole == TEXT("crew") && B->CanBuild(); }});
    Rows->Add({TEXT("rights: taken away, no build; the revoke narrated"), 15, [S](AEidoverseBridge*) { S->Bot(TEXT("/revoke")); },
        [](AEidoverseBridge* B, double) { return HasNarration(B, TEXT("founder took unrealv4 off the list")) && !B->CanBuild(); }});
    Rows->Add({TEXT("whisper: refused here by policy, no DM tab"), 5, [S](AEidoverseBridge* B) { S->bResult = B->Whisper(TEXT("founder"), TEXT("psst")); if (B->HUD) B->HUD->OpenDm(TEXT("founder")); },
        [S](AEidoverseBridge* B, double) { return !S->bResult && B->HUD && B->HUD->GetDmTabs().IsEmpty() && HasNarration(B, TEXT("Direct messages aren't in world chat")); }});
    Rows->Add({TEXT("history: a page at the floor; nothing doubled; the floor said"), 20, [S](AEidoverseBridge* B) { S->bResult = B->RequestEarlierChat(); },
        [S](AEidoverseBridge* B, double) {
            if (!S->bResult || !B->bHistoryDone || B->bHistoryPending || B->ChatHistory.IsEmpty()) return false;
            int32 N = 0; for (const FEidoverseChatLine& L : B->ChatHistory) N += L.Text == TEXT("line one from the founder");
            return N == 1 && B->ChatHistory[0].Text.Contains(TEXT("earlier chat isn't shown here")); }});
    Rows->Add({TEXT("pose.anim in: a peer's once anim is read"), 15, [S](AEidoverseBridge*) { S->Bot(TEXT("/pose?anim=wave")); },
        [Founder](AEidoverseBridge* B, double) { return B->PeerOnceAnim(Founder) == TEXT("wave"); }});
    Rows->Add({TEXT("pose.anim out: my once anim rides as anim"), 15, [](AEidoverseBridge* B) { B->Emote(TEXT("cheer")); },
        [S](AEidoverseBridge*, double In) {
            if (S->bBusy) return false;
            if (S->Body.Contains(TEXT("\"anim\":[\"cheer\"")) && S->Body.Contains(TEXT("\"emote\":[]"))) return true;
            if (FPlatformTime::Seconds() - S->AskAt > 1) { S->AskAt = FPlatformTime::Seconds(); S->Bot(TEXT("/seen")); }
            return false; }});
    Rows->Add({TEXT("/defs: the action bar is read"), 15, [](AEidoverseBridge* B) { if (B->HUD) B->HUD->ReloadDefs(); },
        [](AEidoverseBridge* B, double) { return B->HUD && B->HUD->GetEmoteNames().Contains(TEXT("wave")) && B->ClipEmoteNames().Contains(TEXT("wave")); }});
    Rows->Add({TEXT("/worlds: people are objects, agents declare"), 15, [](AEidoverseBridge* B) { B->RefreshWorlds(); },
        [S, Me, Founder](AEidoverseBridge* B, double) {
            const FEidoverseWorldInfo* W = B->KnownWorlds.FindByPredicate([&](const FEidoverseWorldInfo& X) { return X.Name == S->WorldName; });
            return W && W->People.Contains(Me) && W->Agents.Contains(Founder) && !W->People.Contains(Founder) && !W->People.Contains(FString()); }});
    Rows->Add({TEXT("capture: the server's capture is answered with capture-result"), 60, [](AEidoverseBridge* B) { B->SetShareSceneScreenshots(true); },
        [S, Me](AEidoverseBridge*, double In) {
            if (!S->bAsked) { if (In > 1.5) S->Get(S->Http + TEXT("/snap?renderer=unreal&world=") + Q(S->WorldName) + TEXT("&follow=") + Me); return false; }
            return !S->bBusy && S->Code == 200 && S->ContentType.StartsWith(TEXT("image/")); }});
    Rows->Add({TEXT("queued while reconnecting: a say goes once, after the re-join"), 60, [S](AEidoverseBridge* B) { B->TestDropConnection(2); S->bResult = B->Say(TEXT("queued while away")); },
        [S](AEidoverseBridge* B, double) {
            int32 N = 0; for (const FEidoverseChatLine& L : B->ChatHistory) N += L.Sender == B->AssignedIdentity && L.Text == TEXT("queued while away");
            return S->bResult && B->bJoined && N == 1; }});
    Rows->Add({TEXT("not bridged: /defs is read for main's emotes, as before"), 20, [S](AEidoverseBridge* B) {
        S->SavedEndpoint = B->Endpoint; B->bUseBridge = false; B->Endpoint = S->Control.Replace(TEXT("http://"), TEXT("ws://")) + TEXT("/ws"); if (B->HUD) B->HUD->ReloadDefs(); },
        [](AEidoverseBridge* B, double) {
            const TArray<FString> Bar = B->HUD ? B->HUD->GetEmoteNames() : TArray<FString>(), Clips = B->ClipEmoteNames();
            return Bar.Contains(TEXT("nod-main")) && !Bar.Contains(TEXT("wave-bridge")) && Clips.Contains(TEXT("nod-main")) && !Clips.Contains(TEXT("wave-bridge")); }});

    ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this, S, Rows]() -> bool {
        const double Now = FPlatformTime::Seconds(), In = Now - S->RowAt;
        UWorld* W = S->World.Get(); if (!W) { AddError(TEXT("game world gone")); return true; }
        AEidoverseBridge* B = S->Bridge.Get();
        if (!B) { for (TActorIterator<AEidoverseBridge> It(W); It; ++It) S->Bridge = B = *It; if (!B || !B->HUD) { if (In > 30) { AddError(TEXT("no bridge or HUD")); return true; } return false; } S->RowAt = Now; return false; }
        if (S->Row >= Rows->Num())
        {
            UE_LOG(LogTemp, Display, TEXT("EIDOVERSE_V4CHAT done: %d of %d rows red"), S->Red, Rows->Num());
            B->SetShareSceneScreenshots(false); B->Disconnect(); if (!S->SavedEndpoint.IsEmpty()) B->Endpoint = S->SavedEndpoint; return true;
        }
        const FRow& R = (*Rows)[S->Row];
        if (!S->bStarted) { S->bStarted = true; S->bAsked = false; S->Body.Empty(); S->AskAt = 0; if (R.Start) R.Start(B); return false; }
        const bool bGreen = R.Check(B, In);
        if (!bGreen && In < R.Limit) return false;
        UE_LOG(LogTemp, Display, TEXT("EIDOVERSE_V4CHAT %02d %s: %s"), S->Row, bGreen ? TEXT("green") : TEXT("RED"), R.Name);
        if (!bGreen) { ++S->Red; AddError(FString::Printf(TEXT("row %d red: %s (status %s; http %d %s)"), S->Row, R.Name, *B->ConnectionStatus, S->Code, *S->Body.Left(160))); }
        if (!bGreen && S->Row == 0) return true;   // nothing else can run unjoined
        ++S->Row; S->bStarted = false; S->RowAt = Now;
        return false;
    }));
    return true;
}
#endif
