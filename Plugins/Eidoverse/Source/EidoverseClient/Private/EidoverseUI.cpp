#include "EidoverseUI.h"
#include "EidoverseBridge.h"
#include "EidoverseSettings.h"
#include "GameFramework/GameUserSettings.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/ButtonSlot.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/EditableTextBox.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Image.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/ScrollBox.h"
#include "Components/SizeBox.h"
#include "Components/Spacer.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/WrapBox.h"
#include "Dom/JsonObject.h"
#include "Engine/Texture2D.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/PlayerController.h"
#include "HttpModule.h"
#include "Interfaces/IHttpResponse.h"
#include "Kismet/GameplayStatics.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Styling/CoreStyle.h"
#include "Misc/ConfigCacheIni.h"

// ---------------------------------------------------------------- theme

namespace
{
    FLinearColor Hex(uint32 Rgb, float Alpha = 1) { return FLinearColor(FColor((Rgb >> 16) & 255, (Rgb >> 8) & 255, Rgb & 255)).CopyWithNewOpacity(Alpha); }
}
FLinearColor FEidoverseTheme::Brand() { return Hex(0x8fe8c8); }
FLinearColor FEidoverseTheme::Attention() { return Hex(0xffc46b); }
FLinearColor FEidoverseTheme::Error() { return Hex(0xff8f8f); }
FLinearColor FEidoverseTheme::Text() { return Hex(0xebebe9); }
FLinearColor FEidoverseTheme::Dim() { return Hex(0x97979b); }
FLinearColor FEidoverseTheme::Icon() { return FMath::Lerp(Hex(0xc2c2c6), Brand(), .55f); }
FLinearColor FEidoverseTheme::Panel() { return Hex(0x051414, EidoSettings::PanelOpacity()); }
FLinearColor FEidoverseTheme::Chip() { return FLinearColor(0, 0, 0, .62f); }
FLinearColor FEidoverseTheme::Well() { return FLinearColor(0, 0, 0, .4f); }
FLinearColor FEidoverseTheme::Edge() { return FLinearColor(1, 1, 1, .10f); }
FLinearColor FEidoverseTheme::Hover() { return FLinearColor(1, 1, 1, .06f); }
FLinearColor FEidoverseTheme::Private() { return Hex(0xc4b5ff); }

FSlateBrush FEidoverseTheme::Rounded(const FLinearColor& Fill, float Radius, const FLinearColor& Outline, float Width)
{
    FSlateBrush B;
    B.DrawAs = ESlateBrushDrawType::RoundedBox;
    B.TintColor = FSlateColor(Fill);
    B.OutlineSettings = FSlateBrushOutlineSettings(FVector4(Radius, Radius, Radius, Radius), FSlateColor(Outline), Width);
    B.OutlineSettings.RoundingType = ESlateBrushRoundingType::FixedRadius;
    return B;
}
FSlateFontInfo FEidoverseTheme::Font(int32 Size, bool bBold)
{
    return FCoreStyle::GetDefaultFontStyle(bBold ? "Bold" : "Regular", Size);
}
UTexture2D* FEidoverseTheme::IconTexture(const FString& Name)
{
    const FString Path = FString::Printf(TEXT("/Eidoverse/UI/Icons/T_Icon_%s.T_Icon_%s"), *Name, *Name);
    return LoadObject<UTexture2D>(nullptr, *Path);
}

namespace
{
    FButtonStyle FlatButton(const FLinearColor& Normal, const FLinearColor& Hovered, float Radius = 6, const FLinearColor& Outline = FLinearColor::Transparent)
    {
        FButtonStyle S;
        S.SetNormal(FEidoverseTheme::Rounded(Normal, Radius, Outline, Outline.A > 0 ? 1 : 0));
        S.SetHovered(FEidoverseTheme::Rounded(Hovered, Radius, Outline, Outline.A > 0 ? 1 : 0));
        S.SetPressed(FEidoverseTheme::Rounded(FEidoverseTheme::Well(), Radius, Outline, Outline.A > 0 ? 1 : 0));
        S.SetDisabled(FEidoverseTheme::Rounded(FLinearColor::Transparent, Radius));
        S.SetNormalPadding(FMargin(0));
        S.SetPressedPadding(FMargin(0));
        return S;
    }

    // Minimal HTML -> runs: <kbd>x</kbd> becomes a key chip; other tags are dropped
    // and entities decoded. The help defs are trusted server markup (defs/ui/_help.json).
    struct FRun { FString Text; bool bKey = false; bool bBold = false; };
    TArray<FRun> ParseHelpHtml(const FString& Html)
    {
        TArray<FRun> Runs;
        FString Rest = Html, Current;
        bool bKey = false, bBold = false;
        auto Flush = [&]() {
            Current.ReplaceInline(TEXT("&amp;"), TEXT("&")); Current.ReplaceInline(TEXT("&lt;"), TEXT("<"));
            Current.ReplaceInline(TEXT("&gt;"), TEXT(">")); Current.ReplaceInline(TEXT("&nbsp;"), TEXT(" "));
            if (!Current.IsEmpty()) Runs.Add({Current, bKey, bBold});
            Current.Empty();
        };
        while (!Rest.IsEmpty())
        {
            int32 Open;
            if (!Rest.FindChar('<', Open)) { Current += Rest; break; }
            Current += Rest.Left(Open);
            int32 Close;
            if (!Rest.FindChar('>', Close) || Close < Open) { Current += Rest.Mid(Open); break; }
            const FString Tag = Rest.Mid(Open + 1, Close - Open - 1).TrimStartAndEnd().ToLower();
            Rest = Rest.Mid(Close + 1);
            if (Tag == TEXT("kbd")) { Flush(); bKey = true; }
            else if (Tag == TEXT("/kbd")) { Flush(); bKey = false; }
            else if (Tag == TEXT("b") || Tag == TEXT("strong")) { Flush(); bBold = true; }
            else if (Tag == TEXT("/b") || Tag == TEXT("/strong")) { Flush(); bBold = false; }
            else if (Tag == TEXT("br") || Tag == TEXT("br/")) { Current += TEXT("\n"); }
        }
        Flush();
        return Runs;
    }

    // Web chat semantics: @name or the bare whole-word name (mcpl/agent.ts).
    bool Mentions(const FString& Text, const FString& Me)
    {
        if (Me.IsEmpty()) return false;
        const FString Lower = Text.ToLower(), Name = Me.ToLower();
        int32 From = 0;
        while (true)
        {
            const int32 At = Lower.Find(Name, ESearchCase::CaseSensitive, ESearchDir::FromStart, From);
            if (At == INDEX_NONE) return false;
            const bool bStart = At == 0 || !FChar::IsAlnum(Lower[At - 1]);
            const int32 End = At + Name.Len();
            const bool bEnd = End >= Lower.Len() || !FChar::IsAlnum(Lower[End]);
            if (bStart && bEnd) return true;
            From = At + 1;
        }
    }
}

// ---------------------------------------------------------------- frame

void UEidoverseFrame::SetContent(UWidget* Content)
{
    TakeWidget();  // builds the tree on first use
    if (Body) Body->SetContent(Content);
}

TSharedRef<SWidget> UEidoverseFrame::RebuildWidget()
{
    if (WidgetTree && !WidgetTree->RootWidget)
    {
        auto* Root = WidgetTree->ConstructWidget<UVerticalBox>();
        WidgetTree->RootWidget = Root;
        auto* Head = WidgetTree->ConstructWidget<UHorizontalBox>();
        auto* Chip = WidgetTree->ConstructWidget<UBorder>();
        Chip->SetBrush(FEidoverseTheme::Rounded(FEidoverseTheme::Chip(), 5));
        Chip->SetPadding(FMargin(10, 3));
        auto* Name = WidgetTree->ConstructWidget<UTextBlock>();
        Name->SetText(FText::FromString(Title.ToString().ToUpper()));
        FSlateFontInfo F = FEidoverseTheme::Font(9, true);
        F.LetterSpacing = 140;
        Name->SetFont(F);
        Name->SetColorAndOpacity(FSlateColor(FEidoverseTheme::Text()));
        Chip->SetContent(Name);
        Head->AddChildToHorizontalBox(Chip);
        auto* Fill = WidgetTree->ConstructWidget<USpacer>();
        Head->AddChildToHorizontalBox(Fill)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
        auto* Close = WidgetTree->ConstructWidget<UButton>();
        Close->SetStyle(FlatButton(FEidoverseTheme::Chip(), FEidoverseTheme::Hover(), 5));
        auto* X = WidgetTree->ConstructWidget<UTextBlock>();
        X->SetText(FText::FromString(TEXT("×")));
        X->SetFont(FEidoverseTheme::Font(12));
        X->SetColorAndOpacity(FSlateColor(FEidoverseTheme::Text()));
        Close->SetContent(X);
        Close->OnClicked.AddDynamic(this, &UEidoverseFrame::HandleClose);
        Head->AddChildToHorizontalBox(Close)->SetPadding(FMargin(0, 0, 4, 0));
        Head->SetCursor(EMouseCursor::CardinalCross);  // drag handle
        Root->AddChildToVerticalBox(Head)->SetPadding(FMargin(8, 0, 0, 2));
        auto* Size = WidgetTree->ConstructWidget<USizeBox>();
        Size->SetWidthOverride(PanelSize.X);
        Size->SetHeightOverride(PanelSize.Y);
        SizeBox = Size;
        Body = WidgetTree->ConstructWidget<UBorder>();
        Body->SetBrush(FEidoverseTheme::Rounded(FEidoverseTheme::Panel(), 10, FEidoverseTheme::Edge(), 1));
        Body->SetPadding(FMargin(12));
        Size->SetContent(Body);
        Root->AddChildToVerticalBox(Size);
    }
    return Super::RebuildWidget();
}

namespace { constexpr float FrameHeader = 24, ResizeCorner = 18; const FVector2D MinFrame(200, 120); }

FVector2D UEidoverseFrame::CanvasPoint(const FPointerEvent& Event) const
{
    const UWidget* Parent = GetParent();
    return Parent ? Parent->GetCachedGeometry().AbsoluteToLocal(Event.GetScreenSpacePosition()) : Event.GetScreenSpacePosition();
}

bool UEidoverseFrame::MakeTopLeft()
{
    auto* Canvas = Cast<UCanvasPanelSlot>(Slot);
    if (!Canvas || !GetParent()) return false;
    const FAnchors A = Canvas->GetAnchors();
    if (A.Minimum == FVector2D::ZeroVector && A.Maximum == FVector2D::ZeroVector && Canvas->GetAlignment() == FVector2D::ZeroVector) return true;
    const FVector2D TopLeft = GetParent()->GetCachedGeometry().AbsoluteToLocal(GetCachedGeometry().GetAbsolutePosition());
    Canvas->SetAnchors(FAnchors(0, 0));
    Canvas->SetAlignment(FVector2D::ZeroVector);
    Canvas->SetPosition(TopLeft);
    return true;
}

void UEidoverseFrame::SetLayout(FVector2D Position, FVector2D Size)
{
    auto* Canvas = Cast<UCanvasPanelSlot>(Slot);
    if (!Canvas) return;
    Canvas->SetAnchors(FAnchors(0, 0));
    Canvas->SetAlignment(FVector2D::ZeroVector);
    Canvas->SetPosition(Position);
    PanelSize = FVector2D(FMath::Max(Size.X, MinFrame.X), FMath::Max(Size.Y, MinFrame.Y));
    if (SizeBox) { SizeBox->SetWidthOverride(PanelSize.X); SizeBox->SetHeightOverride(PanelSize.Y); }
}

bool UEidoverseFrame::GetLayout(FVector2D& Position, FVector2D& Size) const
{
    const auto* Canvas = Cast<UCanvasPanelSlot>(Slot);
    if (!Canvas || Canvas->GetAnchors().Minimum != FVector2D::ZeroVector || Canvas->GetAlignment() != FVector2D::ZeroVector) return false;
    Position = Canvas->GetPosition();
    Size = PanelSize;
    return true;
}

