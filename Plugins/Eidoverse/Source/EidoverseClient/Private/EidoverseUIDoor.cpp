// The front door's body picker and door key (ui.js openDoor): the server's avatar
// roster (GET /avatars) as cards with portraits (/thumb/<name>.png, a placeholder
// figure when nobody has worn it yet), the choice remembered; a door key for invites,
// shown after a refused key or on request. The join button says who enters as what.
#include "EidoverseUI.h"
#include "EidoverseBridge.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Components/Image.h"
#include "Components/SizeBox.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/WrapBox.h"
#include "Components/EditableTextBox.h"
#include "Components/ScrollBox.h"
#include "Blueprint/WidgetTree.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/ConfigCacheIni.h"
#include "GenericPlatform/GenericPlatformHttp.h"

UTexture2D* EidoDecodeImage(const TArray<uint8>& Bytes);

namespace
{
    const TCHAR* DoorSection = TEXT("/Script/EidoverseClient.Door");
    FString DoorHttpBase(const FString& Server)
    {
        FString Base = Server.TrimStartAndEnd();
        if (Base.StartsWith(TEXT("wss://"))) Base = TEXT("https://") + Base.Mid(6);
        else if (Base.StartsWith(TEXT("ws://"))) Base = TEXT("http://") + Base.Mid(5);
        else return FString();
        Base.RemoveFromEnd(TEXT("/"));
        Base.RemoveFromEnd(TEXT("/ws"));
        return Base;
    }
}

void UEidoverseHUD::SelectDoorAvatar(const FString& Name)
{
    DoorAvatar = Name;
    GConfig->SetString(DoorSection, TEXT("Avatar"), *Name, GGameUserSettingsIni);
    GConfig->Flush(false, GGameUserSettingsIni);
    RebuildDoorGrid();
}

void UEidoverseHUD::ShowDoorKey(bool bShow)
{
    if (DoorKeyRow) DoorKeyRow->SetVisibility(bShow ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
    if (DoorKeyReveal) DoorKeyReveal->SetVisibility(bShow ? ESlateVisibility::Collapsed : ESlateVisibility::Visible);
}

void UEidoverseHUD::OnDoorKeyReveal() { ShowDoorKey(true); }

void UEidoverseHUD::OnDoorAvatarClicked()
{
    const FString Id = ResolveClicked().ToString();
    if (Id.StartsWith(TEXT("door:"))) SelectDoorAvatar(Id.Mid(5));
}

void UEidoverseHUD::TickDoorRoster(double Now)
{
    if (!DoorServer || Now < NextRosterCheck) return;
    NextRosterCheck = Now + 1.5;
    const FString Base = DoorHttpBase(DoorServer->GetText().ToString());
    if (Base.IsEmpty() || Base == DoorRosterBase) return;
    DoorRosterBase = Base;
    TWeakObjectPtr<UEidoverseHUD> Weak(this);
    auto Req = FHttpModule::Get().CreateRequest();
    Req->SetURL(Base + TEXT("/avatars")); Req->SetVerb(TEXT("GET")); Req->SetTimeout(10);
    Req->OnProcessRequestComplete().BindLambda([Weak, Base](FHttpRequestPtr, FHttpResponsePtr Resp, bool bOk)
    {
        if (!Weak.IsValid() || Weak->DoorRosterBase != Base) return;
        TArray<TSharedPtr<FJsonValue>> List;
        if (!bOk || !Resp || Resp->GetResponseCode() != 200 || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Resp->GetContentAsString()), List)) return;
        Weak->DoorRoster.Reset();
        for (const auto& V : List)
        {
            const TSharedPtr<FJsonObject> O = V.IsValid() ? V->AsObject() : nullptr; if (!O) continue;
            FDoorAvatar A; O->TryGetStringField(TEXT("name"), A.Name); O->TryGetStringField(TEXT("path"), A.Path);
            if (!A.Name.IsEmpty() && !A.Path.IsEmpty() && Weak->DoorRoster.Num() < 128) Weak->DoorRoster.Add(A);
        }
        if (Weak->DoorAvatar.IsEmpty()) { FString Saved; GConfig->GetString(DoorSection, TEXT("Avatar"), Saved, GGameUserSettingsIni); Weak->DoorAvatar = Saved.IsEmpty() ? TEXT("claude") : Saved; }
        Weak->RebuildDoorGrid();
        // Portraits, as they come.
        for (const FDoorAvatar& A : Weak->DoorRoster)
        {
            const FString Key = Base + TEXT("|") + A.Name;
            if (Weak->DoorThumbRequested.Contains(Key)) continue;
            Weak->DoorThumbRequested.Add(Key);
            auto T = FHttpModule::Get().CreateRequest();
            T->SetURL(Base + TEXT("/thumb/") + FGenericPlatformHttp::UrlEncode(A.Name) + TEXT(".png")); T->SetVerb(TEXT("GET")); T->SetTimeout(10);
            T->OnProcessRequestComplete().BindLambda([Weak, Key, Name = A.Name](FHttpRequestPtr, FHttpResponsePtr R, bool bOk2)
            {
                if (!Weak.IsValid() || !bOk2 || !R || R->GetResponseCode() != 200) return;
                if (UTexture2D* Tex = EidoDecodeImage(R->GetContent())) { Weak->DoorThumbs.Add(Name, Tex); Weak->RebuildDoorGrid(); }
            });
            T->ProcessRequest();
        }
    });
    Req->ProcessRequest();
}

