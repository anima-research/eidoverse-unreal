// Eidoverse.LiveV4Bodies (T11, slice D: Bridge/SLICES.md): bodies, seats and access through the local bridge, in a game
// session against Tools/eidoverse/run_v4_bodies.sh's scratch current-protocol server and v4_bodies_bot.ts. Each row is
// checked and the run goes on, so one red row doesn't hide the rest (EIDO_LIVE_V4_URL, _WORLD, _BENCH, _LAMP, _GUEST).
#include "EidoverseBridge.h"
#include "EidoverseScene.h"
#include "EidoverseUI.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"

#if WITH_DEV_AUTOMATION_TESTS
namespace
{
    const TCHAR* DirectoryGap = TEXT("search by name isn't available on this server: pick someone here, or paste their identity (u-…)");
    struct FLiveV4Bodies
    {
        TWeakObjectPtr<UWorld> World;
        TWeakObjectPtr<AEidoverseBridge> Bridge;
        FString Bench, Lamp, Guest, GuestId;
        int32 Step = 0, Frames = 0, Serial = 0; double StepAt = 0, ErrorAt = 0;
        TArray<FString> Lines; int32 Failures = 0; bool bHeld = false, bSat = false, bSent = false;
        void Check(bool bOk, const FString& What) { Lines.Add(FString::Printf(TEXT("%s %s"), bOk ? TEXT("ok  ") : TEXT("FAIL"), *What)); if (!bOk) ++Failures; UE_LOG(LogTemp, Display, TEXT("EIDOVERSE_V4_BODIES %s %s"), bOk ? TEXT("ok") : TEXT("FAIL"), *What); }
    };
    FString AStr(const TSharedPtr<FJsonObject>& O, const TCHAR* K) { FString S; if (O) O->TryGetStringField(K, S); return S; }
    /** The access state's row in `Field` (members, here) whose `Key` is `Value`. */
    TSharedPtr<FJsonObject> Row(const AEidoverseBridge* B, const TCHAR* Field, const TCHAR* Key, const FString& Value)
    {
        const TArray<TSharedPtr<FJsonValue>>* A = nullptr;
        if (B->AccessStateWire && B->AccessStateWire->TryGetArrayField(Field, A))
            for (const auto& V : *A) if (const TSharedPtr<FJsonObject> O = V->AsObject()) if (AStr(O, Key) == Value) return O;
        return nullptr;
    }
    AEidoverseExplorer* ExplorerOf(UWorld* W) { return Cast<AEidoverseExplorer>(UGameplayStatics::GetPlayerPawn(W, 0)); }
    /** Stand the explorer at a wire spot and keep it there (a scratch world may have no ground under it). */
    void Hold(AEidoverseExplorer* E, const AEidoverseBridge* B, const FVector& Wire)
    {
        if (!E) return;
        if (UCharacterMovementComponent* M = E->GetCharacterMovement()) { M->SetMovementMode(MOVE_Flying); M->StopMovementImmediately(); }
        E->SetActorLocation(B->OriginCentimeters + AEidoverseBridge::FromWirePosition(Wire) + FVector(0, 0, 88), false, nullptr, ETeleportType::TeleportPhysics);
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEidoverseLiveV4Bodies, "Eidoverse.LiveV4Bodies", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FEidoverseLiveV4Bodies::RunTest(const FString&)
{
    const FString Url = FPlatformMisc::GetEnvironmentVariable(TEXT("EIDO_LIVE_V4_URL"));
    const FString WorldName = FPlatformMisc::GetEnvironmentVariable(TEXT("EIDO_LIVE_V4_WORLD"));
    TSharedPtr<FLiveV4Bodies> S = MakeShared<FLiveV4Bodies>();
    S->Bench = FPlatformMisc::GetEnvironmentVariable(TEXT("EIDO_LIVE_V4_BENCH"));
    S->Lamp = FPlatformMisc::GetEnvironmentVariable(TEXT("EIDO_LIVE_V4_LAMP"));
    S->Guest = FPlatformMisc::GetEnvironmentVariable(TEXT("EIDO_LIVE_V4_GUEST"));
    if (Url.IsEmpty() || WorldName.IsEmpty() || S->Bench.IsEmpty() || S->Lamp.IsEmpty() || S->Guest.IsEmpty())
    { AddInfo(TEXT("live check skipped: run Tools/eidoverse/run_v4_bodies.sh (-game, a scratch server, v4_bodies_bot.ts)")); return true; }
    if (!Url.StartsWith(TEXT("ws://127.0.0.1:"))) { AddError(TEXT("loopback servers only")); return false; }
    UWorld* Game = nullptr;
    for (const FWorldContext& C : GEngine->GetWorldContexts()) if (C.WorldType == EWorldType::Game && C.World()) Game = C.World();
    if (!Game) { AddError(TEXT("needs a game session (-game)")); return false; }
    S->World = Game; S->StepAt = FPlatformTime::Seconds();

    ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this, S, Url, WorldName]() -> bool {
        const double Now = FPlatformTime::Seconds(), In = Now - S->StepAt;
        UWorld* W = S->World.Get(); if (!W) { AddError(TEXT("game world gone")); return true; }
        AEidoverseBridge* B = S->Bridge.Get();
        AEidoverseExplorer* E = ExplorerOf(W);
        ++S->Frames;
        auto Next = [&]() { ++S->Step; S->StepAt = Now; S->Frames = 0; S->bSent = false; };
        auto Late = [&](double Limit) { return In > Limit && S->Frames > 10; };
        // a body act, retried until it goes (the client spaces them 0.25 s apart, as the main-era verbs were)
        auto Act = [&](TFunctionRef<bool()> Do) { if (!S->bSent && In > .3) S->bSent = Do(); };
        // wait for Done (true: the row passes), or give up after Limit seconds (the row fails); either way, on to the next step
        auto Expect = [&](bool bDone, double Limit, const FString& What) { if (bDone || Late(Limit)) { S->Check(bDone, What); Next(); return true; } return false; };
        const FVector NearBench(8, 0, 7);
        switch (S->Step)
        {
        case 0:
            if (!B) for (TActorIterator<AEidoverseBridge> It(W); It; ++It) { S->Bridge = *It; B = *It; }
            if (!B || !B->HUD) { if (In > 30) { AddError(TEXT("no bridge or HUD")); return true; } return false; }
            if (B->bJoined) { B->Disconnect(); return false; }
            B->bLoadRemoteAvatars = false; B->bReconnect = true; B->bUseBridge = true;
            B->JoinFromDoor(Url, WorldName, TEXT("unrealbodies"));
            Next(); return false;
        case 1:   // joined through the bridge, the bench and the lamp drawn
        {
            const bool bReady = B->bJoined && B->bBridgeReady && B->GetScene() && B->GetScene()->EntityActor(S->Bench) && B->GetScene()->EntityActor(S->Lamp);
            if (bReady || Late(60)) { if (!bReady) { AddError(TEXT("never joined with the bench and lamp drawn: ") + B->ConnectionStatus); return true; } Next(); }
            return false;
        }
        case 2:   // the bot makes me this world's owner
            if (B->WorldRole == TEXT("owner") || Late(20)) { if (B->WorldRole != TEXT("owner")) { AddError(TEXT("never made owner: ") + B->WorldRole); return true; } B->RequestAccessState(); Next(); }
            return false;
        // ---- access (T10's rows through the C++) ----
        case 3:
            if (Expect(B->AccessStateWire && AStr(B->AccessStateWire, TEXT("door")) == TEXT("open") && Row(B, TEXT("members"), TEXT("name"), TEXT("founder")).IsValid(), 10,
                TEXT("A1 the access tab's state is the bridge's: door open, the founder listed")))
            {
                if (const TSharedPtr<FJsonObject> G = Row(B, TEXT("here"), TEXT("name"), S->Guest)) S->GuestId = AStr(G, TEXT("identity"));
                B->HUD->AccessAction(TEXT("door:visitor"));   // the tab's rung for the public door
            }
            return false;
        case 4:
            Expect(B->AccessStateWire && AStr(B->AccessStateWire, TEXT("door")) == TEXT("public"), 10, TEXT("A2 the tab's door is a request the authority applies, pushed back as public"));
            if (S->Step == 5) { B->HUD->AccessAction(TEXT("addrole:builder")); B->HUD->AccessAction(TEXT("addspan:1h")); B->HUD->AccessAction(TEXT("here:") + S->GuestId); }
            return false;
        case 5:
        {
            const TSharedPtr<FJsonObject> M = S->GuestId.IsEmpty() ? nullptr : Row(B, TEXT("members"), TEXT("id"), S->GuestId);
            double Until = 0; if (M) M->TryGetNumberField(TEXT("until"), Until);
            const double NowMs = FDateTime::UtcNow().ToUnixTimestamp() * 1000.0;
            Expect(M && AStr(M, TEXT("role")) == TEXT("builder") && Until > NowMs + 3000000 && Until < NowMs + 3700000, 10, TEXT("A3a here-not-listed → builder for 1 h: listed, with its end"));
            if (S->Step == 6) B->HUD->AccessAction(TEXT("rm:") + S->GuestId);
            return false;
        }
        case 6:
            Expect(!S->GuestId.IsEmpty() && !Row(B, TEXT("members"), TEXT("id"), S->GuestId) && Row(B, TEXT("here"), TEXT("identity"), S->GuestId), 10, TEXT("A3b ✕ takes the grant back: here, not listed"));
            if (S->Step == 7) { S->Serial = B->AccessResultsSerial; B->AccessError.Empty(); B->RequestAccessDirectory(TEXT("Nobody Ever")); }
            return false;
        case 7:
            Expect(B->AccessResultsSerial != S->Serial && B->AccessResults.Num() == 0 && B->AccessError == DirectoryGap, 10, TEXT("A4 a directory miss says search by name isn't available (§3.3 gap)"));
            if (S->Step == 8) Hold(E, B, FVector(0, 0, 0));
            return false;
        // ---- seats and holding (T8, T9's rows through the C++) ----
        case 8:
            Hold(E, B, FVector(0, 0, 0));
            Expect(B->IsBodyTracked(), 15, TEXT("B1 the authority tracks my body (move) where I stand"));
            if (S->Step == 9) S->ErrorAt = B->LastErrorAt;
            return false;
        case 9:
            Hold(E, B, FVector(0, 0, 0));
            Act([&]() { return B->SitOnNamed(E, S->Bench); });
            Expect(B->LastErrorAt > S->ErrorAt && B->LastErrorText.Contains(TEXT("a seat is within 3.5 m")) && B->GetSeatHost().IsEmpty() && !(E && E->bSeated), 10,
                TEXT("S1 a far seat is refused in the authority's words, and I stay standing"));
            return false;
        case 10:
            Hold(E, B, NearBench);
            Expect(In > 1 && B->IsBodyTracked(), 15, TEXT("B2 walked up to the bench: the authority's track follows"));
            return false;
        case 11:
            Hold(E, B, NearBench);
            Act([&]() { return B->GrabEntity(S->Lamp); });
            S->bHeld = B->HeldItem == S->Lamp;
            Expect(S->bHeld, 10, TEXT("H1a grab: the authority hands me the lamp"));
            return false;
        case 12:
            Hold(E, B, NearBench);
            Act([&]() { return B->LetGoOfEntity(false, FVector::ZeroVector); });
            Expect(S->bHeld && In > .5 && B->HeldItem.IsEmpty(), 10, TEXT("H1b put: the lamp is set down, not mine any more"));
            if (S->Step == 13) S->ErrorAt = B->LastErrorAt;
            return false;
        case 13:
            Hold(E, B, NearBench);
            Act([&]() { return B->PuntEntity(S->Lamp, 4); });
            Expect(B->LastErrorAt > S->ErrorAt && B->LastErrorText.Contains(TEXT("nothing flies")), 10, TEXT("H2 a kick is the authority's punt, refused in its words here (no physics)"));
            return false;
        case 14:
            Act([&]() { return B->SitOnNamed(E, S->Bench); });
            // (the explorer's own sitting pose needs a loaded body, which this scratch server has none of)
            S->bSat = In > 5 && B->GetSeatHost() == S->Bench && B->GetSeatOccupant(S->Bench) == B->AssignedIdentity;
            if (Late(20) && !S->bSat) UE_LOG(LogTemp, Display, TEXT("EIDOVERSE_V4_BODIES S2 state: host=%s seated=%d occupant=%s me=%s seats=%d sent=%d"),
                *B->GetSeatHost(), E && E->bSeated, *B->GetSeatOccupant(S->Bench), *B->AssignedIdentity, B->SceneSeats.Num(), S->bSent);
            Expect(S->bSat, 20,
                TEXT("S2 near, I sit: the ride is confirmed, still mine after 5 s, and the scene's seat names me"));
            return false;
        case 15:
            Act([&]() { B->LeaveSeat(); return true; });
            Expect(S->bSat && In > 1 && B->GetSeatHost().IsEmpty() && B->GetSeatOccupant(S->Bench).IsEmpty() && B->IsBodyTracked(1.5f), 15,
                TEXT("S3 standing up: the seat is empty again and my body is where I landed"));
            return false;
        case 16:
            for (const FString& L : S->Lines) AddInfo(L);
            TestEqual(TEXT("rows failed"), S->Failures, 0);
            UE_LOG(LogTemp, Display, TEXT("EIDOVERSE_V4_BODIES done rows=%d failed=%d"), S->Lines.Num(), S->Failures);
            B->Disconnect();
            return true;
        default: return true;
        }
    }));
    return true;
}
#endif
