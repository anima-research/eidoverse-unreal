// Eidoverse.LiveChat: the real bridge against a scratch sequencer and two bots (Tools/eidoverse/
// run_chat_live_test.sh starts them and sets EIDO_LIVE_CHAT_*; without them the test only says how to run it).
//
//   1. scrollback: the arrival window keeps a quiet line above a hole of trimmed loud talk; walking back from
//      the present fills the hole (chat.js loadOlder), every line once, in seq order;
//   2. attention: a live mention and a whisper each reach the gate's player once; my arrival carries my
//      page session;
//   3. resync: a dropped connection held while 60 lines are said re-joins to a window missing most of them;
//      the resync after the last delivered seq slots them in, in order, once each.
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
        FString Control;
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
            S->Close(); return true;
        }
        }
        return true;
    }));
    return true;
}
#endif
