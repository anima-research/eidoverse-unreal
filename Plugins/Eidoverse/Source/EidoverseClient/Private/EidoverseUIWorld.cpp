// World frame sections: here / avatar / sky / ground / scene.
// Wire behaviour mirrors the web panels (client/lib/skypanel.js, groundpanel.js,
// scenegraph.js, mybody.js). The UI only enables what the server would accept
// (yourRights); the server remains the authority and refuses with 'error'.
#include "EidoverseUI.h"
#include "EidoverseBridge.h"
#include "EidoverseScene.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Image.h"
#include "Components/ScrollBox.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/WidgetSwitcher.h"
#include "Components/WrapBox.h"
#include "Dom/JsonObject.h"
#include "Engine/Texture2D.h"
#include "HttpModule.h"
#include "GenericPlatform/GenericPlatformHttp.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Interfaces/IHttpResponse.h"
#include "Modules/ModuleManager.h"

namespace
{
    const TCHAR* WorldTabNames[] = {TEXT("here"), TEXT("avatar"), TEXT("sky"), TEXT("ground"), TEXT("scene")};
    const TCHAR* Clouds[] = {TEXT("clear"), TEXT("cumulus"), TEXT("stratus"), TEXT("cirrus")};
    const TCHAR* Weathers[] = {TEXT("clear"), TEXT("fair"), TEXT("sunshower"), TEXT("overcast"), TEXT("rain"), TEXT("storm")};

    TSharedPtr<FJsonObject> Copy(const TSharedPtr<FJsonObject>& O)
    {
        auto C = MakeShared<FJsonObject>();
        if (O) C->Values = O->Values;
        return C;
    }
    TSharedPtr<FJsonObject> Sub(const TSharedPtr<FJsonObject>& O, const TCHAR* Key)
    {
        const TSharedPtr<FJsonObject>* V = nullptr;
        return O && O->TryGetObjectField(Key, V) ? *V : nullptr;
    }
    FString Leaf(const FString& Path)
    {
        FString Name = Path;
        int32 Slash;
        if (Name.FindLastChar('/', Slash)) Name = Name.Mid(Slash + 1);
        int32 Query;
        if (Name.FindChar('?', Query)) Name = Name.Left(Query);
        return Name;
    }
}

UButton* UEidoverseHUD::ActionButton(const FString& Text, const FString& Id, bool bPrimary)
{
    UButton* B = TextButton(Text, bPrimary);
    B->OnClicked.AddDynamic(this, &UEidoverseHUD::OnWorldClicked);
    ButtonIds.Add(B, FName(*Id));
    return B;
}

UWidget* UEidoverseHUD::BuildWorld()
{
    auto* Column = WidgetTree->ConstructWidget<UVerticalBox>();
    auto* Tabs = WidgetTree->ConstructWidget<UWrapBox>();
    Tabs->SetInnerSlotPadding(FVector2D(4, 4));
    for (int32 I = 0; I < UE_ARRAY_COUNT(WorldTabNames); ++I)
    {
        UButton* B = ActionButton(WorldTabNames[I], FString::Printf(TEXT("wtab:%d"), I));
        WorldTabs.Add(B);
        Tabs->AddChildToWrapBox(B);
    }
    Column->AddChildToVerticalBox(Tabs)->SetPadding(FMargin(0, 0, 0, 8));
    WorldPages = WidgetTree->ConstructWidget<UWidgetSwitcher>();
    WorldPages->AddChild(BuildWorldHere());
    WorldPages->AddChild(BuildWorldAvatar());
    WorldPages->AddChild(BuildWorldSky());
    WorldPages->AddChild(BuildWorldGround());
    WorldPages->AddChild(BuildWorldScene());
    Column->AddChildToVerticalBox(WorldPages)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
    return Column;
}

