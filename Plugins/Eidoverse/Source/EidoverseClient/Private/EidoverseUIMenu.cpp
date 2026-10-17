// The ∃ menu (ui.js topItems): a waterfall of top rows with one flyout at a time beside its row.
//   Save world / Load world — listed, not offered; a press says why (builders / not yet)
//   Panels ▸ — voice, the lantern, every window with its rail pin, pointer, rail edge, lock, reset
//   Log in / Log out, Help (H), Keys, About ▸
// A row never dismisses the menu unless it takes you to another window. The menu closes on
// mouse-away (450 ms grace), on a press out in the world, and on Esc (the flyout first).
#include "EidoverseUI.h"
#include "EidoverseBridge.h"
#include "EidoverseActions.h"
#include "EidoverseSettings.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/ButtonSlot.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Image.h"
#include "Components/ScrollBox.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Dom/JsonObject.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/PlayerController.h"
#include "HttpModule.h"
#include "Interfaces/IHttpResponse.h"
#include "Misc/ConfigCacheIni.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace
{
    // ui.js, verbatim
    const TCHAR* NEEDS_RIGHTS = TEXT("needs build rights in this world");
    const TCHAR* SAVE_WHY = TEXT("not yet — this world already keeps every change as it happens (its log); save points need server support. /fork <name> copies the whole world today (owner)");
    const TCHAR* LOAD_WHY = TEXT("not yet — restoring a saved world needs server support; a copy made with /fork is its own world (?world=<name>)");
    const TCHAR* NO_LOGIN = TEXT("this server has no sign-in — it admits by door key");
    constexpr double AWAY_S = .45, HOVER_OPEN_S = .11, SUB_CLOSE_S = .25;

    FButtonStyle MenuButton()
    {
        FButtonStyle S;
        S.SetNormal(FEidoverseTheme::Rounded(FLinearColor::Transparent, 6));
        S.SetHovered(FEidoverseTheme::Rounded(FEidoverseTheme::Hover(), 6));
        S.SetPressed(FEidoverseTheme::Rounded(FEidoverseTheme::Well(), 6));
        S.SetNormalPadding(FMargin(0)); S.SetPressedPadding(FMargin(0));
        return S;
    }
    bool Shown(const UWidget* W) { return W && W->GetVisibility() != ESlateVisibility::Collapsed && W->GetVisibility() != ESlateVisibility::Hidden; }
    FString ClientVersion()
    {
        FString V;
        GConfig->GetString(TEXT("/Script/EngineSettings.GeneralProjectSettings"), TEXT("ProjectVersion"), V, GGameIni);
        return V.IsEmpty() ? TEXT("dev") : V;
    }
}

bool UEidoverseHUD::IsMenuOpen() const { return Shown(Menu); }

FString UEidoverseHUD::MenuDeadWhy(FName Id) const
{
    const bool bRights = Bridge && Bridge->CanBuild();
    if (Id == "save") return bRights ? SAVE_WHY : NEEDS_RIGHTS;
    if (Id == "load") return bRights ? LOAD_WHY : NEEDS_RIGHTS;
    // the native sign-in is the production one: a joined server elsewhere admits by its door key
    if (Id == "login" && Bridge && !Bridge->HasProductionSession() && Bridge->bJoined && Bridge->Endpoint != AEidoverseBridge::ProductionEndpoint) return NO_LOGIN;
    return FString();
}

