// The lantern (lantern.js + actions.js): one line for everything. Type to talk, '/' for the
// commands, or a word ("sky", "wave", "mute") for the matching actions, keys shown beside them.
// Enter SAYS what you typed (the say row is first); Tab DOES the highlighted action (the best
// match by default). Exceptions to Enter-says: a line starting with '/', and a highlight the
// person moved themselves — then Enter runs that row. Ctrl/Cmd+K or the resting line opens it.
#include "EidoverseUI.h"
#include "EidoverseBridge.h"
#include "EidoverseScene.h"
#include "EidoverseActions.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Components/SizeBox.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/EditableTextBox.h"
#include "Blueprint/WidgetTree.h"
#include "GameFramework/PlayerController.h"
#include "UnrealClient.h"
#include "EidoverseSettings.h"

namespace
{
    FButtonStyle LanternButton(const FLinearColor& Normal, const FLinearColor& Hovered)
    {
        FButtonStyle S;
        S.SetNormal(FEidoverseTheme::Rounded(Normal, 6)); S.SetHovered(FEidoverseTheme::Rounded(Hovered, 6));
        S.SetPressed(FEidoverseTheme::Rounded(FEidoverseTheme::Well(), 6));
        S.SetNormalPadding(FMargin(0)); S.SetPressedPadding(FMargin(0));
        return S;
    }
    const FString Chord = PLATFORM_MAC ? TEXT("Cmd K") : TEXT("Ctrl K");
}

UWidget* UEidoverseHUD::BuildLantern()
{
    auto* Width = WidgetTree->ConstructWidget<USizeBox>(); Width->SetWidthOverride(560);
    auto* Column = WidgetTree->ConstructWidget<UVerticalBox>(); Width->SetContent(Column);
    LanternPanel = WidgetTree->ConstructWidget<UBorder>();
    LanternPanel->SetBrush(FEidoverseTheme::Rounded(FEidoverseTheme::Panel().CopyWithNewOpacity(.94f), 12, FEidoverseTheme::Edge(), 1));
    LanternPanel->SetPadding(FMargin(8));
    auto* Cap = WidgetTree->ConstructWidget<USizeBox>(); Cap->SetMaxDesiredHeight(380);
    LanternList = WidgetTree->ConstructWidget<UVerticalBox>(); Cap->SetContent(LanternList);
    auto* Inner = WidgetTree->ConstructWidget<UVerticalBox>();
    Inner->AddChildToVerticalBox(Cap);
    LanternInput = Input(TEXT("say something, / for commands, or a word to do it  ·  Enter says, Tab does"));
    LanternInput->OnTextChanged.AddDynamic(this, &UEidoverseHUD::OnLanternChanged);
    Inner->AddChildToVerticalBox(LanternInput)->SetPadding(FMargin(0, 6, 0, 0));
    LanternPanel->SetContent(Inner);
    LanternPanel->SetVisibility(ESlateVisibility::Collapsed);
    Column->AddChildToVerticalBox(LanternPanel);
    // the resting line: always there, opens it
    LanternPill = WidgetTree->ConstructWidget<UButton>();
    LanternPill->SetStyle(LanternButton(FEidoverseTheme::Chip(), FEidoverseTheme::Hover()));
    auto* PillText = Label(Chord + TEXT("   say or do anything"), 10, FEidoverseTheme::Dim()); PillText->SetAutoWrapText(false);
    auto* PillPad = WidgetTree->ConstructWidget<UBorder>(); PillPad->SetBrushColor(FLinearColor::Transparent); PillPad->SetPadding(FMargin(14, 5)); PillPad->SetContent(PillText);
    LanternPill->SetContent(PillPad);
    LanternPill->OnClicked.AddDynamic(this, &UEidoverseHUD::OnLanternPill);
    Column->AddChildToVerticalBox(LanternPill)->SetHorizontalAlignment(HAlign_Center);
    LanternRoot = Width;
    return Width;
}

bool UEidoverseHUD::IsLanternOpen() const { return LanternPanel && LanternPanel->GetVisibility() != ESlateVisibility::Collapsed; }
void UEidoverseHUD::OnLanternPill() { OpenLantern(FString()); }

void UEidoverseHUD::OpenLantern(const FString& Text)
{
    if (!LanternPanel) return;
    if (!bActionsRegistered) RegisterHudActions();
    LanternPanel->SetVisibility(ESlateVisibility::Visible);
    LanternPill->SetVisibility(ESlateVisibility::Collapsed);
    LanternInput->SetText(FText::FromString(Text));
    bLanternMoved = false; LanternSel = 0;
    RefreshLantern();
    if (APlayerController* PC = GetOwningPlayer())
    {
        FInputModeGameAndUI Mode; Mode.SetWidgetToFocus(LanternInput->TakeWidget()); Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
        PC->SetInputMode(Mode); PC->SetIgnoreMoveInput(true); PC->SetIgnoreLookInput(true); PC->bShowMouseCursor = true;
    }
    LanternInput->SetKeyboardFocus();
}