FReply UEidoverseFrame::NativeOnMouseButtonDown(const FGeometry& Geometry, const FPointerEvent& Event)
{
    if (bLocked || Event.GetEffectingButton() != EKeys::LeftMouseButton || !MakeTopLeft()) return Super::NativeOnMouseButtonDown(Geometry, Event);
    const FVector2D Local = Geometry.AbsoluteToLocal(Event.GetScreenSpacePosition()), Extent = Geometry.GetLocalSize();
    if (Local.X > Extent.X - ResizeCorner && Local.Y > Extent.Y - ResizeCorner) Drag = EDrag::Resize;
    else if (Local.Y < FrameHeader) Drag = EDrag::Move;
    else return Super::NativeOnMouseButtonDown(Geometry, Event);
    DragMouse = CanvasPoint(Event);
    DragPosition = Cast<UCanvasPanelSlot>(Slot)->GetPosition();
    DragSize = PanelSize;
    return FReply::Handled().CaptureMouse(TakeWidget());
}

FReply UEidoverseFrame::NativeOnMouseMove(const FGeometry& Geometry, const FPointerEvent& Event)
{
    if (Drag == EDrag::None) return Super::NativeOnMouseMove(Geometry, Event);
    const FVector2D Delta = CanvasPoint(Event) - DragMouse;
    const FVector2D Bounds = GetParent() ? GetParent()->GetCachedGeometry().GetLocalSize() : FVector2D(1e6);
    if (Drag == EDrag::Move)
    {
        const FVector2D Extent = GetCachedGeometry().GetLocalSize();
        FVector2D P = DragPosition + Delta;
        // Keep the title reachable: never fully off screen.
        P.X = FMath::Clamp(P.X, 40 - Extent.X, Bounds.X - 40);
        P.Y = FMath::Clamp(P.Y, 0.0, Bounds.Y - FrameHeader);
        Cast<UCanvasPanelSlot>(Slot)->SetPosition(P);
    }
    else
    {
        const FVector2D Max = Bounds - DragPosition - FVector2D(0, FrameHeader);
        SetLayout(DragPosition, FVector2D(FMath::Clamp(DragSize.X + Delta.X, MinFrame.X, FMath::Max(MinFrame.X, Max.X)),
                                          FMath::Clamp(DragSize.Y + Delta.Y, MinFrame.Y, FMath::Max(MinFrame.Y, Max.Y))));
    }
    return FReply::Handled();
}

FReply UEidoverseFrame::NativeOnMouseButtonUp(const FGeometry& Geometry, const FPointerEvent& Event)
{
    if (Drag == EDrag::None) return Super::NativeOnMouseButtonUp(Geometry, Event);
    Drag = EDrag::None;
    if (OnLayoutChanged) OnLayoutChanged();
    return FReply::Handled().ReleaseMouseCapture();
}

void UEidoverseFrame::HandleClose()
{
    SetVisibility(ESlateVisibility::Collapsed);
    OnClosed.Broadcast();
    if (OnLayoutChanged) OnLayoutChanged();
}

// ---------------------------------------------------------------- HUD helpers

UTextBlock* UEidoverseHUD::Label(const FString& Text, int32 Size, const FLinearColor& Color, bool bBold)
{
    auto* T = WidgetTree->ConstructWidget<UTextBlock>();
    T->SetText(FText::FromString(Text));
    T->SetFont(FEidoverseTheme::Font(Size, bBold));
    T->SetColorAndOpacity(FSlateColor(Color));
    T->SetAutoWrapText(true);
    return T;
}

UEditableTextBox* UEidoverseHUD::Input(const FString& Hint)
{
    auto* Box = WidgetTree->ConstructWidget<UEditableTextBox>();
    Box->SetHintText(FText::FromString(Hint));
    FEditableTextBoxStyle& S = Box->WidgetStyle;
    const FSlateBrush Well = FEidoverseTheme::Rounded(FEidoverseTheme::Well(), 6, FEidoverseTheme::Edge(), 1);
    S.SetBackgroundImageNormal(Well);
    S.SetBackgroundImageHovered(FEidoverseTheme::Rounded(FEidoverseTheme::Well(), 6, FLinearColor(1, 1, 1, .2f), 1));
    S.SetBackgroundImageFocused(FEidoverseTheme::Rounded(FEidoverseTheme::Well(), 6, FEidoverseTheme::Brand().CopyWithNewOpacity(.6f), 1));
    S.SetForegroundColor(FSlateColor(FEidoverseTheme::Text()));
    S.SetPadding(FMargin(8, 5));
    S.TextStyle.SetFont(FEidoverseTheme::Font(11));
    S.TextStyle.SetColorAndOpacity(FSlateColor(FEidoverseTheme::Text()));
    return Box;
}

UButton* UEidoverseHUD::TextButton(const FString& Text, bool bPrimary)
{
    auto* B = WidgetTree->ConstructWidget<UButton>();
    B->SetStyle(FlatButton(bPrimary ? FEidoverseTheme::Brand().CopyWithNewOpacity(.18f) : FEidoverseTheme::Well(),
        bPrimary ? FEidoverseTheme::Brand().CopyWithNewOpacity(.28f) : FEidoverseTheme::Hover(), 6,
        bPrimary ? FEidoverseTheme::Brand().CopyWithNewOpacity(.55f) : FEidoverseTheme::Edge()));
    auto* L = Label(Text, 11, bPrimary ? FEidoverseTheme::Brand() : FEidoverseTheme::Text());
    L->SetAutoWrapText(false);
    auto* Pad = WidgetTree->ConstructWidget<UBorder>();
    Pad->SetBrush(FSlateBrush());
    Pad->SetBrushColor(FLinearColor::Transparent);
    Pad->SetPadding(FMargin(10, 4));
    Pad->SetContent(L);
    B->SetContent(Pad);
    return B;
}

UButton* UEidoverseHUD::IconButton(const FString& Icon, float Size, FName Id)
{
    auto* B = WidgetTree->ConstructWidget<UButton>();
    B->SetStyle(FlatButton(FLinearColor::Transparent, FEidoverseTheme::Hover(), 8));
    auto* Img = WidgetTree->ConstructWidget<UImage>();
    if (UTexture2D* T = FEidoverseTheme::IconTexture(Icon)) Img->SetBrushFromTexture(T);
    Img->SetDesiredSizeOverride(FVector2D(Size));
    Img->SetColorAndOpacity(FEidoverseTheme::Icon());
    auto* Pad = WidgetTree->ConstructWidget<UBorder>();
    Pad->SetBrushColor(FLinearColor::Transparent);
    Pad->SetPadding(FMargin(7));
    Pad->SetContent(Img);
    B->SetContent(Pad);
    B->OnClicked.AddDynamic(this, &UEidoverseHUD::OnRailClicked);
    ButtonIds.Add(B, Id);
    RailIcons.Add(Id, Img);
    return B;
}

FName UEidoverseHUD::ResolveClicked() const
{
    for (const auto& Pair : ButtonIds)
        if (Pair.Key.IsValid() && Pair.Key->IsHovered()) return Pair.Value;
    return NAME_None;
}

