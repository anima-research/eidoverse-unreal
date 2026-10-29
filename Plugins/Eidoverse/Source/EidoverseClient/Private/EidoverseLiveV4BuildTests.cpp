// Eidoverse.LiveV4Build (T11, slice C; ADAPTER-DESIGN §3.5, §5): the build and world panels' writes through the local bridge, in a game
// session against a current-protocol world (Tools/eidoverse/run_v4_build.sh: EIDO_LIVE_V4_URL, EIDO_LIVE_V4_WORLD; the client joins as its owner).
// Each row drives what a button or key does (HUD BuildAction / WorldAction / SetComponentJson / a chat command) and waits for the world to show it.
#include "EidoverseBridge.h"
#include "EidoverseScene.h"
#include "EidoverseUI.h"
#include "Dom/JsonObject.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"

#if WITH_DEV_AUTOMATION_TESTS
namespace
{
    const FString V4Lamp = TEXT("eidoverse/assets/models/lamp.glb");
    struct FV4Build
    {
        TWeakObjectPtr<UWorld> World;
        TWeakObjectPtr<AEidoverseBridge> Bridge;
        int32 Step = 0, Row = 0, Frames = 0, Failures = 0, Lamps = 0;
        double StepAt = 0, RowAt = 0, Mark = 0, LastAsk = 0;
        bool bStarted = false;
        FString Thing, ErrorBefore;
        TArray<FString> Report;
    };
    bool V4Pose(AEidoverseBridge* B, const FString& Id, FVector& P, double& Y, double& S)
    {
        AEidoverseScene* Scene = B ? B->GetScene() : nullptr; FString Lib; bool bAnimated = false;
        return Scene && !Id.IsEmpty() && Scene->GetEntityPose(Id, P, Y, S, Lib, bAnimated);
    }
    int32 V4Count(AEidoverseBridge* B, TFunctionRef<bool(const FEidoverseSceneItem&)> Pred)
    {
        int32 N = 0; if (AEidoverseScene* Scene = B ? B->GetScene() : nullptr) for (const auto& P : Scene->GetItems()) if (Pred(P.Value)) ++N; return N;
    }
    FString V4InspectedString(AEidoverseBridge* B, const FString& Id, const TCHAR* Comp)
    {
        const TSharedPtr<FJsonObject>* Bag = nullptr; FString S;
        if (B->InspectedId == Id && B->InspectReply && B->InspectReply->TryGetObjectField(TEXT("comp"), Bag)) (*Bag)->TryGetStringField(Comp, S);
        return S;
    }
    bool V4Inspected(AEidoverseBridge* B, const FString& Id) { const TSharedPtr<FJsonObject>* Bag = nullptr; return B->InspectedId == Id && B->InspectReply && B->InspectReply->TryGetObjectField(TEXT("comp"), Bag); }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEidoverseLiveV4Build, "Eidoverse.LiveV4Build", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FEidoverseLiveV4Build::RunTest(const FString&)
{
    const FString Url = FPlatformMisc::GetEnvironmentVariable(TEXT("EIDO_LIVE_V4_URL"));
    const FString WorldName = FPlatformMisc::GetEnvironmentVariable(TEXT("EIDO_LIVE_V4_WORLD"));
    const FString Owner = FPlatformMisc::GetEnvironmentVariable(TEXT("EIDO_LIVE_V4_OWNER"));
    if (Url.IsEmpty() || WorldName.IsEmpty() || Owner.IsEmpty()) { AddInfo(TEXT("live check skipped: run Tools/eidoverse/run_v4_build.sh (-game, a scratch server)")); return true; }
    if (!Url.StartsWith(TEXT("ws://127.0.0.1:"))) { AddError(TEXT("loopback servers only")); return false; }
    UWorld* Game = nullptr;
    for (const FWorldContext& C : GEngine->GetWorldContexts()) if (C.WorldType == EWorldType::Game && C.World()) Game = C.World();
    if (!Game) { AddError(TEXT("needs a game session (-game)")); return false; }
    TSharedPtr<FV4Build> S = MakeShared<FV4Build>();
    S->World = Game; S->StepAt = FPlatformTime::Seconds();

    ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this, S, Url, WorldName, Owner]() -> bool {
        const double Now = FPlatformTime::Seconds(), In = Now - S->StepAt;
        UWorld* W = S->World.Get(); if (!W) { AddError(TEXT("game world gone")); return true; }
        AEidoverseBridge* B = S->Bridge.Get();
        ++S->Frames;
        if (S->Step == 0)
        {
            if (!B) for (TActorIterator<AEidoverseBridge> It(W); It; ++It) { S->Bridge = *It; B = *It; }
            if (!B || !B->HUD) { if (In > 30) { AddError(TEXT("no bridge or HUD")); return true; } return false; }
            if (B->bJoined) { B->Disconnect(); return false; }
            B->bLoadRemoteAvatars = false; B->bUseBridge = true; B->bRenderDynamicWorld = true;
            B->JoinFromDoor(Url, WorldName, Owner);
            S->Step = 1; S->StepAt = Now; return false;
        }
        if (S->Step == 1)
        {
            if (B->bJoined && B->bBridgeReady && B->GetScene() && B->CanBuild() && V4Count(B, [](const FEidoverseSceneItem&) { return true; }) >= 2)
            { S->Step = 2; S->StepAt = Now; S->RowAt = Now; S->bStarted = false; S->Report.Add(FString::Printf(TEXT("     joined %s through the bridge as %s (role %s)"), *WorldName, *B->AssignedIdentity, *B->WorldRole)); return false; }
            if (In > 60) { AddError(TEXT("never joined through the bridge able to build: ") + B->ConnectionStatus); return true; }
            return false;
        }
        UEidoverseHUD* H = B->HUD;
        const double RowIn = Now - S->RowAt;
        // a row: Start once, then Check until it holds or its time is up; each row is one behaviour, reported ok or FAIL
        auto Row = [&](const TCHAR* What, double Limit, TFunctionRef<void()> Start, TFunctionRef<bool()> Check) -> bool
        {
            if (!S->bStarted) { S->bStarted = true; S->ErrorBefore = B->LastErrorText; S->Mark = B->LastErrorAt; Start(); return false; }
            const bool bOk = Check();
            if (!bOk && RowIn < Limit) return false;
            S->Report.Add(FString::Printf(TEXT("%s %s (%.1f s)"), bOk ? TEXT("ok  ") : TEXT("FAIL"), What, RowIn));
            UE_LOG(LogTemp, Display, TEXT("EIDOVERSE_V4_BUILD %s %s"), bOk ? TEXT("ok") : TEXT("FAIL"), What);
            if (!bOk) ++S->Failures;
            ++S->Row; S->RowAt = Now; S->bStarted = false; return true;
        };
        auto Ask = [&](const FString& Id) { if (Now - S->LastAsk > 1) { S->LastAsk = Now; B->RequestInspect(Id); } };
        FVector P; double Y = 0, Sc = 1;
        switch (S->Row)
        {
        case 0:   // build:spawn → create {model, transform} with a $b<n>; the reply's id is the one the scene draws
            Row(TEXT("spawn makes a thing, and its placeholder becomes the minted id the scene draws"), 15,
                [&]() { S->Lamps = V4Count(B, [](const FEidoverseSceneItem& E) { return E.Lib == V4Lamp; }); H->BuildAction(TEXT("build:spawn:") + V4Lamp); },
                [&]() { const FString Id = H->GetLastSpawnedEntity(); const FEidoverseSceneItem* E = B->GetScene()->GetItems().Find(Id);
                        if (E && E->Lib == V4Lamp && !Id.StartsWith(TEXT("$"))) { S->Thing = Id; return true; } return false; });
            return false;
        case 1:   // build:raise → setTransform {id, patch}
            Row(TEXT("raise moves it up 0.25 m"), 10,
                [&]() { H->SelectEntity(S->Thing); V4Pose(B, S->Thing, P, Y, Sc); S->Mark = P.Y; H->BuildAction(TEXT("build:raise:0.25")); },
                [&]() { return V4Pose(B, S->Thing, P, Y, Sc) && FMath::Abs(P.Y - (S->Mark + .25)) < .02; });
            return false;
        case 2:   // build:undo → the inverse setTransform
            Row(TEXT("undo puts it back down"), 10,
                [&]() { V4Pose(B, S->Thing, P, Y, Sc); S->Mark = P.Y - .25; H->BuildAction(TEXT("build:undo")); },
                [&]() { return V4Pose(B, S->Thing, P, Y, Sc) && FMath::Abs(P.Y - S->Mark) < .02; });
            return false;
        case 3:   // the inspector: set {component, value}, then inspect reads it back
            Row(TEXT("the inspector sets a component, and inspect reads it back"), 12,
                [&]() { H->SetComponentJson(S->Thing, TEXT("label"), TEXT("\"v4 lamp\"")); },
                [&]() { Ask(S->Thing); return V4InspectedString(B, S->Thing, TEXT("label")) == TEXT("v4 lamp"); });
            return false;
        case 4:   // the inspector's empty value: delete {component}
            Row(TEXT("the inspector takes a component off"), 12,
                [&]() { H->SetComponentJson(S->Thing, TEXT("label"), FString()); },
                [&]() { Ask(S->Thing); return V4Inspected(B, S->Thing) && V4InspectedString(B, S->Thing, TEXT("label")).IsEmpty() && !V4InspectedString(B, S->Thing, TEXT("model")).IsEmpty(); });
            return false;
        case 5:   // a refusal is the authority's, shown as it said it
            Row(TEXT("a write to a thing that isn't there is refused in the authority's words"), 10,
                [&]() { H->SetComponentJson(TEXT("no-such-thing"), TEXT("label"), TEXT("\"x\"")); },
                [&]() { return B->LastErrorAt > S->Mark && B->LastErrorText.Contains(TEXT("no-such-thing")); });
            return false;
        case 6:   // build:remove → delete {id}
            Row(TEXT("remove deletes it"), 10,
                [&]() { H->SelectEntity(S->Thing); H->BuildAction(TEXT("build:remove")); },
                [&]() { return !B->GetScene()->GetItems().Contains(S->Thing); });
            return false;
        case 7:   // undo of a remove → create it again (a new id)
            Row(TEXT("undo of the remove makes it again"), 12,
                [&]() { H->BuildAction(TEXT("build:undo")); },
                [&]() { return V4Count(B, [](const FEidoverseSceneItem& E) { return E.Lib == V4Lamp; }) == S->Lamps + 1; });
            return false;
        case 8:   // + new building → create {model, components: {structure}}; selected once the minted thing is drawn
            Row(TEXT("+ new building makes a structure and selects it"), 15,
                [&]() { H->ShowFrame("build", true); H->BuildAction(TEXT("struct:new")); },
                [&]() { const FString Id = H->GetSelectedEntity(); const FEidoverseSceneItem* E = B->GetScene()->GetItems().Find(Id); return E && E->bStructure && !Id.StartsWith(TEXT("$")); });
            return false;
        case 9:   // a light from the palette's ghost → create {components: {light}}
            Row(TEXT("the light tile's ghost places a light"), 15,
                [&]() { S->Lamps = V4Count(B, [](const FEidoverseSceneItem& E) { return E.Light.IsValid(); }); H->SelectEntity(FString()); H->BuildAction(TEXT("build:hold:@light")); },
                [&]() { if (H->HasGhost()) H->PlaceGhost(); return V4Count(B, [](const FEidoverseSceneItem& E) { return E.Light.IsValid(); }) > S->Lamps; });
            return false;
        case 10:   // the sky panel's apply → sky {…}
            Row(TEXT("the sky panel's apply changes the sky"), 12,
                [&]() { H->CancelGhost(); H->SelectWorldTab(2); double Hours = 12; if (B->WorldSky) B->WorldSky->TryGetNumberField(TEXT("hours"), Hours);
                        S->Mark = FMath::Fmod(Hours + 3 + 24, 24.0); H->WorldAction(TEXT("sky:h3")); H->WorldAction(TEXT("sky:apply")); },
                [&]() { double Hours = -1; return B->WorldSky && B->WorldSky->TryGetNumberField(TEXT("hours"), Hours) && FMath::Abs(Hours - S->Mark) < .01; });
            return false;
        case 11:   // a moderation command naming nobody here: answered by the bridge, nothing goes up
            Row(TEXT("/kick someone unknown is answered: who is that?"), 8,
                [&]() { B->HandleInteractionCommand(TEXT("/kick zed-nobody")); },
                [&]() { return B->LastErrorAt > S->Mark && B->LastErrorText.StartsWith(TEXT("who is that?")); });
            return false;
        default:
            for (const FString& L : S->Report) AddInfo(L);
            if (S->Failures) AddError(FString::Printf(TEXT("%d of %d build/world rows failed"), S->Failures, S->Row));
            B->Disconnect();
            return true;
        }
    }));
    return true;
}
#endif
