// Chat tabs for whispers and the composer's autocomplete (chat.js:439-492, 1045-1200):
// all · mentions · system · @name… — a whisper opens its person's tab and counts
// unread there; Enter in a DM tab whispers them; each tab keeps its own draft.
// "/" lists the command table, "@" up to 8 people present; ↑/↓ move, Tab/Enter take.
#include "EidoverseUI.h"
#include "EidoverseBridge.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/EditableTextBox.h"
#include "Blueprint/WidgetTree.h"
#include "Components/ScrollBox.h"

namespace
{
    FButtonStyle FlatButton(const FLinearColor& Normal, const FLinearColor& Hovered, float Radius = 6, const FLinearColor& Outline = FLinearColor::Transparent)
    {
        FButtonStyle S;
        S.SetNormal(FEidoverseTheme::Rounded(Normal, Radius, Outline, Outline.A > 0 ? 1 : 0));
        S.SetHovered(FEidoverseTheme::Rounded(Hovered, Radius, Outline, Outline.A > 0 ? 1 : 0));
        S.SetPressed(FEidoverseTheme::Rounded(FEidoverseTheme::Well(), Radius, Outline, Outline.A > 0 ? 1 : 0));
        S.SetDisabled(FEidoverseTheme::Rounded(FLinearColor::Transparent, Radius));
        S.SetNormalPadding(FMargin(0)); S.SetPressedPadding(FMargin(0));
        return S;
    }
}

void UEidoverseHUD::OpenDm(const FString& Name)
{
    const FString Who = Name.TrimStartAndEnd().Left(64);
    if (Who.IsEmpty() || (Bridge && Who == Bridge->AssignedIdentity)) return;
    if (!DmTabs.Contains(Who)) DmTabs.Add(Who);
    ShowFrame(TEXT("chat"), true);
    SwitchChatTab(3, Who);
    FocusChat();
}

void UEidoverseHUD::CloseDm(const FString& Name)
{
    DmTabs.Remove(Name); DmUnread.Remove(Name); Drafts.Remove(Name);
    if (ChatTab == 3 && ActiveDm == Name) SwitchChatTab(0, FString());
    else RebuildDmTabs();
}

void UEidoverseHUD::SwitchChatTab(int32 Tab, const FString& Dm)
{
    // One draft per tab: park this one's text, bring the other's back.
    if (ChatInput)
    {
        Drafts.Add(ChatTab == 3 ? ActiveDm : FString(), ChatInput->GetText().ToString());
        const FString Key = Tab == 3 ? Dm : FString();
        ChatInput->SetText(FText::FromString(Drafts.FindRef(Key)));
        ChatInput->SetHintText(FText::FromString(Tab == 3 ? FString::Printf(TEXT("whisper to %s…"), *Dm) : FString(TEXT("say something…  @ to mention · / for commands"))));
    }
    ChatTab = Tab; ActiveDm = Tab == 3 ? Dm : FString();
    if (Tab == 3) DmUnread.Remove(Dm);
    RebuildDmTabs();
    RefreshChat();
}

void UEidoverseHUD::RebuildDmTabs()
{
    if (!ChatTabBar) return;
    for (UWidget* W : DmTabWidgets) if (W) W->RemoveFromParent();
    DmTabWidgets.Reset();
    for (const FString& Who : DmTabs)
    {
        const int32 Unread = DmUnread.FindRef(Who);
        const bool bOn = ChatTab == 3 && ActiveDm == Who;
        auto* Tab = TextButton(Unread ? FString::Printf(TEXT("@%s (%d)"), *Who, Unread) : TEXT("@") + Who);
        Tab->SetStyle(FlatButton(bOn ? FEidoverseTheme::Private().CopyWithNewOpacity(.16f) : FLinearColor::Transparent,
            FEidoverseTheme::Hover(), 6, bOn ? FEidoverseTheme::Private().CopyWithNewOpacity(.5f) : FLinearColor::Transparent));
        Tab->OnClicked.AddDynamic(this, &UEidoverseHUD::OnDmTabClicked);
        ButtonIds.Add(Tab, FName(*(TEXT("dm:") + Who)));
        auto* Close = TextButton(TEXT("×"));
        Close->SetToolTipText(FText::FromString(TEXT("close ") + Who));
        Close->OnClicked.AddDynamic(this, &UEidoverseHUD::OnDmTabClicked);
        ButtonIds.Add(Close, FName(*(TEXT("dmx:") + Who)));
        // Before the unread jump button, which stays last.
        const int32 At = FMath::Max(0, ChatTabBar->GetChildrenCount() - 1);
        ChatTabBar->InsertChildAt(At, Tab); ChatTabBar->InsertChildAt(At + 1, Close);
        if (auto* S = Cast<UHorizontalBoxSlot>(Tab->Slot)) S->SetPadding(FMargin(0, 0, 0, 0));
        if (auto* S = Cast<UHorizontalBoxSlot>(Close->Slot)) S->SetPadding(FMargin(0, 0, 6, 0));
        DmTabWidgets.Add(Tab); DmTabWidgets.Add(Close);
    }
}

void UEidoverseHUD::OnDmTabClicked()
{
    const FString Id = ResolveClicked().ToString();
    if (Id.StartsWith(TEXT("dmx:"))) CloseDm(Id.Mid(4));
    else if (Id.StartsWith(TEXT("dm:"))) SwitchChatTab(3, Id.Mid(3));
}