UEidoverseFrame* UEidoverseHUD::AddFrame(FName Id, const FString& Title, UWidget* Content, FVector2D Size, FVector2D Anchor, FVector2D Alignment, FVector2D Offset, bool bOpen)
{
    auto* F = WidgetTree->ConstructWidget<UEidoverseFrame>(UEidoverseFrame::StaticClass());
    F->Title = FText::FromString(Title);
    F->PanelSize = Size;
    F->SetContent(Content);
    F->SetVisibility(bOpen ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
    auto* Slot = Canvas->AddChildToCanvas(F);
    Slot->SetAnchors(FAnchors(Anchor.X, Anchor.Y));
    Slot->SetAlignment(Alignment);
    Slot->SetPosition(Offset);
    Slot->SetAutoSize(true);
    Frames.Add(Id, F);
    FrameDefaults.Add(Id, {Size, Anchor, Alignment, Offset, bOpen});
    PanelBorders.Add(F->Body);
    F->OnLayoutChanged = [this]() { SaveLayout(); };
    return F;
}

// ---------------------------------------------------------------- build

TSharedRef<SWidget> UEidoverseHUD::RebuildWidget()
{
    if (WidgetTree && !WidgetTree->RootWidget) Build();
    return Super::RebuildWidget();
}

void UEidoverseHUD::Build()
{
    EidoSettings::Load();
    Canvas = WidgetTree->ConstructWidget<UCanvasPanel>();
    WidgetTree->RootWidget = Canvas;

    auto Place = [this](UWidget* W, FVector2D Anchor, FVector2D Alignment, FVector2D Offset) {
        auto* S = Canvas->AddChildToCanvas(W);
        S->SetAnchors(FAnchors(Anchor.X, Anchor.Y));
        S->SetAlignment(Alignment);
        S->SetPosition(Offset);
        S->SetAutoSize(true);
        return S;
    };
    Place(BuildRail(), {0, 0}, {0, 0}, {8, 8});
    Place(BuildEmoteBar(), {.5f, 0}, {.5f, 0}, {0, 10});
    AddFrame("chat", TEXT("Chat"), BuildChat(), {520, 300}, {0, 1}, {0, 1}, {10, -10}, true);
    AddFrame("world", TEXT("World"), BuildWorld(), {380, 400}, {1, 0}, {1, 0}, {-10, 10}, true);
    AddFrame("people", TEXT("People here"), BuildPeople(), {240, 280}, {1, .5f}, {1, .5f}, {-10, 40}, false);
    AddFrame("profile", TEXT("Profile"), BuildProfile(), {300, 170}, {0, 0}, {0, 0}, {62, 60}, false);
    AddFrame("settings", TEXT("Settings"), BuildSettings(), {320, 390}, {0, 0}, {0, 0}, {62, 280}, false);
    Menu = Cast<UBorder>(BuildMenu());
    Place(Menu, {0, 0}, {0, 0}, {62, 8});
    ToastBox = WidgetTree->ConstructWidget<UVerticalBox>();
    Place(ToastBox, {1, 1}, {1, 1}, {-10, -10});
    HintChip = WidgetTree->ConstructWidget<UBorder>();
    HintChip->SetBrush(FEidoverseTheme::Rounded(FEidoverseTheme::Chip(), 14));
    HintChip->SetPadding(FMargin(14, 6));
    HintText = Label(TEXT(""), 11);
    HintText->SetAutoWrapText(false);
    HintChip->SetContent(HintText);
    HintChip->SetVisibility(ESlateVisibility::Collapsed);
    Place(HintChip, {.5f, 1}, {.5f, 1}, {0, -24});
    auto* DoorWidget = BuildDoor();
    auto* DoorSlot = Canvas->AddChildToCanvas(DoorWidget);
    DoorSlot->SetAnchors(FAnchors(0, 0, 1, 1));
    DoorSlot->SetOffsets(FMargin(0));
    // Toasts stay above the door (join errors are reported there).
    Canvas->RemoveChild(ToastBox);
    Place(ToastBox, {1, 1}, {1, 1}, {-10, -10});
    auto* Help = BuildHelp();
    auto* HelpSlot = Canvas->AddChildToCanvas(Help);
    HelpSlot->SetAnchors(FAnchors(0, 0, 1, 1));
    HelpSlot->SetOffsets(FMargin(0));
    RefreshRail();
    LoadLayout();
}

namespace { const TCHAR* LayoutSection = TEXT("/Script/EidoverseClient.Layout"); }

void UEidoverseHUD::SaveLayout()
{
    if (bSuppressLayoutSave) return;
    for (const auto& Pair : Frames)
    {
        FVector2D P, Size;
        const bool bMoved = Pair.Value && Pair.Value->GetLayout(P, Size);
        const FString Value = bMoved ? FString::Printf(TEXT("%.0f,%.0f,%.0f,%.0f,%d"), P.X, P.Y, Size.X, Size.Y, IsFrameOpen(Pair.Key) ? 1 : 0)
                                     : FString::Printf(TEXT(",,,,%d"), IsFrameOpen(Pair.Key) ? 1 : 0);
        GConfig->SetString(LayoutSection, *Pair.Key.ToString(), *Value, GGameUserSettingsIni);
    }
    GConfig->SetBool(LayoutSection, TEXT("Locked"), bLayoutLocked, GGameUserSettingsIni);
    GConfig->Flush(false, GGameUserSettingsIni);
}

void UEidoverseHUD::LoadLayout()
{
    TGuardValue<bool> Guard(bSuppressLayoutSave, true);
    bool bLocked = false;
    GConfig->GetBool(LayoutSection, TEXT("Locked"), bLocked, GGameUserSettingsIni);
    SetLayoutLocked(bLocked);
    for (const auto& Pair : Frames)
    {
        FString Value;
        if (!Pair.Value || !GConfig->GetString(LayoutSection, *Pair.Key.ToString(), Value, GGameUserSettingsIni)) continue;
        TArray<FString> Parts;
        Value.ParseIntoArray(Parts, TEXT(","), false);
        if (Parts.Num() != 5) continue;
        if (!Parts[0].IsEmpty())
            Pair.Value->SetLayout(FVector2D(FCString::Atof(*Parts[0]), FCString::Atof(*Parts[1])), FVector2D(FCString::Atof(*Parts[2]), FCString::Atof(*Parts[3])));
        ShowFrame(Pair.Key, Parts[4] == TEXT("1"));
    }
}

void UEidoverseHUD::SetLayoutLocked(bool bLocked)
{
    bLayoutLocked = bLocked;
    for (const auto& Pair : Frames) if (Pair.Value) Pair.Value->bLocked = bLocked;
    if (LockLabel) LockLabel->SetText(FText::FromString(bLocked ? TEXT("layout locked") : TEXT("layout unlocked")));
    SaveLayout();
}

void UEidoverseHUD::ResetLayout()
{
    {
        TGuardValue<bool> Guard(bSuppressLayoutSave, true);
        for (const auto& Pair : Frames)
        {
            const FFrameDefault* D = FrameDefaults.Find(Pair.Key);
            auto* CanvasSlot = Pair.Value ? Cast<UCanvasPanelSlot>(Pair.Value->Slot) : nullptr;
            if (!D || !CanvasSlot) continue;
            Pair.Value->SetLayout(FVector2D::ZeroVector, D->Size);
            CanvasSlot->SetAnchors(FAnchors(D->Anchor.X, D->Anchor.Y));
            CanvasSlot->SetAlignment(D->Alignment);
            CanvasSlot->SetPosition(D->Offset);
            ShowFrame(Pair.Key, D->bOpen);
        }
        GConfig->EmptySection(LayoutSection, GGameUserSettingsIni);
    }
    SetLayoutLocked(false);
    Toast(TEXT("Layout reset."));
}

UWidget* UEidoverseHUD::BuildRail()
{
    auto* Panel = WidgetTree->ConstructWidget<UBorder>();
    Panel->SetBrush(FEidoverseTheme::Rounded(FEidoverseTheme::Panel(), 10, FEidoverseTheme::Edge(), 1));
    Panel->SetPadding(FMargin(3, 4));
    PanelBorders.Add(Panel);
    auto* Column = WidgetTree->ConstructWidget<UVerticalBox>();
    Panel->SetContent(Column);
    // ∃ leads the rail; its ink is the connection truth.
    Column->AddChildToVerticalBox(IconButton(TEXT("Mark"), 22, "menu"));
    MarkIcon = RailIcons.FindRef("menu");
    const TPair<FName, const TCHAR*> Entries[] = {
        {"profile", TEXT("Profile")}, {"chat", TEXT("Chat")}, {"world", TEXT("World")},
        {"emotes", TEXT("Emotes")}, {"people", TEXT("People")}, {"settings", TEXT("Settings")}, {"help", TEXT("Search")}};
    for (const auto& E : Entries) Column->AddChildToVerticalBox(IconButton(E.Value, 20, E.Key));
    return Panel;
}

UWidget* UEidoverseHUD::BuildMenu()
{
    auto* Panel = WidgetTree->ConstructWidget<UBorder>();
    Panel->SetBrush(FEidoverseTheme::Rounded(FEidoverseTheme::Panel(), 10, FEidoverseTheme::Edge(), 1));
    Panel->SetPadding(FMargin(8));
    PanelBorders.Add(Panel);
    auto* Width = WidgetTree->ConstructWidget<USizeBox>();
    Width->SetWidthOverride(210);
    auto* Column = WidgetTree->ConstructWidget<UVerticalBox>();
    Width->SetContent(Column);
    Panel->SetContent(Width);
    Column->AddChildToVerticalBox(Label(TEXT("MENU"), 9, FEidoverseTheme::Dim(), true))->SetPadding(FMargin(8, 0, 8, 6));
    struct FEntry { FName Id; const TCHAR* Icon; const TCHAR* Text; };
    const FEntry Entries[] = {
        {"profile", TEXT("Profile"), TEXT("profile")}, {"chat", TEXT("Chat"), TEXT("chat")},
        {"world", TEXT("World"), TEXT("world")}, {"emotes", TEXT("Emotes"), TEXT("emotes")},
        {"people", TEXT("People"), TEXT("people here  \u00B7  Tab")}, {"settings", TEXT("Settings"), TEXT("settings")}, {"help", TEXT("Search"), TEXT("help  \u00B7  ?")},
        {"pointer", TEXT("Eye"), TEXT("pointer / mouselook  \u00B7  M")},
        {"lock", TEXT("Lock"), TEXT("layout unlocked")}, {"reset", TEXT("Reset"), TEXT("reset layout")}};
    for (const FEntry& E : Entries)
    {
        auto* B = WidgetTree->ConstructWidget<UButton>();
        B->SetStyle(FlatButton(FLinearColor::Transparent, FEidoverseTheme::Hover(), 6));
        auto* Row = WidgetTree->ConstructWidget<UHorizontalBox>();
        auto* Img = WidgetTree->ConstructWidget<UImage>();
        if (UTexture2D* T = FEidoverseTheme::IconTexture(E.Icon)) Img->SetBrushFromTexture(T);
        Img->SetDesiredSizeOverride(FVector2D(15));
        Img->SetColorAndOpacity(FEidoverseTheme::Icon());
        Row->AddChildToHorizontalBox(Img)->SetVerticalAlignment(VAlign_Center);
        auto* Text = Label(E.Text, 11);
        Text->SetAutoWrapText(false);
        if (E.Id == "lock") LockLabel = Text;
        Row->AddChildToHorizontalBox(Text)->SetPadding(FMargin(10, 0, 0, 0));
        auto* Pad = WidgetTree->ConstructWidget<UBorder>();
        Pad->SetBrushColor(FLinearColor::Transparent);
        Pad->SetPadding(FMargin(8, 5));
        Pad->SetContent(Row);
        B->SetContent(Pad);
        if (auto* ContentSlot = Cast<UButtonSlot>(B->GetContentSlot())) ContentSlot->SetHorizontalAlignment(HAlign_Fill);
        B->OnClicked.AddDynamic(this, &UEidoverseHUD::OnMenuEntry);
        ButtonIds.Add(B, E.Id);
        Column->AddChildToVerticalBox(B);
    }
    Panel->SetVisibility(ESlateVisibility::Collapsed);
    return Panel;
}

UWidget* UEidoverseHUD::BuildChat()
{
    auto* Column = WidgetTree->ConstructWidget<UVerticalBox>();
    auto* Tabs = WidgetTree->ConstructWidget<UHorizontalBox>();
    const TCHAR* Names[] = {TEXT("all"), TEXT("mentions"), TEXT("system")};
    for (int32 I = 0; I < 3; ++I)
    {
        auto* B = TextButton(Names[I]);
        if (I == 0) B->OnClicked.AddDynamic(this, &UEidoverseHUD::OnChatTabAll);
        if (I == 1) B->OnClicked.AddDynamic(this, &UEidoverseHUD::OnChatTabMentions);
        if (I == 2) B->OnClicked.AddDynamic(this, &UEidoverseHUD::OnChatTabSystem);
        ChatTabs.Add(B);
        Tabs->AddChildToHorizontalBox(B)->SetPadding(FMargin(0, 0, 6, 0));
    }
    Column->AddChildToVerticalBox(Tabs)->SetPadding(FMargin(0, 0, 0, 6));
    ChatLog = WidgetTree->ConstructWidget<UScrollBox>();
    Column->AddChildToVerticalBox(ChatLog)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
    TypingLine = Label(TEXT(""), 10, FEidoverseTheme::Dim());
    TypingLine->SetVisibility(ESlateVisibility::Collapsed);
    Column->AddChildToVerticalBox(TypingLine)->SetPadding(FMargin(2, 4, 0, 0));
    ChatInput = Input(TEXT("say something…  @ to mention · / for commands"));
    ChatInput->OnTextChanged.AddDynamic(this, &UEidoverseHUD::OnChatChanged);
    ChatInput->OnTextCommitted.AddDynamic(this, &UEidoverseHUD::OnChatCommitted);
    Column->AddChildToVerticalBox(ChatInput)->SetPadding(FMargin(0, 6, 0, 0));
    return Column;
}

UWidget* UEidoverseHUD::BuildWorld()
{
    auto* Scroll = WidgetTree->ConstructWidget<UScrollBox>();
    auto* Column = WidgetTree->ConstructWidget<UVerticalBox>();
    Scroll->AddChild(Column);
    auto Section = [&](const FString& Text) {
        Column->AddChildToVerticalBox(Label(Text.ToUpper(), 9, FEidoverseTheme::Dim(), true))->SetPadding(FMargin(0, 10, 0, 4));
    };
    Section(TEXT("Here"));
    WorldStatus = Label(TEXT("offline"), 11);
    Column->AddChildToVerticalBox(WorldStatus);
    Section(TEXT("Go to a world"));
    WorldName = Input(TEXT("world name, e.g. commons or water"));
    Column->AddChildToVerticalBox(WorldName);
    auto* Row = WidgetTree->ConstructWidget<UWrapBox>();
    Row->SetInnerSlotPadding(FVector2D(6, 6));
    auto* Join = TextButton(TEXT("join"), true);
    Join->OnClicked.AddDynamic(this, &UEidoverseHUD::OnJoinClicked);
    Row->AddChildToWrapBox(Join);
    auto* Local = TextButton(TEXT("local commons"));
    Local->OnClicked.AddDynamic(this, &UEidoverseHUD::OnLocalCommonsClicked);
    Row->AddChildToWrapBox(Local);
    auto* Leave = TextButton(TEXT("disconnect"));
    Leave->OnClicked.AddDynamic(this, &UEidoverseHUD::OnDisconnectClicked);
    Row->AddChildToWrapBox(Leave);
    Column->AddChildToVerticalBox(Row)->SetPadding(FMargin(0, 6, 0, 0));
    Section(TEXT("Sign in"));
    LoginStatus = Label(TEXT(""), 10, FEidoverseTheme::Dim());
    Column->AddChildToVerticalBox(LoginStatus);
    auto* LoginRow = WidgetTree->ConstructWidget<UWrapBox>();
    LoginRow->SetInnerSlotPadding(FVector2D(6, 6));
    auto* SignIn = TextButton(TEXT("sign in with Discord"), true);
    SignIn->OnClicked.AddDynamic(this, &UEidoverseHUD::OnSignInClicked);
    LoginRow->AddChildToWrapBox(SignIn);
    auto* SignOut = TextButton(TEXT("sign out"));
    SignOut->OnClicked.AddDynamic(this, &UEidoverseHUD::OnSignOutClicked);
    LoginRow->AddChildToWrapBox(SignOut);
    Column->AddChildToVerticalBox(LoginRow)->SetPadding(FMargin(0, 6, 0, 0));
    Column->AddChildToVerticalBox(Label(TEXT("Joining shares your name, avatar and movement. Public chat is stored by the server."), 10, FEidoverseTheme::Dim()))->SetPadding(FMargin(0, 12, 0, 0));
    return Scroll;
}

UWidget* UEidoverseHUD::BuildPeople()
{
    auto* Column = WidgetTree->ConstructWidget<UVerticalBox>();
    PeopleCount = Label(TEXT(""), 10, FEidoverseTheme::Dim());
    Column->AddChildToVerticalBox(PeopleCount)->SetPadding(FMargin(0, 0, 0, 6));
    auto* Scroll = WidgetTree->ConstructWidget<UScrollBox>();
    PeopleList = WidgetTree->ConstructWidget<UVerticalBox>();
    Scroll->AddChild(PeopleList);
    Column->AddChildToVerticalBox(Scroll)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
    return Column;
}

UWidget* UEidoverseHUD::BuildProfile()
{
    auto* Column = WidgetTree->ConstructWidget<UVerticalBox>();
    Column->AddChildToVerticalBox(Label(TEXT("NAME"), 9, FEidoverseTheme::Dim(), true))->SetPadding(FMargin(0, 0, 0, 4));
    ProfileName = Input(TEXT("your name in the world"));
    Column->AddChildToVerticalBox(ProfileName);
    Column->AddChildToVerticalBox(Label(TEXT("Used when you join. Signed-in sessions use your account name."), 10, FEidoverseTheme::Dim()))->SetPadding(FMargin(0, 8, 0, 0));
    return Column;
}

UWidget* UEidoverseHUD::BuildSettings()
{
    auto* Scroll = WidgetTree->ConstructWidget<UScrollBox>();
    auto* Column = WidgetTree->ConstructWidget<UVerticalBox>();
    Scroll->AddChild(Column);
    auto Section = [&](const TCHAR* Text) {
        Column->AddChildToVerticalBox(Label(FString(Text).ToUpper(), 9, FEidoverseTheme::Dim(), true))->SetPadding(FMargin(0, 10, 0, 4));
    };
    // Web panel rule: every control operable without a keyboard (steppers, toggles).
    auto Row = [&](const TCHAR* Name, FName Key, TArray<TPair<FString, FName>> Buttons) {
        auto* R = WidgetTree->ConstructWidget<UHorizontalBox>();
        auto* Caption = Label(Name, 11);
        Caption->SetAutoWrapText(false);
        R->AddChildToHorizontalBox(Caption)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
        if (Key != NAME_None)
        {
            auto* Value = Label(TEXT(""), 11, FEidoverseTheme::Brand());
            Value->SetAutoWrapText(false);
            SettingValues.Add(Key, Value);
            R->AddChildToHorizontalBox(Value)->SetPadding(FMargin(8, 3, 8, 0));
        }
        for (const auto& B : Buttons)
        {
            auto* Button = TextButton(B.Key);
            Button->OnClicked.AddDynamic(this, &UEidoverseHUD::OnSettingClicked);
            ButtonIds.Add(Button, B.Value);
            R->AddChildToHorizontalBox(Button)->SetPadding(FMargin(4, 0, 0, 0));
        }
        Column->AddChildToVerticalBox(R)->SetPadding(FMargin(0, 3));
    };
    Section(TEXT("Interface"));
    Row(TEXT("Interface size"), "scale", {{TEXT("−"), "set:scale-"}, {TEXT("+"), "set:scale+"}});
    Row(TEXT("Panel opacity"), "opacity", {{TEXT("−"), "set:opacity-"}, {TEXT("+"), "set:opacity+"}});
    Row(TEXT("Names over people"), "plates", {{TEXT("toggle"), "set:plates"}});
    Row(TEXT("Chat bubbles"), "bubbles", {{TEXT("toggle"), "set:bubbles"}});
    Section(TEXT("Look"));
    Row(TEXT("Mouse sensitivity"), "sens", {{TEXT("−"), "set:sens-"}, {TEXT("+"), "set:sens+"}});
    Row(TEXT("Invert vertical"), "invert", {{TEXT("toggle"), "set:invert"}});
    Section(TEXT("Graphics"));
    Row(TEXT("Quality"), "quality", {});
    auto* Levels = WidgetTree->ConstructWidget<UHorizontalBox>();
    const TCHAR* Names[] = {TEXT("low"), TEXT("medium"), TEXT("high"), TEXT("epic")};
    for (int32 I = 0; I < 4; ++I)
    {
        auto* Button = TextButton(Names[I]);
        Button->OnClicked.AddDynamic(this, &UEidoverseHUD::OnSettingClicked);
        ButtonIds.Add(Button, FName(*FString::Printf(TEXT("set:q%d"), I)));
        Levels->AddChildToHorizontalBox(Button)->SetPadding(FMargin(0, 0, 6, 0));
    }
    Column->AddChildToVerticalBox(Levels)->SetPadding(FMargin(0, 2, 0, 4));
    Row(TEXT("Lumen lighting (next join)"), "lumen", {{TEXT("toggle"), "set:lumen"}});
    RefreshSettings();
    return Scroll;
}

void UEidoverseHUD::RefreshSettings()
{
    auto Put = [this](FName Key, const FString& Value) { if (UTextBlock* T = SettingValues.FindRef(Key)) T->SetText(FText::FromString(Value)); };
    Put("scale", FString::Printf(TEXT("%.0f%%"), EidoSettings::InterfaceScale() * 100));
    Put("opacity", FString::Printf(TEXT("%.0f%%"), EidoSettings::PanelOpacity() * 100));
    Put("plates", EidoSettings::Nameplates() ? TEXT("on") : TEXT("off"));
    Put("bubbles", EidoSettings::Bubbles() ? TEXT("on") : TEXT("off"));
    Put("sens", FString::Printf(TEXT("%.2g×"), EidoSettings::LookSensitivity()));
    Put("invert", EidoSettings::InvertLook() ? TEXT("on") : TEXT("off"));
    Put("lumen", EidoSettings::Lumen() ? TEXT("on") : TEXT("off"));
    const int32 Level = GEngine && GEngine->GetGameUserSettings() ? GEngine->GetGameUserSettings()->GetOverallScalabilityLevel() : -1;
    const TCHAR* Names[] = {TEXT("low"), TEXT("medium"), TEXT("high"), TEXT("epic"), TEXT("cinematic")};
    Put("quality", Level >= 0 && Level <= 4 ? Names[Level] : TEXT("custom"));
}

void UEidoverseHUD::ApplyPanelOpacity()
{
    for (const auto& Weak : PanelBorders)
        if (UBorder* B = Weak.Get()) B->SetBrush(FEidoverseTheme::Rounded(FEidoverseTheme::Panel(), 10, FEidoverseTheme::Edge(), 1));
}

void UEidoverseHUD::OnSettingClicked()
{
    const FString Id = ResolveClicked().ToString();
    auto Step = [](const TCHAR* Name, float Current, float Delta, float Min, float Max) {
        EidoSettings::Set(Name, FString::SanitizeFloat(FMath::Clamp(FMath::RoundToFloat((Current + Delta) * 100) / 100, Min, Max)));
    };
    auto Toggle = [](const TCHAR* Name, bool bCurrent) { EidoSettings::Set(Name, bCurrent ? TEXT("0") : TEXT("1")); };
    if (Id == TEXT("set:scale-") || Id == TEXT("set:scale+"))
    {
        Step(TEXT("Eidoverse.UI.Scale"), EidoSettings::InterfaceScale(), Id.EndsWith(TEXT("+")) ? .05f : -.05f, .75f, 1.5f);
        EidoSettings::ApplyInterfaceScale();
    }
    else if (Id == TEXT("set:opacity-") || Id == TEXT("set:opacity+"))
    {
        Step(TEXT("Eidoverse.UI.PanelOpacity"), EidoSettings::PanelOpacity(), Id.EndsWith(TEXT("+")) ? .05f : -.05f, .3f, 1.f);
        ApplyPanelOpacity();
    }
    else if (Id == TEXT("set:sens-") || Id == TEXT("set:sens+"))
        Step(TEXT("Eidoverse.Look.Sensitivity"), EidoSettings::LookSensitivity(), Id.EndsWith(TEXT("+")) ? .1f : -.1f, .1f, 5.f);
    else if (Id == TEXT("set:plates")) Toggle(TEXT("Eidoverse.UI.Nameplates"), EidoSettings::Nameplates());
    else if (Id == TEXT("set:bubbles")) Toggle(TEXT("Eidoverse.UI.Bubbles"), EidoSettings::Bubbles());
    else if (Id == TEXT("set:invert")) Toggle(TEXT("Eidoverse.Look.InvertY"), EidoSettings::InvertLook());
    else if (Id == TEXT("set:lumen")) Toggle(TEXT("Eidoverse.World.Lumen"), EidoSettings::Lumen());
    else if (Id.StartsWith(TEXT("set:q")) && GEngine && GEngine->GetGameUserSettings())
    {
        UGameUserSettings* S = GEngine->GetGameUserSettings();
        S->SetOverallScalabilityLevel(FCString::Atoi(*Id.Mid(5)));
        S->ApplySettings(false);
    }
    RefreshSettings();
}


UWidget* UEidoverseHUD::BuildEmoteBar()
{
    auto* Panel = WidgetTree->ConstructWidget<UBorder>();
    Panel->SetBrush(FEidoverseTheme::Rounded(FEidoverseTheme::Panel(), 10, FEidoverseTheme::Edge(), 1));
    Panel->SetPadding(FMargin(6, 4));
    PanelBorders.Add(Panel);
    EmoteBar = WidgetTree->ConstructWidget<UHorizontalBox>();
    Panel->SetContent(EmoteBar);
    Emotes = {{TEXT("wave"), true}, {TEXT("cheer"), true}, {TEXT("dance"), true}, {TEXT("point"), true}, {TEXT("salute"), true}, {TEXT("clap"), true}};
    RefreshEmotes();
    Panel->SetVisibility(ESlateVisibility::Collapsed);
    return Panel;
}

UWidget* UEidoverseHUD::BuildDoor()
{
    Door = WidgetTree->ConstructWidget<UBorder>();
    Door->SetBrushColor(FLinearColor(FColor(5, 14, 16)));
    Door->SetHorizontalAlignment(HAlign_Center);
    Door->SetVerticalAlignment(VAlign_Center);
    auto* Width = WidgetTree->ConstructWidget<USizeBox>();
    Width->SetWidthOverride(420);
    auto* Card = WidgetTree->ConstructWidget<UBorder>();
    Card->SetBrush(FEidoverseTheme::Rounded(FEidoverseTheme::Panel().CopyWithNewOpacity(.95f), 14, FEidoverseTheme::Edge(), 1));
    Card->SetPadding(FMargin(28, 24));
    auto* Column = WidgetTree->ConstructWidget<UVerticalBox>();
    Card->SetContent(Column);
    Width->SetContent(Card);
    Door->SetContent(Width);

    auto* Head = WidgetTree->ConstructWidget<UHorizontalBox>();
    auto* Mark = WidgetTree->ConstructWidget<UImage>();
    if (UTexture2D* T = FEidoverseTheme::IconTexture(TEXT("Mark"))) Mark->SetBrushFromTexture(T);
    Mark->SetDesiredSizeOverride(FVector2D(34));
    Mark->SetColorAndOpacity(FEidoverseTheme::Brand());
    Head->AddChildToHorizontalBox(Mark)->SetVerticalAlignment(VAlign_Center);
    auto* Title = Label(TEXT("eidoverse"), 20, FEidoverseTheme::Text(), true);
    Title->SetAutoWrapText(false);
    Head->AddChildToHorizontalBox(Title)->SetPadding(FMargin(12, 0, 0, 0));
    Column->AddChildToVerticalBox(Head);
    Column->AddChildToVerticalBox(Label(TEXT("A shared place for people and AIs. You have a body; everything you do is a verb the world remembers."), 11, FEidoverseTheme::Dim()))->SetPadding(FMargin(0, 10, 0, 16));

    auto Field = [&](const FString& Caption, const FString& Hint) {
        Column->AddChildToVerticalBox(Label(Caption.ToUpper(), 9, FEidoverseTheme::Dim(), true))->SetPadding(FMargin(0, 8, 0, 4));
        UEditableTextBox* Box = Input(Hint);
        Column->AddChildToVerticalBox(Box);
        return Box;
    };
    DoorName = Field(TEXT("Your name"), TEXT("how others will see you"));
    DoorWorld = Field(TEXT("World"), TEXT("commons, water, \u2026"));
    DoorServer = Field(TEXT("Server"), TEXT("wss://eidoverse.animalabs.ai/ws"));

    auto* Row = WidgetTree->ConstructWidget<UWrapBox>();
    Row->SetInnerSlotPadding(FVector2D(8, 8));
    auto* Join = TextButton(TEXT("join"), true);
    Join->OnClicked.AddDynamic(this, &UEidoverseHUD::OnDoorJoin);
    Row->AddChildToWrapBox(Join);
    auto* Public = TextButton(TEXT("public server"));
    Public->OnClicked.AddDynamic(this, &UEidoverseHUD::OnDoorPublic);
    Row->AddChildToWrapBox(Public);
    auto* Local = TextButton(TEXT("local commons"));
    Local->OnClicked.AddDynamic(this, &UEidoverseHUD::OnLocalCommonsClicked);
    Row->AddChildToWrapBox(Local);
    Column->AddChildToVerticalBox(Row)->SetPadding(FMargin(0, 16, 0, 0));
    DoorStatus = Label(TEXT(""), 11, FEidoverseTheme::Attention());
    Column->AddChildToVerticalBox(DoorStatus)->SetPadding(FMargin(0, 10, 0, 0));

    Column->AddChildToVerticalBox(Label(TEXT("PUBLIC SERVER"), 9, FEidoverseTheme::Dim(), true))->SetPadding(FMargin(0, 18, 0, 4));
    DoorLogin = Label(TEXT(""), 11, FEidoverseTheme::Dim());
    Column->AddChildToVerticalBox(DoorLogin);
    auto* LoginRow = WidgetTree->ConstructWidget<UWrapBox>();
    LoginRow->SetInnerSlotPadding(FVector2D(8, 8));
    auto* SignIn = TextButton(TEXT("sign in with Discord"));
    SignIn->OnClicked.AddDynamic(this, &UEidoverseHUD::OnSignInClicked);
    LoginRow->AddChildToWrapBox(SignIn);
    auto* SignOut = TextButton(TEXT("sign out"));
    SignOut->OnClicked.AddDynamic(this, &UEidoverseHUD::OnSignOutClicked);
    LoginRow->AddChildToWrapBox(SignOut);
    Column->AddChildToVerticalBox(LoginRow)->SetPadding(FMargin(0, 8, 0, 0));
    Column->AddChildToVerticalBox(Label(TEXT("Joining shares your name, avatar and movement. Public chat is stored by the server."), 10, FEidoverseTheme::Dim()))->SetPadding(FMargin(0, 16, 0, 0));
    Door->SetVisibility(ESlateVisibility::Collapsed);
    return Door;
}

void UEidoverseHUD::UpdateDoor()
{
    if (!Door || !Bridge) return;
    const bool bShow = Bridge->bFrontDoor && !Bridge->bJoined;
    if (bShow && !bDoorFilled)
    {
        DoorName->SetText(FText::FromString(Bridge->ParticipantName == TEXT("unreal-diver") ? FString() : Bridge->ParticipantName));
        DoorWorld->SetText(FText::FromString(Bridge->WorldName));
        DoorServer->SetText(FText::FromString(Bridge->Endpoint));
        bDoorFilled = true;
    }
    if (bShow != bDoorShown)
    {
        bDoorShown = bShow;
        Door->SetVisibility(bShow ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
        if (bShow) { SetPointerMode(true); }
        else { SetPointerMode(false); bDoorFilled = false; LoadLayout(); }  // refill with the last world next time
    }
    if (!bShow) return;
    const FString Status = Bridge->ConnectionStatus;
    DoorStatus->SetText(FText::FromString(Status == TEXT("offline") ? FString() : Status));
    DoorLogin->SetText(FText::FromString(Bridge->HasProductionSession() ? TEXT("Signed in as ") + Bridge->LoginName : Bridge->LoginStatus));
}

void UEidoverseHUD::OnDoorJoin()
{
    if (!Bridge) return;
    Bridge->JoinFromDoor(DoorServer->GetText().ToString(), DoorWorld->GetText().ToString(), DoorName->GetText().ToString());
}

void UEidoverseHUD::OnDoorPublic()
{
    DoorServer->SetText(FText::FromString(AEidoverseBridge::ProductionEndpoint));
}

UWidget* UEidoverseHUD::BuildHelp()
{
    HelpOverlay = WidgetTree->ConstructWidget<UBorder>();
    HelpOverlay->SetBrushColor(FLinearColor(0, 0, 0, .45f));
    HelpOverlay->SetHorizontalAlignment(HAlign_Center);
    HelpOverlay->SetVerticalAlignment(VAlign_Center);
    auto* Size = WidgetTree->ConstructWidget<USizeBox>();
    Size->SetWidthOverride(600);
    Size->SetMaxDesiredHeight(760);
    auto* Panel = WidgetTree->ConstructWidget<UBorder>();
    Panel->SetBrush(FEidoverseTheme::Rounded(FEidoverseTheme::Panel().CopyWithNewOpacity(.94f), 12, FEidoverseTheme::Edge(), 1));
    Panel->SetPadding(FMargin(22, 18));
    auto* Scroll = WidgetTree->ConstructWidget<UScrollBox>();
    HelpBody = WidgetTree->ConstructWidget<UVerticalBox>();
    Scroll->AddChild(HelpBody);
    Panel->SetContent(Scroll);
    Size->SetContent(Panel);
    HelpOverlay->SetContent(Size);
    HelpOverlay->SetVisibility(ESlateVisibility::Collapsed);
    RefreshHelp();
    return HelpOverlay;
}

// ---------------------------------------------------------------- refresh

void UEidoverseHUD::RefreshRail()
{
    for (const auto& Pair : RailIcons)
    {
        if (!Pair.Value) continue;
        bool bOn = Pair.Key == "help" ? HelpOverlay && HelpOverlay->GetVisibility() != ESlateVisibility::Collapsed
            : Pair.Key == "emotes" ? EmoteBar && EmoteBar->GetParent() && EmoteBar->GetParent()->GetVisibility() != ESlateVisibility::Collapsed
            : IsFrameOpen(Pair.Key);
        if (Pair.Key == "menu") continue;
        Pair.Value->SetColorAndOpacity(bOn ? FEidoverseTheme::Brand() : FEidoverseTheme::Icon());
    }
    if (MarkIcon)
    {
        const bool bLive = Bridge && Bridge->bJoined;
        const bool bWaiting = Bridge && !bLive && Bridge->ConnectionStatus != TEXT("offline");
        const float Pulse = bWaiting ? .55f + .45f * FMath::Sin(FPlatformTime::Seconds() * 2.4) : 1.f;
        MarkIcon->SetColorAndOpacity((bLive ? FEidoverseTheme::Brand() : FEidoverseTheme::Dim()).CopyWithNewOpacity(Pulse));
    }
}

void UEidoverseHUD::RefreshEmotes()
{
    if (!EmoteBar) return;
    EmoteBar->ClearChildren();
    int32 Key = 1;
    for (const auto& E : Emotes)
    {
        if (!E.Value) continue;
        auto* B = TextButton(FString::Printf(TEXT("%d  %s"), Key, *E.Key));
        B->SetStyle(FlatButton(FLinearColor::Transparent, FEidoverseTheme::Hover(), 7));
        B->OnClicked.AddDynamic(this, &UEidoverseHUD::OnEmoteClicked);
        ButtonIds.Add(B, FName(*FString::Printf(TEXT("emote:%d"), Key - 1)));
        EmoteBar->AddChildToHorizontalBox(B)->SetPadding(FMargin(1, 0));
        if (++Key > 9) break;
    }
}

void UEidoverseHUD::RefreshHelp()
{
    if (!HelpBody) return;
    HelpBody->ClearChildren();
    const TSharedPtr<FJsonObject>* HelpObject = nullptr;
    const TSharedPtr<FJsonObject> Help = Defs && Defs->TryGetObjectField(TEXT("uiHelp"), HelpObject) ? *HelpObject : nullptr;
    auto* Head = WidgetTree->ConstructWidget<UHorizontalBox>();
    FString Title = TEXT("eidoverse"), Sub = TEXT("A shared place for people and AIs. Help loads from the world's server once you join.");
    if (Help) { Help->TryGetStringField(TEXT("title"), Title); Help->TryGetStringField(TEXT("sub"), Sub); }
    auto* TitleText = Label(Title.ToUpper(), 11, FEidoverseTheme::Text(), true);
    Head->AddChildToHorizontalBox(TitleText)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
    auto* Close = TextButton(TEXT("×"));
    Close->OnClicked.AddDynamic(this, &UEidoverseHUD::OnHelpClose);
    Head->AddChildToHorizontalBox(Close);
    HelpBody->AddChildToVerticalBox(Head);
    HelpBody->AddChildToVerticalBox(Label(Sub, 11, FEidoverseTheme::Dim()))->SetPadding(FMargin(0, 6, 0, 12));

    auto AddRich = [this](UWrapBox* Box, const FString& Html) {
        for (const FRun& R : ParseHelpHtml(Html))
        {
            if (R.bKey)
            {
                auto* Chip = WidgetTree->ConstructWidget<UBorder>();
                Chip->SetBrush(FEidoverseTheme::Rounded(FEidoverseTheme::Well(), 4, FLinearColor(1, 1, 1, .22f), 1));
                Chip->SetPadding(FMargin(5, 1));
                auto* T = Label(R.Text, 9);
                T->SetAutoWrapText(false);
                Chip->SetContent(T);
                Box->AddChildToWrapBox(Chip);
                continue;
            }
            // Words wrap individually so runs flow like the web's inline text.
            TArray<FString> Words;
            R.Text.ParseIntoArray(Words, TEXT(" "), true);
            for (const FString& W : Words)
            {
                auto* T = Label(W, 11, FEidoverseTheme::Text(), R.bBold);
                T->SetAutoWrapText(false);
                Box->AddChildToWrapBox(T);
            }
        }
    };
    HelpBody->AddChildToVerticalBox(Label(TEXT("KEYS"), 9, FEidoverseTheme::Dim(), true))->SetPadding(FMargin(0, 0, 0, 6));
    TArray<TPair<FString, FString>> Keys;
    const TArray<TSharedPtr<FJsonValue>>* KeyRows = nullptr;
    if (Help && Help->TryGetArrayField(TEXT("keys"), KeyRows))
        for (const auto& V : *KeyRows)
        {
            const TArray<TSharedPtr<FJsonValue>>* Pair = nullptr;
            if (V->TryGetArray(Pair) && Pair->Num() >= 2) Keys.Add({(*Pair)[0]->AsString(), (*Pair)[1]->AsString()});
        }
    if (Keys.IsEmpty())
        Keys = {{TEXT("Move"), TEXT("<kbd>W</kbd><kbd>A</kbd><kbd>S</kbd><kbd>D</kbd>")},
                {TEXT("Swim"), TEXT("<kbd>Space</kbd> rise · <kbd>C</kbd> dive · <kbd>Shift</kbd> faster")},
                {TEXT("Chat"), TEXT("<kbd>Enter</kbd> to open")}};
    // The native client's own pointer model (the web's drag-to-look has no equivalent here).
    Keys.Add({TEXT("Pointer"), TEXT("<kbd>M</kbd> toggles mouselook and the pointer · <kbd>Esc</kbd> closes panels, frees the pointer")});
    for (const auto& K : Keys)
    {
        auto* Row = WidgetTree->ConstructWidget<UHorizontalBox>();
        auto* Name = WidgetTree->ConstructWidget<USizeBox>();
        Name->SetWidthOverride(120);
        auto* NameText = Label(K.Key, 11, FEidoverseTheme::Dim());
        NameText->SetJustification(ETextJustify::Right);
        Name->SetContent(NameText);
        Row->AddChildToHorizontalBox(Name)->SetPadding(FMargin(0, 0, 14, 0));
        auto* Wrap = WidgetTree->ConstructWidget<UWrapBox>();
        Wrap->SetInnerSlotPadding(FVector2D(4, 3));
        Wrap->SetExplicitWrapSize(true);
        Wrap->SetWrapSize(420);
        AddRich(Wrap, K.Value);
        Row->AddChildToHorizontalBox(Wrap)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
        HelpBody->AddChildToVerticalBox(Row)->SetPadding(FMargin(0, 3));
    }
    const TArray<TSharedPtr<FJsonValue>>* Sections = nullptr;
    if (Help && Help->TryGetArrayField(TEXT("sections"), Sections))
        for (const auto& V : *Sections)
        {
            const TSharedPtr<FJsonObject> S = V->AsObject();
            if (!S) continue;
            HelpBody->AddChildToVerticalBox(Label(S->GetStringField(TEXT("h")).ToUpper(), 9, FEidoverseTheme::Dim(), true))->SetPadding(FMargin(0, 14, 0, 4));
            auto* Wrap = WidgetTree->ConstructWidget<UWrapBox>();
            Wrap->SetInnerSlotPadding(FVector2D(4, 3));
            Wrap->SetExplicitWrapSize(true);
            Wrap->SetWrapSize(550);
            AddRich(Wrap, S->GetStringField(TEXT("html")));
            HelpBody->AddChildToVerticalBox(Wrap);
        }
}

void UEidoverseHUD::RefreshChat()
{
    if (!ChatLog) return;
    ChatLog->ClearChildren();
    for (auto It = ButtonIds.CreateIterator(); It; ++It) if (!It->Key.IsValid()) It.RemoveCurrent();
    for (const FLine& L : Lines)
    {
        if ((ChatTab == 1 && !L.bMention) || (ChatTab == 2 && !L.bSystem)) continue;
        auto* Row = WidgetTree->ConstructWidget<UHorizontalBox>();
        if (!L.bSystem)
        {
            auto* Who = Label((L.bPrivate ? TEXT("→ ") : TEXT("")) + L.Sender, 11, L.bPrivate ? FEidoverseTheme::Private() : FEidoverseTheme::Brand(), true);
            Who->SetAutoWrapText(false);
            // Click a name to start a mention of them (web chat.js).
            auto* Name = WidgetTree->ConstructWidget<UButton>();
            Name->SetStyle(FlatButton(FLinearColor::Transparent, FEidoverseTheme::Hover(), 4));
            Name->SetContent(Who);
            Name->SetToolTipText(FText::FromString(TEXT("mention ") + L.Sender));
            Name->OnClicked.AddDynamic(this, &UEidoverseHUD::OnNameClicked);
            ButtonIds.Add(Name, FName(*(TEXT("who:@") + L.Sender)));
            Row->AddChildToHorizontalBox(Name)->SetPadding(FMargin(0, 0, 6, 0));
        }
        auto* Text = Label(L.Text, 11, L.bSystem ? FEidoverseTheme::Dim() : L.bMention ? FEidoverseTheme::Attention() : FEidoverseTheme::Text());
        Row->AddChildToHorizontalBox(Text)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
        ChatLog->AddChild(Row);
    }
    for (int32 I = 0; I < ChatTabs.Num(); ++I)
        ChatTabs[I]->SetStyle(FlatButton(I == ChatTab ? FEidoverseTheme::Brand().CopyWithNewOpacity(.16f) : FLinearColor::Transparent,
            FEidoverseTheme::Hover(), 6, I == ChatTab ? FEidoverseTheme::Brand().CopyWithNewOpacity(.5f) : FLinearColor::Transparent));
    ChatLog->ScrollToEnd();
}

void UEidoverseHUD::RefreshPeople(bool bForce)
{
    if (!PeopleList || !Bridge) return;
    TArray<FString> Names = Bridge->GetParticipants();
    Names.Sort();
    const int32 Hash = GetTypeHash(FString::Join(Names, TEXT("|")) + Bridge->AssignedIdentity);
    if (!bForce && Hash == LastPeopleHash) return;
    LastPeopleHash = Hash;
    PeopleList->ClearChildren();
    auto AddRow = [this](const FString& Name, bool bYou) {
        auto* Row = WidgetTree->ConstructWidget<UHorizontalBox>();
        auto* Dot = Label(TEXT("●"), 8, FEidoverseTheme::Brand());
        Dot->SetAutoWrapText(false);
        Row->AddChildToHorizontalBox(Dot)->SetPadding(FMargin(0, 3, 8, 0));
        Row->AddChildToHorizontalBox(Label(Name + (bYou ? TEXT("  (you)") : TEXT("")), 11));
        if (bYou) { PeopleList->AddChildToVerticalBox(Row)->SetPadding(FMargin(0, 2)); return; }
        // Click someone to start a private message.
        auto* Person = WidgetTree->ConstructWidget<UButton>();
        Person->SetStyle(FlatButton(FLinearColor::Transparent, FEidoverseTheme::Hover(), 5));
        Person->SetContent(Row);
        Person->SetToolTipText(FText::FromString(TEXT("whisper to ") + Name));
        Person->OnClicked.AddDynamic(this, &UEidoverseHUD::OnNameClicked);
        ButtonIds.Add(Person, FName(*(TEXT("who:/w ") + Name)));
        PeopleList->AddChildToVerticalBox(Person)->SetPadding(FMargin(0, 1));
    };
    if (Bridge->bJoined) AddRow(Bridge->AssignedIdentity, true);
    for (const FString& N : Names) AddRow(N, false);
    PeopleCount->SetText(FText::FromString(Bridge->bJoined ? FString::Printf(TEXT("%d here"), Names.Num() + 1) : TEXT("not in a world")));
}

// ---------------------------------------------------------------- behaviour

void UEidoverseHUD::NativeTick(const FGeometry& Geometry, float Delta)
{
    Super::NativeTick(Geometry, Delta);
    if (!Bridge) return;
    const double Now = FPlatformTime::Seconds();
    // Connection transitions are system lines and toasts, as in the web client.
    const FString Status = Bridge->ConnectionStatus;
    if (Status != LastStatus)
    {
        if (!LastStatus.IsEmpty()) AddSystemLine(Status);
        if (Status.StartsWith(TEXT("joined"))) Toast(Status);
        else if (Status.Contains(TEXT("refused")) || Status.Contains(TEXT("taken over"))) Toast(Status, true);
        LastStatus = Status;
    }
    if (Bridge->WorldName != LastWorld) { LastWorld = Bridge->WorldName; if (WorldName && !WorldName->HasKeyboardFocus()) WorldName->SetText(FText::FromString(LastWorld)); }
    if (ProfileName && !ProfileName->HasKeyboardFocus() && ProfileName->GetText().IsEmpty()) ProfileName->SetText(FText::FromString(Bridge->ParticipantName));
    if (HttpBase() != LastHttpBase && Bridge->bJoined) { LastHttpBase = HttpBase(); ReloadDefs(); }
    if (WorldStatus)
    {
        FString S = Bridge->bJoined ? FString::Printf(TEXT("%s  ·  %s  ·  %d others"), *Bridge->WorldName, *Bridge->AssignedIdentity, Bridge->RemoteCount) : Status;
        if (!Bridge->WorldRenderStatus.IsEmpty()) S += TEXT("\n") + Bridge->WorldRenderStatus;
        if (!Bridge->WorldLightingStatus.IsEmpty()) S += TEXT("\n") + Bridge->WorldLightingStatus;
        WorldStatus->SetText(FText::FromString(S));
    }
    if (LoginStatus) LoginStatus->SetText(FText::FromString(Bridge->LoginStatus));
    UpdateDoor();
    RefreshPeople(false);
    RefreshRail();
    if (TypingLine)
    {
        for (auto It = Typers.CreateIterator(); It; ++It) if (It->Value < Now) It.RemoveCurrent();
        TArray<FString> Names;
        Typers.GetKeys(Names);
        Names.Sort();
        const FString Text = Names.IsEmpty() ? FString() : Names.Num() == 1 ? Names[0] + TEXT(" is typing…")
            : Names.Num() == 2 ? Names[0] + TEXT(" and ") + Names[1] + TEXT(" are typing…") : FString::Printf(TEXT("%d people are typing…"), Names.Num());
        TypingLine->SetText(FText::FromString(Text));
        TypingLine->SetVisibility(Names.IsEmpty() ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
    }
    if (HintChip && HintUntil > 0 && Now > HintUntil) { HintChip->SetVisibility(ESlateVisibility::Collapsed); HintUntil = 0; }
    for (int32 I = Toasts.Num() - 1; I >= 0; --I)
        if (Now > Toasts[I].Until) { if (auto* W = Toasts[I].Widget.Get()) W->RemoveFromParent(); Toasts.RemoveAt(I); }

    // Keys while the game has focus (the chat box handles its own when focused).
    auto* PC = GetOwningPlayer();
    if (bDoorShown) return;
    if (!PC || (ChatInput && ChatInput->HasKeyboardFocus()) || (WorldName && WorldName->HasKeyboardFocus()) || (ProfileName && ProfileName->HasKeyboardFocus())) return;
    if (PC->WasInputKeyJustPressed(EKeys::Enter)) FocusChat();
    else if (PC->WasInputKeyJustPressed(EKeys::Tab)) ToggleFrame("people");
    else if (PC->WasInputKeyJustPressed(EKeys::M)) SetPointerMode(!bPointerMode);
    else if (PC->WasInputKeyJustPressed(EKeys::F1)) SetVisibility(GetVisibility() == ESlateVisibility::Hidden ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Hidden);
    else if (PC->WasInputKeyJustPressed(EKeys::Slash) && (PC->IsInputKeyDown(EKeys::LeftShift) || PC->IsInputKeyDown(EKeys::RightShift))) ToggleHelp();
    else if (PC->WasInputKeyJustPressed(EKeys::Escape))
    {
        if (HelpOverlay && HelpOverlay->GetVisibility() != ESlateVisibility::Collapsed) ToggleHelp();
        else SetPointerMode(true);
    }
    const FKey Digits[] = {EKeys::One, EKeys::Two, EKeys::Three, EKeys::Four, EKeys::Five, EKeys::Six, EKeys::Seven, EKeys::Eight, EKeys::Nine};
    for (int32 I = 0; I < 9; ++I) if (PC->WasInputKeyJustPressed(Digits[I])) Emote(I);
}

FReply UEidoverseHUD::NativeOnPreviewKeyDown(const FGeometry& Geometry, const FKeyEvent& Event)
{
    if (ChatInput && ChatInput->HasKeyboardFocus())
    {
        if (Event.GetKey() == EKeys::Escape)
        {
            ChatInput->SetText(FText::GetEmpty());
            FSlateApplication::Get().SetUserFocusToGameViewport(0);
            SetPointerMode(false);
            return FReply::Handled();
        }
        if (Event.GetKey() == EKeys::Up && !SentHistory.IsEmpty())
        {
            RecallIndex = RecallIndex < 0 ? SentHistory.Num() - 1 : FMath::Max(0, RecallIndex - 1);
            ChatInput->SetText(FText::FromString(SentHistory[RecallIndex]));
            return FReply::Handled();
        }
    }
    return Super::NativeOnPreviewKeyDown(Geometry, Event);
}

void UEidoverseHUD::SetPointerMode(bool bPointer)
{
    auto* PC = GetOwningPlayer();
    if (!PC) return;
    bPointerMode = bPointer;
    PC->SetIgnoreLookInput(false);
    PC->ResetIgnoreLookInput();
    if (bPointer)
    {
        PC->SetIgnoreLookInput(true);
        PC->bShowMouseCursor = true;
        FInputModeGameAndUI Mode;
        Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
        Mode.SetHideCursorDuringCapture(false);
        PC->SetInputMode(Mode);
    }
    else
    {
        PC->bShowMouseCursor = false;
        PC->SetInputMode(FInputModeGameOnly());
    }
}

void UEidoverseHUD::FocusChat()
{
    ShowFrame("chat", true);
    if (!ChatInput) return;
    if (auto* PC = GetOwningPlayer())
    {
        FInputModeGameAndUI Mode;
        Mode.SetWidgetToFocus(ChatInput->TakeWidget());
        Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
        PC->SetInputMode(Mode);
        PC->SetIgnoreMoveInput(true);
        PC->SetIgnoreLookInput(true);
    }
    ChatInput->SetKeyboardFocus();
    RecallIndex = -1;
}

void UEidoverseHUD::SendChat()
{
    if (!Bridge || !ChatInput) return;
    const FString Text = ChatInput->GetText().ToString().TrimStartAndEnd();
    if (!Text.IsEmpty())
    {
        bool bOk = false;
        if (Text.StartsWith(TEXT("/w ")) || Text.StartsWith(TEXT("/whisper ")))
        {
            FString Cmd, To, Body;
            Text.Split(TEXT(" "), &Cmd, &Body);
            Body.Split(TEXT(" "), &To, &Body);
            bOk = Bridge->Whisper(To, Body);
            if (bOk) Lines.Add({To, Body, true, false, false});
        }
        else if (Text.StartsWith(TEXT("/")))
        {
            bOk = Bridge->HandleInteractionCommand(Text);
            if (!Bridge->InteractionStatus.IsEmpty()) AddSystemLine(Bridge->InteractionStatus);
        }
        else bOk = Bridge->Say(Text);
        if (!bOk && !Text.StartsWith(TEXT("/"))) Toast(TEXT("Not sent: join a world first, and wait a moment between messages."), true);
        SentHistory.Add(Text);
        ChatInput->SetText(FText::GetEmpty());
        RefreshChat();
    }
    FSlateApplication::Get().SetUserFocusToGameViewport(0);
    if (auto* PC = GetOwningPlayer()) { PC->SetIgnoreMoveInput(false); PC->ResetIgnoreMoveInput(); }
    SetPointerMode(bPointerMode);
}

void UEidoverseHUD::NotifyChatLine(const FString& Sender, const FString& Text, bool bPrivate)
{
    FLine L{Sender, Text, bPrivate, false, false};
    L.bMention = Bridge && Sender != Bridge->AssignedIdentity && Mentions(Text, Bridge->AssignedIdentity);
    Lines.Add(L);
    if (Lines.Num() > 300) Lines.RemoveAt(0, Lines.Num() - 300);
    if (L.bMention) Toast(Sender + TEXT(" mentioned you"), true);
    RefreshChat();
}

bool UEidoverseHUD::IsChatFocused() const { return ChatInput && ChatInput->HasKeyboardFocus(); }
FString UEidoverseHUD::ChatDraft() const { return ChatInput ? ChatInput->GetText().ToString() : FString(); }

void UEidoverseHUD::NoteTyping(const FString& Id)
{
    Typers.Add(Id, FPlatformTime::Seconds() + 4);
}

void UEidoverseHUD::OnChatChanged(const FText& Text)
{
    const FString Draft = Text.ToString().TrimStartAndEnd();
    const double Now = FPlatformTime::Seconds();
    // Web cadence: at most every 2.5 s while there is a draft; slash commands are not speech.
    if (!Bridge || Draft.IsEmpty() || (Draft.StartsWith(TEXT("/")) && !Draft.StartsWith(TEXT("/w "))) || Now - LastTypingSent < 2.5) return;
    LastTypingSent = Now;
    FString To;
    if (Draft.StartsWith(TEXT("/w "))) { FString Rest = Draft.Mid(3); Rest.Split(TEXT(" "), &To, nullptr); }
    Bridge->SendTyping(To);
}

void UEidoverseHUD::InsertIntoChat(const FString& Text, bool bReplace)
{
    FocusChat();
    if (!ChatInput) return;
    FString Draft = bReplace ? FString() : ChatInput->GetText().ToString();
    if (!Draft.IsEmpty() && !Draft.EndsWith(TEXT(" "))) Draft += TEXT(" ");
    ChatInput->SetText(FText::FromString(Draft + Text));
}

void UEidoverseHUD::OnNameClicked()
{
    const FString Id = ResolveClicked().ToString();
    if (!Id.StartsWith(TEXT("who:"))) return;
    const FString Text = Id.Mid(4);
    InsertIntoChat(Text + TEXT(" "), Text.StartsWith(TEXT("/w ")));
}

void UEidoverseHUD::AddSystemLine(const FString& Text)
{
    Lines.Add({TEXT(""), Text, false, true, false});
    RefreshChat();
}

void UEidoverseHUD::Toast(const FString& Text, bool bAttention)
{
    if (!ToastBox) return;
    auto* Chip = WidgetTree->ConstructWidget<UBorder>();
    Chip->SetBrush(FEidoverseTheme::Rounded(FEidoverseTheme::Panel().CopyWithNewOpacity(.9f), 8,
        bAttention ? FEidoverseTheme::Attention() : FEidoverseTheme::Edge(), bAttention ? 1.5f : 1));
    Chip->SetPadding(FMargin(12, 8));
    auto* Size = WidgetTree->ConstructWidget<USizeBox>();
    Size->SetMaxDesiredWidth(380);
    Size->SetContent(Label(Text, 11, bAttention ? FEidoverseTheme::Attention() : FEidoverseTheme::Text()));
    Chip->SetContent(Size);
    ToastBox->AddChildToVerticalBox(Chip)->SetPadding(FMargin(0, 4, 0, 0));
    FToast T;
    T.Widget = Chip;
    T.Until = FPlatformTime::Seconds() + 6;
    Toasts.Add(T);
}

void UEidoverseHUD::Hint(const FString& Text, float Seconds)
{
    if (!HintChip) return;
    HintText->SetText(FText::FromString(Text));
    HintChip->SetVisibility(ESlateVisibility::HitTestInvisible);
    HintUntil = FPlatformTime::Seconds() + Seconds;
}

bool UEidoverseHUD::IsFrameOpen(FName Id) const
{
    const auto* F = Frames.Find(Id);
    return F && *F && (*F)->GetVisibility() != ESlateVisibility::Collapsed;
}

void UEidoverseHUD::ShowFrame(FName Id, bool bShow)
{
    if (Id == "emotes") { if (EmoteBar && EmoteBar->GetParent()) EmoteBar->GetParent()->SetVisibility(bShow ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed); return; }
    if (Id == "help") { if (bShow != (HelpOverlay->GetVisibility() != ESlateVisibility::Collapsed)) ToggleHelp(); return; }
    if (Id == "pointer") { SetPointerMode(!bPointerMode); return; }
    if (UEidoverseFrame* F = Frames.FindRef(Id).Get()) F->SetVisibility(bShow ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
    if (Id == "people") RefreshPeople(true);
    if (!bDoorShown) SaveLayout();
}

void UEidoverseHUD::ToggleFrame(FName Id)
{
    const bool bOpen = Id == "emotes" ? EmoteBar && EmoteBar->GetParent() && EmoteBar->GetParent()->GetVisibility() != ESlateVisibility::Collapsed
        : Id == "help" ? HelpOverlay && HelpOverlay->GetVisibility() != ESlateVisibility::Collapsed : IsFrameOpen(Id);
    ShowFrame(Id, !bOpen);
}

void UEidoverseHUD::ToggleHelp()
{
    if (!HelpOverlay) return;
    const bool bShow = HelpOverlay->GetVisibility() == ESlateVisibility::Collapsed;
    HelpOverlay->SetVisibility(bShow ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
    if (bShow) SetPointerMode(true);
}

void UEidoverseHUD::ToggleMenu()
{
    if (Menu) Menu->SetVisibility(Menu->GetVisibility() == ESlateVisibility::Collapsed ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
}

void UEidoverseHUD::Emote(int32 Index)
{
    if (!Bridge) return;
    int32 Listed = 0;
    for (const auto& E : Emotes)
    {
        if (!E.Value) continue;
        if (Listed++ == Index)
        {
            if (Bridge->Emote(E.Key)) Hint(E.Key, 1.5f);
            else Toast(TEXT("Join a world to use gestures."));
            return;
        }
    }
}

FString UEidoverseHUD::HttpBase() const
{
    return Bridge ? Bridge->HttpBase() : FString();
}

void UEidoverseHUD::ReloadDefs()
{
    const FString Base = HttpBase();
    if (Base.IsEmpty()) return;
    auto Request = FHttpModule::Get().CreateRequest();
    Request->SetURL(Base + TEXT("/defs"));
    Request->SetVerb(TEXT("GET"));
    Request->SetTimeout(10);
    TWeakObjectPtr<UEidoverseHUD> Weak(this);
    Request->OnProcessRequestComplete().BindLambda([Weak](FHttpRequestPtr, FHttpResponsePtr Response, bool bOk)
    {
        auto* Self = Weak.Get();
        TSharedPtr<FJsonObject> J;
        if (!Self || !bOk || !Response || Response->GetResponseCode() != 200 || Response->GetContentLength() > 4 * 1024 * 1024 ||
            !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Response->GetContentAsString()), J) || !J) return;
        Self->Defs = J;
        const TSharedPtr<FJsonObject>* E = nullptr;
        if (J->TryGetObjectField(TEXT("emotes"), E))
        {
            Self->Emotes.Reset();
            // Key order is the bar and number-key order (defs/animations/_emotes.json).
            for (const auto& Pair : (*E)->Values)
            {
                bool bListed = true;
                if (const TSharedPtr<FJsonObject> O = Pair.Value->AsObject()) O->TryGetBoolField(TEXT("listed"), bListed);
                Self->Emotes.Add(TPair<FString, bool>(Pair.Key, bListed));
            }
            Self->RefreshEmotes();
        }
        Self->RefreshHelp();
    });
    Request->ProcessRequest();
}

// ---------------------------------------------------------------- clicks

void UEidoverseHUD::OnRailClicked()
{
    const FName Id = ResolveClicked();
    if (Id == "menu") ToggleMenu();
    else if (Id != NAME_None) ToggleFrame(Id);
}
void UEidoverseHUD::OnMenuEntry()
{
    const FName Id = ResolveClicked();
    if (Id == "lock") SetLayoutLocked(!bLayoutLocked);
    else if (Id == "reset") ResetLayout();
    else if (Id != NAME_None) ToggleFrame(Id);
    ToggleMenu();
}
void UEidoverseHUD::OnEmoteClicked()
{
    const FString Id = ResolveClicked().ToString();
    if (Id.StartsWith(TEXT("emote:"))) Emote(FCString::Atoi(*Id.Mid(6)));
}
void UEidoverseHUD::OnChatCommitted(const FText&, ETextCommit::Type Kind)
{
    if (Kind == ETextCommit::OnEnter) SendChat();
}
void UEidoverseHUD::OnJoinClicked()
{
    if (!Bridge || !WorldName) return;
    const FString Name = WorldName->GetText().ToString().TrimStartAndEnd();
    if (!AEidoverseBridge::IsWorldName(Name)) { Toast(TEXT("World names are letters, numbers, - and _."), true); return; }
    if (ProfileName && !ProfileName->GetText().IsEmpty()) Bridge->ParticipantName = ProfileName->GetText().ToString().TrimStartAndEnd().Left(64);
    Bridge->bRenderDynamicWorld = true;
    if (Bridge->HasProductionSession()) { Bridge->Disconnect(); Bridge->WorldName = Name; Bridge->JoinProduction(); }
    else Bridge->SwitchWorld(Name);
}
void UEidoverseHUD::OnLocalCommonsClicked() { if (Bridge) Bridge->JoinLocalCommons(); }
void UEidoverseHUD::OnSignInClicked() { if (Bridge) Bridge->BeginProductionLogin(); }
void UEidoverseHUD::OnSignOutClicked() { if (Bridge) Bridge->LogoutProduction(); }
void UEidoverseHUD::OnDisconnectClicked() { if (Bridge) Bridge->Disconnect(); }
void UEidoverseHUD::OnChatTabAll() { ChatTab = 0; RefreshChat(); }
void UEidoverseHUD::OnChatTabMentions() { ChatTab = 1; RefreshChat(); }
void UEidoverseHUD::OnChatTabSystem() { ChatTab = 2; RefreshChat(); }
void UEidoverseHUD::OnHelpClose() { ToggleHelp(); }

#if WITH_DEV_AUTOMATION_TESTS
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
// Drives the real keyboard path: Enter opens chat, characters type, Enter sends,
// and focus returns to the game. Logs EIDOVERSE_HUD_CHAT_PROBE; the message goes
// to whichever server the bridge is joined to (use an isolated preview).
static FAutoConsoleCommandWithWorld HUDChatProbe(TEXT("Eidoverse.TestHUDChat"), TEXT("Type and send a chat line through the UMG shell with synthetic key events."),
    FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* World)
    {
        if (!World || !World->IsGameWorld()) return;
        TActorIterator<AEidoverseBridge> It(World);
        if (!It || !It->HUD) { UE_LOG(LogTemp, Warning, TEXT("EIDOVERSE_HUD_CHAT_PROBE no hud")); return; }
        TWeakObjectPtr<UEidoverseHUD> Weak(It->HUD);
        auto Key = [](const FKey& K) {
            FSlateApplication::Get().ProcessKeyDownEvent(FKeyEvent(K, FModifierKeysState(), 0, false, 0, 0));
            FSlateApplication::Get().ProcessKeyUpEvent(FKeyEvent(K, FModifierKeysState(), 0, false, 0, 0));
        };
        Weak->SetPointerMode(false);
        Key(EKeys::Enter);  // polled by the HUD on its next tick
        FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Weak, Key](float) mutable
        {
            UEidoverseHUD* HUD = Weak.Get();
            if (!HUD) return false;
            const bool bFocused = HUD->IsChatFocused();
            for (TCHAR C : FString(TEXT("typed through the UMG shell")))
                FSlateApplication::Get().ProcessKeyCharEvent(FCharacterEvent(C, FModifierKeysState(), 0, false));
            const FString Typed = HUD->ChatDraft();
            Key(EKeys::Enter);
            UE_LOG(LogTemp, Warning, TEXT("EIDOVERSE_HUD_CHAT_PROBE focused=%d typed=%d cleared=%d gameFocus=%d"), bFocused,
                Typed == TEXT("typed through the UMG shell"), HUD->ChatDraft().IsEmpty(), !HUD->IsChatFocused());
            return false;
        }), .5f);
    }));
