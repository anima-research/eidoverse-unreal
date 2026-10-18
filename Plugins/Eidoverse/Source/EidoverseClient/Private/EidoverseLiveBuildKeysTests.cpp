// Eidoverse.LiveBuildKeys: build mode's keys, through the real input path. Runs only in a game session (-game,
// a game viewport and the HUD) with Tools/eidoverse/run_game_tests.sh's scratch sequencer: it joins a fresh
// world (its first joiner owns it), spawns a pallet, and does what a person does, through Slate as a keyboard and
// mouse would (FSlateApplication::Process*Event): arrives in mouselook, presses B (the pointer must come free),
// clicks the pallet (it must be selected), and again straight from typing in chat, then presses each key
// with focus on the world and on a panel button, at a person's pace. Each press must move the pallet as build.js
// does: Q/E turn 15 deg, R/F raise and lower 0.25 m, , . resize, arrows nudge 0.5 m.
#include "EidoverseBridge.h"
#include "EidoverseScene.h"
#include "EidoverseUI.h"
#include "Components/Button.h"
#include "Components/EditableTextBox.h"
#include "Widgets/SViewport.h"
#include "Slate/SceneViewport.h"
#include "Engine/GameViewportClient.h"
#include "Blueprint/WidgetTree.h"
#include "Dom/JsonObject.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "EngineUtils.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Application/SlateUser.h"
#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"

