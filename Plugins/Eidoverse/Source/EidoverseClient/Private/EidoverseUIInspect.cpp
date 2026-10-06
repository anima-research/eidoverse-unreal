// Scene inspector: the selected entity's authored components, from the server's
// `inspect` reply. Generic by design, like the browser's scenegraph.js component
// editor: every component type is a row, editable as raw JSON, removable, and new
// types can be added. A saved edit replaces that type's data wholesale (the `comp`
// verb's contract); data null removes it. The server decides (rights, lock, guard).
#include "EidoverseUI.h"
#include "EidoverseBridge.h"
#include "EidoverseScene.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/EditableTextBox.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/MultiLineEditableTextBox.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Policies/PrettyJsonPrintPolicy.h"

namespace
{
    FString JsonText(const TSharedPtr<FJsonValue>& Value, bool bPretty)
    {
        FString Out;
        if (!Value.IsValid()) return TEXT("null");
        if (bPretty)
        {
            auto W = TJsonWriterFactory<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>::Create(&Out);
            FJsonSerializer::Serialize(Value, FString(), W);
            Out.ReplaceInline(TEXT("\t"), TEXT("  "));
        }
        else
        {
            auto W = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Out);
            FJsonSerializer::Serialize(Value, FString(), W);
        }
        return Out;
    }

    // Any JSON value (components may be `true`, numbers, arrays or objects).
    TSharedPtr<FJsonValue> ParseValue(const FString& Text)
    {
        TArray<TSharedPtr<FJsonValue>> Values;
        auto Reader = TJsonReaderFactory<>::Create(TEXT("[") + Text + TEXT("]"));
        if (!FJsonSerializer::Deserialize(Reader, Values) || Values.Num() != 1) return nullptr;
        return Values[0];
    }

    bool ValidType(const FString& Type)
    {
        if (Type.IsEmpty() || Type.Len() > 64) return false;
        for (TCHAR C : Type) if (!FChar::IsAlnum(C) && C != '_' && C != '-') return false;
        return true;
    }
}

bool UEidoverseHUD::SetComponentJson(const FString& Id, const FString& Type, const FString& Json)
{
    if (!Bridge || Id.IsEmpty() || !ValidType(Type)) { Toast(TEXT("A component type is letters, digits, _ or -."), true); return false; }
    TSharedPtr<FJsonValue> Data = Json.TrimStartAndEnd().IsEmpty() ? MakeShared<FJsonValueNull>() : ParseValue(Json);
    if (!Data.IsValid()) { Toast(TEXT("Not valid JSON."), true); return false; }
    if (!Bridge->CanBuild()) { Toast(TEXT("You can't build in this world."), true); return false; }
    auto Args = MakeShared<FJsonObject>();
    Args->SetStringField(TEXT("id"), Id);
    Args->SetStringField(TEXT("type"), Type);
    Args->SetField(TEXT("data"), Data);
    if (!Bridge->SendVerb(TEXT("comp"), Args)) return false;
    // Reactions and other bag-only edits don't change the render revision: ask again.
    ReinspectAt = FPlatformTime::Seconds() + .35;
    return true;
}

void UEidoverseHUD::RefreshInspector(double Now)
{
    AEidoverseScene* Scene = Bridge ? Bridge->GetScene() : nullptr;
    const FString Selected = Bridge && Bridge->bJoined ? GetSelectedEntity() : FString();
    if (Selected != InspectingId)
    {
        InspectingId = Selected; InspectingRevision.Empty(); EditingComp.Empty(); bAddingComp = false;
    }
    if (!Selected.IsEmpty() && Scene)
    {
        const FString Revision = Scene->GetEntityRevision(Selected);
        if (Revision != InspectingRevision) { InspectingRevision = Revision; Bridge->RequestInspect(Selected); }
        if (ReinspectAt > 0 && Now >= ReinspectAt) { ReinspectAt = 0; Bridge->RequestInspect(Selected); }
        FVector Pos; double Yaw, Scale; FString Lib; bool bAnimated;
        if (InspectPose && Scene->GetEntityPose(Selected, Pos, Yaw, Scale, Lib, bAnimated))
            InspectPose->SetText(FText::FromString(FString::Printf(TEXT("(%.2f, %.2f, %.2f)  yaw %.0f°  scale %.2f%s"),
                Pos.X, Pos.Y, Pos.Z, FMath::RadiansToDegrees(Yaw), Scale, bAnimated ? TEXT("  ·  moving") : TEXT(""))));
        if (InspectFields.Num() == 5 && Scene->GetEntityPose(Selected, Pos, Yaw, Scale, Lib, bAnimated))
        {
            bool bTyping = false; for (UEditableTextBox* B : InspectFields) bTyping |= B && B->HasKeyboardFocus();
            if (!bTyping)
            {
                const double V[] = {Pos.X, Pos.Y, Pos.Z, FMath::RadiansToDegrees(Yaw), Scale};
                for (int32 I = 0; I < 5; ++I) InspectFields[I]->SetText(FText::FromString(I == 3 ? FString::Printf(TEXT("%.0f"), V[I]) : FString::Printf(TEXT("%.2f"), V[I])));
            }
        }
    }
    int32 Hash = HashCombine(GetTypeHash(Selected), GetTypeHash(Bridge ? Bridge->InspectSerial : 0));
    Hash = HashCombine(Hash, HashCombine(GetTypeHash(EditingComp), GetTypeHash(bAddingComp)));
    Hash = HashCombine(Hash, GetTypeHash(Bridge && Bridge->CanBuild()));
    if (Hash != InspectHash) { InspectHash = Hash; RebuildInspector(); }
}