#endif

#if WITH_DEV_AUTOMATION_TESTS
// Drags the chat frame by its title with synthetic Slate mouse events and
// reports the slot move and the persisted layout. EIDOVERSE_HUD_DRAG_PROBE.
static FAutoConsoleCommandWithWorld HUDDragProbe(TEXT("Eidoverse.TestHUDDrag"), TEXT("Drag the chat frame through real Slate mouse events."),
    FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* World)
    {
        TActorIterator<AEidoverseBridge> It(World);
        UEidoverseHUD* HUD = It ? It->HUD.Get() : nullptr;
        UEidoverseFrame* Frame = HUD ? HUD->GetFrame("chat") : nullptr;
        if (!Frame) { UE_LOG(LogTemp, Warning, TEXT("EIDOVERSE_HUD_DRAG_PROBE no frame")); return; }
        HUD->SetPointerMode(true);
        const FGeometry G = Frame->GetCachedGeometry();
        const FVector2D Grab = G.LocalToAbsolute(FVector2D(G.GetLocalSize().X * .5f, 8));
        const FVector2D Before = G.GetAbsolutePosition();
        const FVector2D Delta(200, -150);
        auto Event = [](const FVector2D& At, const FVector2D& Last, bool bDown) {
            TSet<FKey> Pressed; if (bDown) Pressed.Add(EKeys::LeftMouseButton);
            return FPointerEvent(0, At, Last, Pressed, EKeys::LeftMouseButton, 0, FModifierKeysState());
        };
        auto& App = FSlateApplication::Get();
        App.SetCursorPos(Grab);
        App.ProcessMouseButtonDownEvent(nullptr, Event(Grab, Grab, true));
        for (int32 I = 1; I <= 10; ++I)
        {
            const FVector2D At = Grab + Delta * (I / 10.f), Last = Grab + Delta * ((I - 1) / 10.f);
            App.SetCursorPos(At);
            App.ProcessMouseMoveEvent(Event(At, Last, true));
        }
        App.ProcessMouseButtonUpEvent(Event(Grab + Delta, Grab + Delta, false));
        TWeakObjectPtr<UEidoverseFrame> Weak(Frame);
        FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Weak, Before, Delta](float)
        {
            if (!Weak.IsValid()) return false;
            const FVector2D After = Weak->GetCachedGeometry().GetAbsolutePosition();
            FVector2D P, S;
            const bool bSaved = Weak->GetLayout(P, S);
            FString Stored;
            GConfig->GetString(TEXT("/Script/EidoverseClient.Layout"), TEXT("chat"), Stored, GGameUserSettingsIni);
            UE_LOG(LogTemp, Warning, TEXT("EIDOVERSE_HUD_DRAG_PROBE moved=(%.0f,%.0f) expected=(%.0f,%.0f) topleft=%d stored=%s"),
                After.X - Before.X, After.Y - Before.Y, Delta.X, Delta.Y, bSaved, *Stored);
            return false;
        }), .3f);
    }));