int32 UEidoverseHUD::GetVisibleChatLineCount() const { return ChatLog ? ChatLog->GetChildrenCount() : 0; }

void UEidoverseHUD::SetChatDraft(const FString& Text)
{
    if (!ChatInput) return;
    ChatInput->SetText(FText::FromString(Text));
    OnChatChanged(FText::FromString(Text));
}

// ---------------------------------------------------------------- autocomplete

void UEidoverseHUD::UpdateAutocomplete(const FString& Draft)
{
    AutoItems.Reset(); AutoStart = -1;
    if (Draft.StartsWith(TEXT("/")) && !Draft.Contains(TEXT(" ")))
    {
        // ^/(\w*)$ — the command table, with its help.
        if (Bridge) for (const FString& C : Bridge->CommandCompletions(Draft.Mid(1)))
        { FString Name, Help; if (!C.Split(TEXT("\t"), &Name, &Help)) Name = C; AutoItems.Emplace(Name, Help); }
        AutoStart = 0;
    }
    else
    {
        // (?:^|\s)@([\w-]*)$ — up to 8 people present, not me.
        int32 At = INDEX_NONE; Draft.FindLastChar(TEXT('@'), At);
        if (At != INDEX_NONE && (At == 0 || FChar::IsWhitespace(Draft[At - 1])))
        {
            const FString Prefix = Draft.Mid(At + 1);
            bool bWord = true; for (const TCHAR C : Prefix) bWord &= FChar::IsAlnum(C) || C == '_' || C == '-';
            if (bWord && Bridge)
            {
                const TArray<FString> Ids = Bridge->GetPresentIds();
                for (const FString& Id : Ids)
                    if (Id != Bridge->AssignedIdentity && Id.StartsWith(Prefix, ESearchCase::IgnoreCase) && AutoItems.Num() < 8)
                        AutoItems.Emplace(TEXT("@") + Id, TEXT("here"));
                AutoStart = At;
            }
        }
    }
    // Nothing to offer, or the only offer is exactly what's typed: no list.
    if (AutoItems.Num() == 1 && Draft.Mid(AutoStart) == AutoItems[0].Key) AutoItems.Reset();
    AutoIndex = FMath::Clamp(AutoIndex, 0, FMath::Max(0, AutoItems.Num() - 1));
    RefreshAutocomplete();
}

void UEidoverseHUD::RefreshAutocomplete()
{
    if (!AutoList) return;
    AutoList->ClearChildren();
    AutoList->SetVisibility(AutoItems.Num() ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
    for (int32 I = 0; I < AutoItems.Num() && I < 12; ++I)
    {
        auto* Row = WidgetTree->ConstructWidget<UHorizontalBox>();
        auto* Name = Label(AutoItems[I].Key, 11, I == AutoIndex ? FEidoverseTheme::Brand() : FEidoverseTheme::Text(), true);
        Name->SetAutoWrapText(false);
        Row->AddChildToHorizontalBox(Name)->SetPadding(FMargin(0, 0, 8, 0));
        auto* Help = Label(AutoItems[I].Value, 10, FEidoverseTheme::Dim());
        Help->SetAutoWrapText(false);
        Row->AddChildToHorizontalBox(Help)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
        auto* B = WidgetTree->ConstructWidget<UButton>();
        B->SetStyle(FlatButton(I == AutoIndex ? FEidoverseTheme::Brand().CopyWithNewOpacity(.14f) : FLinearColor::Transparent, FEidoverseTheme::Hover(), 4));
        B->SetContent(Row);
        B->OnClicked.AddDynamic(this, &UEidoverseHUD::OnAutoClicked);
        ButtonIds.Add(B, FName(*FString::Printf(TEXT("auto:%d"), I)));
        AutoList->AddChildToVerticalBox(B);
    }
}

void UEidoverseHUD::AcceptAutocomplete()
{
    if (!ChatInput || !AutoItems.IsValidIndex(AutoIndex) || AutoStart < 0) return;
    const FString Draft = ChatInput->GetText().ToString();
    const FString Next = Draft.Left(AutoStart) + AutoItems[AutoIndex].Key + TEXT(" ");
    AutoItems.Reset(); AutoIndex = 0; AutoStart = -1;
    ChatInput->SetText(FText::FromString(Next));
    RefreshAutocomplete();
    ChatInput->SetKeyboardFocus();
}

void UEidoverseHUD::OnAutoClicked()
{
    const FString Id = ResolveClicked().ToString();
    if (!Id.StartsWith(TEXT("auto:"))) return;
    AutoIndex = FCString::Atoi(*Id.Mid(5));
    AcceptAutocomplete();
}

TArray<FString> UEidoverseHUD::GetAutocomplete() const
{
    TArray<FString> Out; for (const auto& I : AutoItems) Out.Add(I.Key); return Out;
}

bool UEidoverseHUD::ChatKey(const FString& KeyName)
{
    // The composer's list keys, callable without a keyboard (tests, gamepads).
    if (!AutoItems.Num()) return false;
    if (KeyName == TEXT("Down")) AutoIndex = (AutoIndex + 1) % AutoItems.Num();
    else if (KeyName == TEXT("Up")) AutoIndex = (AutoIndex + AutoItems.Num() - 1) % AutoItems.Num();
    else if (KeyName == TEXT("Tab") || KeyName == TEXT("Enter")) { AcceptAutocomplete(); return true; }
    else if (KeyName == TEXT("Escape")) AutoItems.Reset();
    else return false;
    RefreshAutocomplete();
    return true;
}