void UEidoverseHUD::RebuildInspector()
{
    SceneInspector->ClearChildren();
    InspectPose = nullptr; CompEditor = nullptr; CompTypeInput = nullptr;
    AEidoverseScene* Scene = Bridge ? Bridge->GetScene() : nullptr;
    if (InspectingId.IsEmpty() || !Scene)
    {
        SceneInspector->AddChildToVerticalBox(Label(TEXT("Nothing selected — inspect a thing above, or click one in build mode."), 10, FEidoverseTheme::Dim()));
        return;
    }
    const bool bBuild = Bridge->CanBuild();
    FVector Pos; double Yaw = 0, Scale = 1; FString Lib; bool bAnimated = false;
    Scene->GetEntityPose(InspectingId, Pos, Yaw, Scale, Lib, bAnimated);
    SceneInspector->AddChildToVerticalBox(Label((Lib.IsEmpty() ? TEXT("thing") : FPaths::GetBaseFilename(Lib)) + TEXT("  ·  ") + InspectingId, 12, FEidoverseTheme::Text(), true));
    InspectPose = Label(TEXT(""), 10, FEidoverseTheme::Dim());
    SceneInspector->AddChildToVerticalBox(InspectPose)->SetPadding(FMargin(0, 2, 0, 8));
    InspectFields.Reset();
    if (bBuild && !bAnimated)
    {
        // Typed transforms: x y z (m), yaw (deg), scale; Enter or leaving a field places it.
        auto* Row = WidgetTree->ConstructWidget<UHorizontalBox>();
        const TCHAR* Names[] = {TEXT("x"), TEXT("y"), TEXT("z"), TEXT("yaw"), TEXT("scale")};
        for (int32 I = 0; I < 5; ++I)
        {
            Row->AddChildToHorizontalBox(Label(Names[I], 9, FEidoverseTheme::Dim()))->SetPadding(FMargin(I ? 6 : 0, 4, 3, 0));
            UEditableTextBox* Box = Input(TEXT(""));
            Box->OnTextCommitted.AddDynamic(this, &UEidoverseHUD::OnInspectFieldCommitted);
            auto* Size = WidgetTree->ConstructWidget<USizeBox>(); Size->SetWidthOverride(I < 3 ? 58 : 48); Size->SetContent(Box);
            Row->AddChildToHorizontalBox(Size);
            InspectFields.Add(Box);
        }
        SceneInspector->AddChildToVerticalBox(Row)->SetPadding(FMargin(0, 0, 0, 8));
    }

    auto StyleEditor = [](UMultiLineEditableTextBox* Box)
    {
        FEditableTextBoxStyle& S = Box->WidgetStyle;
        S.SetBackgroundImageNormal(FEidoverseTheme::Rounded(FEidoverseTheme::Well(), 6, FEidoverseTheme::Edge(), 1));
        S.SetBackgroundImageHovered(FEidoverseTheme::Rounded(FEidoverseTheme::Well(), 6, FLinearColor(1, 1, 1, .2f), 1));
        S.SetBackgroundImageFocused(FEidoverseTheme::Rounded(FEidoverseTheme::Well(), 6, FEidoverseTheme::Brand().CopyWithNewOpacity(.6f), 1));
        S.SetForegroundColor(FSlateColor(FEidoverseTheme::Text()));
        S.SetPadding(FMargin(8, 5));
        S.TextStyle.SetFont(FEidoverseTheme::Font(10));
        S.TextStyle.SetColorAndOpacity(FSlateColor(FEidoverseTheme::Text()));
    };
    auto Editor = [&](const FString& Text)
    {
        CompEditor = WidgetTree->ConstructWidget<UMultiLineEditableTextBox>();
        StyleEditor(CompEditor);
        CompEditor->SetText(FText::FromString(Text));
        auto* Box = WidgetTree->ConstructWidget<USizeBox>();
        Box->SetMinDesiredHeight(110); Box->SetMaxDesiredHeight(220);
        Box->SetContent(CompEditor);
        SceneInspector->AddChildToVerticalBox(Box)->SetPadding(FMargin(0, 2, 0, 4));
    };
    auto Buttons = [&](std::initializer_list<TPair<const TCHAR*, FString>> List)
    {
        auto* Row = WidgetTree->ConstructWidget<UHorizontalBox>();
        for (const auto& B : List) Row->AddChildToHorizontalBox(ActionButton(B.Key, B.Value, B.Value == TEXT("comp:apply") || B.Value == TEXT("comp:create")))->SetPadding(FMargin(0, 0, 4, 0));
        SceneInspector->AddChildToVerticalBox(Row)->SetPadding(FMargin(0, 0, 0, 8));
    };

    const TSharedPtr<FJsonObject> Reply = Bridge->InspectedId == InspectingId ? Bridge->InspectReply : nullptr;
    FString Error;
    const TSharedPtr<FJsonObject>* Comp = nullptr;
    if (!Reply) SceneInspector->AddChildToVerticalBox(Label(TEXT("loading components…"), 10, FEidoverseTheme::Dim()));
    else if (Reply->TryGetStringField(TEXT("error"), Error))
    {
        FString Types;
        const TArray<TSharedPtr<FJsonValue>>* List = nullptr;
        if (Reply->TryGetArrayField(TEXT("types"), List)) for (const auto& V : *List) Types += (Types.IsEmpty() ? TEXT("") : TEXT(", ")) + V->AsString();
        SceneInspector->AddChildToVerticalBox(Label(Error == TEXT("too_large") ? TEXT("Too large to edit here: ") + Types
            : Error == TEXT("not_found") ? FString(TEXT("It's gone.")) : Error, 10, FEidoverseTheme::Attention()));
    }
    else if (Reply->TryGetObjectField(TEXT("comp"), Comp))
    {
        TArray<FString> Types;
        for (const auto& P : (*Comp)->Values) Types.Add(FString(P.Key));
        Types.Sort();
        if (Types.IsEmpty()) SceneInspector->AddChildToVerticalBox(Label(TEXT("No components — a plain thing."), 10, FEidoverseTheme::Dim()));
        for (const FString& Type : Types)
        {
            const TSharedPtr<FJsonValue> Value = (*Comp)->TryGetField(Type);
            if (Type == EditingComp)
            {
                SceneInspector->AddChildToVerticalBox(Label(Type, 11, FEidoverseTheme::Brand(), true));
                Editor(JsonText(Value, true));
                Buttons({{TEXT("apply"), TEXT("comp:apply")}, {TEXT("cancel"), TEXT("comp:cancel")}});
                continue;
            }
            auto* Row = WidgetTree->ConstructWidget<UHorizontalBox>();
            auto* Name = Label(Type, 11, FEidoverseTheme::Text(), true);
            Name->SetAutoWrapText(false);
            Row->AddChildToHorizontalBox(Name)->SetPadding(FMargin(0, 2, 6, 0));
            FString Preview = JsonText(Value, false);
            if (Preview.Len() > 48) Preview = Preview.Left(47) + TEXT("…");
            auto* Short = Label(Preview, 9, FEidoverseTheme::Dim());
            Short->SetAutoWrapText(false);
            Short->SetClipping(EWidgetClipping::ClipToBounds);
            Row->AddChildToHorizontalBox(Short)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
            if (bBuild)
            {
                Row->AddChildToHorizontalBox(ActionButton(TEXT("edit"), TEXT("comp:edit:") + Type))->SetPadding(FMargin(4, 0, 0, 0));
                Row->AddChildToHorizontalBox(ActionButton(TEXT("✕"), TEXT("comp:del:") + Type))->SetPadding(FMargin(4, 0, 0, 0));
            }
            SceneInspector->AddChildToVerticalBox(Row)->SetPadding(FMargin(0, 2));
        }
    }
    if (!bBuild) { SceneInspector->AddChildToVerticalBox(Label(TEXT("View only — this world's owner hasn't made you a builder."), 10, FEidoverseTheme::Dim()))->SetPadding(FMargin(0, 8, 0, 0)); return; }
    if (bAddingComp)
    {
        SceneInspector->AddChildToVerticalBox(Label(TEXT("NEW COMPONENT"), 9, FEidoverseTheme::Dim(), true))->SetPadding(FMargin(0, 10, 0, 4));
        CompTypeInput = Input(TEXT("type, e.g. reactions"));
        SceneInspector->AddChildToVerticalBox(CompTypeInput);
        Editor(TEXT("{}"));
        Buttons({{TEXT("add"), TEXT("comp:create")}, {TEXT("cancel"), TEXT("comp:cancel")}});
    }
    else if (EditingComp.IsEmpty())
    {
        auto* Row = WidgetTree->ConstructWidget<UHorizontalBox>();
        Row->AddChildToHorizontalBox(ActionButton(TEXT("+ component"), TEXT("comp:add")));
        SceneInspector->AddChildToVerticalBox(Row)->SetPadding(FMargin(0, 10, 0, 0));
    }
}