#if WITH_DEV_AUTOMATION_TESTS
namespace
{
    struct FKeyCase { FKey Key; const TCHAR* What; };
    struct FBuildKeys
    {
        TWeakObjectPtr<UWorld> World;
        TWeakObjectPtr<AEidoverseBridge> Bridge;
        int32 Step = 0, Case = 0, Frames = 0; double StepAt = 0;
        bool bButtonFocus = false; int32 ClickFrame = 0;
        FVector Pos; double Yaw = 0, Scale = 1;
        TArray<FString> Report; int32 Failures = 0;
        FVector2D PressedAt = FVector2D::ZeroVector, LastPx = FVector2D(-1e6, -1e6), PressedMouse = FVector2D::ZeroVector;
        double HeldDrift = 0;
    };
    const FString PalletId = TEXT("buildkeys-pallet");
    // Place the faux cursor (see RunTest) where the injected events say the mouse is: a captured viewport reads the
    // cursor, not the events. Never the platform cursor -- that would move the real mouse of whoever uses this Mac.
    bool bFauxCursor = false;   // set only once UsePlatformCursorForCursorUser(false) has replaced a platform cursor
    void PlaceFauxCursor(const FVector2D& Abs) { if (bFauxCursor && FSlateApplication::Get().GetCursorUser()) FSlateApplication::Get().SetCursorPos(Abs); }
    // A left click at a world point, as the mouse would deliver it to the game viewport. Move first; press only once
    // the game reads the mouse where the point is (the viewport's cached geometry can lag a frame or two). The release
    // goes where the press went (Release): chasing the point while the button is held is a drag, and a drag moves it.
    bool Click(APlayerController* PC, const FVector& World, bool bDown, bool bMoveOnly = false, FVector2D* PressedAt = nullptr)
    {
        // FSceneViewport reads the mouse as AbsoluteToLocal(screen) x Scale on its own cached geometry: invert that
        FVector2D Px;
        FSceneViewport* SV = GEngine && GEngine->GameViewport ? GEngine->GameViewport->GetGameViewport() : nullptr;
        if (!SV || !PC->ProjectWorldLocationToScreen(World, Px, true)) return false;
        const FGeometry& G = SV->GetCachedGeometry();
        const FVector2D Abs = G.LocalToAbsolute(Px / FMath::Max(G.Scale, .01f));
        FSlateApplication& App = FSlateApplication::Get();
        PlaceFauxCursor(Abs);
        App.ProcessMouseMoveEvent(FPointerEvent(0, Abs, Abs, TSet<FKey>(), EKeys::Invalid, 0, FModifierKeysState()));
        if (bMoveOnly) return true;
        if (PressedAt) *PressedAt = Abs;
        if (bDown) return App.ProcessMouseButtonDownEvent(nullptr, FPointerEvent(0, Abs, Abs, TSet<FKey>({EKeys::LeftMouseButton}), EKeys::LeftMouseButton, 0, FModifierKeysState()));
        return App.ProcessMouseButtonUpEvent(FPointerEvent(0, Abs, Abs, TSet<FKey>(), EKeys::LeftMouseButton, 0, FModifierKeysState()));
    }
    void Release(const FVector2D& Abs)
    {
        PlaceFauxCursor(Abs);
        FSlateApplication::Get().ProcessMouseButtonUpEvent(FPointerEvent(0, Abs, Abs, TSet<FKey>(), EKeys::LeftMouseButton, 0, FModifierKeysState()));
    }
    void Press(const FKey& Key, bool bDown)
    {
        if (bDown) FSlateApplication::Get().ProcessKeyDownEvent(FKeyEvent(Key, FModifierKeysState(), 0, false, 0, 0));
        else FSlateApplication::Get().ProcessKeyUpEvent(FKeyEvent(Key, FModifierKeysState(), 0, false, 0, 0));
    }
    bool Pose(AEidoverseBridge* B, FVector& P, double& Y, double& S)
    {
        AEidoverseScene* Scene = B ? B->GetScene() : nullptr; FString Lib; bool bAnimated = false;
        return Scene && Scene->GetEntityPose(PalletId, P, Y, S, Lib, bAnimated);
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEidoverseLiveBuildKeys, "Eidoverse.LiveBuildKeys", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FEidoverseLiveBuildKeys::RunTest(const FString&)
{
    const FString Url = FPlatformMisc::GetEnvironmentVariable(TEXT("EIDO_LIVE_BUILD_URL"));
    const FString WorldName = FPlatformMisc::GetEnvironmentVariable(TEXT("EIDO_LIVE_BUILD_WORLD"));
    if (Url.IsEmpty() || WorldName.IsEmpty()) { AddInfo(TEXT("live check skipped: run Tools/eidoverse/run_game_tests.sh (-game, a scratch sequencer)")); return true; }
    if (!Url.StartsWith(TEXT("ws://127.0.0.1:"))) { AddError(TEXT("loopback servers only")); return false; }
    UWorld* Game = nullptr;
    for (const FWorldContext& C : GEngine->GetWorldContexts()) if (C.WorldType == EWorldType::Game && C.World()) Game = C.World();
    if (!Game) { AddError(TEXT("needs a game session (-game)")); return false; }
    // focusing a text box in an offscreen window: macOS's input method can't find the window (rendering offscreen only)
    AddExpectedError(TEXT("context failed when its window couldn't be found"), EAutomationExpectedErrorFlags::Contains, 0);
    // The pointer is the injected one only: while a button is held the viewport captures the mouse and reads the
    // platform cursor -- the machine's REAL mouse (seen 795 px from the injected point on a Mac someone was using),
    // which turned the harness's click into a drag. Slate's faux cursor follows events alone; restored at the end.
    {
        // nothing else in the engine switches it back (UE 5.8: no other caller), so from here the cursor user's cursor
        // is the faux one and SetCursorPos moves only that
        FSlateApplication& App = FSlateApplication::Get();
        const TSharedPtr<GenericApplication> Platform = App.GetPlatformApplication();
        App.UsePlatformCursorForCursorUser(false);
        bFauxCursor = Platform && Platform->Cursor.IsValid() && App.GetCursorUser().IsValid();
    }
    TSharedPtr<FBuildKeys> S = MakeShared<FBuildKeys>();
    S->World = Game; S->StepAt = FPlatformTime::Seconds();
    static const FKeyCase Cases[] = {
        {EKeys::Q, TEXT("Q turns left 15 deg")}, {EKeys::E, TEXT("E turns right 15 deg")}, {EKeys::R, TEXT("R raises 0.25 m")},
        {EKeys::F, TEXT("F lowers 0.25 m")}, {EKeys::Period, TEXT(". grows")}, {EKeys::Comma, TEXT(", shrinks")}, {EKeys::Up, TEXT("Up nudges 0.5 m")}};
    constexpr int32 NumCases = UE_ARRAY_COUNT(Cases);

    ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this, S, Url, WorldName]() -> bool {
        const double Now = FPlatformTime::Seconds(), In = Now - S->StepAt;
        UWorld* W = S->World.Get();
        if (!W) { AddError(TEXT("game world gone")); return true; }
        AEidoverseBridge* B = S->Bridge.Get();
        auto Next = [&](int32 To) { S->Step = To; S->StepAt = Now; S->Frames = 0; };
        auto Done = [&]() { for (const FString& L : S->Report) AddInfo(L); if (S->Failures) AddError(FString::Printf(TEXT("%d build key presses did nothing"), S->Failures)); return true; };
        switch (S->Step)
        {
        case 0:   // the client map's bridge and its HUD
        {
            if (!B) for (TActorIterator<AEidoverseBridge> It(W); It; ++It) { S->Bridge = *It; B = *It; }
            if (!B || !B->HUD) { if (In > 30) { AddError(TEXT("no bridge or HUD in the game world")); return true; } return false; }
            B->bLoadRemoteAvatars = false;
            B->JoinFromDoor(Url, WorldName, TEXT("buildkeys"));
            Next(1); return false;
        }
        case 1:   // in, owner of the fresh world, the explorer possessed
            if (!B || !B->bJoined || !B->GetScene() || !B->CanBuild() || !Cast<AEidoverseExplorer>(UGameplayStatics::GetPlayerPawn(W, 0)))
            { if (In > 40) { AddError(TEXT("never joined able to build: ") + (B ? B->ConnectionStatus : FString())); return true; } return false; }
            {
                auto Args = MakeShared<FJsonObject>();
                Args->SetStringField(TEXT("id"), PalletId); Args->SetStringField(TEXT("lib"), TEXT("eidoverse/assets/models/scifi_single_pallet.glb"));
                Args->SetArrayField(TEXT("pos"), {MakeShared<FJsonValueNumber>(0), MakeShared<FJsonValueNumber>(1), MakeShared<FJsonValueNumber>(4)});
                Args->SetNumberField(TEXT("yaw"), 0);
                B->SendVerb(TEXT("spawn"), Args);
            }
            Next(2); return false;
        case 2:   // the pallet in the scene; I look at it, in mouselook (as a join leaves me), and press B
        {
            if (!Pose(B, S->Pos, S->Yaw, S->Scale)) { if (In > 20) { AddError(TEXT("the pallet never arrived")); return true; } return false; }
            if (In < 1.5) return false;   // let the model settle in
            APlayerController* PC = UGameplayStatics::GetPlayerController(W, 0);
            AActor* Pallet = B->GetScene()->EntityActor(PalletId);
            if (!PC || !Pallet) { AddError(TEXT("no controller or pallet actor")); return true; }
            if (++S->Frames == 1)
            {
                { const TSharedPtr<SWidget> F = FSlateApplication::Get().GetKeyboardFocusedWidget();
                  S->Report.Add(FString::Printf(TEXT("     after joining from the door, keyboard focus is on %s"), F.IsValid() ? *F->GetTypeAsString() : TEXT("nothing"))); }
                // as a join leaves me: mouselook, Build closed (a saved layout may have kept it open), facing the pallet
                FVector Eye; FRotator View; PC->GetPlayerViewPoint(Eye, View);
                PC->SetControlRotation((Pallet->GetActorLocation() - Eye).Rotation());
                B->HUD->ShowFrame("build", false);
                B->HUD->SetPointerMode(false);
                FSlateApplication::Get().SetUserFocusToGameViewport(0);
            }
            if (S->Frames == 2) Press(EKeys::B, true);
            if (S->Frames == 4) Press(EKeys::B, false);
            if (S->Frames < 8) return false;
            S->Report.Add(FString::Printf(TEXT("%s B opens Build with the pointer free (build open %d, pointer %d)"),
                B->HUD->IsFrameOpen("build") && B->HUD->IsPointerMode() ? TEXT("ok  ") : TEXT("FAIL"), B->HUD->IsFrameOpen("build") ? 1 : 0, B->HUD->IsPointerMode() ? 1 : 0));
            if (!(B->HUD->IsFrameOpen("build") && B->HUD->IsPointerMode())) { ++S->Failures; B->HUD->ShowFrame("build", true); B->HUD->SetPointerMode(true); }
            Next(4); return false;
        }
        case 4:   // click the pallet; then once more straight from typing in chat
        case 5:
        {
            APlayerController* PC = UGameplayStatics::GetPlayerController(W, 0);
            AActor* Pallet = B->GetScene()->EntityActor(PalletId);
            FVector Center, Extent; Pallet->GetActorBounds(true, Center, Extent);
            if (++S->Frames == 1)
            {
                B->HUD->SelectEntity(FString());
                Pose(B, S->Pos, S->Yaw, S->Scale);   // a click is not a drag: it must leave the pallet where it is
                if (S->Step == 5) B->HUD->FocusChat();   // typing in the chat line mid-build (Enter), then clicking a thing
            }
            // move until the game reads the mouse on the pallet, then press and release
            if (S->Frames == 3 && S->ClickFrame == 0)
            { const TSharedPtr<SWidget> F = FSlateApplication::Get().GetKeyboardFocusedWidget();
              S->Report.Add(FString::Printf(TEXT("     before the click, keyboard focus is on %s"), F.IsValid() ? *F->GetTypeAsString() : TEXT("nothing"))); }
            if (S->Frames >= 3 && S->ClickFrame == 0)
            {
                Click(PC, Center, false, true);
                float MX = 0, MY = 0; FVector2D Px;
                // press only once the pallet holds still on screen (the camera may still be easing round to it):
                // a hand clicks what it sees, and the game picks with the camera of the frame it reads the press in
                const bool bSteady = PC->ProjectWorldLocationToScreen(Center, Px, true) && FVector2D::Distance(Px, S->LastPx) < 1;
                S->LastPx = Px;
                if (bSteady && PC->GetMousePosition(MX, MY) && FVector2D::Distance(FVector2D(MX, MY), Px) < 3) { Click(PC, Center, true, false, &S->PressedAt); S->ClickFrame = S->Frames; S->LastPx = FVector2D(-1e6, -1e6); S->PressedMouse = FVector2D(MX, MY); S->HeldDrift = 0; }
                else if (S->Frames > 120) { AddError(TEXT("test harness: could not place the mouse on the pallet")); return true; }
                return false;
            }
            if (S->Frames <= S->ClickFrame + 2)
            { float HX = 0, HY = 0; if (PC->GetMousePosition(HX, HY)) S->HeldDrift = FMath::Max(S->HeldDrift, FVector2D::Distance(FVector2D(HX, HY), S->PressedMouse)); }
            if (S->Frames == S->ClickFrame + 2) Release(S->PressedAt);
            if (S->Frames < S->ClickFrame + 6) return false;
            S->ClickFrame = 0;
            FVector After; double AfterYaw = 0, AfterScale = 1; Pose(B, After, AfterYaw, AfterScale);
            const bool bStill = (After - S->Pos).GetAbsMax() < .005;
            const bool bPicked = B->HUD->GetSelectedEntity() == PalletId && B->HUD->IsPointerMode() && bStill;
            {
                // where the click went: the mouse as the game saw it, the pallet on screen, and what the pick ray hit
                float MX = 0, MY = 0; PC->GetMousePosition(MX, MY); FVector2D Px; PC->ProjectWorldLocationToScreen(Center, Px, true);
                FHitResult Hit; PC->GetHitResultUnderCursor(ECC_Visibility, true, Hit);
                S->Report.Add(FString::Printf(TEXT("     click: mouse (%.0f,%.0f), pallet on screen (%.0f,%.0f), hit %s (entity '%s')"), MX, MY, Px.X, Px.Y,
                    Hit.GetActor() ? *Hit.GetActor()->GetName() : TEXT("nothing"), Hit.GetActor() ? *B->GetScene()->EntityId(Hit.GetActor()) : TEXT("")));
            }
            S->Report.Add(FString::Printf(TEXT("%s clicking the pallet selects it%s, leaving it in place (selected '%s', pointer %d, moved %.3f m, pointer drift while held %.0f px)"), bPicked ? TEXT("ok  ") : TEXT("FAIL"),
                S->Step == 5 ? TEXT(" straight from typing in chat") : TEXT(""), *B->HUD->GetSelectedEntity(), B->HUD->IsPointerMode() ? 1 : 0, (After - S->Pos).GetAbsMax(), S->HeldDrift));
            if (!bPicked) { ++S->Failures; B->HUD->SetPointerMode(true); B->HUD->SelectEntity(PalletId); }
            FSlateApplication::Get().SetUserFocusToGameViewport(0);
            Next(S->Step == 4 ? 5 : 3); return false;
        }
        case 3:   // press: down this frame, up two frames later
        {
            if (In < .45) return false;   // a person's pace: the server takes 12 verbs per 4 s
            if (++S->Frames == 1)
            {
                Pose(B, S->Pos, S->Yaw, S->Scale);
                const auto& K = Cases[S->Case];
                if (S->bButtonFocus)
                {
                    // where a click on a panel button leaves keyboard focus
                    TArray<UWidget*> All; B->HUD->WidgetTree->GetAllWidgets(All);
                    for (UWidget* Wd : All) if (UButton* Btn = Cast<UButton>(Wd)) if (Btn->IsVisible() && Btn->GetCachedWidget().IsValid()) { FSlateApplication::Get().SetKeyboardFocus(Btn->GetCachedWidget(), EFocusCause::Mouse); break; }
                }
                else FSlateApplication::Get().SetUserFocusToGameViewport(0);
                const TSharedPtr<SWidget> Focus = FSlateApplication::Get().GetKeyboardFocusedWidget();
                S->Report.Add(FString::Printf(TEXT("focus %s, selected '%s', build open %d, can build %d"), Focus.IsValid() ? *Focus->GetTypeAsString() : TEXT("none"),
                    *B->HUD->GetSelectedEntity(), B->HUD->IsFrameOpen("build") ? 1 : 0, B->CanBuild() ? 1 : 0));
                FSlateApplication::Get().ProcessKeyDownEvent(FKeyEvent(K.Key, FModifierKeysState(), 0, false, 0, 0));
            }
            else if (S->Frames == 3) FSlateApplication::Get().ProcessKeyUpEvent(FKeyEvent(Cases[S->Case].Key, FModifierKeysState(), 0, false, 0, 0));
            FVector P; double Y, Sc;
            const bool bMoved = Pose(B, P, Y, Sc) && (FMath::Abs(FMath::UnwindRadians(Y - S->Yaw)) > .1 || FVector::Dist(P, S->Pos) > .1 || FMath::Abs(Sc - S->Scale) > .03);
            if (!bMoved && In < 8) return false;   // patient with a frame hitch: a model or shader first seen
            const auto& K = Cases[S->Case];
            S->Report.Add(FString::Printf(TEXT("%s %s (%s): %s"), bMoved ? TEXT("ok  ") : TEXT("FAIL"), K.What, S->bButtonFocus ? TEXT("focus on a panel button") : TEXT("focus on the world"),
                bMoved ? *FString::Printf(TEXT("yaw %.1f -> %.1f deg, pos (%.2f,%.2f,%.2f) -> (%.2f,%.2f,%.2f), scale %.3f -> %.3f"),
                    FMath::RadiansToDegrees(S->Yaw), FMath::RadiansToDegrees(Y), S->Pos.X, S->Pos.Y, S->Pos.Z, P.X, P.Y, P.Z, S->Scale, Sc) : TEXT("nothing moved in 8 s")));
            if (!bMoved) ++S->Failures;
            if (++S->Case >= NumCases) { if (S->bButtonFocus) return Done(); S->bButtonFocus = true; S->Case = 0; }
            Next(3); return false;
        }
        }
        return true;
    }));
    ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([]() -> bool { bFauxCursor = false; FSlateApplication::Get().UsePlatformCursorForCursorUser(true); return true; }));
    return true;
}
#endif