void UEidoverseHUD::CloseLantern()
{
    if (!IsLanternOpen()) return;
    LanternPanel->SetVisibility(ESlateVisibility::Collapsed);
    LanternPill->SetVisibility(ESlateVisibility::Visible);
    LeaveChat(true);   // back to the world, keys and look restored
}

void UEidoverseHUD::LanternType(const FString& Text) { if (!IsLanternOpen()) OpenLantern(Text); else { LanternInput->SetText(FText::FromString(Text)); bLanternMoved = false; RefreshLantern(); } }
void UEidoverseHUD::OnLanternChanged(const FText&) { bLanternMoved = false; LanternSel = 0; RefreshLantern(); }

TArray<FString> UEidoverseHUD::GetLanternRows() const
{
    TArray<FString> Out;
    for (int32 I = 0; I < LanternRows.Num(); ++I)
        Out.Add(FString::Printf(TEXT("%s%s:%s"), I == (bLanternMoved ? LanternSel : (LanternBest >= 0 ? LanternBest : 0)) ? TEXT("*") : TEXT(""), *LanternRows[I].Kind, *LanternRows[I].Title));
    return Out;
}

void UEidoverseHUD::RefreshLantern()
{
    if (!LanternList || !Bridge) return;
    const FString Q = LanternInput->GetText().ToString();
    const FString T = Q.TrimStartAndEnd();
    LanternRows.Reset(); LanternBest = -1;
    if (T.StartsWith(TEXT("/")))
    {
        // the command path: the command table, a command's help as its row
        FString Head = T.Mid(1); int32 Sp; if (Head.FindChar(' ', Sp)) Head = Head.Left(Sp);
        for (const FString& C : Bridge->CommandCompletions(Head))
        {
            FString Name, Help; if (!C.Split(TEXT("\t"), &Name, &Help)) Name = C;
            FString Title = Help, Args; int32 Dash = Help.Find(TEXT("—"));
            if (Dash > 0) { Args = Help.Left(Dash).TrimStartAndEnd(); Args.RemoveFromStart(Name); Title = Help.Mid(Dash + 1).TrimStartAndEnd(); }
            LanternRows.Add({TEXT("cmd"), Name, Title, Args.TrimStartAndEnd(), Name});
        }
    }
    else
    {
        if (!T.IsEmpty())
            LanternRows.Add({TEXT("say"), FString(), (GetActiveDm().IsEmpty() ? FString(TEXT("say “")) : FString::Printf(TEXT("whisper to %s “"), *GetActiveDm())) + T + TEXT("”"), FString(), TEXT("Enter")});
        for (const auto& R : EidoActions::List(T, T.IsEmpty() ? 14 : 10))
        {
            if (LanternBest < 0 && !T.IsEmpty()) LanternBest = LanternRows.Num();
            LanternRows.Add({TEXT("act"), R.Key->Id, R.Key->Title, R.Key->Detail.IsEmpty() ? R.Key->Group : R.Key->Detail, R.Key->Key});
        }
    }
    if (!bLanternMoved) LanternSel = LanternBest >= 0 ? LanternBest : 0;
    LanternSel = FMath::Clamp(LanternSel, 0, FMath::Max(0, LanternRows.Num() - 1));
    LanternList->ClearChildren();
    for (auto It = ButtonIds.CreateIterator(); It; ++It) if (!It->Key.IsValid()) It.RemoveCurrent();
    for (int32 I = 0; I < LanternRows.Num(); ++I)
    {
        const FLanternRow& R = LanternRows[I];
        const bool bOn = I == LanternSel;
        auto* B = WidgetTree->ConstructWidget<UButton>();
        B->SetStyle(LanternButton(bOn ? FEidoverseTheme::Brand().CopyWithNewOpacity(.15f) : FLinearColor::Transparent, FEidoverseTheme::Hover()));
        auto* Row = WidgetTree->ConstructWidget<UHorizontalBox>();
        auto* Title = Label(R.Title, 11, R.Kind == TEXT("say") ? FEidoverseTheme::Text() : bOn ? FEidoverseTheme::Brand() : FEidoverseTheme::Text(), bOn);
        Title->SetAutoWrapText(false);
        Row->AddChildToHorizontalBox(Title)->SetPadding(FMargin(8, 4, 8, 4));
        auto* Detail = Label(R.Detail, 10, FEidoverseTheme::Dim()); Detail->SetAutoWrapText(false);
        Row->AddChildToHorizontalBox(Detail)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
        if (!R.Badge.IsEmpty() || I == LanternBest)
        {
            auto* Badge = Label(I == LanternBest && R.Kind == TEXT("act") ? (R.Badge.IsEmpty() ? FString(TEXT("Tab")) : R.Badge + TEXT("  ·  Tab")) : R.Badge, 9, FEidoverseTheme::Dim(), true);
            Badge->SetAutoWrapText(false);
            Row->AddChildToHorizontalBox(Badge)->SetPadding(FMargin(8, 5, 8, 0));
        }
        B->SetContent(Row);
        B->OnClicked.AddDynamic(this, &UEidoverseHUD::OnLanternRowClicked);
        ButtonIds.Add(B, FName(*FString::Printf(TEXT("lantern:%d"), I)));
        LanternList->AddChildToVerticalBox(B);
    }
}

