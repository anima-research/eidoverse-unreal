// Build mode: library search, spawning, selection and nudging, with undo.
// Wire behaviour mirrors client/lib/build.js + palette.js: spawn {id, lib, pos,
// yaw, scale?} with an 8-hex client id, place {id, pos, yaw, scale}, remove {id};
// undo sends the inverse verb. The server decides (rights, lock, guard).
#include "EidoverseUI.h"
#include "EidoverseBridge.h"
#include "EidoverseScene.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/EditableTextBox.h"
#include "Components/HorizontalBox.h"
#include "Components/Image.h"
#include "Components/ScrollBox.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/WrapBox.h"
#include "Dom/JsonObject.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "GenericPlatform/GenericPlatformHttp.h"
#include "HttpModule.h"
#include "Interfaces/IHttpResponse.h"
#include "Misc/Guid.h"
#include "Misc/Paths.h"
#include "Framework/Application/SlateApplication.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace
{
    // Web palette.js STARTER: what an empty search shows.
    const TCHAR* Starter[][2] = {
        {TEXT("crate (red)"), TEXT("eidoverse/assets/models/crate_large_red.glb")},
        {TEXT("crate (blue)"), TEXT("eidoverse/assets/models/crate_large_blue.glb")},
        {TEXT("palm tree"), TEXT("eidoverse/assets/models/palm_date_tree_tropical_deseert_oasis_plant.glb")},
        {TEXT("joshua tree"), TEXT("eidoverse/assets/models/stylized_yucca_joshua_tree_desert_cactus_plant.glb")},
        {TEXT("streetlight"), TEXT("eidoverse/assets/models/streetlight_lamp_light_street_blade_runner_cyberpunk.glb")},
        {TEXT("deco desk"), TEXT("eidoverse/assets/models/scifi_art_deco_office_desk.glb")},
        {TEXT("retro computer"), TEXT("eidoverse/assets/models/scif_cyberpunk_crt_retro_computer_monitor_screen_keyboard_tower.glb")},
        {TEXT("barrels"), TEXT("eidoverse/assets/models/scifi_barrels_group_of_four.glb")},
    };

    FString ToJson(const TSharedRef<FJsonObject>& O)
    {
        FString S;
        FJsonSerializer::Serialize(O, TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&S));
        return S;
    }
    TArray<TSharedPtr<FJsonValue>> Vec3(const FVector& V)
    {
        auto R = [](double X) { return MakeShared<FJsonValueNumber>(FMath::RoundToDouble(X * 1000) / 1000); };
        return {R(V.X), R(V.Y), R(V.Z)};
    }
    FString NewEntityId() { return FGuid::NewGuid().ToString(EGuidFormats::Digits).ToLower().Left(8); }
}