#endif

// ---------------------------------------------------------------- nameplate

TSharedRef<SWidget> UEidoverseNameplate::RebuildWidget()
{
    if (WidgetTree && !WidgetTree->RootWidget)
    {
        auto* Column = WidgetTree->ConstructWidget<UVerticalBox>();
        WidgetTree->RootWidget = Column;
        Bubble = WidgetTree->ConstructWidget<UBorder>();
        Bubble->SetBrush(FEidoverseTheme::Rounded(FLinearColor(FColor(8, 20, 28)).CopyWithNewOpacity(.86f), 10, FEidoverseTheme::Brand().CopyWithNewOpacity(.25f), 1));
        Bubble->SetPadding(FMargin(10, 6));
        auto* Width = WidgetTree->ConstructWidget<USizeBox>();
        Width->SetMaxDesiredWidth(260);
        BubbleText = WidgetTree->ConstructWidget<UTextBlock>();
        BubbleText->SetFont(FEidoverseTheme::Font(11));
        BubbleText->SetColorAndOpacity(FSlateColor(FLinearColor(FColor(0xe8, 0xf4, 0xef))));
        BubbleText->SetAutoWrapText(true);
        Width->SetContent(BubbleText);
        Bubble->SetContent(Width);
        Bubble->SetVisibility(ESlateVisibility::Collapsed);
        auto* BubbleSlot = Column->AddChildToVerticalBox(Bubble);
        BubbleSlot->SetHorizontalAlignment(HAlign_Center);
        BubbleSlot->SetPadding(FMargin(0, 0, 0, 4));
        auto* Pill = WidgetTree->ConstructWidget<UBorder>();
        Pill->SetBrush(FEidoverseTheme::Rounded(FLinearColor(FColor(8, 20, 28)).CopyWithNewOpacity(.86f), 9, FEidoverseTheme::Brand().CopyWithNewOpacity(.25f), 1));
        Pill->SetPadding(FMargin(9, 2));
        NameText = WidgetTree->ConstructWidget<UTextBlock>();
        NameText->SetFont(FEidoverseTheme::Font(11, true));
        NameText->SetColorAndOpacity(FSlateColor(FEidoverseTheme::Brand()));
        NameText->SetText(FText::FromString(PendingName));
        auto* PillRow = WidgetTree->ConstructWidget<UHorizontalBox>();
        PillRow->AddChildToHorizontalBox(NameText);
        TypingDots = WidgetTree->ConstructWidget<UTextBlock>();
        TypingDots->SetFont(FEidoverseTheme::Font(11, true));
        TypingDots->SetColorAndOpacity(FSlateColor(FMath::Lerp(FEidoverseTheme::Brand(), FLinearColor::White, .15f)));
        TypingDots->SetText(FText::FromString(TEXT("  •••")));
        TypingDots->SetVisibility(ESlateVisibility::Collapsed);
        PillRow->AddChildToHorizontalBox(TypingDots);
        Pill->SetContent(PillRow);
        Column->AddChildToVerticalBox(Pill)->SetHorizontalAlignment(HAlign_Center);
    }
    return Super::RebuildWidget();
}