UWidget* UEidoverseHUD::BuildWorldAvatar()
{
    auto* Scroll = WidgetTree->ConstructWidget<UScrollBox>();
    auto* Column = WidgetTree->ConstructWidget<UVerticalBox>();
    Scroll->AddChild(Column);
    AvatarNote = Label(TEXT("Join a world to choose a body."), 10, FEidoverseTheme::Dim());
    Column->AddChildToVerticalBox(AvatarNote)->SetPadding(FMargin(0, 0, 0, 8));
    AvatarGrid = WidgetTree->ConstructWidget<UWrapBox>();
    AvatarGrid->SetInnerSlotPadding(FVector2D(6, 6));
    Column->AddChildToVerticalBox(AvatarGrid);
    return Scroll;
}

UWidget* UEidoverseHUD::BuildWorldSky()
{
    auto* Scroll = WidgetTree->ConstructWidget<UScrollBox>();
    auto* Column = WidgetTree->ConstructWidget<UVerticalBox>();
    Scroll->AddChild(Column);
    SkyNote = Label(TEXT(""), 10, FEidoverseTheme::Attention());
    Column->AddChildToVerticalBox(SkyNote)->SetPadding(FMargin(0, 0, 0, 6));
    SkyState = Label(TEXT(""), 11, FEidoverseTheme::Brand());
    Column->AddChildToVerticalBox(SkyState)->SetPadding(FMargin(0, 0, 0, 8));
    auto Section = [&](const TCHAR* Text) { Column->AddChildToVerticalBox(Label(FString(Text).ToUpper(), 9, FEidoverseTheme::Dim(), true))->SetPadding(FMargin(0, 8, 0, 4)); };
    auto Wrap = [&]() { auto* W = WidgetTree->ConstructWidget<UWrapBox>(); W->SetInnerSlotPadding(FVector2D(4, 4)); Column->AddChildToVerticalBox(W); return W; };
    Section(TEXT("Presets"));
    SkyPresets = Wrap();
    Section(TEXT("Time of day"));
    UWrapBox* Hours = Wrap();
    for (const TCHAR* Step : {TEXT("-3h"), TEXT("-1h"), TEXT("+1h"), TEXT("+3h")}) Hours->AddChildToWrapBox(ActionButton(Step, FString(TEXT("sky:h")) + Step));
    Section(TEXT("Clouds"));
    UWrapBox* CloudRow = Wrap();
    for (const TCHAR* C : Clouds) CloudRow->AddChildToWrapBox(ActionButton(C, FString(TEXT("sky:clouds:")) + C));
    Section(TEXT("Weather"));
    UWrapBox* WeatherRow = Wrap();
    for (const TCHAR* W : Weathers) WeatherRow->AddChildToWrapBox(ActionButton(W, FString(TEXT("sky:weather:")) + W));
    UWrapBox* Commit = Wrap();
    UButton* Apply = ActionButton(TEXT("apply to world"), TEXT("sky:apply"), true);
    OwnerButtons.Add(Apply);
    Commit->AddChildToWrapBox(Apply);
    Commit->AddChildToWrapBox(ActionButton(TEXT("revert"), TEXT("sky:revert")));
    return Scroll;
}