UWidget* UEidoverseHUD::BuildBuild()
{
    auto* Column = WidgetTree->ConstructWidget<UVerticalBox>();
    BuildNote = Label(TEXT(""), 10, FEidoverseTheme::Attention());
    Column->AddChildToVerticalBox(BuildNote);
    BuildSearch = Input(TEXT("search the library…"));
    BuildSearch->OnTextChanged.AddDynamic(this, &UEidoverseHUD::OnBuildSearchChanged);
    Column->AddChildToVerticalBox(BuildSearch)->SetPadding(FMargin(0, 4, 0, 6));
    auto* Height = WidgetTree->ConstructWidget<USizeBox>();
    Height->SetHeightOverride(210);
    auto* Scroll = WidgetTree->ConstructWidget<UScrollBox>();
    BuildResults = WidgetTree->ConstructWidget<UWrapBox>();
    BuildResults->SetInnerSlotPadding(FVector2D(6, 6));
    Scroll->AddChild(BuildResults);
    Height->SetContent(Scroll);
    Column->AddChildToVerticalBox(Height);
    Column->AddChildToVerticalBox(Label(TEXT("SELECTED"), 9, FEidoverseTheme::Dim(), true))->SetPadding(FMargin(0, 10, 0, 4));
    BuildSelected = Label(TEXT("nothing — click a thing in the world"), 11, FEidoverseTheme::Brand());
    Column->AddChildToVerticalBox(BuildSelected);
    auto* Actions = WidgetTree->ConstructWidget<UWrapBox>();
    Actions->SetInnerSlotPadding(FVector2D(4, 4));
    const TPair<const TCHAR*, const TCHAR*> Buttons[] = {
        {TEXT("↺ turn"), TEXT("build:turn:15")}, {TEXT("↻ turn"), TEXT("build:turn:-15")},
        {TEXT("raise"), TEXT("build:raise:0.25")}, {TEXT("lower"), TEXT("build:raise:-0.25")},
        {TEXT("smaller"), TEXT("build:size:0.92")}, {TEXT("bigger"), TEXT("build:size:1.087")},
        {TEXT("remove"), TEXT("build:remove")}, {TEXT("undo"), TEXT("build:undo")}};
    for (const auto& B : Buttons)
    {
        UButton* Button = TextButton(B.Key);
        Button->OnClicked.AddDynamic(this, &UEidoverseHUD::OnBuildClicked);
        ButtonIds.Add(Button, FName(B.Value));
        Actions->AddChildToWrapBox(Button);
    }
    Column->AddChildToVerticalBox(Actions)->SetPadding(FMargin(0, 6, 0, 0));
    Column->AddChildToVerticalBox(Label(TEXT("B build · click to select (pointer: M) · Q/E turn · R/F raise · , . size · arrows nudge · Del remove · Ctrl+Z undo"),
        9, FEidoverseTheme::Dim()))->SetPadding(FMargin(0, 8, 0, 0));
    SearchLibrary(TEXT(""));
    return Column;
}

void UEidoverseHUD::PaintLibrary(const TArray<TSharedPtr<FJsonValue>>& Items)
{
    if (!BuildResults) return;
    BuildResults->ClearChildren();
    for (auto It = ButtonIds.CreateIterator(); It; ++It) if (!It->Key.IsValid()) It.RemoveCurrent();
    const FString Base = HttpBase();
    for (const auto& V : Items)
    {
        const TSharedPtr<FJsonObject> O = V->AsObject();
        FString Path, Name, Preview;
        if (!O || !O->TryGetStringField(TEXT("path"), Path) || !Path.EndsWith(TEXT(".glb"))) continue;
        O->TryGetStringField(TEXT("name"), Name);
        O->TryGetStringField(TEXT("preview"), Preview);
        LibNames.Add(Path, Name.IsEmpty() ? Path : Name);
        auto* Card = WidgetTree->ConstructWidget<UVerticalBox>();
        auto* Img = WidgetTree->ConstructWidget<UImage>();
        Img->SetDesiredSizeOverride(FVector2D(72));
        const FString Key = TEXT("lib:") + Path;
        if (UTexture2D* T = Thumbs.FindRef(Key)) Img->SetBrushFromTexture(T);
        else
        {
            Img->SetColorAndOpacity(FLinearColor(1, 1, 1, .06f));
            if (!Preview.IsEmpty() && !Base.IsEmpty()) LoadImage(Base + TEXT("/library/") + Preview, Key);
        }
        ThumbImages.Add(Key, Img);
        Card->AddChildToVerticalBox(Img)->SetHorizontalAlignment(HAlign_Center);
        auto* Text = Label(Name.Left(22), 9);
        Text->SetAutoWrapText(false);
        Card->AddChildToVerticalBox(Text)->SetHorizontalAlignment(HAlign_Center);
        auto* Width = WidgetTree->ConstructWidget<USizeBox>();
        Width->SetWidthOverride(96);
        Width->SetContent(Card);
        auto* Button = WidgetTree->ConstructWidget<UButton>();
        Button->SetStyle(FEidoverseTheme::Button(FEidoverseTheme::Well(), FEidoverseTheme::Hover(), 8, FEidoverseTheme::Edge()));
        Button->SetContent(Width);
        Button->SetToolTipText(FText::FromString(TEXT("place ") + Name + TEXT(" in front of you")));
        Button->OnClicked.AddDynamic(this, &UEidoverseHUD::OnBuildClicked);
        ButtonIds.Add(Button, FName(*(TEXT("build:spawn:") + Path)));
        BuildResults->AddChildToWrapBox(Button);
    }
    if (BuildResults->GetChildrenCount() == 0) BuildResults->AddChildToWrapBox(Label(TEXT("nothing matched"), 10, FEidoverseTheme::Dim()));
}