UWidget* UEidoverseHUD::BuildMenu()
{
    auto Panel = [this](float Width) {
        auto* P = WidgetTree->ConstructWidget<UBorder>();
        P->SetBrush(FEidoverseTheme::Rounded(FEidoverseTheme::Panel(), 10, FEidoverseTheme::Edge(), 1));
        P->SetPadding(FMargin(6));
        PanelBorders.Add(P);
        auto* Size = WidgetTree->ConstructWidget<USizeBox>(); Size->SetWidthOverride(Width);
        P->SetContent(Size);
        return TPair<UBorder*, USizeBox*>(P, Size);
    };
    // one row: icon, name (+ dim detail), and a tail (a key, a tag or a caret)
    auto Row = [this](FName Id, const TCHAR* Icon, const FString& Text, const FString& Detail, const FString& Tail, bool bCaret,
                      UTextBlock** OutText = nullptr, UTextBlock** OutDetail = nullptr, UTextBlock** OutTail = nullptr, UImage** OutIcon = nullptr) {
        auto* B = WidgetTree->ConstructWidget<UButton>();
        B->SetStyle(MenuButton());
        auto* Line = WidgetTree->ConstructWidget<UHorizontalBox>();
        auto* Img = WidgetTree->ConstructWidget<UImage>();
        if (UTexture2D* T = FEidoverseTheme::IconTexture(Icon)) Img->SetBrushFromTexture(T);
        Img->SetDesiredSizeOverride(FVector2D(15)); Img->SetColorAndOpacity(FEidoverseTheme::Icon());
        Line->AddChildToHorizontalBox(Img)->SetVerticalAlignment(VAlign_Center);
        auto* Name = Label(Text, 11); Name->SetAutoWrapText(false);
        Line->AddChildToHorizontalBox(Name)->SetPadding(FMargin(10, 0, 0, 0));
        auto* Det = Label(Detail, 10, FEidoverseTheme::Dim()); Det->SetAutoWrapText(false);
        auto* DS = Line->AddChildToHorizontalBox(Det); DS->SetPadding(FMargin(6, 0, 0, 0)); DS->SetVerticalAlignment(VAlign_Center);
        DS->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
        if (bCaret)
        {
            auto* C = WidgetTree->ConstructWidget<UImage>();
            if (UTexture2D* T = FEidoverseTheme::IconTexture(TEXT("Caret"))) C->SetBrushFromTexture(T);
            C->SetDesiredSizeOverride(FVector2D(10)); C->SetColorAndOpacity(FEidoverseTheme::Dim());
            Line->AddChildToHorizontalBox(C)->SetVerticalAlignment(VAlign_Center);
        }
        else
        {
            auto* T = Label(Tail, 9, FEidoverseTheme::Dim(), true); T->SetAutoWrapText(false);
            Line->AddChildToHorizontalBox(T)->SetVerticalAlignment(VAlign_Center);
            if (OutTail) *OutTail = T;
        }
        auto* Pad = WidgetTree->ConstructWidget<UBorder>();
        Pad->SetBrushColor(FLinearColor::Transparent); Pad->SetPadding(FMargin(8, 5)); Pad->SetContent(Line);
        B->SetContent(Pad);
        if (auto* CS = Cast<UButtonSlot>(B->GetContentSlot())) CS->SetHorizontalAlignment(HAlign_Fill);
        B->OnClicked.AddDynamic(this, &UEidoverseHUD::OnMenuEntry);
        if (OutText) *OutText = Name;
        if (OutDetail) *OutDetail = Det;
        if (OutIcon) *OutIcon = Img;
        return B;
    };
    auto Sep = [this](UVerticalBox* Col) {
        auto* S = WidgetTree->ConstructWidget<UBorder>();
        S->SetBrushColor(FEidoverseTheme::Edge());
        auto* H = WidgetTree->ConstructWidget<USizeBox>(); H->SetHeightOverride(1); S->SetContent(H);
        Col->AddChildToVerticalBox(S)->SetPadding(FMargin(8, 4));
    };

    // ---- the top level
    auto [Top, TopSize] = Panel(230);
    auto* Col = WidgetTree->ConstructWidget<UVerticalBox>();
    TopSize->SetContent(Col);
    auto AddTop = [&](FName Id, UButton* B) { ButtonIds.Add(B, FName(*(TEXT("top:") + Id.ToString()))); MenuRows.Add(Id, B); Col->AddChildToVerticalBox(B); };
    UTextBlock *SaveName = nullptr, *LoadName = nullptr, *SaveTag = nullptr, *LoadTag = nullptr, *LoginName = nullptr, *LoginDetail = nullptr;
    UImage* LoginIcon = nullptr;
    AddTop("save", Row("save", TEXT("Save"), TEXT("Save world"), FString(), TEXT("builders"), false, &SaveName, nullptr, &SaveTag));
    AddTop("load", Row("load", TEXT("Load"), TEXT("Load world"), FString(), TEXT("builders"), false, &LoadName, nullptr, &LoadTag));
    MenuSaveTag = SaveTag; MenuLoadTag = LoadTag;
    // listed, not offered: dim
    for (UTextBlock* T : {SaveName, LoadName}) if (T) T->SetColorAndOpacity(FEidoverseTheme::Dim());
    Sep(Col);
    AddTop("panels", Row("panels", TEXT("Layout"), TEXT("Panels"), FString(), FString(), true));
    Sep(Col);
    AddTop("login", Row("login", TEXT("SignIn"), TEXT("Log in"), TEXT("with Discord"), FString(), false, &LoginName, &LoginDetail, nullptr, &LoginIcon));
    MenuLoginLabel = LoginName; MenuLoginDetail = LoginDetail; MenuLoginIcon = LoginIcon;
    AddTop("help", Row("help", TEXT("Help"), TEXT("Help"), FString(), TEXT("H"), false));
    AddTop("keys", Row("keys", TEXT("Keys"), TEXT("Keys"), FString(), FString(), false));
    AddTop("about", Row("about", TEXT("Info"), TEXT("About"), FString(), FString(), true));
    Top->SetVisibility(ESlateVisibility::Collapsed);

    // ---- the flyout: Panels (built once) or About (lines)
    auto [Sub, SubSize] = Panel(250);
    MenuSub = Sub;
    auto* SubCol = WidgetTree->ConstructWidget<UVerticalBox>();
    SubSize->SetContent(SubCol);
    auto* PanelsCol = WidgetTree->ConstructWidget<UVerticalBox>();
    MenuSubPanels = PanelsCol;
    SubCol->AddChildToVerticalBox(PanelsCol);
    MenuSubAbout = WidgetTree->ConstructWidget<UVerticalBox>();
    SubCol->AddChildToVerticalBox(MenuSubAbout);
    struct FEntry { FName Id; const TCHAR* Icon; const TCHAR* Text; bool bPin; };
    const FEntry Voice[] = {{"mic", TEXT("Mic"), TEXT("mic  ·  V"), false}, {"ear", TEXT("Ear"), TEXT("headphones"), false}};
    const FEntry Lantern[] = {{"lantern", TEXT("Search"), PLATFORM_MAC ? TEXT("lantern  ·  Cmd K") : TEXT("lantern  ·  Ctrl K"), false}};
    const FEntry Windows[] = {
        {"profile", TEXT("Profile"), TEXT("profile"), true}, {"chat", TEXT("Chat"), TEXT("chat"), true},
        {"world", TEXT("World"), TEXT("world  \u00B7  travel"), true}, {"emotes", TEXT("Emotes"), TEXT("emotes"), true},
        {"people", TEXT("People"), TEXT("people here  ·  Tab"), true}, {"build", TEXT("Build"), TEXT("build  ·  B"), true},
        {"settings", TEXT("Settings"), TEXT("settings"), true}, {"help", TEXT("Help"), TEXT("help  ·  H"), true}};
    const FEntry Layout[] = {
        {"pointer", TEXT("Eye"), TEXT("pointer / mouselook  ·  M"), false}, {"railedge", TEXT("Move"), TEXT("rail on the other side"), false},
        {"lock", TEXT("Lock"), TEXT("layout unlocked"), false}, {"reset", TEXT("Reset"), TEXT("reset layout"), false}};
    auto AddEntries = [&](TArrayView<const FEntry> Entries) {
        for (const FEntry& E : Entries)
        {
            UTextBlock* Text = nullptr; UImage* Icon = nullptr;
            UButton* B = Row(E.Id, E.Icon, E.Text, FString(), FString(), false, &Text, nullptr, nullptr, &Icon);
            ButtonIds.Add(B, E.Id);
            MenuStateIcons.Add(E.Id, Icon);
            if (E.Id == "lock") LockLabel = Text;
            if (!E.bPin) { PanelsCol->AddChildToVerticalBox(B); continue; }
            // windows get a pin beside them: pinned icons stay on the rail when closed
            auto* Line = WidgetTree->ConstructWidget<UHorizontalBox>();
            Line->AddChildToHorizontalBox(B)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
            auto* Pin = WidgetTree->ConstructWidget<UButton>();
            Pin->SetStyle(MenuButton());
            auto* PinText = Label(TEXT("pinned"), 9, FEidoverseTheme::Dim()); PinText->SetAutoWrapText(false);
            Pin->SetContent(PinText); Pin->SetToolTipText(FText::FromString(TEXT("keep on the rail when closed")));
            Pin->OnClicked.AddDynamic(this, &UEidoverseHUD::OnMenuEntry);
            ButtonIds.Add(Pin, FName(*(TEXT("pin:") + E.Id.ToString())));
            MenuPinLabels.Add(E.Id, PinText);
            Line->AddChildToHorizontalBox(Pin)->SetVerticalAlignment(VAlign_Center);
            PanelsCol->AddChildToVerticalBox(Line);
        }
    };
    AddEntries(Voice); Sep(PanelsCol);
    AddEntries(Lantern); Sep(PanelsCol);
    AddEntries(Windows); Sep(PanelsCol);
    AddEntries(Layout);
    Sub->SetVisibility(ESlateVisibility::Collapsed);
    return Top;
}

