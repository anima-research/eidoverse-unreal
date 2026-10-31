// Eidoverse.LiveV4Scene (slice A, T11): the world drawn from bridge/scene v2 follows the fold, and a thing my hand holds is drawn at my hand.
// Runs against Tools/eidoverse/run_v4_join.sh's scratch server (EIDO_V4_TESTS=Eidoverse.LiveV4Scene; Bridge/SLICES.md A).
#include "EidoverseBridge.h"
#include "EidoverseScene.h"
#include "Dom/JsonObject.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"

#if WITH_DEV_AUTOMATION_TESTS
namespace
{
    struct FLiveV4Scene
    {
        TWeakObjectPtr<UWorld> World;
        TWeakObjectPtr<AEidoverseBridge> Bridge;
        int32 Step = 0, Frames = 0, Items = 0;
        double StepAt = 0;
        FString Id, Grab, Drop;
        bool bCreated = false;
        FVector Left;
    };
    /** My hand as the bridge places a held thing: my feet (SendPose's) + HAND_Y (engine shared/protocol/physconst.js). */
    FVector HandOf(const APawn* P) { return P->GetActorLocation() - FVector(0, 0, 85) + AEidoverseBridge::FromWirePosition(FVector(0, 1.1, 0)); }
    TSharedRef<FJsonValue> Nums(std::initializer_list<double> V) { TArray<TSharedPtr<FJsonValue>> A; for (double N : V) A.Add(MakeShared<FJsonValueNumber>(N)); return MakeShared<FJsonValueArray>(A); }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEidoverseLiveV4Scene, "Eidoverse.LiveV4Scene", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FEidoverseLiveV4Scene::RunTest(const FString&)
{
    const FString Url = FPlatformMisc::GetEnvironmentVariable(TEXT("EIDO_LIVE_V4_URL"));
    const FString WorldName = FPlatformMisc::GetEnvironmentVariable(TEXT("EIDO_LIVE_V4_WORLD"));
    if (Url.IsEmpty() || WorldName.IsEmpty()) { AddInfo(TEXT("live check skipped: run Tools/eidoverse/run_v4_join.sh (-game, a scratch server)")); return true; }
    if (!Url.StartsWith(TEXT("ws://127.0.0.1:"))) { AddError(TEXT("loopback servers only")); return false; }
    UWorld* Game = nullptr;
    for (const FWorldContext& C : GEngine->GetWorldContexts()) if (C.WorldType == EWorldType::Game && C.World()) Game = C.World();
    if (!Game) { AddError(TEXT("needs a game session (-game)")); return false; }
    TSharedPtr<FLiveV4Scene> S = MakeShared<FLiveV4Scene>();
    S->World = Game; S->StepAt = FPlatformTime::Seconds();
    S->Items = FCString::Atoi(*FPlatformMisc::GetEnvironmentVariable(TEXT("EIDO_LIVE_V4_ITEMS")));

    ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this, S, Url, WorldName]() -> bool {
        const double Now = FPlatformTime::Seconds(), In = Now - S->StepAt;
        UWorld* W = S->World.Get(); if (!W) { AddError(TEXT("game world gone")); return true; }
        AEidoverseBridge* B = S->Bridge.Get();
        APawn* Me = UGameplayStatics::GetPlayerPawn(W, 0);
        AEidoverseScene* Scene = B ? B->GetScene() : nullptr;
        AActor* Thing = Scene && !S->Id.IsEmpty() ? Scene->EntityActor(S->Id) : nullptr;
        ++S->Frames;
        auto Next = [&]() { ++S->Step; S->StepAt = Now; S->Frames = 0; };
        auto Late = [&](double Limit) { return In > Limit && S->Frames > 30; };
        auto Ask = [&](const FString& Verb, const TSharedRef<FJsonObject>& Args, TFunction<void(const TSharedPtr<FJsonObject>&)> Done) {
            if (B->Request(Verb, Args, Done).IsEmpty()) AddError(TEXT("couldn't send ") + Verb);
        };
        switch (S->Step)
        {
        case 0:   // the client map's bridge, through the local bridge
            if (!B) for (TActorIterator<AEidoverseBridge> It(W); It; ++It) { S->Bridge = *It; B = *It; }
            if (!B || !B->HUD) { if (In > 30) { AddError(TEXT("no bridge or HUD")); return true; } return false; }
            if (B->bJoined) { B->Disconnect(); return false; }
            B->bLoadRemoteAvatars = false; B->bReconnect = false; B->bUseBridge = true;
            B->JoinFromDoor(Url, WorldName, TEXT("unrealscene"));
            Next(); return false;
        case 1:   // the seeded things, drawn from the bridge's fold
            if (B->bJoined && B->bBridgeReady && Scene && Scene->GetItems().Num() >= S->Items && Me)
            {
                AddInfo(FString::Printf(TEXT("the seeded scene: %d things (%s)"), Scene->GetItems().Num(), *B->WorldRenderStatus));
                auto Move = MakeShared<FJsonObject>(); Move->SetField(TEXT("to"), Nums({0, 0, 0}));
                Ask(TEXT("move"), Move, [](const TSharedPtr<FJsonObject>&) {});
                Next(); return false;
            }
            if (Late(40)) { AddError(TEXT("the seeded scene never showed: ") + B->WorldRenderStatus); return true; }
            return false;
        case 2:   // row 1: a thing made after the first scene is drawn (the fold moved; nothing else in this world moves)
            if (S->Frames == 30)
            {
                auto C = MakeShared<FJsonObject>(), T = MakeShared<FJsonObject>();
                T->SetField(TEXT("pos"), Nums({0.4, 0, 0.4}));
                C->SetObjectField(TEXT("transform"), T); C->SetObjectField(TEXT("grabbable"), MakeShared<FJsonObject>()); C->SetStringField(TEXT("label"), TEXT("scene test jar"));
                auto A = MakeShared<FJsonObject>(); A->SetStringField(TEXT("model"), TEXT("eidoverse/assets/models/lamp.glb")); A->SetObjectField(TEXT("components"), C);
                Ask(TEXT("create"), A, [S](const TSharedPtr<FJsonObject>& R) {
                    const TArray<TSharedPtr<FJsonValue>>* Refs = nullptr; bool bOk = false;
                    if (R && R->TryGetBoolField(TEXT("ok"), bOk) && bOk && R->TryGetArrayField(TEXT("refs"), Refs) && Refs->Num() && (*Refs)[0]->AsObject()) S->Id = (*Refs)[0]->AsObject()->GetStringField(TEXT("id"));
                    S->bCreated = true;
                });
            }
            if (S->bCreated && S->Id.IsEmpty()) { AddError(TEXT("the authority refused the create")); return true; }
            if (Thing)
            {
                AddInfo(TEXT("drawn after the first scene: ") + S->Id);
                auto G = MakeShared<FJsonObject>(); G->SetStringField(TEXT("id"), S->Id);
                Ask(TEXT("grab"), G, [S](const TSharedPtr<FJsonObject>& R) { bool bOk = false; const TSharedPtr<FJsonObject>* No = nullptr; FString Why = TEXT("no reply");
                    if (R && R->TryGetObjectField(TEXT("refused"), No)) (*No)->TryGetStringField(TEXT("reason"), Why);
                    S->Grab = R && R->TryGetBoolField(TEXT("ok"), bOk) && bOk ? FString(TEXT("ok")) : Why; });
                Next(); return false;
            }
            if (Late(15)) { AddError(FString::Printf(TEXT("a thing made after the first scene was never drawn (%s): %s"), *S->Id, *B->WorldRenderStatus)); return true; }
            return false;
        case 3:   // row 2: held by me, it is drawn at my hand
            if (!S->Grab.IsEmpty() && S->Grab != TEXT("ok")) { AddError(TEXT("the authority refused the grab: ") + S->Grab); return true; }
            if (S->Grab == TEXT("ok") && Thing && Me && FVector::Dist(Thing->GetActorLocation(), HandOf(Me)) < 10)
            {
                AddInfo(TEXT("held, at my hand"));
                Me->SetActorLocation(Me->GetActorLocation() + FVector(300, 0, 0), false, nullptr, ETeleportType::TeleportPhysics);
                Next(); return false;
            }
            if (Late(10)) { AddError(FString::Printf(TEXT("the held thing isn't at my hand: %.0f cm off (grab %s)"), Thing && Me ? FVector::Dist(Thing->GetActorLocation(), HandOf(Me)) : -1., *S->Grab)); return true; }
            return false;
        case 4:   // row 2: and follows my hand at frame rate, not at the scene's poll
            if (Thing && Me && FVector::Dist(Thing->GetActorLocation(), HandOf(Me)) < 10)
            {
                AddInfo(FString::Printf(TEXT("followed my hand within %d frames"), S->Frames));
                auto D = MakeShared<FJsonObject>(); D->SetStringField(TEXT("id"), S->Id);
                Ask(TEXT("drop"), D, [S](const TSharedPtr<FJsonObject>& R) { bool bOk = false; S->Drop = R && R->TryGetBoolField(TEXT("ok"), bOk) && bOk ? TEXT("ok") : TEXT("refused"); });
                Next(); return false;
            }
            if (S->Frames > 4) { AddError(FString::Printf(TEXT("the held thing didn't follow my hand: %.0f cm off after %d frames"), Thing && Me ? FVector::Dist(Thing->GetActorLocation(), HandOf(Me)) : -1., S->Frames)); return true; }
            return false;
        case 5:   // row 3: let go, it is the scene's again and no longer follows me
            if (!S->Drop.IsEmpty() && S->Drop != TEXT("ok")) { AddError(TEXT("the authority refused the drop")); return true; }
            if (S->Drop == TEXT("ok") && In > 2 && Thing && Me)
            {
                S->Left = Thing->GetActorLocation();
                Me->SetActorLocation(Me->GetActorLocation() - FVector(300, 0, 0), false, nullptr, ETeleportType::TeleportPhysics);
                Next(); return false;
            }
            if (Late(10)) { AddError(TEXT("no answer to the drop")); return true; }
            return false;
        case 6:
            if (S->Frames < 20) return false;
            TestTrue(FString::Printf(TEXT("let go, it stays where it was left (moved %.0f cm, %.0f cm from my hand)"), Thing ? FVector::Dist(Thing->GetActorLocation(), S->Left) : -1., Thing && Me ? FVector::Dist(Thing->GetActorLocation(), HandOf(Me)) : -1.),
                Thing && Me && FVector::Dist(Thing->GetActorLocation(), S->Left) < 10 && FVector::Dist(Thing->GetActorLocation(), HandOf(Me)) > 100);
            UE_LOG(LogTemp, Display, TEXT("EIDOVERSE_V4_SCENE ok world=%s thing=%s"), *WorldName, *S->Id);
            B->Disconnect();
            return true;
        default: return true;
        }
    }));
    return true;
}
#endif
