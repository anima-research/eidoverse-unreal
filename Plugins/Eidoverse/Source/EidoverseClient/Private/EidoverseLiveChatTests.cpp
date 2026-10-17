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
#endif
