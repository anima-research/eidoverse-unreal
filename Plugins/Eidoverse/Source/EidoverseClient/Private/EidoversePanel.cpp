#include "EidoversePanel.h"
#include "EidoverseBridge.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Styling/CoreStyle.h"

namespace {FText Txt(const FString& S){return FText::FromString(S);}}
FText SEidoversePanel::StatusText() const
{
    if(!Owner.IsValid())return Txt(TEXT("Offline"));
    const auto* B=Owner.Get();
    return Txt(B->bJoined?FString::Printf(TEXT("%s  |  %s  |  %d others%s"),*B->WorldName,*B->AssignedIdentity,B->RemoteCount,B->bSpectator?TEXT("  |  spectator"):TEXT("")):B->ConnectionStatus);
}
void SEidoversePanel::Construct(const FArguments& Args)
{
    Owner=Args._Bridge;
    ChildSlot[SNew(SBox).WidthOverride(480)
        [SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush")).BorderBackgroundColor(FLinearColor(.015f,.035f,.055f,.94f)).Padding(16)
        [SNew(SVerticalBox)
            +SVerticalBox::Slot().AutoHeight()[SNew(SHorizontalBox)
                +SHorizontalBox::Slot().FillWidth(1)[SNew(STextBlock).Text(Txt(TEXT("EIDOVERSE"))).Font(FCoreStyle::GetDefaultFontStyle("Bold",16)).ColorAndOpacity(FLinearColor(.3f,.85f,.9f))]
                +SHorizontalBox::Slot().AutoWidth()[SNew(SButton).Text_Lambda([this](){return Txt(bOpen?TEXT("Close · Esc"):TEXT("Chat / Login · Enter"));}).OnClicked_Lambda([this](){if(bOpen)Close();else Open();return FReply::Handled();})]]
            +SVerticalBox::Slot().AutoHeight().Padding(0,8)[SNew(STextBlock).Text(this,&SEidoversePanel::StatusText).AutoWrapText(true)]
            +SVerticalBox::Slot().AutoHeight()[SNew(SVerticalBox).Visibility_Lambda([this](){return bOpen?EVisibility::Visible:EVisibility::Collapsed;})
                +SVerticalBox::Slot().AutoHeight().Padding(0,4)[SNew(STextBlock).Text(Txt(TEXT("WORLDS · production or configured server"))).Font(FCoreStyle::GetDefaultFontStyle("Bold",12))]
                +SVerticalBox::Slot().AutoHeight()[SNew(SButton).Text(Txt(TEXT("Join Local Commons")))
                    .ToolTipText(Txt(TEXT("Join the local copy at 127.0.0.1:19002. Restores all preview settings; no Discord login. The local server must be running.")))
                    .OnClicked_Lambda([this](){if(Owner.IsValid()){Destination->SetText(Txt(TEXT("commons")));Owner->JoinLocalCommons();}return FReply::Handled();})]
                +SVerticalBox::Slot().AutoHeight().Padding(0,2,0,6)[SNew(STextBlock).Text(Txt(TEXT("Local copy · localhost:19002 · no Discord login"))).ColorAndOpacity(FLinearColor(.6f,.72f,.78f))]
                +SVerticalBox::Slot().AutoHeight()[SAssignNew(Destination,SEditableTextBox).Text(Txt(TEXT("commons"))).HintText(Txt(TEXT("World name")))]
                +SVerticalBox::Slot().AutoHeight()[SNew(SCheckBox).IsEnabled_Lambda([this](){return Owner.IsValid()&&!Owner->bJoined;})
                    .IsChecked_Lambda([this](){return Owner.IsValid()&&Owner->bRenderDynamicWorld?ECheckBoxState::Checked:ECheckBoxState::Unchecked;})
                    .OnCheckStateChanged_Lambda([this](ECheckBoxState S){if(Owner.IsValid())Owner->bRenderDynamicWorld=S==ECheckBoxState::Checked;})
                    [SNew(STextBlock).Text(Txt(TEXT("Render server world · land exploration (WASD / mouse / Space)")))]]
                +SVerticalBox::Slot().AutoHeight()[SNew(STextBlock).Text_Lambda([this](){return Owner.IsValid()?Txt(Owner->WorldRenderStatus):FText::GetEmpty();}).AutoWrapText(true)]
                +SVerticalBox::Slot().AutoHeight().Padding(0,4)[SNew(STextBlock).Text_Lambda([this](){return Owner.IsValid()?Txt(Owner->LoginStatus):FText::GetEmpty();}).AutoWrapText(true)]
                +SVerticalBox::Slot().AutoHeight()[SNew(SHorizontalBox)
                    +SHorizontalBox::Slot().AutoWidth()[SNew(SButton).Text(Txt(TEXT("Sign in with Discord"))).IsEnabled_Lambda([this](){return Owner.IsValid()&&!Owner->bAuthenticating&&!Owner->HasProductionSession();}).OnClicked_Lambda([this](){if(Owner.IsValid())Owner->BeginProductionLogin();return FReply::Handled();})]
                    +SHorizontalBox::Slot().AutoWidth().Padding(8,0)[SNew(SButton).Text(Txt(TEXT("Reopen browser"))).IsEnabled_Lambda([this](){return Owner.IsValid()&&Owner->bAuthenticating;}).OnClicked_Lambda([this](){if(Owner.IsValid())Owner->OpenProductionLoginBrowser();return FReply::Handled();})]]
                +SVerticalBox::Slot().AutoHeight().Padding(0,6)[SNew(SHorizontalBox)
                    +SHorizontalBox::Slot().AutoWidth()[SNew(SButton).Text(Txt(TEXT("Join production world"))).IsEnabled_Lambda([this](){return Owner.IsValid()&&Owner->HasProductionSession();}).OnClicked_Lambda([this](){if(Owner.IsValid()){FString Name=Destination->GetText().ToString().TrimStartAndEnd();if(AEidoverseBridge::IsWorldName(Name)){Owner->Disconnect();Owner->WorldName=Name;Owner->JoinProduction();}}return FReply::Handled();})]
                    +SHorizontalBox::Slot().AutoWidth().Padding(8,0)[SNew(SButton).Text_Lambda([this](){return Txt(Owner.IsValid()&&Owner->bAuthenticating?TEXT("Cancel sign-in"):TEXT("Sign out"));}).OnClicked_Lambda([this](){if(Owner.IsValid())Owner->LogoutProduction();return FReply::Handled();})]]
                +SVerticalBox::Slot().AutoHeight()[SNew(SHorizontalBox)
                    +SHorizontalBox::Slot().AutoWidth()[SNew(SButton).Text(Txt(TEXT("Join on configured server"))).OnClicked_Lambda([this](){if(Owner.IsValid())Owner->SwitchWorld(Destination->GetText().ToString().TrimStartAndEnd());return FReply::Handled();})]
                    +SHorizontalBox::Slot().AutoWidth().Padding(8,0)[SNew(SButton).Text(Txt(TEXT("Disconnect"))).OnClicked_Lambda([this](){if(Owner.IsValid())Owner->Disconnect();return FReply::Handled();})]]
                +SVerticalBox::Slot().AutoHeight().Padding(0,8)[SNew(STextBlock).Text(Txt(TEXT("Joining shares your name, avatar and movement. Public chat is stored by the server."))).AutoWrapText(true).ColorAndOpacity(FLinearColor(.6f,.72f,.78f))]
                +SVerticalBox::Slot().AutoHeight()[SNew(SCheckBox)
                    .IsChecked_Lambda([this](){return Owner.IsValid()&&Owner->bShareSceneScreenshots?ECheckBoxState::Checked:ECheckBoxState::Unchecked;})
                    .OnCheckStateChanged_Lambda([this](ECheckBoxState S){if(Owner.IsValid())Owner->SetShareSceneScreenshots(S==ECheckBoxState::Checked);})
                    [SNew(STextBlock).Text(Txt(TEXT("Share scene screenshots with headless clients")))]]
                +SVerticalBox::Slot().AutoHeight().Padding(0,4)[SNew(STextBlock).Text_Lambda([this](){return Owner.IsValid()?Txt(Owner->CaptureStatus):FText::GetEmpty();}).AutoWrapText(true)]
                +SVerticalBox::Slot().AutoHeight()[SNew(STextBlock).Text(Txt(TEXT("Opt-in: the server's public screenshot API can request views from participants. No chat, HUD or desktop. Captures may briefly pause rendering."))).AutoWrapText(true).ColorAndOpacity(FLinearColor(.6f,.72f,.78f))]
                +SVerticalBox::Slot().AutoHeight()[SNew(SBox).HeightOverride(190)[SAssignNew(History,SScrollBox)]]
                +SVerticalBox::Slot().AutoHeight().Padding(0,8,0,4)[SAssignNew(Recipient,SEditableTextBox).HintText(Txt(TEXT("Private recipient (blank = public)"))).OnTextChanged_Lambda([this](const FText& T){if(T.ToString().Len()>64)Recipient->SetText(Txt(T.ToString().Left(64)));})]
                +SVerticalBox::Slot().AutoHeight()[SNew(SHorizontalBox)
                    +SHorizontalBox::Slot().FillWidth(1)[SAssignNew(Compose,SEditableTextBox).HintText(Txt(TEXT("Message · Enter to send"))).ClearKeyboardFocusOnCommit(false)
                        .OnTextChanged_Lambda([this](const FText& T){if(T.ToString().Len()>4000)Compose->SetText(Txt(T.ToString().Left(4000)));})
                        .OnTextCommitted_Lambda([this](const FText&,ETextCommit::Type Kind){if(Kind==ETextCommit::OnEnter)Send();})]
                    +SHorizontalBox::Slot().AutoWidth().Padding(8,0,0,0)[SNew(SButton).Text(Txt(TEXT("Send"))).IsEnabled_Lambda([this](){return Owner.IsValid()&&Owner->bJoined&&!Owner->bSpectator&&PendingPublic.IsEmpty();}).OnClicked(this,&SEidoversePanel::Send)]]
                +SVerticalBox::Slot().AutoHeight().Padding(0,6,0,0)[SNew(STextBlock).Text_Lambda([this](){return Owner.IsValid()?Txt(Owner->ChatSendStatus):FText::GetEmpty();}).AutoWrapText(true).ColorAndOpacity(FLinearColor(1,.75f,.4f))]
            ]
        ]]];
    RefreshHistory();
}
void SEidoversePanel::Open()
{
    if(bOpen||!Owner.IsValid())return;
    auto* PC=UGameplayStatics::GetPlayerController(Owner.Get(),0);if(!PC)return;
    bOpen=true;InputOwner=PC;bPreviousCursor=PC->bShowMouseCursor;
    PC->SetIgnoreLookInput(true);PC->SetIgnoreMoveInput(true);PC->bShowMouseCursor=true;
    FInputModeUIOnly Mode;Mode.SetWidgetToFocus(Compose);Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);PC->SetInputMode(Mode);
    FSlateApplication::Get().SetKeyboardFocus(Compose,EFocusCause::SetDirectly);
    // Opening from a collapsed branch/console must outlive the current Slate
    // event; the next layout pass makes the composer a focusable widget path.
    RegisterActiveTimer(0.f,FWidgetActiveTimerDelegate::CreateSP(this,&SEidoversePanel::FocusComposer));
}
EActiveTimerReturnType SEidoversePanel::FocusComposer(double,float)
{
    if(bOpen)FSlateApplication::Get().SetKeyboardFocus(Compose,EFocusCause::SetDirectly);
    return EActiveTimerReturnType::Stop;
}
#if WITH_DEV_AUTOMATION_TESTS
void SEidoversePanel::ProbeLocalUI(bool bPrivate)
{
    if(!Owner.IsValid()||Owner->Endpoint!=TEXT("ws://127.0.0.1:18995/ws"))return;
    Open();Recipient->SetText(bPrivate?Txt(TEXT("fixture")):FText::GetEmpty());Compose->SetText(FText::GetEmpty());
    RegisterActiveTimer(.1f,FWidgetActiveTimerDelegate::CreateLambda([this](double,float)
    {
        FSlateApplication::Get().SetKeyboardFocus(Compose,EFocusCause::SetDirectly);
        for(TCHAR C:FString(TEXT("Native UI keyboard test")))FSlateApplication::Get().ProcessKeyCharEvent(FCharacterEvent(C,FModifierKeysState(),0,false));
        const bool Typed=Compose->GetText().ToString()==TEXT("Native UI keyboard test");
        FSlateApplication::Get().ProcessKeyDownEvent(FKeyEvent(EKeys::Enter,FModifierKeysState(),0,false,0,0));
        FSlateApplication::Get().ProcessKeyUpEvent(FKeyEvent(EKeys::Enter,FModifierKeysState(),0,false,0,0));
        UE_LOG(LogTemp,Display,TEXT("EIDOVERSE_UI_PROBE typed=%d cleared=%d"),Typed,Compose->GetText().IsEmpty());
        return EActiveTimerReturnType::Stop;
    }));
}
#endif
void SEidoversePanel::Close()
{
    if(!bOpen)return;bOpen=false;
    if(auto* PC=InputOwner.Get())
    {PC->SetIgnoreLookInput(false);PC->SetIgnoreMoveInput(false);PC->bShowMouseCursor=bPreviousCursor;PC->SetInputMode(FInputModeGameOnly());}
    InputOwner.Reset();
}
FReply SEidoversePanel::OnPreviewKeyDown(const FGeometry&,const FKeyEvent& E)
{
    if(E.GetKey()==EKeys::Escape){Close();return FReply::Handled();}
    return FReply::Unhandled();
}
FReply SEidoversePanel::Send()
{
    if(!Owner.IsValid()||!PendingPublic.IsEmpty())return FReply::Handled();
    const FString Message=Compose->GetText().ToString().TrimStartAndEnd(),To=Recipient->GetText().ToString().TrimStartAndEnd();
    if(To.IsEmpty()&&Message.StartsWith(TEXT("/"))&&Owner->HandleInteractionCommand(Message))
    {Compose->SetText(FText::GetEmpty());Owner->ChatSendStatus=Owner->InteractionStatus;return FReply::Handled();}
    const bool Sent=To.IsEmpty()?Owner->Say(Message):Owner->Whisper(To,Message);
    if(Sent)
    {
        Compose->SetText(FText::GetEmpty());
        if(To.IsEmpty()){PendingPublic=Message;PendingSince=FPlatformTime::Seconds();}
        Owner->ChatSendStatus=To.IsEmpty()?TEXT("Sending; waiting for server confirmation…"):TEXT("Private message sent (delivery is not acknowledged).");
    }
    else Owner->ChatSendStatus=TEXT("Not sent. Join first, enter a message, and wait briefly between sends. Spectators cannot send.");
    FSlateApplication::Get().SetKeyboardFocus(Compose,EFocusCause::SetDirectly);return FReply::Handled();
}
void SEidoversePanel::NotifyChatLine(const FString& Sender,const FString& Text,bool bPrivate)
{
    if(Owner.IsValid()&&!bPrivate&&!PendingPublic.IsEmpty()&&Sender==Owner->AssignedIdentity&&Text==PendingPublic)
    {PendingPublic.Empty();Owner->ChatSendStatus=TEXT("Delivered — confirmed by the server.");}
}
void SEidoversePanel::Tick(const FGeometry& Geometry,double Time,float Delta)
{
    SCompoundWidget::Tick(Geometry,Time,Delta);
    if(!Owner.IsValid()||PendingPublic.IsEmpty())return;
    if(!Owner->bJoined||FPlatformTime::Seconds()-PendingSince>=10)
    {
        const bool Restore=Compose->GetText().IsEmpty();if(Restore)Compose->SetText(Txt(PendingPublic));
        PendingPublic.Empty();Owner->ChatSendStatus=Restore?TEXT("Delivery unconfirmed. Message restored; check history before retrying."):TEXT("Delivery unconfirmed. Check history before retrying; current draft preserved.");
    }
}
void SEidoversePanel::RefreshHistory()
{
    if(!History||!Owner.IsValid())return;History->ClearChildren();
    for(const auto& Line:Owner->ChatHistory)
        History->AddSlot().Padding(0,3)[SNew(STextBlock).Text(Txt((Line.bPrivate?TEXT("[private] "):TEXT(""))+Line.Sender+TEXT(": ")+Line.Text))
            .WrapTextAt(420).ColorAndOpacity(Line.bPrivate?FLinearColor(.75f,.68f,1):FLinearColor(.85f,.92f,.95f))];
    History->ScrollToEnd();
}