UWidget* UEidoverseHUD::BuildWorldGround()
{
    auto* Scroll = WidgetTree->ConstructWidget<UScrollBox>();
    auto* Column = WidgetTree->ConstructWidget<UVerticalBox>();
    Scroll->AddChild(Column);
    GroundNote = Label(TEXT(""), 10, FEidoverseTheme::Attention());
    Column->AddChildToVerticalBox(GroundNote)->SetPadding(FMargin(0, 0, 0, 6));
    GroundState = Label(TEXT(""), 11, FEidoverseTheme::Brand());
    Column->AddChildToVerticalBox(GroundState)->SetPadding(FMargin(0, 0, 0, 6));
    auto Section = [&](const TCHAR* Text) { Column->AddChildToVerticalBox(Label(FString(Text).ToUpper(), 9, FEidoverseTheme::Dim(), true))->SetPadding(FMargin(0, 8, 0, 4)); };
    auto Wrap = [&]() { auto* W = WidgetTree->ConstructWidget<UWrapBox>(); W->SetInnerSlotPadding(FVector2D(4, 4)); Column->AddChildToVerticalBox(W); return W; };
    Section(TEXT("Shape"));
    GroundShapes = Wrap();
    Section(TEXT("Tint"));
    GroundTints = Wrap();
    UWrapBox* Terrain = Wrap();
    UButton* Apply = ActionButton(TEXT("apply terrain"), TEXT("ground:apply"), true);
    UButton* Shuffle = ActionButton(TEXT("reshuffle"), TEXT("ground:reshuffle"));
    OwnerButtons.Add(Apply);
    OwnerButtons.Add(Shuffle);
    Terrain->AddChildToWrapBox(Apply);
    Terrain->AddChildToWrapBox(Shuffle);
    Section(TEXT("Grass height"));
    UWrapBox* Heights = Wrap();
    for (const TCHAR* H : {TEXT("lawn"), TEXT("meadow"), TEXT("tall")}) Heights->AddChildToWrapBox(ActionButton(H, FString(TEXT("ground:height:")) + H));
    Section(TEXT("Grass density"));
    UWrapBox* Density = Wrap();
    for (const TCHAR* D : {TEXT("sparse"), TEXT("normal"), TEXT("lush")}) Density->AddChildToWrapBox(ActionButton(D, FString(TEXT("ground:density:")) + D));
    Section(TEXT("Plant"));
    GroundPlantings = Wrap();
    UWrapBox* Mow = Wrap();
    UButton* MowButton = ActionButton(TEXT("mow"), TEXT("ground:mow"));
    OwnerButtons.Add(MowButton);
    Mow->AddChildToWrapBox(MowButton);
    return Scroll;
}

UWidget* UEidoverseHUD::BuildWorldScene()
{
    auto* Column = WidgetTree->ConstructWidget<UVerticalBox>();
    SceneNote = Label(TEXT(""), 10, FEidoverseTheme::Dim());
    Column->AddChildToVerticalBox(SceneNote)->SetPadding(FMargin(0, 0, 0, 6));
    auto* Scroll = WidgetTree->ConstructWidget<UScrollBox>();
    SceneList = WidgetTree->ConstructWidget<UVerticalBox>();
    Scroll->AddChild(SceneList);
    Column->AddChildToVerticalBox(Scroll)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
    return Column;
}

void UEidoverseHUD::LoadThumb(const FString& Name)
{
    const FString Base = HttpBase();
    if (Base.IsEmpty() || ThumbRequested.Contains(Name)) return;
    ThumbRequested.Add(Name);
    auto Request = FHttpModule::Get().CreateRequest();
    Request->SetURL(Base + TEXT("/thumb/") + FGenericPlatformHttp::UrlEncode(Name) + TEXT(".png"));
    Request->SetVerb(TEXT("GET"));
    Request->SetTimeout(10);
    TWeakObjectPtr<UEidoverseHUD> Weak(this);
    Request->OnProcessRequestComplete().BindLambda([Weak, Name](FHttpRequestPtr, FHttpResponsePtr Response, bool bOk)
    {
        UEidoverseHUD* Self = Weak.Get();
        // No thumbnail yet is normal (the first viewer contributes it on the web).
        if (!Self || !bOk || !Response || Response->GetResponseCode() != 200 || Response->GetContent().Num() > 512 * 1024) return;
        IImageWrapperModule& Images = FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
        TSharedPtr<IImageWrapper> Png = Images.CreateImageWrapper(EImageFormat::PNG);
        TArray<uint8> Pixels;
        if (!Png || !Png->SetCompressed(Response->GetContent().GetData(), Response->GetContent().Num()) || Png->GetWidth() > 1024 || Png->GetHeight() > 1024 ||
            !Png->GetRaw(ERGBFormat::BGRA, 8, Pixels)) return;
        UTexture2D* T = UTexture2D::CreateTransient(Png->GetWidth(), Png->GetHeight(), PF_B8G8R8A8);
        if (!T) return;
        void* Data = T->GetPlatformData()->Mips[0].BulkData.Lock(LOCK_READ_WRITE);
        FMemory::Memcpy(Data, Pixels.GetData(), Pixels.Num());
        T->GetPlatformData()->Mips[0].BulkData.Unlock();
        T->UpdateResource();
        Self->Thumbs.Add(Name, T);
        if (UImage* Img = Self->ThumbImages.FindRef(Name).Get()) { Img->SetBrushFromTexture(T); Img->SetDesiredSizeOverride(FVector2D(64)); }
    });
    Request->ProcessRequest();
}

