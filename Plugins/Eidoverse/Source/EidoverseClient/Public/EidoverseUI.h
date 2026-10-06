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
class UWidgetSwitcher;

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
    static FButtonStyle Button(const FLinearColor& Normal, const FLinearColor& Hovered, float Radius = 6, const FLinearColor& Outline = FLinearColor::Transparent);
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
    // Panels own the wheel: scrolling chat (even past its end) must never zoom the camera.
    virtual FReply NativeOnMouseWheel(const FGeometry& Geometry, const FPointerEvent& Event) override;
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
    // A spoken utterance's continuation (same author, same utt): one paragraph (chat.js).
    void AppendToLastChatLine(const FString& Sender, const FString& Text);
    void NoteTyping(const FString& Id);
    // Open chat with text appended to the draft (e.g. "@name ", "/w name ").
    UFUNCTION(BlueprintCallable, Category="Eidoverse|UI") void InsertIntoChat(const FString& Text, bool bReplace = false);
    void SystemLine(const FString& Text) { AddSystemLine(Text); }
    // Older lines from history paging, oldest first; keeps the reader's place.
    void PrependChatLines(const TArray<TPair<FString, FString>>& Older, const FString& Note);
    void ClearChat();
    void RefreshPanelOpacity() { ApplyPanelOpacity(); }
    // World frame: select a section (0 here, 1 avatar, 2 sky, 3 ground, 4 scene) and run
    // the same actions its buttons do (e.g. "sky:preset:dusk", "sky:apply", "ground:apply").
    UFUNCTION(BlueprintCallable, Category="Eidoverse|UI") void SelectWorldTab(int32 Tab);
    UFUNCTION(BlueprintCallable, Category="Eidoverse|UI") void WorldAction(const FString& Action);
    // Build mode (EidoverseUIBuild.cpp): same actions as its buttons and keys, e.g.
    // "build:hold:<lib path>" (ghost), "build:spawn:<lib path>" (4 m ahead), "build:turn:15", "build:raise:0.25", "build:remove", "build:undo".
    UFUNCTION(BlueprintCallable, Category="Eidoverse|UI") void BuildAction(const FString& Action);
    UFUNCTION(BlueprintCallable, Category="Eidoverse|UI") void SelectEntity(const FString& Id);
    UFUNCTION(BlueprintPure, Category="Eidoverse|UI") FString GetSelectedEntity() const;
    UFUNCTION(BlueprintCallable, Category="Eidoverse|UI") void SearchLibrary(const FString& Query);
    UFUNCTION(BlueprintPure, Category="Eidoverse|UI") FString GetLastSpawnedEntity() const { return LastSpawned; }
    // Placement ghost ("build:hold:<lib>" from a library tile): follows the aim until a
    // click places it (PlaceGhost) or Esc cancels (CancelGhost).
    UFUNCTION(BlueprintPure, Category="Eidoverse|UI") bool HasGhost() const;
    UFUNCTION(BlueprintCallable, Category="Eidoverse|UI") bool CancelGhost();
    UFUNCTION(BlueprintCallable, Category="Eidoverse|UI") bool PlaceGhost();
    // World transform of the ghost (identity when none).
    UFUNCTION(BlueprintPure, Category="Eidoverse|UI") FTransform GetGhostTransform() const;
    // Component authoring (scene inspector): data as JSON text, any JSON value; an
    // empty Json removes the component. Sends the `comp` verb; the server decides.
    UFUNCTION(BlueprintCallable, Category="Eidoverse|UI") bool SetComponentJson(const FString& Id, const FString& Type, const FString& Json);
    bool IsChatFocused() const;
    UFUNCTION(BlueprintPure, Category="Eidoverse|UI") UEidoverseFrame* GetFrame(FName Id) const { return Frames.FindRef(Id); }
    UScrollBox* GetChatLog() const { return ChatLog; }
    UFUNCTION(BlueprintPure, Category="Eidoverse|UI") FString ChatDraft() const;
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
    int32 ChatTab = 0;  // 0 all, 1 mentions, 2 system, 3 the DM tab ActiveDm
    // Whisper tabs (chat.js): one per conversation, unread counts, a draft per tab;
    // Enter in one whispers that person. And the composer's / and @ autocomplete.
    UPROPERTY() TObjectPtr<UHorizontalBox> ChatTabBar;
    UPROPERTY() TArray<TObjectPtr<UWidget>> DmTabWidgets;
    TArray<FString> DmTabs;
    TMap<FString, int32> DmUnread;
    TMap<FString, FString> Drafts;
    FString ActiveDm;
    void RebuildDmTabs();
    void SwitchChatTab(int32 Tab, const FString& Dm);
    UFUNCTION() void OnDmTabClicked();
    UPROPERTY() TObjectPtr<UVerticalBox> AutoList;
    TArray<TPair<FString, FString>> AutoItems;   // value inserted, help shown
    int32 AutoIndex = 0, AutoStart = -1;
    void UpdateAutocomplete(const FString& Draft);
    void RefreshAutocomplete();
    void AcceptAutocomplete();
    UFUNCTION() void OnAutoClicked();