void UEidoverseHUD::SetMenuOpen(bool bOpen)
{
    if (!Menu) return;
    if (!bOpen) { CloseMenuSub(); Menu->SetVisibility(ESlateVisibility::Collapsed); return; }
    Menu->SetVisibility(ESlateVisibility::Visible);
    MenuAwaySince = 0; MenuSubCloseAt = 0; MenuHoverRow = NAME_None;
    MenuOpenCursor = FSlateApplication::IsInitialized() ? FSlateApplication::Get().GetCursorPos() : FVector2D::ZeroVector;
    PlaceMenu();
}

void UEidoverseHUD::OpenMenuSub(FName Id)
{
    if (!MenuSub || (Id != "panels" && Id != "about")) return;
    MenuSubCloseAt = 0;
    MenuSubFor = Id;
    if (MenuSubPanels) MenuSubPanels->SetVisibility(Id == "panels" ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
    if (MenuSubAbout) MenuSubAbout->SetVisibility(Id == "about" ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
    if (Id == "about") RefreshAbout();
    MenuSub->SetVisibility(ESlateVisibility::Visible);
    PlaceMenu();
}

void UEidoverseHUD::CloseMenuSub()
{
    MenuSubFor = NAME_None; MenuSubCloseAt = 0;
    if (MenuSub) MenuSub->SetVisibility(ESlateVisibility::Collapsed);
}

void UEidoverseHUD::RunMenuRow(FName Id)
{
    if (Id == "panels" || Id == "about") { OpenMenuSub(Id); return; }   // opens, never toggles shut: a hover may have opened it a moment ago
    CloseMenuSub();
    const FString Why = MenuDeadWhy(Id);
    if (!Why.IsEmpty())
    {
        // listed, not offered — and it SAYS why on a press too
        const TCHAR* Name = Id == "save" ? TEXT("Save world") : Id == "load" ? TEXT("Load world") : TEXT("Log in");
        Toast(FString::Printf(TEXT("%s — %s"), Name, *Why));
        return;
    }
    if (Id == "login")
    {
        SetMenuOpen(false);
        if (!Bridge) return;
        if (Bridge->HasProductionSession()) Bridge->LogoutProduction(); else Bridge->BeginProductionLogin();
    }
    else if (Id == "help") { SetMenuOpen(false); ToggleHelp(); }
    else if (Id == "keys") { SetMenuOpen(false); OpenKeys(); }
}

void UEidoverseHUD::OpenKeys()
{
    if (HelpOverlay && HelpOverlay->GetVisibility() == ESlateVisibility::Collapsed) ToggleHelp();
    if (HelpScroll && HelpKeysHeading) HelpScroll->ScrollWidgetIntoView(HelpKeysHeading, false, EDescendantScrollDestination::TopOrLeft);
}

TArray<FString> UEidoverseHUD::GetMenuRows() const
{
    TArray<FString> Out;
    const TPair<FName, const TCHAR*> Order[] = {{"save", TEXT("Save world")}, {"load", TEXT("Load world")}, {"panels", TEXT("Panels")},
        {"login", nullptr}, {"help", TEXT("Help")}, {"keys", TEXT("Keys")}, {"about", TEXT("About")}};
    for (const auto& P : Order)
    {
        const FString Name = P.Value ? FString(P.Value) : MenuLoginLabel ? MenuLoginLabel->GetText().ToString() : FString();
        const FString Tail = P.Key == "panels" || P.Key == "about" ? TEXT("▸") : P.Key == "help" ? TEXT("H")
            : P.Key == "save" && MenuSaveTag ? MenuSaveTag->GetText().ToString() : P.Key == "load" && MenuLoadTag ? MenuLoadTag->GetText().ToString() : FString();
        Out.Add(FString::Printf(TEXT("%s|%s|%s|%s"), *P.Key.ToString(), *Name, *Tail, *MenuDeadWhy(P.Key)));
    }
    return Out;
}

// About: what this build is, and what the server's /version says it is
void UEidoverseHUD::RefreshAbout()
{
    auto Paint = [this]() {
        if (!MenuSubAbout) return;
        MenuSubAbout->ClearChildren();
        MenuSubAbout->AddChildToVerticalBox(Label(TEXT("eidoverse-unreal"), 11, FEidoverseTheme::Text(), true))->SetPadding(FMargin(8, 4));
        for (const FString& L : AboutLines) { auto* T = Label(L, 10, FEidoverseTheme::Dim()); MenuSubAbout->AddChildToVerticalBox(T)->SetPadding(FMargin(8, 2)); }
    };
    AboutLines = {FString::Printf(TEXT("client %s · built %s"), *ClientVersion(), ANSI_TO_TCHAR(__DATE__))};
    const FString Base = Bridge && Bridge->bJoined ? Bridge->HttpBase() : FString();
    if (Base.IsEmpty()) { AboutLines.Add(TEXT("server — not joined")); Paint(); return; }
    AboutLines.Add(TEXT("asking the server…"));
    Paint();
    TWeakObjectPtr<UEidoverseHUD> Weak(this);
    auto Req = FHttpModule::Get().CreateRequest();
    Req->SetURL(Base + TEXT("/version")); Req->SetVerb(TEXT("GET")); Req->SetTimeout(8);
    Req->OnProcessRequestComplete().BindLambda([Weak, Paint](FHttpRequestPtr, FHttpResponsePtr R, bool bOk) {
        if (!Weak.IsValid()) return;
        UEidoverseHUD* H = Weak.Get();
        H->AboutLines.SetNum(1);
        TSharedPtr<FJsonObject> V;
        if (bOk && R.IsValid() && R->GetResponseCode() == 200 && FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(R->GetContentAsString()), V) && V.IsValid())
        {
            FString Sha = V->GetStringField(TEXT("sha")).Left(7);
            if (Sha.IsEmpty()) Sha = TEXT("unknown");
            bool bDirty = false;
            if (V->TryGetBoolField(TEXT("dirty"), bDirty) && bDirty) Sha += TEXT(" +dirty");
            auto When = [&V](const TCHAR* F) {
                FString S; FDateTime T;
                if (!V->TryGetStringField(F, S) || S == TEXT("unknown") || !FDateTime::ParseIso8601(*S, T)) return FString(TEXT("unknown"));
                static const TCHAR* Mon[] = {TEXT("Jan"), TEXT("Feb"), TEXT("Mar"), TEXT("Apr"), TEXT("May"), TEXT("Jun"), TEXT("Jul"), TEXT("Aug"), TEXT("Sep"), TEXT("Oct"), TEXT("Nov"), TEXT("Dec")};
                const FDateTime L = T + (FDateTime::Now() - FDateTime::UtcNow());
                return FString::Printf(TEXT("%d %s %d, %02d:%02d"), L.GetDay(), Mon[L.GetMonth() - 1], L.GetYear(), L.GetHour(), L.GetMinute());
            };
            H->AboutLines.Add(FString::Printf(TEXT("server build %s"), *Sha));
            H->AboutLines.Add(FString::Printf(TEXT("code from %s"), *When(TEXT("commitTime"))));
            H->AboutLines.Add(FString::Printf(TEXT("server up since %s"), *When(TEXT("startedAt"))));
        }
        else H->AboutLines.Add(TEXT("server build unknown — /version did not answer"));
        if (H->MenuSubFor == "about") Paint();
    });
    Req->ProcessRequest();
}

// Beside the rail's ∃, away from the rail's edge; the flyout beside its row, clamped to the screen.
void UEidoverseHUD::PlaceMenu()
{
    if (auto* S = Menu ? Cast<UCanvasPanelSlot>(Menu->Slot) : nullptr)
    {
        S->SetAnchors(FAnchors(bRailRight ? 1.f : 0.f, 0.f));
        S->SetAlignment(FVector2D(bRailRight ? 1.f : 0.f, 0.f));
        S->SetPosition(FVector2D(bRailRight ? -62.f : 62.f, 8.f));
    }
    auto* SS = MenuSub ? Cast<UCanvasPanelSlot>(MenuSub->Slot) : nullptr;
    UButton* RowB = MenuRows.FindRef(MenuSubFor);
    if (!SS || !RowB || !Canvas || !Shown(MenuSub)) return;
    const FGeometry& CG = Canvas->GetCachedGeometry();
    const FVector2D View = CG.GetLocalSize();
    if (View.X <= 0) return;
    const FGeometry& MG = Menu->GetCachedGeometry();
    const FVector2D MenuTL = CG.AbsoluteToLocal(MG.GetAbsolutePosition());
    const FVector2D MenuBR = CG.AbsoluteToLocal(MG.GetAbsolutePosition() + MG.GetAbsoluteSize());
    const float RowTop = CG.AbsoluteToLocal(RowB->GetCachedGeometry().GetAbsolutePosition()).Y;
    const FVector2D Size = MenuSub->GetDesiredSize();
    // away from the rail: right of the menu, unless the rail is on the right; the other side when that one has no room
    const float Right = MenuBR.X + 2, Left = MenuTL.X - 2 - Size.X;
    const bool bFitsR = Right + Size.X <= View.X - 4, bFitsL = Left >= 4;
    const float X = (bRailRight ? (bFitsL || !bFitsR) : !bFitsR) ? Left : Right;
    const float Y = FMath::Clamp(RowTop - 6, 4.f, FMath::Max(4.f, View.Y - Size.Y - 4));
    SS->SetAnchors(FAnchors(0, 0)); SS->SetAlignment(FVector2D::ZeroVector);
    SS->SetPosition(FVector2D(FMath::Max(4.f, X), Y));
}

void UEidoverseHUD::TickMenu()
{
    if (!IsMenuOpen()) return;
    const double Now = FPlatformTime::Seconds();
    // the top rows' state
    const bool bRights = Bridge && Bridge->CanBuild();
    if (MenuSaveTag) MenuSaveTag->SetText(FText::FromString(bRights ? TEXT("not yet") : TEXT("builders")));
    if (MenuLoadTag) MenuLoadTag->SetText(FText::FromString(bRights ? TEXT("not yet") : TEXT("builders")));
    const bool bAuthed = Bridge && Bridge->HasProductionSession();
    const FString LoginWhy = MenuDeadWhy("login");
    if (MenuLoginLabel)
    {
        MenuLoginLabel->SetText(FText::FromString(bAuthed ? TEXT("Log out") : TEXT("Log in")));
        MenuLoginLabel->SetColorAndOpacity(LoginWhy.IsEmpty() ? FEidoverseTheme::Text() : FEidoverseTheme::Dim());
    }
    if (MenuLoginDetail) MenuLoginDetail->SetText(FText::FromString(bAuthed ? (Bridge->LoginName.IsEmpty() ? Bridge->ParticipantName : Bridge->LoginName) : LoginWhy.IsEmpty() ? TEXT("with Discord") : TEXT("")));
    if (MenuLoginIcon && bAuthed != bMenuLoginOut)
    {
        bMenuLoginOut = bAuthed;
        if (UTexture2D* T = FEidoverseTheme::IconTexture(bAuthed ? TEXT("SignOut") : TEXT("SignIn"))) MenuLoginIcon->SetBrushFromTexture(T);
    }
    for (const auto& P : MenuRows) if (P.Value) P.Value->SetToolTipText(MenuDeadWhy(P.Key).IsEmpty() ? FText::GetEmpty() : FText::FromString(MenuDeadWhy(P.Key)));
    // the flyout's rows: brightness is the open state
    if (MenuSubFor == "panels")
        for (const auto& P : MenuStateIcons)
        {
            if (!P.Value) continue;
            const FName Id = P.Key;
            const bool bOn = Id == "mic" ? Bridge && Bridge->IsMicOn() : Id == "ear" ? EidoSettings::HearVoices() : Id == "lantern" ? IsLanternOpen()
                : Id == "help" ? Shown(HelpOverlay) : Id == "emotes" ? EmoteBar && EmoteBar->GetParent() && Shown(EmoteBar->GetParent())
                : Id == "pointer" ? bPointerMode : Id == "lock" ? bLayoutLocked : Id == "railedge" || Id == "reset" ? false : IsFrameOpen(Id);
            P.Value->SetColorAndOpacity(bOn ? FEidoverseTheme::Brand() : FEidoverseTheme::Icon());
        }
    PlaceMenu();

    // hover: a ▸ row opens its flyout after a beat; a plain row shuts another's flyout after a longer one,
    // which reaching the flyout cancels (the way there can graze the row below)
    FName Hover = NAME_None;
    for (const auto& P : MenuRows) if (P.Value && P.Value->IsHovered()) Hover = P.Key;
    if (Hover != MenuHoverRow) { MenuHoverRow = Hover; MenuHoverSince = Now; }
    const bool bSubRow = Hover == "panels" || Hover == "about";
    if (bSubRow) { MenuSubCloseAt = 0; if (Hover != MenuSubFor && Now - MenuHoverSince >= HOVER_OPEN_S) OpenMenuSub(Hover); }
    else if (Hover != NAME_None && MenuSubFor != NAME_None && MenuSubCloseAt == 0) MenuSubCloseAt = Now + SUB_CLOSE_S;
    if (Shown(MenuSub) && MenuSub->IsHovered()) MenuSubCloseAt = 0;
    if (MenuSubCloseAt > 0 && Now >= MenuSubCloseAt) { UE_LOG(LogTemp, Log, TEXT("EIDOVERSE_MENU flyout shut: hovered row %s"), *Hover.ToString()); CloseMenuSub(); }

    // mouse-away, armed once the pointer has moved since the menu opened (a menu opened by a key
    // or a script, with the mouse still, stays)
    UWidget* Mark = nullptr;
    if (UImage* I = RailIcons.FindRef("menu")) Mark = I->GetParent() ? I->GetParent()->GetParent() : nullptr;
    const bool bOver = Menu->IsHovered() || (Shown(MenuSub) && MenuSub->IsHovered()) || (Mark && Mark->IsHovered());
    const bool bMoved = FSlateApplication::IsInitialized() && FVector2D::Distance(FSlateApplication::Get().GetCursorPos(), MenuOpenCursor) > 2;
    if (!bMenuAutoClose) return;
    if (bOver || !bMoved) MenuAwaySince = 0;
    else if (MenuAwaySince == 0) MenuAwaySince = Now;
    else if (Now - MenuAwaySince >= AWAY_S) { UE_LOG(LogTemp, Log, TEXT("EIDOVERSE_MENU closed: mouse away (cursor moved %s)"), *(FSlateApplication::Get().GetCursorPos() - MenuOpenCursor).ToString()); SetMenuOpen(false); return; }
    // a press out in the world dismisses it
    if (APlayerController* PC = GetOwningPlayer(); PC && !bOver && (PC->WasInputKeyJustPressed(EKeys::LeftMouseButton) || PC->WasInputKeyJustPressed(EKeys::RightMouseButton)))
    { UE_LOG(LogTemp, Log, TEXT("EIDOVERSE_MENU closed: a press out in the world")); SetMenuOpen(false); }
}

void UEidoverseHUD::RegisterMenuActions()
{
    // the same acts, findable by name in the lantern ("save", "keys")
    TWeakObjectPtr<UEidoverseHUD> W(this);
    auto Reg = [](const FString& Id, const FString& Title, const FString& Group, TArray<FString> Keywords, TFunction<void()> Run, const FString& Detail = FString())
    { FEidoAction A; A.Id = Id; A.Title = Title; A.Group = Group; A.Keywords = MoveTemp(Keywords); A.Run = MoveTemp(Run); A.Detail = Detail; EidoActions::Register(A); };
    Reg(TEXT("menu:save"), TEXT("save world"), TEXT("world"), {TEXT("save"), TEXT("snapshot"), TEXT("world"), TEXT("file")}, [W]() { if (W.IsValid()) W->RunMenuRow("save"); }, TEXT("not yet — it tells you why"));
    Reg(TEXT("menu:load"), TEXT("load world"), TEXT("world"), {TEXT("load"), TEXT("open"), TEXT("restore"), TEXT("world"), TEXT("file")}, [W]() { if (W.IsValid()) W->RunMenuRow("load"); }, TEXT("not yet — it tells you why"));
    Reg(TEXT("menu:reset-layout"), TEXT("reset layout"), TEXT("panels"), {TEXT("layout"), TEXT("reset"), TEXT("default"), TEXT("panels")}, [W]() { if (W.IsValid()) W->ResetLayout(); }, TEXT("every window back where it started"));
    Reg(TEXT("menu:lock"), TEXT("lock / unlock the layout"), TEXT("panels"), {TEXT("layout"), TEXT("lock"), TEXT("unlock"), TEXT("panels")}, [W]() { if (W.IsValid()) W->SetLayoutLocked(!W->IsLayoutLocked()); });
    Reg(TEXT("menu:keys"), TEXT("keys"), TEXT("view"), {TEXT("keys"), TEXT("controls"), TEXT("shortcuts"), TEXT("bindings")}, [W]() { if (W.IsValid()) W->OpenKeys(); }, TEXT("the key table"));
    Reg(TEXT("menu:about"), TEXT("about this build"), TEXT("view"), {TEXT("about"), TEXT("version"), TEXT("build"), TEXT("sha")}, [W]() { if (W.IsValid()) { W->SetMenuOpen(true); W->OpenMenuSub("about"); } });
    Reg(TEXT("menu:login"), TEXT("log in / log out"), TEXT("view"), {TEXT("login"), TEXT("logout"), TEXT("sign in"), TEXT("sign out"), TEXT("account"), TEXT("discord")}, [W]() { if (W.IsValid()) W->RunMenuRow("login"); });
}
