// Eidoverse.LiveV4Join (T0, ADAPTER-DESIGN §5): a world of the current protocol refuses this client's own join (4005) and
// admits it through the local bridge as a replica: snapshot, bridge/ready, then the scene the bridge folds. Runs in a game
// session against Tools/eidoverse/run_v4_join.sh's scratch server (EIDO_LIVE_V4_URL, EIDO_LIVE_V4_WORLD, EIDO_LIVE_V4_ITEMS; EIDO_LIVE_V4_SHOT=0: no screenshot).
#include "EidoverseBridge.h"
#include "EidoverseScene.h"
#include "EidoverseUI.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "UnrealClient.h"
#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"

#if WITH_DEV_AUTOMATION_TESTS
namespace
{
    struct FLiveV4Join
    {
        TWeakObjectPtr<UWorld> World;
        TWeakObjectPtr<AEidoverseBridge> Bridge;
        int32 Step = 0, Frames = 0, Items = 0;
        double StepAt = 0;
        FString DirectRefusal;
    };
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEidoverseLiveV4Join, "Eidoverse.LiveV4Join", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FEidoverseLiveV4Join::RunTest(const FString&)
{
    const FString Url = FPlatformMisc::GetEnvironmentVariable(TEXT("EIDO_LIVE_V4_URL"));
    const FString WorldName = FPlatformMisc::GetEnvironmentVariable(TEXT("EIDO_LIVE_V4_WORLD"));
    if (Url.IsEmpty() || WorldName.IsEmpty()) { AddInfo(TEXT("live check skipped: run Tools/eidoverse/run_v4_join.sh (-game, a scratch server)")); return true; }
    if (!Url.StartsWith(TEXT("ws://127.0.0.1:"))) { AddError(TEXT("loopback servers only")); return false; }
    UWorld* Game = nullptr;
    for (const FWorldContext& C : GEngine->GetWorldContexts()) if (C.WorldType == EWorldType::Game && C.World()) Game = C.World();
    if (!Game) { AddError(TEXT("needs a game session (-game)")); return false; }
    TSharedPtr<FLiveV4Join> S = MakeShared<FLiveV4Join>();
    S->World = Game; S->StepAt = FPlatformTime::Seconds();
    S->Items = FCString::Atoi(*FPlatformMisc::GetEnvironmentVariable(TEXT("EIDO_LIVE_V4_ITEMS")));

    ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this, S, Url, WorldName]() -> bool {
        const double Now = FPlatformTime::Seconds(), In = Now - S->StepAt;
        UWorld* W = S->World.Get(); if (!W) { AddError(TEXT("game world gone")); return true; }
        AEidoverseBridge* B = S->Bridge.Get();
        ++S->Frames;
        auto Next = [&]() { ++S->Step; S->StepAt = Now; S->Frames = 0; };
        auto Late = [&](double Limit) { return In > Limit && S->Frames > 30; };
        switch (S->Step)
        {
        case 0:   // the client map's bridge, offline
            if (!B) for (TActorIterator<AEidoverseBridge> It(W); It; ++It) { S->Bridge = *It; B = *It; }
            if (!B || !B->HUD) { if (In > 30) { AddError(TEXT("no bridge or HUD")); return true; } return false; }
            if (B->bJoined) { B->Disconnect(); return false; }
            B->bLoadRemoteAvatars = false; B->bReconnect = false;
            B->bUseBridge = false; B->bBridgeAuto = false;   // the direct refusal itself, measured once
            B->JoinFromDoor(Url, WorldName, TEXT("unrealdirect"));
            Next(); return false;
        case 1:   // red without the bridge: the server refuses a join that doesn't fold (4005), and the client says why
            if (B->bJoined) { AddError(TEXT("the server admitted a join that doesn't fold: this isn't a current-protocol server")); return true; }
            if (B->ConnectionStatus.StartsWith(TEXT("couldn't join")))
            {
                S->DirectRefusal = B->ConnectionStatus;
                AddInfo(TEXT("without the bridge: ") + S->DirectRefusal);
                UE_LOG(LogTemp, Display, TEXT("EIDOVERSE_V4_DIRECT refused: %s"), *S->DirectRefusal);
                B->Disconnect(); B->bUseBridge = true; B->bReconnect = true;
                B->JoinFromDoor(Url, WorldName, TEXT("unrealv4"));
                Next(); return false;
            }
            if (Late(20)) { AddError(TEXT("the direct join was neither refused nor admitted: ") + B->ConnectionStatus); return true; }
            return false;
        case 2:   // through the bridge: the snapshot and bridge/ready (the cut folded)
            if (B->bJoined && B->bBridgeReady)
            {
                AddInfo(FString::Printf(TEXT("through the bridge: joined as %s, bridge/ready for %s (%s)"), *B->AssignedIdentity, *WorldName, *B->BridgeWorldId));
                Next(); return false;
            }
            if (Late(40)) { AddError(TEXT("no snapshot + bridge/ready through the bridge: ") + B->ConnectionStatus); return true; }
            return false;
        case 3:   // the world drawn from the bridge's fold: its things are in the scene
        {
            AEidoverseScene* Scene = B->GetScene();
            const int32 Have = Scene ? Scene->GetItems().Num() : -1;
            if (Scene && Have >= S->Items && B->WorldRenderStatus.Contains(TEXT("models loaded")))
            {
                UE_LOG(LogTemp, Display, TEXT("EIDOVERSE_V4_JOIN ok world=%s you=%s worldId=%s items=%d status=%s"), *WorldName, *B->AssignedIdentity, *B->BridgeWorldId, Have, *B->WorldRenderStatus);
                AddInfo(FString::Printf(TEXT("the scene from the bridge's fold: %d things (%s)"), Have, *B->WorldRenderStatus));
                if (FPlatformMisc::GetEnvironmentVariable(TEXT("EIDO_LIVE_V4_SHOT")) != TEXT("0")) FScreenshotRequest::RequestScreenshot(TEXT("eidoverse-v4-join.png"), false, false);
                Next(); return false;
            }
            if (Late(40)) { AddError(FString::Printf(TEXT("the scene never showed %d things (have %d): %s"), S->Items, Have, *B->WorldRenderStatus)); return true; }
            return false;
        }
        case 4:   // a few frames for the screenshot, then still joined (no close, no resync loop)
            if (S->Frames < 60) return false;
            TestTrue(TEXT("still joined through the bridge"), B->bJoined && B->bBridgeReady);
            TestEqual(TEXT("one bridge/ready (no resync loop)"), B->BridgeReadyCount, 1);
            B->Disconnect();
            B->bUseBridge = false; B->bBridgeAuto = true; B->bReconnect = true;   // the shipped defaults: nobody turned the bridge on
            B->JoinFromDoor(Url, WorldName, TEXT("unrealauto"));
            Next(); return false;
        case 5:   // with the defaults, the refused direct join goes on through the bridge by itself
            if (B->bJoined && B->bBridgeReady)
            {
                TestTrue(TEXT("the client turned the bridge on itself"), B->bUseBridge);
                AddInfo(TEXT("defaults: joined through the bridge by itself as ") + B->AssignedIdentity);
                UE_LOG(LogTemp, Display, TEXT("EIDOVERSE_V4_AUTO ok you=%s"), *B->AssignedIdentity);
                B->Disconnect();
                return true;
            }
            if (Late(60)) { AddError(TEXT("with the defaults, the client never reached the world through the bridge: ") + B->ConnectionStatus); return true; }
            return false;
        default: return true;
        }
    }));
    return true;
}
#endif