void UEidoverseHUD::SearchLibrary(const FString& Query)
{
    LastQuery = Query.TrimStartAndEnd();
    bSearchPending = false;
    if (LastQuery.IsEmpty())
    {
        // An empty box shows the curated starters (web palette.js).
        TArray<TSharedPtr<FJsonValue>> Items;
        for (const auto& S : Starter)
        {
            auto O = MakeShared<FJsonObject>();
            O->SetStringField(TEXT("name"), S[0]);
            O->SetStringField(TEXT("path"), S[1]);
            O->SetStringField(TEXT("preview"), FString(S[1]).Replace(TEXT(".glb"), TEXT("_preview.jpg")));
            Items.Add(MakeShared<FJsonValueObject>(O));
        }
        PaintLibrary(Items);
        return;
    }
    const FString Base = HttpBase();
    if (Base.IsEmpty()) return;
    auto Request = FHttpModule::Get().CreateRequest();
    Request->SetURL(Base + TEXT("/library-models?q=") + FGenericPlatformHttp::UrlEncode(LastQuery));
    Request->SetVerb(TEXT("GET"));
    Request->SetTimeout(10);
    TWeakObjectPtr<UEidoverseHUD> Weak(this);
    const FString Asked = LastQuery;
    Request->OnProcessRequestComplete().BindLambda([Weak, Asked](FHttpRequestPtr, FHttpResponsePtr Response, bool bOk)
    {
        UEidoverseHUD* Self = Weak.Get();
        TArray<TSharedPtr<FJsonValue>> Items;
        if (!Self || Self->LastQuery != Asked || !bOk || !Response || Response->GetResponseCode() != 200 ||
            !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Response->GetContentAsString()), Items)) return;
        if (Items.Num() > 90) Items.SetNum(90);
        Self->PaintLibrary(Items);
    });
    Request->ProcessRequest();
}

void UEidoverseHUD::OnBuildSearchChanged(const FText& Text)
{
    PendingQuery = Text.ToString();
    SearchAt = FPlatformTime::Seconds() + .3;  // debounce typing
    bSearchPending = true;
}

void UEidoverseHUD::OnBuildClicked() { BuildAction(ResolveClicked().ToString()); }

FString UEidoverseHUD::GetSelectedEntity() const
{
    const AEidoverseScene* Scene = Bridge ? Bridge->GetScene() : nullptr;
    return Scene ? Scene->GetSelected() : FString();
}

void UEidoverseHUD::SelectEntity(const FString& Id)
{
    if (AEidoverseScene* Scene = Bridge ? Bridge->GetScene() : nullptr) Scene->SetSelected(Id);
    RefreshBuild();
}

bool UEidoverseHUD::PlaceSelected(TFunctionRef<void(FVector& Pos, double& Yaw, double& Scale)> Edit)
{
    AEidoverseScene* Scene = Bridge ? Bridge->GetScene() : nullptr;
    const FString Id = GetSelectedEntity();
    FVector Pos; double Yaw, Scale; FString Lib; bool bAnimated;
    if (!Scene || Id.IsEmpty() || !Scene->GetEntityPose(Id, Pos, Yaw, Scale, Lib, bAnimated)) return false;
    if (bAnimated) { Toast(TEXT("Moving or mounted things can't be placed from here yet."), true); return false; }
    auto Before = MakeShared<FJsonObject>();
    Before->SetStringField(TEXT("id"), Id); Before->SetArrayField(TEXT("pos"), Vec3(Pos));
    Before->SetNumberField(TEXT("yaw"), Yaw); Before->SetNumberField(TEXT("scale"), Scale);
    Edit(Pos, Yaw, Scale);
    auto Args = MakeShared<FJsonObject>();
    Args->SetStringField(TEXT("id"), Id); Args->SetArrayField(TEXT("pos"), Vec3(Pos));
    Args->SetNumberField(TEXT("yaw"), FMath::RoundToDouble(Yaw * 10000) / 10000); Args->SetNumberField(TEXT("scale"), FMath::RoundToDouble(Scale * 1000) / 1000);
    if (!Bridge->SendVerb(TEXT("place"), Args)) return false;
    UndoStack.Add({TEXT("place"), ToJson(Before)});
    if (UndoStack.Num() > 40) UndoStack.RemoveAt(0);
    return true;
}