void UEidoverseHUD::OnLanternRowClicked()
{
    const FString Id = ResolveClicked().ToString();
    if (Id.StartsWith(TEXT("lantern:"))) RunLanternRow(FCString::Atoi(*Id.Mid(8)), true);
}

void UEidoverseHUD::RunLanternRow(int32 Index, bool bFromTab)
{
    if (!LanternRows.IsValidIndex(Index) || !Bridge) return;
    const FLanternRow R = LanternRows[Index];
    const FString Text = LanternInput->GetText().ToString().TrimStartAndEnd();
    if (R.Kind == TEXT("say"))
    {
        if (bFromTab) return;   // Tab never says
        const FString Dm = GetActiveDm();
        if (!Dm.IsEmpty()) Bridge->Whisper(Dm, Text); else Bridge->Say(Text);
        CloseLantern();
        return;
    }
    if (R.Kind == TEXT("cmd"))
    {
        // a bare command fills the line (it may want arguments); typed arguments run it
        if (!Text.Contains(TEXT(" "))) { LanternInput->SetText(FText::FromString(R.Id + TEXT(" "))); RefreshLantern(); return; }
        Bridge->HandleInteractionCommand(Text); CloseLantern(); return;
    }
    for (const auto& A : EidoActions::List(FString(), 1000))
        if (A.Key->Id == R.Id)
        {
            if (!A.Key->Fill.IsEmpty() && !A.Key->Run) { LanternInput->SetText(FText::FromString(A.Key->Fill)); RefreshLantern(); return; }
            CloseLantern();
            EidoActions::Run(R.Id);
            return;
        }
}

bool UEidoverseHUD::LanternKey(const FString& KeyName)
{
    if (!IsLanternOpen()) return false;
    const FString Text = LanternInput->GetText().ToString().TrimStartAndEnd();
    if (KeyName == TEXT("Escape")) { CloseLantern(); return true; }
    if (KeyName == TEXT("Down") || KeyName == TEXT("Up"))
    {
        if (!LanternRows.Num()) return true;
        bLanternMoved = true;
        LanternSel = (LanternSel + (KeyName == TEXT("Down") ? 1 : LanternRows.Num() - 1)) % LanternRows.Num();
        RefreshLantern(); return true;
    }
    if (KeyName == TEXT("Tab"))
    {
        const int32 I = bLanternMoved ? LanternSel : LanternBest;
        if (I >= 0) RunLanternRow(I, true);     // nothing to do: Tab does nothing (a typo + Tab posts nothing)
        return true;
    }
    if (KeyName == TEXT("Enter"))
    {
        if (Text.IsEmpty()) { CloseLantern(); return true; }
        if (Text.StartsWith(TEXT("/")) && !bLanternMoved) { Bridge->HandleInteractionCommand(Text); CloseLantern(); return true; }
        if (bLanternMoved) { RunLanternRow(LanternSel, false); return true; }
        RunLanternRow(0, false);   // the say row
        return true;
    }
    return false;
}