void UEidoverseHUD::InspectorAction(const FString& Action)
{
    if (Action.StartsWith(TEXT("comp:edit:"))) { EditingComp = Action.Mid(10); bAddingComp = false; }
    else if (Action.StartsWith(TEXT("comp:del:"))) SetComponentJson(InspectingId, Action.Mid(9), FString());
    else if (Action == TEXT("comp:add")) { bAddingComp = true; EditingComp.Empty(); }
    else if (Action == TEXT("comp:cancel")) { bAddingComp = false; EditingComp.Empty(); }
    else if (Action == TEXT("comp:apply") && CompEditor)
    {
        const FString Text = CompEditor->GetText().ToString();
        if (Text.TrimStartAndEnd().IsEmpty()) { Toast(TEXT("Empty — use ✕ to remove a component."), true); return; }
        if (SetComponentJson(InspectingId, EditingComp, Text)) EditingComp.Empty();
    }
    else if (Action == TEXT("comp:create") && CompEditor && CompTypeInput)
    {
        const FString Type = CompTypeInput->GetText().ToString().TrimStartAndEnd();
        const FString Text = CompEditor->GetText().ToString();
        if (Text.TrimStartAndEnd().IsEmpty()) { Toast(TEXT("Give it some data, e.g. {} or true."), true); return; }
        if (SetComponentJson(InspectingId, Type, Text)) bAddingComp = false;
    }
    InspectHash = 0;   // repaint now
}