void UEidoverseNameplate::SetName(const FString& Name)
{
    PendingName = Name;
    if (NameText) NameText->SetText(FText::FromString(Name));
}

void UEidoverseNameplate::ShowBubble(const FString& Text, float Seconds)
{
    TakeWidget();
    // One line of the latest message, as the web bubbles do; long lines are trimmed.
    FString Line = Text.Replace(TEXT("\n"), TEXT(" "));
    if (Line.Len() > 160) Line = Line.Left(157) + TEXT("…");
    BubbleText->SetText(FText::FromString(Line));
    Bubble->SetVisibility(ESlateVisibility::HitTestInvisible);
    BubbleUntil = FPlatformTime::Seconds() + Seconds;
}

void UEidoverseNameplate::ShowTyping(float Seconds)
{
    TakeWidget();
    TypingDots->SetVisibility(ESlateVisibility::HitTestInvisible);
    TypingUntil = FPlatformTime::Seconds() + Seconds;
}

void UEidoverseNameplate::SetDistanceFade(float Alpha)
{
    SetRenderOpacity(FMath::Clamp(Alpha, 0.f, 1.f));
}

void UEidoverseNameplate::NativeTick(const FGeometry& Geometry, float Delta)
{
    Super::NativeTick(Geometry, Delta);
    if (TypingDots && TypingUntil > 0 && FPlatformTime::Seconds() > TypingUntil)
    {
        TypingDots->SetVisibility(ESlateVisibility::Collapsed);
        TypingUntil = 0;
    }
    if (Bubble && BubbleUntil > 0 && FPlatformTime::Seconds() > BubbleUntil)
    {
        Bubble->SetVisibility(ESlateVisibility::Collapsed);
        BubbleUntil = 0;
    }
}