void UEidoverseHUD::RegisterHudActions()
{
    bActionsRegistered = true;
    RegisterMenuActions();
    TWeakObjectPtr<UEidoverseHUD> W(this);
    auto Reg = [](const FString& Id, const FString& Title, const FString& Group, const FString& Key, TArray<FString> Keywords, TFunction<void()> Run, const FString& Detail = FString())
    { FEidoAction A; A.Id = Id; A.Title = Title; A.Group = Group; A.Key = Key; A.Keywords = MoveTemp(Keywords); A.Run = MoveTemp(Run); A.Detail = Detail; EidoActions::Register(A); };
    auto Explorer = [W]() { return W.IsValid() ? Cast<AEidoverseExplorer>(W->GetOwningPlayerPawn()) : nullptr; };
    auto B = [W]() { return W.IsValid() ? W->Bridge.Get() : nullptr; };
    // panels
    const TPair<const TCHAR*, const TCHAR*> Panels[] = {{TEXT("chat"), TEXT("chat")}, {TEXT("world"), TEXT("world")}, {TEXT("people"), TEXT("people here")},
        {TEXT("build"), TEXT("build")}, {TEXT("settings"), TEXT("settings")}};
    for (const auto& P : Panels) { const FName Id(P.Key); Reg(FString(TEXT("panel:")) + P.Key, P.Value, TEXT("panels"), Id == "build" ? TEXT("B") : Id == "people" ? TEXT("Tab") : FString(), {TEXT("window"), TEXT("panel")}, [W, Id]() { if (W.IsValid()) W->ToggleFrame(Id); }, TEXT("open or close the window")); }
    Reg(TEXT("panel:help"), TEXT("help and keys"), TEXT("panels"), TEXT("H"), {TEXT("keys"), TEXT("controls"), TEXT("?")}, [W]() { if (W.IsValid()) W->ToggleHelp(); });
    Reg(TEXT("panel:emotes"), TEXT("emote bar"), TEXT("panels"), FString(), {TEXT("gestures")}, [W]() { if (W.IsValid()) W->ToggleFrame("emotes"); });
    // the body
    Reg(TEXT("body:sit"), TEXT("sit / stand"), TEXT("body"), TEXT("X"), {TEXT("seat"), TEXT("chair"), TEXT("posture")}, [B]() { if (auto* Br = B()) Br->HandleInteractionCommand(TEXT("/sit")); });
    Reg(TEXT("body:lie"), TEXT("lie down"), TEXT("body"), TEXT("Z"), {TEXT("posture"), TEXT("rest"), TEXT("sleep")}, [Explorer]() { if (auto* X = Explorer()) X->Posture = X->Posture == TEXT("lie") ? FString() : TEXT("lie"); });
    Reg(TEXT("body:fly"), TEXT("fly"), TEXT("body"), TEXT("F"), {TEXT("flight"), TEXT("wings"), TEXT("land")}, [B]() { if (auto* Br = B()) Br->HandleFlyKey(); });
    Reg(TEXT("body:fold"), TEXT("fold wings"), TEXT("body"), TEXT("G"), {TEXT("wings"), TEXT("unfold")}, [Explorer]() { if (auto* X = Explorer()) X->bFoldPressed = true; });
    Reg(TEXT("body:limp"), TEXT("go limp / get up"), TEXT("body"), TEXT("R"), {TEXT("ragdoll"), TEXT("fall"), TEXT("collapse")}, [B]() { if (auto* Br = B()) { if (Br->IsDowned()) Br->GetUp(); else Br->GoLimp(); } });
    Reg(TEXT("body:climb"), TEXT("climb"), TEXT("body"), TEXT("E"), {TEXT("ladder"), TEXT("use"), TEXT("helm")}, [B]() { if (auto* Br = B()) Br->HandleUseKey(); });
    // the view
    Reg(TEXT("view:photo"), TEXT("photo mode"), TEXT("view"), TEXT("P"), {TEXT("camera"), TEXT("free")}, [W]() { if (W.IsValid()) W->TogglePhotoMode(); });
    Reg(TEXT("view:shot"), TEXT("screenshot"), TEXT("view"), TEXT("F2"), {TEXT("picture"), TEXT("capture")}, []() { FScreenshotRequest::RequestScreenshot(TEXT("eidoverse-shot.png"), false, false); });
    Reg(TEXT("view:debug"), TEXT("debug overlay"), TEXT("view"), TEXT("F3"), {TEXT("colliders"), TEXT("physics")}, [B]() { if (auto* Br = B()) Br->ToggleDebugOverlay(); });
    Reg(TEXT("view:pointer"), TEXT("pointer / mouselook"), TEXT("view"), TEXT("M"), {TEXT("cursor"), TEXT("mouse")}, [W]() { if (W.IsValid()) W->SetPointerMode(!W->IsPointerMode()); });
    // voice
    Reg(TEXT("voice:mic"), TEXT("microphone on / off"), TEXT("voice"), TEXT("V"), {TEXT("mute"), TEXT("talk"), TEXT("voice")}, [B]() { if (auto* Br = B()) Br->SetMicOn(!Br->IsMicOn()); });
    Reg(TEXT("voice:hear"), TEXT("headphones on / off"), TEXT("voice"), FString(), {TEXT("hear"), TEXT("voices"), TEXT("listen"), TEXT("deafen")}, [B]() { if (auto* Br = B()) Br->SetHearVoices(!EidoSettings::HearVoices()); });
    // emotes, by their number keys
    for (int32 I = 0; I < Emotes.Num() && I < 9; ++I)
        if (Emotes[I].Value) Reg(FString::Printf(TEXT("emote:%d"), I), Emotes[I].Key, TEXT("emotes"), FString::FromInt(I + 1), {TEXT("emote"), TEXT("gesture")}, [W, I]() { if (W.IsValid()) W->Emote(I); });
}
