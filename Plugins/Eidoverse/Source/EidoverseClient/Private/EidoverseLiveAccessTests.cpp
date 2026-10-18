// Eidoverse.LiveAccess: world access (#258) through the real client in a game session, against a scratch sequencer
// and Tools/eidoverse/access_bot.ts (run_game_tests.sh starts both, with a scratch identity issuer for the bot).
// I found a world under a typed name and a guest walks in: the World panel's access tab shows the open door and the
// guest, here and not listed; "visitors" sets the door from the tab; "members only" asks first (the guest would have
// to leave), then the server refuses it -- a typed name can't hold a closed world's door -- and the refusal shows in
// the tab; /grant guest visitor for 1h lists them with an end, /revoke takes it back. Then the signed-in keeper's
// worlds: its members-only world refuses me at the door with 4010, and when it closes the world I'm standing in, I'm
// removed with 4010; neither retries.
#include "EidoverseBridge.h"
#include "EidoverseUI.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Misc/AutomationTest.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Tests/AutomationCommon.h"

#if WITH_DEV_AUTOMATION_TESTS
namespace
{
    struct FLiveAccess : TSharedFromThis<FLiveAccess>
    {
        TWeakObjectPtr<UWorld> World;
        TWeakObjectPtr<AEidoverseBridge> Bridge;
        FString Control, Status;
        int32 Step = 0, StepFrames = 0; double StepAt = 0;
        bool bHttpBusy = false; TSharedPtr<FJsonObject> Report;
        TArray<FString> Lines; int32 Failures = 0;
        void Http(const FString& Verb, const FString& Path)
        {
            bHttpBusy = true; Report.Reset();
            auto Req = FHttpModule::Get().CreateRequest(); Req->SetVerb(Verb); Req->SetURL(Control + Path);
            Req->OnProcessRequestComplete().BindLambda([Self = AsWeak()](FHttpRequestPtr, FHttpResponsePtr R, bool bOk)
            {
                if (const TSharedPtr<FLiveAccess> L = Self.Pin())
                { if (bOk && R) FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(R->GetContentAsString()), L->Report); L->bHttpBusy = false; }
            });
            Req->ProcessRequest();
        }
        void Check(bool bOk, const FString& What) { Lines.Add(FString::Printf(TEXT("%s %s"), bOk ? TEXT("ok  ") : TEXT("FAIL"), *What)); if (!bOk) ++Failures; }
    };
    FString Door(const AEidoverseBridge* B) { FString D; if (B->AccessState) B->AccessState->TryGetStringField(TEXT("door"), D); return D; }
    const TSharedPtr<FJsonObject> Entry(const AEidoverseBridge* B, const TCHAR* Field, const FString& Id)
    {
        const TArray<TSharedPtr<FJsonValue>>* A = nullptr;
        if (B->AccessState && B->AccessState->TryGetArrayField(Field, A))
            for (const auto& V : *A) if (const TSharedPtr<FJsonObject> O = V->AsObject()) { FString S; O->TryGetStringField(TEXT("id"), S); if (S == Id) return O; }
        return nullptr;
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEidoverseLiveAccess, "Eidoverse.LiveAccess", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FEidoverseLiveAccess::RunTest(const FString&)
{
    const FString Url = FPlatformMisc::GetEnvironmentVariable(TEXT("EIDO_LIVE_BUILD_URL"));
    const FString OpenWorld = FPlatformMisc::GetEnvironmentVariable(TEXT("EIDO_LIVE_ACCESS_WORLD"));
    const FString ClosedWorld = FPlatformMisc::GetEnvironmentVariable(TEXT("EIDO_LIVE_ACCESS_CLOSED"));
    const FString KeptWorld = FPlatformMisc::GetEnvironmentVariable(TEXT("EIDO_LIVE_ACCESS_KEPT"));
    const FString Control = FPlatformMisc::GetEnvironmentVariable(TEXT("EIDO_LIVE_ACCESS_CONTROL"));
    if (Url.IsEmpty() || OpenWorld.IsEmpty() || ClosedWorld.IsEmpty() || KeptWorld.IsEmpty() || Control.IsEmpty())
    { AddInfo(TEXT("live check skipped: run Tools/eidoverse/run_game_tests.sh (-game, a scratch sequencer, access_bot.ts)")); return true; }
    if (!Url.StartsWith(TEXT("ws://127.0.0.1:")) || !Control.StartsWith(TEXT("http://127.0.0.1:"))) { AddError(TEXT("loopback servers only")); return false; }
    UWorld* Game = nullptr;
    for (const FWorldContext& C : GEngine->GetWorldContexts()) if (C.WorldType == EWorldType::Game && C.World()) Game = C.World();
    if (!Game) { AddError(TEXT("needs a game session (-game)")); return false; }
    TSharedPtr<FLiveAccess> S = MakeShared<FLiveAccess>();
    S->World = Game; S->Control = Control; S->StepAt = FPlatformTime::Seconds();

    ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this, S, Url, OpenWorld, ClosedWorld, KeptWorld]() -> bool {
        const double Now = FPlatformTime::Seconds(), In = Now - S->StepAt;
        UWorld* W = S->World.Get(); if (!W) { AddError(TEXT("game world gone")); return true; }
        AEidoverseBridge* B = S->Bridge.Get();
        ++S->StepFrames;
        auto Next = [&]() { ++S->Step; S->StepAt = Now; S->StepFrames = 0; };
        // leave the session as found for the next game test: the World panel closed
        auto Done = [&]() { if (B && B->HUD) B->HUD->ShowFrame("world", false); for (const FString& L : S->Lines) AddInfo(L); if (S->Failures) AddError(FString::Printf(TEXT("%d access checks failed"), S->Failures)); return true; };
        // wall time AND frames: one long hitch on a loaded machine (seen: 11 s) must not use up a step's whole wait
        auto Timeout = [&](double Limit, const FString& What) { if (In < Limit || S->StepFrames < 30) return false; S->Check(false, What + TEXT(" (timed out)")); return true; };
        switch (S->Step)
        {
        case 0:   // the client map's bridge; I found the open world
            if (!B) for (TActorIterator<AEidoverseBridge> It(W); It; ++It) { S->Bridge = *It; B = *It; }
            if (!B || !B->HUD) { if (In > 30) { AddError(TEXT("no bridge or HUD")); return true; } return false; }
            if (B->bJoined) { B->Disconnect(); return false; }   // a previous test's world: start from the door
            B->bLoadRemoteAvatars = false;
            B->JoinFromDoor(Url, OpenWorld, TEXT("accessowner"));
            Next(); return false;
        case 1:   // its owner; the guest walks in
            if (!B->bJoined || !B->IsOwner()) { if (Timeout(30, TEXT("founded the world as its owner"))) return Done(); return false; }
            S->Http(TEXT("POST"), TEXT("/guest")); Next(); return false;
        case 2:   // the access tab: the door is open, the guest here and not listed; "visitors" from the tab
            if (!B->FindPeer(TEXT("guest"))) { if (Timeout(20, TEXT("the guest arrived"))) return Done(); return false; }
            B->HUD->ShowFrame("world", true); B->HUD->WorldAction(TEXT("wtab:5"));
            if (Door(B) != TEXT("builder") || !Entry(B, TEXT("here"), TEXT("guest"))) { if (Timeout(10, TEXT("the access tab shows the open door and the guest, here and not listed"))) return Done(); return false; }
            S->Check(true, TEXT("the access tab shows the open door and the guest, here and not listed"));
            B->HUD->AccessAction(TEXT("door:visitor"));
            Next(); return false;
        case 3:   // visitors only; then members only: the first click only asks, the second sends
            if (Door(B) != TEXT("visitor")) { if (Timeout(10, TEXT("\"visitors\" sets the door from the tab"))) return Done(); return false; }
            S->Check(true, TEXT("\"visitors\" sets the door from the tab"));
            B->HUD->AccessAction(TEXT("door:none"));
            S->Check(B->AccessError.IsEmpty() && Door(B) == TEXT("visitor"), TEXT("members only, first click: asks (the guest would have to leave)"));
            B->HUD->AccessAction(TEXT("door:none"));
            Next(); return false;
        case 4:   // a typed-name owner can't close the door: the server's reason shows in the tab
            if (!B->AccessError.Contains(TEXT("sign in first"))) { if (Timeout(10, TEXT("members only is refused for a typed name, the reason in the tab"))) return Done(); return false; }
            S->Check(Door(B) == TEXT("visitor") && B->FindPeer(TEXT("guest")) != nullptr, TEXT("members only is refused for a typed name: ") + B->AccessError);
            B->HandleInteractionCommand(TEXT("/grant guest visitor for 1h"));
            Next(); return false;
        case 5:   // a timed grant: listed, with its end
        {
            const TSharedPtr<FJsonObject> G = Entry(B, TEXT("members"), TEXT("guest"));
            double Until = 0, ServerNow = 0; if (G) G->TryGetNumberField(TEXT("until"), Until); if (B->AccessState) B->AccessState->TryGetNumberField(TEXT("now"), ServerNow);
            if (!G || Until <= 0) { if (Timeout(10, TEXT("/grant guest visitor for 1h lists them with an end"))) return Done(); return false; }
            S->Check(FMath::Abs(Until - ServerNow - 3600000) < 300000, FString::Printf(TEXT("/grant guest visitor for 1h lists them as %s, %.0f min left"), *G->GetStringField(TEXT("role")), (Until - ServerNow) / 60000));
            B->HandleInteractionCommand(TEXT("/revoke guest"));
            Next(); return false;
        }
        case 6:   // revoked: off the list; the door back open
            if (Entry(B, TEXT("members"), TEXT("guest"))) { if (Timeout(10, TEXT("/revoke guest takes them off the list"))) return Done(); return false; }
            S->Check(true, TEXT("/revoke guest takes them off the list"));
            B->HUD->AccessAction(TEXT("door:builder"));
            Next(); return false;
        case 7:
            if (Door(B) != TEXT("builder")) { if (Timeout(10, TEXT("\"open\" from the tab opens the door again"))) return Done(); return false; }
            S->Check(true, TEXT("\"open\" from the tab opens the door again"));
            B->TravelTo(ClosedWorld);
            Next(); return false;
        case 8:   // the keeper's members-only world refuses me at the door
            if (B->bJoined || !B->ConnectionStatus.Contains(TEXT("not in"))) { if (Timeout(15, TEXT("travelling to a members-only world is refused at the door"))) return Done(); return false; }
            S->Status = B->ConnectionStatus;
            S->Check(true, TEXT("travelling to a members-only world is refused: ") + S->Status);
            Next(); return false;
        case 9:   // and stays refused: no reconnect loop; then into the keeper's open world
            if (In < 4) return false;
            S->Check(!B->bJoined && B->ConnectionStatus == S->Status, TEXT("no retry after 4010 at the door (status unchanged after 4 s)"));
            B->JoinFromDoor(Url, KeptWorld, TEXT("accessowner"));
            Next(); return false;
        case 10:  // in; the keeper closes it with me inside
            if (!B->bJoined || B->WorldName != KeptWorld) { if (Timeout(20, TEXT("joined the keeper's open world"))) return Done(); return false; }
            S->Check(!B->IsOwner(), TEXT("joined the keeper's open world, not as its owner"));
            S->Http(TEXT("POST"), TEXT("/close-open"));
            Next(); return false;
        case 11:  // removed with 4010
            if (B->bJoined || !B->ConnectionStatus.Contains(TEXT("not in"))) { if (Timeout(15, TEXT("closing the world I'm in removes me"))) return Done(); return false; }
            S->Status = B->ConnectionStatus;
            S->Check(true, TEXT("closing the world I'm in removes me: ") + S->Status);
            Next(); return false;
        case 12:
            if (In < 4) return false;
            S->Check(!B->bJoined && B->ConnectionStatus == S->Status, TEXT("no retry after being removed with 4010 (status unchanged after 4 s)"));
            return Done();
        }
        return true;
    }));
    return true;
}
#endif
