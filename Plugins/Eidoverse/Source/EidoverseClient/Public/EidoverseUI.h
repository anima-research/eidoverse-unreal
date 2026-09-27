#pragma once
#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "EidoverseUI.generated.h"

class AEidoverseBridge;
class UBorder;
class UButton;
class UCanvasPanel;
class UEditableTextBox;
class UImage;
class UScrollBox;
class UTextBlock;
class UVerticalBox;
class UHorizontalBox;
class UWrapBox;
class UTexture2D;
class UEidoverseFrame;

// The native client's UMG shell, matching the web client's chrome (rail, ∃ menu,
// frames, emote bar, toasts, hint bar, help overlay). Every widget builds its
// own tree in C++ when none was authored, so a packaged client needs no assets
// beyond the icons; a Widget Blueprint subclass can replace any of them.

// One place for the web theme tokens (client/index.html :root).
struct EIDOVERSECLIENT_API FEidoverseTheme
{
    static FLinearColor Brand();      // #8fe8c8 seafoam
    static FLinearColor Attention();  // #ffc46b amber
    static FLinearColor Error();      // #ff8f8f
    static FLinearColor Text();       // #ebebe9
    static FLinearColor Dim();        // #97979b
    static FLinearColor Icon();       // brand 55% + #c2c2c6
    static FLinearColor Panel();      // rgb(5 20 20 / .7)
    static FLinearColor Chip();       // black / .62
    static FLinearColor Well();       // black / .4
    static FLinearColor Edge();       // white / .10
    static FLinearColor Hover();      // white / .06
    static FLinearColor Private();    // whisper violet
    static FSlateBrush Rounded(const FLinearColor& Fill, float Radius = 10, const FLinearColor& Outline = FLinearColor::Transparent, float Width = 0);
    static FSlateFontInfo Font(int32 Size, bool bBold = false);
    static UTexture2D* IconTexture(const FString& Name);
};

// A titled, closable window: small-caps title chip above a rounded teal panel.
UCLASS()
class EIDOVERSECLIENT_API UEidoverseFrame : public UUserWidget
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Eidoverse|UI") FText Title;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Eidoverse|UI") FVector2D PanelSize = FVector2D(420, 300);
    UPROPERTY(BlueprintReadOnly, Category="Eidoverse|UI") TObjectPtr<UBorder> Body;
    DECLARE_DYNAMIC_MULTICAST_DELEGATE(FClosed);
    UPROPERTY(BlueprintAssignable, Category="Eidoverse|UI") FClosed OnClosed;
    // Content goes here (built by the owning HUD).
    void SetContent(UWidget* Content);
    // Drag by the title row, resize from the bottom-right corner (web frames.js).
    bool bLocked = false;
    TFunction<void()> OnLayoutChanged;
    // Canvas-local top-left; converts the slot to top-left anchoring first.
    void SetLayout(FVector2D Position, FVector2D Size);
    bool GetLayout(FVector2D& Position, FVector2D& Size) const;
protected:
    virtual TSharedRef<SWidget> RebuildWidget() override;
    virtual FReply NativeOnMouseButtonDown(const FGeometry& Geometry, const FPointerEvent& Event) override;
    virtual FReply NativeOnMouseMove(const FGeometry& Geometry, const FPointerEvent& Event) override;
    virtual FReply NativeOnMouseButtonUp(const FGeometry& Geometry, const FPointerEvent& Event) override;
private:
    UFUNCTION() void HandleClose();
    UPROPERTY() TObjectPtr<class USizeBox> SizeBox;
    enum class EDrag : uint8 { None, Move, Resize } Drag = EDrag::None;
    FVector2D DragMouse = FVector2D::ZeroVector, DragPosition = FVector2D::ZeroVector, DragSize = FVector2D::ZeroVector;
    bool MakeTopLeft();
    FVector2D CanvasPoint(const FPointerEvent& Event) const;
};

UCLASS()
class EIDOVERSECLIENT_API UEidoverseHUD : public UUserWidget
{
    GENERATED_BODY()
public:
    UPROPERTY(BlueprintReadWrite, Category="Eidoverse|UI") TObjectPtr<AEidoverseBridge> Bridge;