void UEidoverseHUD::RefreshWorldPanels()
{
    if (!Bridge || !WorldPages) return;
    WorldPages->SetActiveWidgetIndex(WorldTab);
    for (int32 I = 0; I < WorldTabs.Num(); ++I)
        WorldTabs[I]->SetStyle(FEidoverseTheme::Button(I == WorldTab ? FEidoverseTheme::Brand().CopyWithNewOpacity(.16f) : FEidoverseTheme::Well(),
            FEidoverseTheme::Hover(), 6, I == WorldTab ? FEidoverseTheme::Brand().CopyWithNewOpacity(.5f) : FEidoverseTheme::Edge()));
    const bool bOwner = Bridge->IsOwner(), bBuild = Bridge->CanBuild();
    for (const auto& B : OwnerButtons) if (B) B->SetIsEnabled(bOwner);

    // Avatar: the server's roster, thumbnails when someone has contributed them.
    int32 Hash = Bridge->AvatarChoices.Num();
    for (const auto& C : Bridge->AvatarChoices) Hash = HashCombine(Hash, GetTypeHash(C.Path));
    Hash = HashCombine(Hash, GetTypeHash(Bridge->AvatarPath));
    if (AvatarGrid && Hash != AvatarHash)
    {
        AvatarHash = Hash;
        AvatarGrid->ClearChildren();
        ThumbImages.Reset();
        for (const auto& C : Bridge->AvatarChoices)
        {
            const bool bWorn = !Bridge->AvatarPath.IsEmpty() && Leaf(C.Path) == Leaf(Bridge->AvatarPath);
            auto* Card = WidgetTree->ConstructWidget<UVerticalBox>();
            auto* Img = WidgetTree->ConstructWidget<UImage>();
            Img->SetDesiredSizeOverride(FVector2D(64));
            if (UTexture2D* T = Thumbs.FindRef(C.Name)) Img->SetBrushFromTexture(T);
            else { Img->SetColorAndOpacity(FLinearColor(1, 1, 1, .06f)); LoadThumb(C.Name); }
            ThumbImages.Add(C.Name, Img);
            Card->AddChildToVerticalBox(Img)->SetHorizontalAlignment(HAlign_Center);
            auto* Name = Label(C.Name, 10, bWorn ? FEidoverseTheme::Brand() : FEidoverseTheme::Text(), bWorn);
            Name->SetAutoWrapText(false);
            Card->AddChildToVerticalBox(Name)->SetHorizontalAlignment(HAlign_Center);
            auto* Button = WidgetTree->ConstructWidget<UButton>();
            Button->SetStyle(FEidoverseTheme::Button(bWorn ? FEidoverseTheme::Brand().CopyWithNewOpacity(.12f) : FEidoverseTheme::Well(), FEidoverseTheme::Hover(), 8,
                bWorn ? FEidoverseTheme::Brand().CopyWithNewOpacity(.6f) : FEidoverseTheme::Edge()));
            auto* Pad = WidgetTree->ConstructWidget<USizeBox>();
            Pad->SetWidthOverride(92);
            Pad->SetContent(Card);
            Button->SetContent(Pad);
            Button->SetToolTipText(FText::FromString(bWorn ? TEXT("you are wearing this") : TEXT("wear ") + C.Name));
            Button->OnClicked.AddDynamic(this, &UEidoverseHUD::OnWorldClicked);
            ButtonIds.Add(Button, FName(*(TEXT("avatar:") + C.Path)));
            AvatarGrid->AddChildToWrapBox(Button);
        }
        AvatarNote->SetText(FText::FromString(!Bridge->bJoined ? TEXT("Join a world to choose a body.")
            : Bridge->AvatarChoices.IsEmpty() ? TEXT("This server offers no avatars.") : TEXT("Others see you in the body you choose.")));
    }

    // Sky: a draft over the folded sky; the fold replaces it wholesale on apply.
    if (Bridge->WorldSky != SkySource) { SkySource = Bridge->WorldSky; if (!bSkyDirty) SkyDraft = Copy(SkySource); }
    if (!SkyDraft) SkyDraft = Copy(SkySource);
    if (SkyPresets && SkyPresets->GetChildrenCount() == 0 && Defs)
        if (const TSharedPtr<FJsonObject> Presets = Sub(Defs, TEXT("skyPresets")))
            for (const auto& P : Presets->Values) SkyPresets->AddChildToWrapBox(ActionButton(FString(P.Key), TEXT("sky:preset:") + FString(P.Key)));
    double Hours = 12;
    SkyDraft->TryGetNumberField(TEXT("hours"), Hours);
    FString CloudState = TEXT("clear"), WeatherState = TEXT("clear");
    SkyDraft->TryGetStringField(TEXT("clouds"), CloudState);
    SkyDraft->TryGetStringField(TEXT("weather"), WeatherState);
    if (SkyState) SkyState->SetText(FText::FromString(FString::Printf(TEXT("%02d:%02d  ·  %s clouds  ·  %s%s"), int32(Hours), int32(FMath::Fmod(Hours, 1.0) * 60),
        *CloudState, *WeatherState, bSkyDirty ? TEXT("  ·  not applied") : TEXT(""))));
    if (SkyNote) SkyNote->SetText(FText::FromString(bOwner ? FString() : TEXT("Only this world's owner can change the sky. You can still preview the values here.")));

    // Ground: palette from /defs (groundPalette), exact web terrain/grass args.
    if (const TSharedPtr<FJsonObject> Palette = Sub(Defs, TEXT("groundPalette")))
    {
        const int32 DefsHash = GetTypeHash(Palette->Values.Num());
        if (DefsHash != GroundDefsHash && GroundShapes)
        {
            GroundDefsHash = DefsHash;
            GroundShapes->ClearChildren(); GroundTints->ClearChildren(); GroundPlantings->ClearChildren();
            if (auto Shapes = Sub(Palette, TEXT("shapes"))) for (const auto& P : Shapes->Values) GroundShapes->AddChildToWrapBox(ActionButton(FString(P.Key), TEXT("ground:shape:") + FString(P.Key)));
            if (auto Tints = Sub(Palette, TEXT("tints"))) for (const auto& P : Tints->Values) GroundTints->AddChildToWrapBox(ActionButton(FString(P.Key), TEXT("ground:tint:") + FString(P.Key)));
            if (auto Plantings = Sub(Palette, TEXT("plantings")))
                for (const auto& P : Plantings->Values)
                {
                    UButton* B = ActionButton(FString(P.Key), TEXT("ground:plant:") + FString(P.Key));
                    OwnerButtons.Add(B);
                    GroundPlantings->AddChildToWrapBox(B);
                }
        }
    }
    if (GroundState) GroundState->SetText(FText::FromString(FString::Printf(TEXT("%s · %s · seed %d  ·  grass %s, %s"), *GroundShape, *GroundTint, GroundSeed, *GrassHeight, *GrassDensity)));
    if (GroundNote) GroundNote->SetText(FText::FromString(bOwner ? FString() : TEXT("Only this world's owner can reshape the ground.")));

    // Scene: every projected entity; builders may remove (server checks lock/guard).
    const double Now = FPlatformTime::Seconds();
    if (SceneList && WorldTab == 4 && Now >= NextSceneRefresh)
    {
        NextSceneRefresh = Now + 1;
        TArray<TPair<FString, FString>> Entities;
        if (AEidoverseScene* Scene = Bridge->GetScene()) Scene->GetEntities(Entities);
        int32 SHash = GetTypeHash(bBuild);
        for (const auto& E : Entities) SHash = HashCombine(SHash, GetTypeHash(E.Key));
        if (SHash != SceneHash)
        {
            SceneHash = SHash;
            SceneList->ClearChildren();
            for (const auto& E : Entities)
            {
                auto* Row = WidgetTree->ConstructWidget<UHorizontalBox>();
                auto* Name = Label(Leaf(E.Value).IsEmpty() ? E.Key : Leaf(E.Value), 11);
                Name->SetAutoWrapText(false);
                Row->AddChildToHorizontalBox(Name)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
                auto* Id = Label(E.Key, 9, FEidoverseTheme::Dim());
                Id->SetAutoWrapText(false);
                Row->AddChildToHorizontalBox(Id)->SetPadding(FMargin(6, 2, 6, 0));
                if (bBuild) Row->AddChildToHorizontalBox(ActionButton(TEXT("remove"), TEXT("scene:remove:") + E.Key));
                SceneList->AddChildToVerticalBox(Row)->SetPadding(FMargin(0, 2));
            }
            SceneNote->SetText(FText::FromString(!Bridge->bJoined ? TEXT("Join a world to see its contents.")
                : FString::Printf(TEXT("%d things here%s"), Entities.Num(), bBuild ? TEXT("") : TEXT("  ·  view only"))));
        }
    }
}