void UEidoverseHUD::RebuildDoorGrid()
{
    if (!DoorGrid) return;
    DoorGrid->ClearChildren();
    for (auto It = ButtonIds.CreateIterator(); It; ++It) if (!It->Key.IsValid()) It.RemoveCurrent();
    for (const FDoorAvatar& A : DoorRoster)
    {
        const bool bOn = A.Name == DoorAvatar;
        auto* Card = WidgetTree->ConstructWidget<UButton>();
        FButtonStyle S;
        S.SetNormal(FEidoverseTheme::Rounded(bOn ? FEidoverseTheme::Brand().CopyWithNewOpacity(.14f) : FEidoverseTheme::Well(), 8, bOn ? FEidoverseTheme::Brand() : FEidoverseTheme::Edge(), 1));
        S.SetHovered(FEidoverseTheme::Rounded(FEidoverseTheme::Hover(), 8, bOn ? FEidoverseTheme::Brand() : FEidoverseTheme::Edge(), 1));
        S.SetPressed(FEidoverseTheme::Rounded(FEidoverseTheme::Well(), 8));
        S.SetNormalPadding(FMargin(4)); S.SetPressedPadding(FMargin(4));
        Card->SetStyle(S);
        auto* Col = WidgetTree->ConstructWidget<UVerticalBox>();
        auto* Box = WidgetTree->ConstructWidget<USizeBox>();
        Box->SetWidthOverride(64); Box->SetHeightOverride(64);
        if (UTexture2D* T = DoorThumbs.FindRef(A.Name)) { auto* Img = WidgetTree->ConstructWidget<UImage>(); Img->SetBrushFromTexture(T); Box->SetContent(Img); }
        else { auto* Ph = Label(TEXT("\U0001F9CD"), 28, FEidoverseTheme::Dim()); Ph->SetJustification(ETextJustify::Center); Box->SetContent(Ph); }
        Col->AddChildToVerticalBox(Box)->SetHorizontalAlignment(HAlign_Center);
        auto* Name = Label(A.Name, 9, bOn ? FEidoverseTheme::Brand() : FEidoverseTheme::Text());
        Name->SetAutoWrapText(false); Name->SetJustification(ETextJustify::Center);
        Col->AddChildToVerticalBox(Name)->SetHorizontalAlignment(HAlign_Center);
        Card->SetContent(Col);
        Card->SetToolTipText(FText::FromString(A.Path));
        Card->OnClicked.AddDynamic(this, &UEidoverseHUD::OnDoorAvatarClicked);
        ButtonIds.Add(Card, FName(*(TEXT("door:") + A.Name)));
        DoorGrid->AddChildToWrapBox(Card);
    }
}

void UEidoverseHUD::SetDoorFields(const FString& Server, const FString& World, const FString& Name)
{
    if (DoorServer) DoorServer->SetText(FText::FromString(Server));
    if (DoorWorld) DoorWorld->SetText(FText::FromString(World));
    if (DoorName) DoorName->SetText(FText::FromString(Name));
    NextRosterCheck = 0;
}