    // Frames by id: chat, world, people, profile, settings. The emote bar and help are separate.
    UFUNCTION(BlueprintCallable, Category="Eidoverse|UI") void ToggleFrame(FName Id);
    UFUNCTION(BlueprintCallable, Category="Eidoverse|UI") void ShowFrame(FName Id, bool bShow);
    UFUNCTION(BlueprintPure, Category="Eidoverse|UI") bool IsFrameOpen(FName Id) const;
    UFUNCTION(BlueprintCallable, Category="Eidoverse|UI") void ToggleHelp();
    UFUNCTION(BlueprintCallable, Category="Eidoverse|UI") void ToggleMenu();
    // Frame layout (position, size, open) persists per user; lock stops dragging.
    UFUNCTION(BlueprintCallable, Category="Eidoverse|UI") void SetLayoutLocked(bool bLocked);
    UFUNCTION(BlueprintCallable, Category="Eidoverse|UI") void ResetLayout();
    UFUNCTION(BlueprintPure, Category="Eidoverse|UI") bool IsLayoutLocked() const { return bLayoutLocked; }
    UFUNCTION(BlueprintCallable, Category="Eidoverse|UI") void FocusChat();
    UFUNCTION(BlueprintCallable, Category="Eidoverse|UI") void Toast(const FString& Text, bool bAttention = false);
    UFUNCTION(BlueprintCallable, Category="Eidoverse|UI") void Hint(const FString& Text, float Seconds = 4);
    // Mouselook vs pointer: the web client's M toggle; Esc frees the pointer.
    UFUNCTION(BlueprintCallable, Category="Eidoverse|UI") void SetPointerMode(bool bPointer);
    UFUNCTION(BlueprintPure, Category="Eidoverse|UI") bool IsPointerMode() const { return bPointerMode; }
    void NotifyChatLine(const FString& Sender, const FString& Text, bool bPrivate);
    bool IsChatFocused() const;
    UEidoverseFrame* GetFrame(FName Id) const { return Frames.FindRef(Id); }
    FString ChatDraft() const;
    void ReloadDefs();

protected:
    virtual TSharedRef<SWidget> RebuildWidget() override;
    virtual void NativeTick(const FGeometry& Geometry, float Delta) override;
    virtual FReply NativeOnPreviewKeyDown(const FGeometry& Geometry, const FKeyEvent& Event) override;

private:
    // Layout
    UPROPERTY() TObjectPtr<UCanvasPanel> Canvas;
    UPROPERTY() TMap<FName, TObjectPtr<UEidoverseFrame>> Frames;
    UPROPERTY() TMap<FName, TObjectPtr<UImage>> RailIcons;
    UPROPERTY() TObjectPtr<UImage> MarkIcon;
    UPROPERTY() TObjectPtr<UBorder> Menu;
    UPROPERTY() TObjectPtr<UBorder> HelpOverlay;
    UPROPERTY() TObjectPtr<UVerticalBox> HelpBody;
    UPROPERTY() TObjectPtr<UVerticalBox> ToastBox;
    UPROPERTY() TObjectPtr<UBorder> HintChip;
    UPROPERTY() TObjectPtr<UTextBlock> HintText;
    UPROPERTY() TObjectPtr<UHorizontalBox> EmoteBar;
    // Front door (offline): the only thing shown until a world is joined.
    UPROPERTY() TObjectPtr<UBorder> Door;
    UPROPERTY() TObjectPtr<UEditableTextBox> DoorName;
    UPROPERTY() TObjectPtr<UEditableTextBox> DoorServer;
    UPROPERTY() TObjectPtr<UEditableTextBox> DoorWorld;
    UPROPERTY() TObjectPtr<UTextBlock> DoorStatus;
    UPROPERTY() TObjectPtr<UTextBlock> DoorLogin;
    bool bDoorShown = false, bDoorFilled = false;
    // Layout
    struct FFrameDefault { FVector2D Size, Anchor, Alignment, Offset; bool bOpen = false; };
    TMap<FName, FFrameDefault> FrameDefaults;
    bool bLayoutLocked = false, bSuppressLayoutSave = false;
    UPROPERTY() TObjectPtr<UTextBlock> LockLabel;
    void LoadLayout();
    void SaveLayout();
    // Chat
    UPROPERTY() TObjectPtr<UScrollBox> ChatLog;
    UPROPERTY() TObjectPtr<UEditableTextBox> ChatInput;
    UPROPERTY() TArray<TObjectPtr<UButton>> ChatTabs;
    int32 ChatTab = 0;  // 0 all, 1 mentions, 2 system
    struct FLine { FString Sender, Text; bool bPrivate = false, bSystem = false, bMention = false; };
    TArray<FLine> Lines;
    TArray<FString> SentHistory;
    int32 RecallIndex = -1;
    // World / people / profile
    UPROPERTY() TObjectPtr<UTextBlock> WorldStatus;
    UPROPERTY() TObjectPtr<UTextBlock> LoginStatus;
    UPROPERTY() TObjectPtr<UEditableTextBox> WorldName;
    UPROPERTY() TObjectPtr<UEditableTextBox> ProfileName;
    UPROPERTY() TObjectPtr<UVerticalBox> PeopleList;
    UPROPERTY() TObjectPtr<UTextBlock> PeopleCount;
    // State
    bool bPointerMode = false, bPrevCursor = false;
    TArray<TPair<FString, bool>> Emotes;  // name, listed
    TSharedPtr<class FJsonObject> Defs;
    FString LastStatus, LastWorld, LastHttpBase;
    int32 LastPeopleHash = 0;
    double HintUntil = 0;
    struct FToast { TWeakObjectPtr<UWidget> Widget; double Until = 0; };
    TArray<FToast> Toasts;