void UEidoverseHUD::SelectWorldTab(int32 Tab)
{
    ShowFrame("world", true);
    WorldAction(FString::Printf(TEXT("wtab:%d"), Tab));
}

void UEidoverseHUD::OnWorldClicked()
{
    WorldAction(ResolveClicked().ToString());
}

void UEidoverseHUD::WorldAction(const FString& Action)
{
    const FString Id = Action;
    if (!Bridge || Id.IsEmpty()) return;
    auto Refused = [this](const TCHAR* What) { Toast(FString::Printf(TEXT("Not sent: %s."), What), true); };
    if (Id.StartsWith(TEXT("wtab:"))) { WorldTab = FMath::Clamp(FCString::Atoi(*Id.Mid(5)), 0, 4); NextSceneRefresh = 0; SceneHash = 0; }
    else if (Id.StartsWith(TEXT("avatar:")))
    {
        if (Bridge->ChangeAvatar(Id.Mid(7))) Toast(TEXT("Changing body…"));
        else Refused(TEXT("join a world first"));
    }
    else if (Id.StartsWith(TEXT("sky:")))
    {
        if (!SkyDraft) SkyDraft = Copy(SkySource);
        if (Id.StartsWith(TEXT("sky:preset:")))
        {
            if (auto Preset = Sub(Sub(Defs, TEXT("skyPresets")), *Id.Mid(11))) for (const auto& P : Preset->Values) SkyDraft->Values.Add(P.Key, P.Value);
            bSkyDirty = true;
        }
        else if (Id.StartsWith(TEXT("sky:h")))
        {
            double Hours = 12;
            SkyDraft->TryGetNumberField(TEXT("hours"), Hours);
            SkyDraft->SetNumberField(TEXT("hours"), FMath::Fmod(Hours + FCString::Atof(*Id.Mid(5)) + 24, 24.0));
            bSkyDirty = true;
        }
        else if (Id.StartsWith(TEXT("sky:clouds:"))) { SkyDraft->SetStringField(TEXT("clouds"), Id.Mid(11)); bSkyDirty = true; }
        else if (Id.StartsWith(TEXT("sky:weather:"))) { SkyDraft->SetStringField(TEXT("weather"), Id.Mid(12)); SkyDraft->SetNumberField(TEXT("weatherSeconds"), 12); bSkyDirty = true; }
        else if (Id == TEXT("sky:revert")) { SkyDraft = Copy(SkySource); bSkyDirty = false; }
        else if (Id == TEXT("sky:apply"))
        {
            // Web skypanel gather(): the whole sky, hours and azimuth always present.
            auto Args = Copy(SkyDraft);
            if (!Args->HasField(TEXT("hours"))) Args->SetNumberField(TEXT("hours"), 12);
            if (!Args->HasField(TEXT("azimuth"))) Args->SetNumberField(TEXT("azimuth"), 180);
            if (Bridge->IsOwner() && Bridge->SendVerb(TEXT("sky"), Args.ToSharedRef())) { bSkyDirty = false; Toast(TEXT("Sky applied.")); }
            else Refused(TEXT("only the owner can change the sky"));
        }
    }
    else if (Id.StartsWith(TEXT("ground:")))
    {
        const TSharedPtr<FJsonObject> Palette = Sub(Defs, TEXT("groundPalette"));
        auto SendTerrain = [&]() {
            double Amplitude = 2.6;
            if (auto Shapes = Sub(Palette, TEXT("shapes"))) Shapes->TryGetNumberField(*GroundShape, Amplitude);
            FString Layer = TEXT("#4a5d33");
            if (auto Tint = Sub(Sub(Palette, TEXT("tints")), *GroundTint)) Tint->TryGetStringField(TEXT("layer"), Layer);
            auto Args = MakeShared<FJsonObject>();
            Args->SetNumberField(TEXT("seed"), GroundSeed); Args->SetNumberField(TEXT("size"), 160); Args->SetNumberField(TEXT("segments"), 200);
            Args->SetNumberField(TEXT("amplitude"), Amplitude); Args->SetNumberField(TEXT("flatRadius"), 16);
            auto L = MakeShared<FJsonObject>(); L->SetStringField(TEXT("color"), Layer); L->SetNumberField(TEXT("repeat"), 16);
            Args->SetArrayField(TEXT("layers"), {MakeShared<FJsonValueObject>(L)});
            if (Bridge->IsOwner() && Bridge->SendVerb(TEXT("terrain"), Args)) Toast(TEXT("Terrain applied."));
            else Refused(TEXT("only the owner can reshape the ground"));
        };
        if (Id.StartsWith(TEXT("ground:shape:"))) GroundShape = Id.Mid(13);
        else if (Id.StartsWith(TEXT("ground:tint:"))) GroundTint = Id.Mid(12);
        else if (Id.StartsWith(TEXT("ground:height:"))) GrassHeight = Id.Mid(14);
        else if (Id.StartsWith(TEXT("ground:density:"))) GrassDensity = Id.Mid(15);
        else if (Id == TEXT("ground:apply")) SendTerrain();
        else if (Id == TEXT("ground:reshuffle")) { GroundSeed = FMath::RandRange(0, 9998); SendTerrain(); }
        else if (Id == TEXT("ground:mow"))
        {
            auto Args = MakeShared<FJsonObject>(); Args->SetBoolField(TEXT("clear"), true);
            if (!(Bridge->IsOwner() && Bridge->SendVerb(TEXT("grass"), Args))) Refused(TEXT("only the owner can mow"));
        }
        else if (Id.StartsWith(TEXT("ground:plant:")))
        {
            const TSharedPtr<FJsonObject> Planting = Sub(Sub(Palette, TEXT("plantings")), *Id.Mid(13));
            if (!Planting) return;
            auto Args = Copy(Sub(Planting, TEXT("args")));
            bool bBlade = false;
            Planting->TryGetBoolField(TEXT("blade"), bBlade);
            if (bBlade)
            {
                double Height;
                if (auto H = Sub(Palette, TEXT("grassHeight")); H && H->TryGetNumberField(*GrassHeight, Height)) Args->SetNumberField(TEXT("height"), Height);
                FString TintKey, Color;
                if (Planting->TryGetStringField(TEXT("tint"), TintKey))
                    if (auto Tint = Sub(Sub(Palette, TEXT("tints")), *GroundTint); Tint && Tint->TryGetStringField(*TintKey, Color)) Args->SetStringField(TEXT("color"), Color);
            }
            double Density = 1;
            if (auto D = Sub(Palette, TEXT("grassDensity"))) D->TryGetNumberField(*GrassDensity, Density);
            Args->SetNumberField(TEXT("density"), Density);
            if (Bridge->IsOwner() && Bridge->SendVerb(TEXT("grass"), Args.ToSharedRef())) Toast(TEXT("Planted."));
            else Refused(TEXT("only the owner can plant"));
        }
    }
    else if (Id.StartsWith(TEXT("scene:remove:")))
    {
        auto Args = MakeShared<FJsonObject>(); Args->SetStringField(TEXT("id"), Id.Mid(13));
        if (!(Bridge->CanBuild() && Bridge->SendVerb(TEXT("remove"), Args))) Refused(TEXT("you can't build here"));
        SceneHash = 0;
    }
    RefreshWorldPanels();
}