public:
    UFUNCTION(BlueprintCallable, Category="Eidoverse|UI") void OpenDm(const FString& Name);
    UFUNCTION(BlueprintCallable, Category="Eidoverse|UI") void CloseDm(const FString& Name);
    UFUNCTION(BlueprintPure, Category="Eidoverse|UI") TArray<FString> GetDmTabs() const { return DmTabs; }
    UFUNCTION(BlueprintPure, Category="Eidoverse|UI") FString GetActiveDm() const { return ChatTab == 3 ? ActiveDm : FString(); }
    UFUNCTION(BlueprintPure, Category="Eidoverse|UI") int32 GetDmUnread(const FString& Name) const { return DmUnread.FindRef(Name); }
    UFUNCTION(BlueprintPure, Category="Eidoverse|UI") TArray<FString> GetAutocomplete() const;
    UFUNCTION(BlueprintPure, Category="Eidoverse|UI") int32 GetVisibleChatLineCount() const;
    UFUNCTION(BlueprintCallable, Category="Eidoverse|UI") void SetChatDraft(const FString& Text);
    UFUNCTION(BlueprintCallable, Category="Eidoverse|UI") bool ChatKey(const FString& KeyName);
    UFUNCTION(BlueprintCallable, Category="Eidoverse|UI") void SubmitChat() { SendChat(); }
    // Front door (ui.js openDoor): the avatar roster with portraits, a door key.
    UFUNCTION(BlueprintPure, Category="Eidoverse|UI") int32 GetDoorRosterCount() const { return DoorRoster.Num(); }
    UFUNCTION(BlueprintPure, Category="Eidoverse|UI") int32 GetDoorPortraitCount() const { return DoorThumbs.Num(); }
    UFUNCTION(BlueprintCallable, Category="Eidoverse|UI") void SelectDoorAvatar(const FString& Name);
    UFUNCTION(BlueprintPure, Category="Eidoverse|UI") FString GetDoorAvatar() const { return DoorAvatar; }
    UFUNCTION(BlueprintCallable, Category="Eidoverse|UI") void ShowDoorKey(bool bShow);
    UFUNCTION(BlueprintCallable, Category="Eidoverse|UI") void DoorEnter() { OnDoorJoin(); }
    UFUNCTION(BlueprintCallable, Category="Eidoverse|UI") void SetDoorFields(const FString& Server, const FString& World, const FString& Name);
    // Chat markdown (chat.js CH-E): **b**, *i* / _i_, `code` on a rich text block.
    UFUNCTION(BlueprintPure, Category="Eidoverse|UI") static FString ChatMarkup(const FString& Text);
    // Inspector transform fields (scenegraph.js:137-154): x y z (wire m), yaw degrees, scale.
    UFUNCTION(BlueprintCallable, Category="Eidoverse|UI") bool ApplyInspectTransform(FVector Pos, float YawDegrees, float Scale);
    UFUNCTION(BlueprintCallable, Category="Eidoverse|UI") void SetSceneFind(const FString& Query);
    UFUNCTION(BlueprintPure, Category="Eidoverse|UI") int32 GetSceneListCount() const;
    // Seat-anchor gizmos (seatedit.js): markers for the selected thing's seats, picked
    // by click; Q/E face, Del removes, drag slides along the surface, Esc done.
    UFUNCTION(BlueprintPure, Category="Eidoverse|UI") int32 GetSeatGizmoCount() const;
    UFUNCTION(BlueprintPure, Category="Eidoverse|UI") FString GetSelectedSeat() const { return SeatSel; }
    UFUNCTION(BlueprintCallable, Category="Eidoverse|UI") void SelectSeat(const FString& SeatSlot) { SeatSel = SeatSlot; }