void UEidoverseHUD::BuildAction(const FString& Action)
{
    if (!Bridge) return;
    if (!Bridge->CanBuild()) { Toast(TEXT("You can't build in this world."), true); return; }
    AEidoverseScene* Scene = Bridge->GetScene();
    if (Action.StartsWith(TEXT("build:spawn:")))
    {
        const FString Lib = Action.Mid(12);
        APlayerController* PC = GetOwningPlayer();
        APawn* Pawn = PC ? PC->GetPawn() : nullptr;
        if (!Pawn || !Scene) { Toast(TEXT("Join a world first."), true); return; }
        // On the ground 4 m ahead, facing you (web ghost default: in front of the camera).
        const FRotator Yaw(0, PC->GetControlRotation().Yaw, 0);
        const FVector Ahead = Pawn->GetActorLocation() + Yaw.Vector() * 400;
        FHitResult Hit;
        FCollisionQueryParams Query(SCENE_QUERY_STAT(EidoverseBuildSpawn), true, Pawn);
        const FVector Ground = GetWorld()->LineTraceSingleByChannel(Hit, Ahead + FVector(0, 0, 300), Ahead - FVector(0, 0, 5000), ECC_Visibility, Query) ? Hit.ImpactPoint : Ahead;
        const FVector Wire = AEidoverseBridge::ToWirePosition(Ground - Scene->GetSceneOrigin());
        const FVector Me = AEidoverseBridge::ToWirePosition(Pawn->GetActorLocation() - Scene->GetSceneOrigin());
        auto Args = MakeShared<FJsonObject>();
        const FString Id = NewEntityId();
        Args->SetStringField(TEXT("id"), Id);
        Args->SetStringField(TEXT("lib"), Lib);
        Args->SetArrayField(TEXT("pos"), Vec3(Wire));
        Args->SetNumberField(TEXT("yaw"), FMath::RoundToDouble(FMath::Atan2(Me.X - Wire.X, Me.Z - Wire.Z) * 10000) / 10000);
        if (!Bridge->SendVerb(TEXT("spawn"), Args)) return;
        LastSpawned = Id;
        auto Undo = MakeShared<FJsonObject>(); Undo->SetStringField(TEXT("id"), Id);
        UndoStack.Add({TEXT("remove"), ToJson(Undo)});
        Hint(TEXT("placed ") + LibNames.FindRef(Lib), 2);
        return;
    }
    if (Action == TEXT("build:undo"))
    {
        if (UndoStack.IsEmpty()) { Hint(TEXT("nothing to undo"), 1.5f); return; }
        const auto Last = UndoStack.Pop();
        Bridge->SendVerbJson(Last.Key, Last.Value);
        Hint(TEXT("undo ") + Last.Key, 1.5f);
        return;
    }
    const FString Id = GetSelectedEntity();
    if (Id.IsEmpty()) { Hint(TEXT("select something first (click it with the pointer)"), 2); return; }
    if (Action.StartsWith(TEXT("build:turn:")))
    {
        const double Degrees = FCString::Atod(*Action.Mid(11));
        PlaceSelected([Degrees](FVector&, double& Yaw, double&) { Yaw = FMath::UnwindRadians(Yaw + FMath::DegreesToRadians(Degrees)); });
    }
    else if (Action.StartsWith(TEXT("build:raise:")))
    {
        const double Dy = FCString::Atod(*Action.Mid(12));
        PlaceSelected([Dy](FVector& Pos, double&, double&) { Pos.Y += Dy; });
    }
    else if (Action.StartsWith(TEXT("build:size:")))
    {
        const double K = FCString::Atod(*Action.Mid(11));
        PlaceSelected([K](FVector&, double&, double& Scale) { Scale = FMath::Clamp(Scale * K, .05, 50.0); });
    }
    else if (Action.StartsWith(TEXT("build:nudge:")))
    {
        // Relative to the view: "forward", "back", "left", "right" by 0.5 m (Shift: 0.1 m).
        TArray<FString> Parts;
        Action.Mid(12).ParseIntoArray(Parts, TEXT(":"));
        const double Step = Parts.Num() > 1 ? FCString::Atod(*Parts[1]) : .5;
        APlayerController* PC = GetOwningPlayer();
        const FRotator View(0, PC ? PC->GetControlRotation().Yaw : 0, 0);
        FVector Dir = Parts[0] == TEXT("forward") ? View.Vector() : Parts[0] == TEXT("back") ? -View.Vector()
            : Parts[0] == TEXT("right") ? FRotationMatrix(View).GetUnitAxis(EAxis::Y) : -FRotationMatrix(View).GetUnitAxis(EAxis::Y);
        const FVector WireDir = AEidoverseBridge::ToWirePosition(Dir * 100);
        PlaceSelected([&](FVector& Pos, double&, double&) { Pos.X += WireDir.X * Step; Pos.Z += WireDir.Z * Step; });
    }
    else if (Action == TEXT("build:remove") && Scene)
    {
        FVector Pos; double Yaw, Scale; FString Lib; bool bAnimated;
        if (!Scene->GetEntityPose(Id, Pos, Yaw, Scale, Lib, bAnimated)) return;
        auto Args = MakeShared<FJsonObject>(); Args->SetStringField(TEXT("id"), Id);
        if (!Bridge->SendVerb(TEXT("remove"), Args)) return;
        if (!Lib.IsEmpty())
        {
            auto Undo = MakeShared<FJsonObject>();
            Undo->SetStringField(TEXT("id"), Id); Undo->SetStringField(TEXT("lib"), Lib);
            Undo->SetArrayField(TEXT("pos"), Vec3(Pos)); Undo->SetNumberField(TEXT("yaw"), Yaw); Undo->SetNumberField(TEXT("scale"), Scale);
            UndoStack.Add({TEXT("spawn"), ToJson(Undo)});
        }
        Scene->SetSelected(FString());
    }
    RefreshBuild();
}