bool UEidoverseHUD::ApplyInspectTransform(FVector Pos, float YawDegrees, float Scale)
{
    if (Pos.ContainsNaN() || !FMath::IsFinite(YawDegrees) || !FMath::IsFinite(Scale)) return false;
    return PlaceSelected([&](FVector& P, double& Yaw, double& S)
    {
        P = Pos; Yaw = FMath::DegreesToRadians(double(YawDegrees)); S = FMath::Clamp(double(Scale), .05, 50.);
    });
}

void UEidoverseHUD::OnInspectFieldCommitted(const FText&, ETextCommit::Type Kind)
{
    if (Kind == ETextCommit::OnCleared || InspectFields.Num() != 5) return;
    double V[5];
    for (int32 I = 0; I < 5; ++I)
    {
        const FString T = InspectFields[I]->GetText().ToString().TrimStartAndEnd();
        if (!T.IsNumeric()) { Toast(TEXT("Numbers only in the transform fields."), true); return; }
        V[I] = FCString::Atod(*T);
    }
    // Only place when something actually changed (leaving a field commits too).
    AEidoverseScene* Scene = Bridge ? Bridge->GetScene() : nullptr;
    FVector Pos; double Yaw, Scale; FString Lib; bool bAnimated;
    if (!Scene || !Scene->GetEntityPose(GetSelectedEntity(), Pos, Yaw, Scale, Lib, bAnimated)) return;
    const bool bSame = FVector::Dist(Pos, FVector(V[0], V[1], V[2])) < .005 && FMath::Abs(FMath::RadiansToDegrees(Yaw) - V[3]) < .5 && FMath::Abs(Scale - V[4]) < .005;
    if (!bSame) ApplyInspectTransform(FVector(V[0], V[1], V[2]), float(V[3]), float(V[4]));
}
