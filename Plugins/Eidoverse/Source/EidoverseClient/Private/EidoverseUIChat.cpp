// Chat tabs and the composer's autocomplete (chat.js:1045-1200): all · mentions · system, each with its own draft.
// No DM tabs: whisper is gone by policy (ADAPTER-DESIGN §3.4). "/" lists the command table, "@" up to 8 people present.
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
#include "Components/RichTextBlock.h"
#include "Engine/DataTable.h"
#include "Framework/Text/RichTextMarkupProcessing.h"
#include "Internationalization/Regex.h"
#include "Styling/CoreStyle.h"

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
    if (Bridge) Bridge->Narrate(AEidoverseBridge::NoDirectMessages);
    ShowFrame(TEXT("chat"), true);
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
    if (Tab == 3) Tab = 0;   // no DM tab (OpenDm)
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
    for (UWidget* W : DmTabWidgets) if (W) W->RemoveFromParent();   // no DM tabs (OpenDm)
    DmTabWidgets.Reset(); DmTabs.Reset(); DmUnread.Reset();
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

// ---------------------------------------------------------------- markdown

namespace
{
    FString MdReplace(const FString& In, const FString& Pattern, const FString& Open)
    {
        FRegexMatcher M(FRegexPattern(Pattern), In);
        FString Out; int32 Last = 0;
        while (M.FindNext())
        {
            Out += In.Mid(Last, M.GetMatchBeginning() - Last) + Open + M.GetCaptureGroup(1) + TEXT("</>");
            Last = M.GetMatchEnding();
        }
        return Out + In.Mid(Last);
    }
}

FString UEidoverseHUD::ChatMarkup(const FString& Text)
{
    // User text never carries markup of its own: escape first, then the four rules.
    FString S = Text.Replace(TEXT("&"), TEXT("&amp;")).Replace(TEXT("\""), TEXT("&quot;")).Replace(TEXT("<"), TEXT("&lt;")).Replace(TEXT(">"), TEXT("&gt;"));
    S = MdReplace(S, TEXT("`([^`]+)`"), TEXT("<code>"));
    S = MdReplace(S, TEXT("\\*\\*(?!\\s)(.+?)(?<!\\s)\\*\\*"), TEXT("<b>"));
    S = MdReplace(S, TEXT("(?<![\\w*])\\*(?![\\s*])(.+?)(?<![\\s*])\\*(?![\\w*])"), TEXT("<i>"));
    S = MdReplace(S, TEXT("(?<!\\w)_(?!\\s)(.+?)(?<!\\s)_(?!\\w)"), TEXT("<i>"));
    return S;
}

UDataTable* UEidoverseHUD::ChatStyleSet(int32 Size, const FLinearColor& Color)
{
    const FString Key = FString::Printf(TEXT("%d|%s"), Size, *Color.ToString());
    if (UDataTable* T = ChatStyleSets.FindRef(Key)) return T;
    UDataTable* T = NewObject<UDataTable>(this);
    T->RowStruct = FRichTextStyleRow::StaticStruct();
    auto Add = [T, Size, Color](const TCHAR* Name, const FSlateFontInfo& Font, const FLinearColor& C)
    {
        FRichTextStyleRow Row;
        Row.TextStyle.SetFont(Font); Row.TextStyle.SetColorAndOpacity(FSlateColor(C));
        T->AddRow(FName(Name), Row);
    };
    Add(TEXT("Default"), FEidoverseTheme::Font(Size), Color);
    Add(TEXT("b"), FEidoverseTheme::Font(Size, true), Color);
    Add(TEXT("i"), FCoreStyle::GetDefaultFontStyle("Italic", Size), Color);
    Add(TEXT("code"), FCoreStyle::GetDefaultFontStyle("Mono", Size), FEidoverseTheme::Brand());
    ChatStyleSets.Add(Key, T);
    return T;
}

UWidget* UEidoverseHUD::ChatTextWidget(const FString& Text, int32 Size, const FLinearColor& Color)
{
    static IConsoleVariable* Md = IConsoleManager::Get().RegisterConsoleVariable(TEXT("Eidoverse.Chat.Markdown"), 1, TEXT("Render **bold**, *italic*, `code` in chat."), ECVF_Default);
    const bool bMarks = Text.Contains(TEXT("*")) || Text.Contains(TEXT("_")) || Text.Contains(TEXT("`"));
    if (!Md->GetInt() || !bMarks) return Label(Text, Size, Color);
    auto* R = WidgetTree->ConstructWidget<URichTextBlock>();
    R->SetTextStyleSet(ChatStyleSet(Size, Color));
    R->SetAutoWrapText(true);
    R->SetText(FText::FromString(ChatMarkup(Text)));
    return R;
}

void UEidoverseHUD::AdoptFrom(const UEidoverseHUD* Old)
{
    if (!Old) return;
    Lines = Old->Lines; DmTabs = Old->DmTabs; DmUnread = Old->DmUnread; Drafts = Old->Drafts; SentHistory = Old->SentHistory;
    RebuildDmTabs(); RefreshChat();
}