void UEidoverseHUD::RefreshBuild()
{
    if (!Bridge) return;
    if (BuildNote) BuildNote->SetText(FText::FromString(!Bridge->bJoined ? TEXT("Join a world to build.")
        : Bridge->CanBuild() ? FString() : TEXT("You can look, but this world's owner hasn't made you a builder.")));
    AEidoverseScene* Scene = Bridge->GetScene();
    const FString Id = GetSelectedEntity();
    FVector Pos; double Yaw = 0, Scale = 1; FString Lib; bool bAnimated = false;
    if (BuildSelected)
    {
        if (!Id.IsEmpty() && Scene && Scene->GetEntityPose(Id, Pos, Yaw, Scale, Lib, bAnimated))
        {
            const FString Name = LibNames.Contains(Lib) ? LibNames[Lib] : FPaths::GetBaseFilename(Lib);
            BuildSelected->SetText(FText::FromString(FString::Printf(TEXT("%s  ·  %s\n(%.2f, %.2f, %.2f)  yaw %.0f°  scale %.2f%s"), *Name, *Id,
                Pos.X, Pos.Y, Pos.Z, FMath::RadiansToDegrees(Yaw), Scale, bAnimated ? TEXT("  ·  moving") : TEXT(""))));
        }
        else BuildSelected->SetText(FText::FromString(TEXT("nothing — click a thing in the world")));
    }
}