    void Build();
    UWidget* BuildRail();
    UWidget* BuildMenu();
    UWidget* BuildChat();
    UWidget* BuildWorld();
    UWidget* BuildPeople();
    UWidget* BuildProfile();
    UWidget* BuildEmoteBar();
    UWidget* BuildHelp();
    UWidget* BuildDoor();
    void UpdateDoor();
    UEidoverseFrame* AddFrame(FName Id, const FString& Title, UWidget* Content, FVector2D Size, FVector2D Anchor, FVector2D Alignment, FVector2D Offset, bool bOpen);
    UButton* IconButton(const FString& Icon, float Size, FName Id);
    UButton* TextButton(const FString& Label, bool bPrimary = false);
    UTextBlock* Label(const FString& Text, int32 Size = 12, const FLinearColor& Color = FEidoverseTheme::Text(), bool bBold = false);
    UEditableTextBox* Input(const FString& Hint);
    void RefreshChat();
    void RefreshPeople(bool bForce);
    void RefreshRail();
    void RefreshEmotes();
    void RefreshHelp();
    void AddSystemLine(const FString& Text);
    void SendChat();
    void Emote(int32 Index);
    FString HttpBase() const;

    UFUNCTION() void OnRailClicked();
    UFUNCTION() void OnChatCommitted(const FText& Text, ETextCommit::Type Kind);
    UFUNCTION() void OnJoinClicked();
    UFUNCTION() void OnLocalCommonsClicked();
    UFUNCTION() void OnSignInClicked();
    UFUNCTION() void OnSignOutClicked();
    UFUNCTION() void OnDisconnectClicked();
    UFUNCTION() void OnChatTabAll();
    UFUNCTION() void OnChatTabMentions();
    UFUNCTION() void OnChatTabSystem();
    UFUNCTION() void OnEmoteClicked();
    UFUNCTION() void OnMenuEntry();
    UFUNCTION() void OnHelpClose();
    UFUNCTION() void OnDoorJoin();
    UFUNCTION() void OnDoorPublic();
    // Buttons carry their target in a tag; the last hovered one identifies the click.
    TMap<TWeakObjectPtr<UButton>, FName> ButtonIds;
    FName ResolveClicked() const;
};

// Floating name pill + speech bubble over a participant (web: avatar.js label
// sprite and chat bubbles; --pill-* tokens). Screen-space, constant size.
UCLASS()
class EIDOVERSECLIENT_API UEidoverseNameplate : public UUserWidget
{
    GENERATED_BODY()
public:
    UFUNCTION(BlueprintCallable, Category="Eidoverse|UI") void SetName(const FString& Name);
    UFUNCTION(BlueprintCallable, Category="Eidoverse|UI") void ShowBubble(const FString& Text, float Seconds = 8);
    UFUNCTION(BlueprintCallable, Category="Eidoverse|UI") void SetDistanceFade(float Alpha);
protected:
    virtual TSharedRef<SWidget> RebuildWidget() override;
    virtual void NativeTick(const FGeometry& Geometry, float Delta) override;
private:
    UPROPERTY() TObjectPtr<UTextBlock> NameText;
    UPROPERTY() TObjectPtr<UBorder> Bubble;
    UPROPERTY() TObjectPtr<UTextBlock> BubbleText;
    FString PendingName;
    double BubbleUntil = 0;
};