private:
    FString SeatSel, SeatSelOn;
    bool bSeatDragArmed = false, bSeatDragging = false;
    FVector2D SeatDragStart = FVector2D::ZeroVector;
    FVector SeatDragLocal = FVector::ZeroVector;   // wire metres, model frame
    UPROPERTY() TObjectPtr<AActor> SeatGizmoHolder;
    UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> SeatGizmoParts;
    bool TickSeatGizmos(APlayerController* PC, class AEidoverseScene* Scene, bool bShift);
public:
private:
    UPROPERTY() TArray<TObjectPtr<UEditableTextBox>> InspectFields;
    UPROPERTY() TObjectPtr<UEditableTextBox> SceneFind;
    UFUNCTION() void OnInspectFieldCommitted(const FText& Text, ETextCommit::Type Kind);
    UFUNCTION() void OnSceneFindChanged(const FText& Text);
public:
private:
    UPROPERTY() TMap<FString, TObjectPtr<class UDataTable>> ChatStyleSets;
    class UDataTable* ChatStyleSet(int32 Size, const FLinearColor& Color);
    UWidget* ChatTextWidget(const FString& Text, int32 Size, const FLinearColor& Color);
public:
private:
    struct FDoorAvatar { FString Name, Path; };
    TArray<FDoorAvatar> DoorRoster;
    FString DoorRosterBase, DoorAvatar;
    double NextRosterCheck = 0;
    UPROPERTY() TMap<FString, TObjectPtr<UTexture2D>> DoorThumbs;
    TSet<FString> DoorThumbRequested;
    UPROPERTY() TObjectPtr<class UWrapBox> DoorGrid;
    UPROPERTY() TObjectPtr<UEditableTextBox> DoorKey;
    UPROPERTY() TObjectPtr<UWidget> DoorKeyRow;
    UPROPERTY() TObjectPtr<UButton> DoorKeyReveal;
    UPROPERTY() TObjectPtr<UButton> DoorGo;
    void TickDoorRoster(double Now);
    void RebuildDoorGrid();
    UFUNCTION() void OnDoorAvatarClicked();
    UFUNCTION() void OnDoorKeyReveal();