void UEidoverseHUD::TickBuild(double Now)
{
    if (bSearchPending && Now >= SearchAt) SearchLibrary(PendingQuery);
    if (!IsFrameOpen("build") || !Bridge || !Bridge->bJoined) return;
    RefreshBuild();
    APlayerController* PC = GetOwningPlayer();
    AEidoverseScene* Scene = Bridge->GetScene();
    if (!PC || !Scene) return;
    // Selection lost (removed by anyone): clear it.
    FVector P; double Y, S; FString L; bool bA;
    if (!GetSelectedEntity().IsEmpty() && !Scene->GetEntityPose(GetSelectedEntity(), P, Y, S, L, bA)) Scene->SetSelected(FString());
    // Keys and clicks belong to text fields while one is focused (chat, search, ...).
    const TSharedPtr<SWidget> Focused = FSlateApplication::Get().GetKeyboardFocusedWidget();
    if (Focused.IsValid() && (Focused->GetType() == TEXT("SEditableText") || Focused->GetType() == TEXT("SMultiLineEditableText"))) return;
    // Click in the world with the pointer to select (clicks on panels never reach the game).
    if (bPointerMode && PC->WasInputKeyJustPressed(EKeys::LeftMouseButton))
    {
        FHitResult Hit;
        if (PC->GetHitResultUnderCursor(ECC_Visibility, true, Hit) && Hit.GetActor()) SelectEntity(Scene->EntityId(Hit.GetActor()));
        else SelectEntity(FString());
    }
    const bool bShift = PC->IsInputKeyDown(EKeys::LeftShift) || PC->IsInputKeyDown(EKeys::RightShift);
    const bool bCtrl = PC->IsInputKeyDown(EKeys::LeftControl) || PC->IsInputKeyDown(EKeys::RightControl) || PC->IsInputKeyDown(EKeys::LeftCommand);
    if (bCtrl && PC->WasInputKeyJustPressed(EKeys::Z)) { BuildAction(TEXT("build:undo")); return; }
    if (GetSelectedEntity().IsEmpty()) return;
    // Web build.js keys (Q/E only steer while something is selected).
    if (PC->WasInputKeyJustPressed(EKeys::Q)) BuildAction(bShift ? TEXT("build:turn:1.15") : TEXT("build:turn:15"));
    if (PC->WasInputKeyJustPressed(EKeys::E)) BuildAction(bShift ? TEXT("build:turn:-1.15") : TEXT("build:turn:-15"));
    if (PC->WasInputKeyJustPressed(EKeys::R)) BuildAction(bShift ? TEXT("build:raise:0.05") : TEXT("build:raise:0.25"));
    if (PC->WasInputKeyJustPressed(EKeys::F)) BuildAction(bShift ? TEXT("build:raise:-0.05") : TEXT("build:raise:-0.25"));
    if (PC->WasInputKeyJustPressed(EKeys::Comma)) BuildAction(TEXT("build:size:0.92"));
    if (PC->WasInputKeyJustPressed(EKeys::Period)) BuildAction(TEXT("build:size:1.087"));
    if (PC->WasInputKeyJustPressed(EKeys::Delete) || PC->WasInputKeyJustPressed(EKeys::BackSpace)) BuildAction(TEXT("build:remove"));
    const FString Step = bShift ? TEXT(":0.1") : TEXT(":0.5");
    if (PC->WasInputKeyJustPressed(EKeys::Up)) BuildAction(TEXT("build:nudge:forward") + Step);
    if (PC->WasInputKeyJustPressed(EKeys::Down)) BuildAction(TEXT("build:nudge:back") + Step);
    if (PC->WasInputKeyJustPressed(EKeys::Left)) BuildAction(TEXT("build:nudge:left") + Step);
    if (PC->WasInputKeyJustPressed(EKeys::Right)) BuildAction(TEXT("build:nudge:right") + Step);
}
