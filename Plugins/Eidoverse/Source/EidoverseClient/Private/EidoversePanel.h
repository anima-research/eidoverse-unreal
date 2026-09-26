#pragma once
#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
class AEidoverseBridge;
class APlayerController;
class SEditableTextBox;
class SScrollBox;

// Native Slate UI: packaged clients need no editor-built widget asset or browser.
class SEidoversePanel : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SEidoversePanel){} SLATE_ARGUMENT(AEidoverseBridge*,Bridge) SLATE_END_ARGS()
    void Construct(const FArguments& Args);
    void Open();
    void Close();
    void RefreshHistory();
    void NotifyChatLine(const FString& Sender,const FString& Text,bool bPrivate);
    virtual void Tick(const FGeometry&,double,float) override;
#if WITH_DEV_AUTOMATION_TESTS
    void ProbeLocalUI(bool bPrivate);
#endif
    virtual bool SupportsKeyboardFocus() const override{return true;}
    virtual FReply OnPreviewKeyDown(const FGeometry&,const FKeyEvent&) override;
private:
    TWeakObjectPtr<AEidoverseBridge> Owner;
    TWeakObjectPtr<APlayerController> InputOwner;
    TSharedPtr<SEditableTextBox> Compose,Recipient,Destination;
    TSharedPtr<SScrollBox> History;
    bool bOpen=false,bPreviousCursor=false;
    FString PendingPublic;
    double PendingSince=0;
    FReply Send();
    FText StatusText() const;
    EActiveTimerReturnType FocusComposer(double,float);
};