#if WITH_DEV_AUTOMATION_TESTS
static FAutoConsoleCommandWithWorld HUDDumpFrames(TEXT("Eidoverse.DumpFrames"), TEXT("Log every HUD frame's visibility, slot and geometry."),
    FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* World)
    {
        TActorIterator<AEidoverseBridge> It(World);
        UEidoverseHUD* HUD = It ? It->HUD.Get() : nullptr;
        if (!HUD) return;
        for (const TCHAR* Id : {TEXT("chat"), TEXT("world"), TEXT("people"), TEXT("profile"), TEXT("settings")})
        {
            UEidoverseFrame* F = HUD->GetFrame(Id);
            if (!F) { UE_LOG(LogTemp, Warning, TEXT("EIDOVERSE_FRAME %s missing"), Id); continue; }
            const auto* S = Cast<UCanvasPanelSlot>(F->Slot);
            const FGeometry G = F->GetCachedGeometry();
            UE_LOG(LogTemp, Warning, TEXT("EIDOVERSE_FRAME %s vis=%d slot=%s anchors=%s size=%s abs=%s body=%d"), Id, int32(F->GetVisibility()),
                S ? *S->GetPosition().ToString() : TEXT("-"), S ? *S->GetAnchors().Minimum.ToString() : TEXT("-"),
                *G.GetLocalSize().ToString(), *G.GetAbsolutePosition().ToString(), F->Body != nullptr);
            UWidget* C = F->Body ? F->Body->GetContent() : nullptr;
            const TSharedPtr<SWidget> FW = F->GetCachedWidget(), BW = F->Body ? F->Body->GetCachedWidget() : nullptr, CW = C ? C->GetCachedWidget() : nullptr;
            UE_LOG(LogTemp, Warning, TEXT("EIDOVERSE_FRAME3 %s now=%u framePaint=%u bodyPaint=%u contentPaint=%u parentSlate=%s"), Id, GFrameCounter,
                FW ? FW->Debug_GetLastPaintFrame() : 0, BW ? BW->Debug_GetLastPaintFrame() : 0, CW ? CW->Debug_GetLastPaintFrame() : 0,
                FW && FW->GetParentWidget() ? *FW->GetParentWidget()->GetTypeAsString() : TEXT("none"));
            UE_LOG(LogTemp, Warning, TEXT("EIDOVERSE_FRAME2 %s opacity=%.2f slate=%d bodyvis=%d content=%s contentvis=%d desired=%s bodytint=%s"), Id, F->GetRenderOpacity(),
                F->GetCachedWidget().IsValid(), F->Body ? int32(F->Body->GetVisibility()) : -1, C ? *C->GetClass()->GetName() : TEXT("-"),
                C ? int32(C->GetVisibility()) : -1, C ? *C->GetDesiredSize().ToString() : TEXT("-"), F->Body ? *F->Body->GetBrushColor().ToString() : TEXT("-"));
        }
    }));