public:
private:
    struct FLine { FString Sender, Text; bool bPrivate = false, bSystem = false, bMention = false; FDateTime At = FDateTime::Now(); FString Peer; };
    // chat.js polish: name colours (FNV-1a over a 12-colour palette, collisions step),
    // grouping within 90 s, HH:MM, links, the unread jump, persisted recall.
    TMap<FString, int32> NameColours;
    FLinearColor NameColour(const FString& Name);
    int32 UnreadLines = 0, UnreadMentions = 0;
    UPROPERTY() TObjectPtr<UButton> UnreadButton;
    UFUNCTION() void OnUnreadClicked();
    void LoadChatHistory();
    void SaveChatHistory() const;
    bool bKeepChatScroll = false;
    double NextHistoryCheck = 0;
    TArray<FLine> Lines;
    TArray<FString> SentHistory;
    TMap<FString, double> Typers;
    UPROPERTY() TObjectPtr<UTextBlock> TypingLine;
    double LastTypingSent = 0;
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
    // Chat opened from mouselook (Enter) returns to mouselook when it closes.
    bool bChatFromMouselook = false;
    void LeaveChat(bool bToMouselook);
    TArray<TPair<FString, bool>> Emotes;  // name, listed
    TSharedPtr<class FJsonObject> Defs;
    FString LastStatus, LastWorld, LastHttpBase;
    int32 LastPeopleHash = 0;
    double HintUntil = 0;
    struct FToast { TWeakObjectPtr<UWidget> Widget; TWeakObjectPtr<UTextBlock> Label; FString Text; bool bAttention = false; int32 Count = 1; double Until = 0; };
    double NextPeopleRepaint = 0;
    TArray<FToast> Toasts;

    void Build();
    UWidget* BuildRail();
    UWidget* BuildMenu();
    UWidget* BuildChat();
    UWidget* BuildWorld();
    // World frame sections (EidoverseUIWorld.cpp): here / avatar / sky / ground / scene.
    UWidget* BuildWorldHere();
    UWidget* BuildWorldAvatar();
    UWidget* BuildWorldSky();
    UWidget* BuildWorldGround();
    UWidget* BuildWorldScene();
    void RefreshWorldPanels();
    void LoadThumb(const FString& Name);
    void LoadImage(const FString& Url, const FString& Key);
    UWidget* BuildBuild();
    void TickBuild(double Now);
    void RefreshBuild();
    void PaintLibrary(const TArray<TSharedPtr<FJsonValue>>& Items);
    bool PlaceSelected(TFunctionRef<void(FVector& Pos, double& Yaw, double& Scale)> Edit);
    UPROPERTY() TObjectPtr<UEditableTextBox> BuildSearch;
    UPROPERTY() TObjectPtr<UWrapBox> BuildResults;
    UPROPERTY() TObjectPtr<UTextBlock> BuildSelected;
    UPROPERTY() TObjectPtr<UTextBlock> BuildNote;
    double SearchAt = 0;
    FString PendingQuery, LastQuery;
    bool bSearchPending = false;
    TArray<TPair<FString, FString>> UndoStack;  // inverse verb, args JSON
    TMap<FString, FString> LibNames;
    FString LastSpawned;
    double GhostYaw = 0, GhostScale = 1, GhostLift = 0;
    // ---- build tools (EidoverseUIBuildTools.cpp, build.js / lights.js / scenegraph.js) ----
    struct FDrag { FString Id; FVector2D StartMouse=FVector2D::ZeroVector; bool bArmed=false, bActive=false, bVertical=false;
        double Depth=0, Yaw=0, Scale=1, LastSent=0; FVector Grab=FVector::ZeroVector, StartWorld=FVector::ZeroVector, BeforeWire=FVector::ZeroVector, CurrentWire=FVector::ZeroVector; };
    FDrag Drag;
    void TickDrag(class APlayerController* PC, class AEidoverseScene* Scene, bool bShift);
    UPROPERTY() TObjectPtr<UVerticalBox> BuildExtras;
    FString ExtrasFor;
    FString AttachPick;          // "attach to…" armed: the child waiting for a parent click
    void RebuildBuildExtras();
    FString AuthorRefusal(const FString& Id) const;
    bool BuildToolAction(const FString& Action);
    void PushUndo(const FString& Verb, const TSharedRef<FJsonObject>& Args);
    // Seat anchors (seatedit.js), sound and picture (sounds.js, pictures.js), uploads.
    bool bArmSeat = false;
    FString SelectedSlot;
    UPROPERTY() TObjectPtr<UEditableTextBox> FileField;
    UPROPERTY() TObjectPtr<UEditableTextBox> PartField;
    void TickSeatPlacement(class APlayerController* PC, class AEidoverseScene* Scene);
    void SendSockets(const FString& Id, const TSharedPtr<FJsonObject>& Sockets, const TSharedPtr<FJsonObject>& Before);
    void SendComp(const FString& Id, const FString& Type, const TSharedPtr<FJsonObject>& Data);
    // Structure editor (structure_ui.js over shared/structure_edit.js), EidoverseUIStructure.cpp.
    FString StructTool, StructTarget;
    TSharedPtr<FJsonObject> StructData;
    int32 StructLevel = 0, StructInspectSerial = -1;
    bool bStructDragging = false;
    FIntPoint StructFrom = FIntPoint::ZeroValue;
    TArray<FString> StructUndo;
    // The structure editor's preview (structure_ui.js): footprint grid +-3 cells, the
    // would-be result as a translucent ghost, the cell/edge under the cursor.
    UPROPERTY() TObjectPtr<AActor> StructPreviewHolder;
    UPROPERTY() TArray<TObjectPtr<class UInstancedStaticMeshComponent>> StructPreviewSets;   // grid, ghost, hover
    FString StructPreviewKey;
    void DrawStructPreview(class AEidoverseScene* Scene, const TSharedPtr<FJsonObject>& Result, const FVector& Pos, double Yaw, double Scale, double PlaneY, FIntPoint HoverCell, int32 HoverAxis, FIntPoint HoverEdge);
    void HideStructPreview();
public:
    UFUNCTION(BlueprintPure, Category="Eidoverse|UI") int32 GetStructPreviewCount(int32 Set) const;
private:
    bool TickStructure(class APlayerController* PC, class AEidoverseScene* Scene);
    bool StructureAction(const FString& Action);
    void AddStructureRows(const FString& Id);
    bool bGhostAimed = false;
    bool AimGhost(FVector& OutWorld) const;
    UButton* ActionButton(const FString& Text, const FString& Id, bool bPrimary = false);
    UPROPERTY() TObjectPtr<UWidgetSwitcher> WorldPages;
    UPROPERTY() TArray<TObjectPtr<UButton>> WorldTabs;
    int32 WorldTab = 0;
    UPROPERTY() TObjectPtr<UWrapBox> AvatarGrid;
    UPROPERTY() TObjectPtr<UTextBlock> AvatarNote;
    UPROPERTY() TMap<FString, TObjectPtr<UTexture2D>> Thumbs;
    TMap<FString, TWeakObjectPtr<UImage>> ThumbImages;
    TSet<FString> ThumbRequested;
    int32 AvatarHash = 0;
    TSharedPtr<class FJsonObject> SkyDraft, SkySource;
    bool bSkyDirty = false;
    UPROPERTY() TObjectPtr<UTextBlock> SkyState;
    UPROPERTY() TObjectPtr<UTextBlock> SkyNote;
    UPROPERTY() TObjectPtr<UWrapBox> SkyPresets;
    // skypanel.js sliders: field -> slider, and the value last shown (user moves differ).
    UPROPERTY() TMap<FString, TObjectPtr<class USlider>> SkySliders;
    TMap<FString, float> SkySliderShown;
    UPROPERTY() TObjectPtr<UWrapBox> SkyClocks;
    void PreviewSkyDraft(bool bOn);
    UPROPERTY() TArray<TObjectPtr<UButton>> OwnerButtons;
    FString GroundShape = TEXT("hills"), GroundTint = TEXT("meadow"), GrassHeight = TEXT("meadow"), GrassDensity = TEXT("normal");
    int32 GroundSeed = 7;
    // groundpanel.js: shape/tint apply at once; height/density regrow what was planted.
    FString LastPlanting; bool bGrassGrown = false;
    UPROPERTY() TObjectPtr<UTextBlock> GroundState;
    UPROPERTY() TObjectPtr<UTextBlock> GroundNote;
    UPROPERTY() TObjectPtr<UWrapBox> GroundShapes;
    UPROPERTY() TObjectPtr<UWrapBox> GroundTints;
    UPROPERTY() TObjectPtr<UWrapBox> GroundPlantings;
    int32 GroundDefsHash = 0;
    UPROPERTY() TObjectPtr<UVerticalBox> SceneList;
    // Scene inspector (EidoverseUIInspect.cpp): the selected entity's components.
    UPROPERTY() TObjectPtr<UVerticalBox> SceneInspector;
    UPROPERTY() TObjectPtr<UTextBlock> InspectPose;
    UPROPERTY() TObjectPtr<class UMultiLineEditableTextBox> CompEditor;
    UPROPERTY() TObjectPtr<UEditableTextBox> CompTypeInput;
    FString InspectingId, InspectingRevision, EditingComp;
    bool bAddingComp = false;
    int32 InspectHash = 0;
    double ReinspectAt = 0;
    void RefreshInspector(double Now);
    void RebuildInspector();
    void InspectorAction(const FString& Action);
    UPROPERTY() TObjectPtr<UTextBlock> SceneNote;
    double NextSceneRefresh = 0;
    int32 SceneHash = 0;
    UWidget* BuildPeople();
    UWidget* BuildProfile();
    UWidget* BuildSettings();
    void RefreshSettings();
    void ApplyPanelOpacity();
    TMap<FName, TObjectPtr<UTextBlock>> SettingValues;
    TArray<TWeakObjectPtr<UBorder>> PanelBorders;
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
    UFUNCTION() void OnChatChanged(const FText& Text);
    UFUNCTION() void OnNameClicked();
    UFUNCTION() void OnWorldClicked();
    UFUNCTION() void OnBuildClicked();
    UFUNCTION() void OnBuildSearchChanged(const FText& Text);
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
    UFUNCTION() void OnSettingClicked();
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
    // Composing indicator after the name (web: typing dots on the pill).
    UFUNCTION(BlueprintCallable, Category="Eidoverse|UI") void ShowTyping(float Seconds = 4);
    // Presence glyph instead of dots (typing state: ear | think | tool | mic); empty =
    // composing dots; bStop clears (avatar.js setTyping(null)).
    void ShowTypingState(const FString& State, bool bStop = false);
protected:
    virtual TSharedRef<SWidget> RebuildWidget() override;
    virtual void NativeTick(const FGeometry& Geometry, float Delta) override;
private:
    UPROPERTY() TObjectPtr<UTextBlock> NameText;
    UPROPERTY() TObjectPtr<UTextBlock> TypingDots;
    UPROPERTY() TObjectPtr<class UImage> StateIcon;
    double TypingUntil = 0;
    FString TypingState;
    UPROPERTY() TObjectPtr<UBorder> Bubble;
    UPROPERTY() TObjectPtr<UTextBlock> BubbleText;
    FString PendingName;
    double BubbleUntil = 0;
};