#endif

#if WITH_DEV_AUTOMATION_TESTS
// Clicks the newest sender name in the chat log with real Slate mouse events and
// logs the resulting draft. EIDOVERSE_HUD_NAME_PROBE.
static FAutoConsoleCommandWithWorld HUDNameProbe(TEXT("Eidoverse.TestHUDNameClick"), TEXT("Click the newest chat sender name."),
    FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* World)
    {
        TActorIterator<AEidoverseBridge> It(World);
        UEidoverseHUD* HUD = It ? It->HUD.Get() : nullptr;
        UScrollBox* Log = HUD ? HUD->GetChatLog() : nullptr;
        UButton* Target = nullptr;
        for (int32 I = Log ? Log->GetChildrenCount() - 1 : -1; I >= 0 && !Target; --I)
            if (auto* Row = Cast<UHorizontalBox>(Log->GetChildAt(I))) if (Row->GetChildrenCount()) Target = Cast<UButton>(Row->GetChildAt(0));
        if (!Target) { UE_LOG(LogTemp, Warning, TEXT("EIDOVERSE_HUD_NAME_PROBE no name button")); return; }
        HUD->SetPointerMode(true);
        const FGeometry G = Target->GetCachedGeometry();
        const FVector2D At = G.LocalToAbsolute(G.GetLocalSize() * .5f);
        auto& App = FSlateApplication::Get();
        TSet<FKey> Down{EKeys::LeftMouseButton};
        App.SetCursorPos(At);
        App.ProcessMouseMoveEvent(FPointerEvent(0, At, At, TSet<FKey>(), EKeys::Invalid, 0, FModifierKeysState()));
        App.ProcessMouseButtonDownEvent(nullptr, FPointerEvent(0, At, At, Down, EKeys::LeftMouseButton, 0, FModifierKeysState()));
        App.ProcessMouseButtonUpEvent(FPointerEvent(0, At, At, TSet<FKey>(), EKeys::LeftMouseButton, 0, FModifierKeysState()));
        TWeakObjectPtr<UEidoverseHUD> Weak(HUD);
        FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Weak](float)
        {
            if (Weak.IsValid()) UE_LOG(LogTemp, Warning, TEXT("EIDOVERSE_HUD_NAME_PROBE draft=[%s] focused=%d"), *Weak->ChatDraft(), Weak->IsChatFocused());
            return false;
        }), .3f);
    }));
#endif
