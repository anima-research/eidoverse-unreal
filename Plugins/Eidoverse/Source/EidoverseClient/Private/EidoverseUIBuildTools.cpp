// Build tools beyond spawn/place (web build.js, lights.js, placer.js, scenegraph.js):
// drag-to-move with the live `drag` stream, lock/guard and attribution, light editing,
// attach/detach/find. The server still decides; these only shape the requests.
#include "EidoverseUI.h"
#include "EidoverseBridge.h"
#include "EidoverseScene.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/WrapBox.h"
#include "Components/PointLightComponent.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/Guid.h"

namespace
{
    FString ToolJson(const TSharedRef<FJsonObject>& O)
    {
        FString S; FJsonSerializer::Serialize(O, TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&S)); return S;
    }
    TArray<TSharedPtr<FJsonValue>> ToolVec3(const FVector& V)
    {
        auto R = [](double X) { return MakeShared<FJsonValueNumber>(FMath::RoundToDouble(X * 1000) / 1000); };
        return {R(V.X), R(V.Y), R(V.Z)};
    }
    FString IStr(const TSharedPtr<FJsonObject>& O, const TCHAR* K) { FString S; if (O) O->TryGetStringField(K, S); return S; }
    bool IFlag(const TSharedPtr<FJsonObject>& O, const TCHAR* K) { bool B = false; if (O) O->TryGetBoolField(K, B); return B; }
}

void UEidoverseHUD::PushUndo(const FString& Verb, const TSharedRef<FJsonObject>& Args)
{
    UndoStack.Add({Verb, ToolJson(Args)});
    if (UndoStack.Num() > 40) UndoStack.RemoveAt(0, UndoStack.Num() - 40);
}

// build.js: guarded by someone else -> only they or the owner; locked -> unlock first.
FString UEidoverseHUD::AuthorRefusal(const FString& Id) const
{
    const AEidoverseScene* Scene = Bridge ? Bridge->GetScene() : nullptr;
    const FEidoverseSceneItem* E = Scene ? Scene->GetItems().Find(Id) : nullptr;
    if (!E) return FString();
    const FString Placer = IStr(E->Interact, TEXT("placer")), PlacerSub = IStr(E->Interact, TEXT("placerSub"));
    const bool bMine = Bridge->IsOwner() || (!PlacerSub.IsEmpty() ? PlacerSub == Bridge->MySub : Placer == Bridge->AssignedIdentity);
    if (IFlag(E->Interact, TEXT("guarded")) && !bMine)
        return FString::Printf(TEXT("\U0001F6E1 guarded by %s — only they or the world's owner can move or change it"), Placer.IsEmpty() ? TEXT("someone") : *Placer);
    if (IFlag(E->Interact, TEXT("locked"))) return TEXT("\U0001F512 locked — uncheck lock in the inspector to move or remove it");
    return FString();
}

void UEidoverseHUD::TickDrag(APlayerController* PC, AEidoverseScene* Scene, bool bShift)
{
    float MX = 0, MY = 0; PC->GetMousePosition(MX, MY);
    const FVector2D Mouse(MX, MY);
    if (bPointerMode && PC->WasInputKeyJustPressed(EKeys::LeftMouseButton))
    {
        FHitResult Hit;
        const FString Id = PC->GetHitResultUnderCursor(ECC_Visibility, true, Hit) && Hit.GetActor() ? Scene->EntityId(Hit.GetActor()) : FString();
        // "attach to…": this click names the parent.
        if (!AttachPick.IsEmpty())
        {
            const FString Child = AttachPick; AttachPick.Empty();
            if (!Id.IsEmpty() && Id != Child) Bridge->AttachEntity(Child, Id, FString());
            else Hint(TEXT("attach cancelled"), 2);
            return;
        }
        SelectEntity(Id);
        Drag = FDrag();
        FVector Pos; double Yaw, Scale; FString Lib; bool bAnimated;
        if (!Id.IsEmpty() && Scene->GetEntityPose(Id, Pos, Yaw, Scale, Lib, bAnimated) && !bAnimated)
        {
            Drag.Id = Id; Drag.bArmed = true; Drag.StartMouse = Mouse; Drag.bVertical = bShift;
            Drag.Yaw = Yaw; Drag.Scale = Scale; Drag.BeforeWire = Pos; Drag.CurrentWire = Pos;
            FVector Eye; FRotator View; PC->GetPlayerViewPoint(Eye, View);
            const FVector World = Scene->GetSceneOrigin() + AEidoverseBridge::FromWirePosition(Pos);
            Drag.StartWorld = World; Drag.Depth = FVector::Dist(Eye, World);
            Drag.Grab = World - Hit.ImpactPoint; Drag.Grab.Z = 0;
        }
        return;
    }
    if (!Drag.bArmed) return;
    if (!PC->IsInputKeyDown(EKeys::LeftMouseButton))
    {
        // Release: commit if anything moved (>= 5 mm), with an undo; stop the hold.
        if (Drag.bActive)
        {
            Scene->HoldTransform(Drag.Id, false);
            if ((Drag.CurrentWire - Drag.BeforeWire).GetAbsMax() >= .005)
            {
                auto Args = MakeShared<FJsonObject>();
                Args->SetStringField(TEXT("id"), Drag.Id); Args->SetArrayField(TEXT("pos"), ToolVec3(Drag.CurrentWire));
                Args->SetNumberField(TEXT("yaw"), FMath::RoundToDouble(Drag.Yaw * 10000) / 10000); Args->SetNumberField(TEXT("scale"), FMath::RoundToDouble(Drag.Scale * 1000) / 1000);
                if (Bridge->SendVerb(TEXT("place"), Args))
                {
                    auto Undo = MakeShared<FJsonObject>(); Undo->SetStringField(TEXT("id"), Drag.Id); Undo->SetArrayField(TEXT("pos"), ToolVec3(Drag.BeforeWire));
                    Undo->SetNumberField(TEXT("yaw"), Drag.Yaw); Undo->SetNumberField(TEXT("scale"), Drag.Scale);
                    PushUndo(TEXT("place"), Undo);
                }
            }
        }
        Drag = FDrag();
        return;
    }
    if (!Drag.bActive)
    {
        if (FVector2D::Distance(Mouse, Drag.StartMouse) <= 4) return;   // 4 px slop
        const FString Why = AuthorRefusal(Drag.Id);
        if (!Why.IsEmpty()) { Hint(Why, 3); Drag = FDrag(); return; }
        Drag.bActive = true; Scene->HoldTransform(Drag.Id, true);
    }
    FVector World = Drag.StartWorld;
    if (Drag.bVertical)
    {
        // Shift: straight up/down, a screen pixel worth of depth per pixel; never under ground.
        int32 SX = 1, SY = 1; PC->GetViewportSize(SX, SY);
        const double Fov = FMath::DegreesToRadians(PC->PlayerCameraManager ? PC->PlayerCameraManager->GetFOVAngle() : 90.f);
        World.Z = Drag.StartWorld.Z + (Drag.StartMouse.Y - Mouse.Y) * 2 * Drag.Depth * FMath::Tan(Fov / 2) / FMath::Max(1, SY);
        FHitResult Floor; FCollisionQueryParams Q(SCENE_QUERY_STAT(EidoverseDragFloor), true);
        if (AActor* A = Scene->EntityActor(Drag.Id)) Q.AddIgnoredActor(A);
        if (GetWorld()->LineTraceSingleByChannel(Floor, FVector(World.X, World.Y, World.Z + 5000), FVector(World.X, World.Y, World.Z - 20000), ECC_Visibility, Q))
            World.Z = FMath::Max(World.Z, Floor.ImpactPoint.Z);
    }
    else
    {
        // Horizontal: where the cursor meets the world (ignoring the thing itself) plus the grab offset.
        FVector Start, Dir;
        if (!PC->DeprojectMousePositionToWorld(Start, Dir)) return;
        FCollisionQueryParams Q(SCENE_QUERY_STAT(EidoverseDrag), true, PC->GetPawn());
        if (AActor* A = Scene->EntityActor(Drag.Id)) Q.AddIgnoredActor(A);
        FHitResult Hit;
        if (!GetWorld()->LineTraceSingleByChannel(Hit, Start, Start + Dir * 6000, ECC_Visibility, Q)) return;
        World = Hit.ImpactPoint + Drag.Grab;
        if (Hit.ImpactNormal.Z <= .6) World.Z = Drag.StartWorld.Z;    // only up-facing faces carry it
        else World.Z = Hit.ImpactPoint.Z;
    }
    Drag.CurrentWire = AEidoverseBridge::ToWirePosition(World - Scene->GetSceneOrigin());
    if (AActor* A = Scene->EntityActor(Drag.Id)) A->SetActorLocation(World);
    // The live stream others see (<= 15 Hz), relayed by the server for builders.
    const double Now = FPlatformTime::Seconds();
    if (Now - Drag.LastSent >= .066)
    {
        Drag.LastSent = Now;
        auto J = MakeShared<FJsonObject>();
        J->SetStringField(TEXT("id"), Drag.Id); J->SetArrayField(TEXT("pos"), ToolVec3(Drag.CurrentWire));
        J->SetNumberField(TEXT("yaw"), FMath::RoundToDouble(Drag.Yaw * 10000) / 10000);
        Bridge->SendRaw(TEXT("drag"), J);
    }
}

bool UEidoverseHUD::BuildToolAction(const FString& Action)
{
    if (!Action.StartsWith(TEXT("tool:"))) return false;
    AEidoverseScene* Scene = Bridge ? Bridge->GetScene() : nullptr;
    const FString Id = GetSelectedEntity();
    if (!Scene || Id.IsEmpty()) return true;
    const FEidoverseSceneItem* E = Scene->GetItems().Find(Id);
    if (!E) return true;
    auto Comp = [this, &Id](const TCHAR* Type, bool bOn)
    {
        auto A = MakeShared<FJsonObject>(); A->SetStringField(TEXT("id"), Id); A->SetStringField(TEXT("type"), Type);
        if (bOn) A->SetBoolField(TEXT("data"), true); else A->SetField(TEXT("data"), MakeShared<FJsonValueNull>());
        Bridge->SendVerb(TEXT("comp"), A);
    };
    if (Action == TEXT("tool:lock")) Comp(TEXT("lock"), !IFlag(E->Interact, TEXT("locked")));
    else if (Action == TEXT("tool:guard"))
    {
        const FString Why = AuthorRefusal(Id);
        if (!Why.IsEmpty() && Why.StartsWith(TEXT("\U0001F6E1"))) { Hint(Why, 3); return true; }
        Comp(TEXT("guard"), !IFlag(E->Interact, TEXT("guarded")));
    }
    else if (Action == TEXT("tool:find"))
    {
        FVector Pos; double Yaw, Scale; FString Lib; bool bAnimated;
        APlayerController* PC = GetOwningPlayer();
        if (PC && PC->GetPawn() && Scene->GetEntityPose(Id, Pos, Yaw, Scale, Lib, bAnimated))
        {
            const FVector Me = AEidoverseBridge::ToWirePosition(PC->GetPawn()->GetActorLocation() - Scene->GetSceneOrigin());
            Hint(FString::Printf(TEXT("%s is %.0f m away at (%.1f, %.1f)"), *Id, FVector::Dist2D(FVector(Me.X, Me.Z, 0), FVector(Pos.X, Pos.Z, 0)), Pos.X, Pos.Z), 5);
        }
    }
    else if (Action == TEXT("tool:attach")) { AttachPick = Id; Hint(TEXT("now click the thing it should ride (in the world)"), 5); }
    else if (Action == TEXT("tool:detach")) Bridge->DetachEntity(Id);
    else if (Action.StartsWith(TEXT("tool:light:")))
    {
        // lights.js: a partial light verb changes only the field touched.
        const UPointLightComponent* L = E->Light.Get();
        auto A = MakeShared<FJsonObject>(); A->SetStringField(TEXT("id"), Id);
        const FString What = Action.Mid(11);
        if (What == TEXT("brighter") || What == TEXT("dimmer")) A->SetNumberField(TEXT("intensity"), FMath::RoundToDouble(FMath::Clamp(E->LightIntensity * (What == TEXT("brighter") ? 1.25 : .8), 0., 400.) * 10) / 10);
        else if (What == TEXT("farther") || What == TEXT("nearer")) A->SetNumberField(TEXT("range"), FMath::Clamp(E->LightRange + (What == TEXT("farther") ? 2 : -2), 1., 200.));
        else if (What == TEXT("keep")) A->SetBoolField(TEXT("keep"), !E->bLightKeep);
        else if (What == TEXT("noon")) A->SetBoolField(TEXT("day"), E->bLightDayAware);   // "burns at noon" flips day-awareness
        else if (What.StartsWith(TEXT("color:"))) A->SetNumberField(TEXT("color"), double(FParse::HexNumber(*What.Mid(6))));
        else return true;
        if (Bridge->SendVerb(TEXT("light"), A)) Hint(TEXT("light updated"), 1.2f);
        (void)L;
    }
    ExtrasFor.Empty();   // rebuild the inspector rows with the new state
    return true;
}

void UEidoverseHUD::RebuildBuildExtras()
{
    if (!BuildExtras || !Bridge) return;
    const AEidoverseScene* Scene = Bridge->GetScene();
    const FString Id = GetSelectedEntity();
    const FEidoverseSceneItem* E = Scene && !Id.IsEmpty() ? Scene->GetItems().Find(Id) : nullptr;
    const FString Key = E ? Id + TEXT("|") + E->Revision + (E->MountTo.IsEmpty() ? TEXT("") : TEXT("|m")) : FString();
    if (Key == ExtrasFor) return;
    ExtrasFor = Key;
    BuildExtras->ClearChildren();
    if (!E) return;
    // placer.js: "placed by X · last change by Y".
    const FString Placer = IStr(E->Interact, TEXT("placer")), Last = IStr(E->Interact, TEXT("lastActor"));
    FString Who = Placer.IsEmpty() ? FString() : TEXT("placed by ") + Placer;
    if (!Last.IsEmpty() && Last != Placer) Who += (Who.IsEmpty() ? FString() : TEXT(" · ")) + TEXT("last change by ") + Last;
    if (!Who.IsEmpty()) BuildExtras->AddChildToVerticalBox(Label(Who, 9, FEidoverseTheme::Dim()));
    auto* Row = WidgetTree->ConstructWidget<UWrapBox>(); Row->SetInnerSlotPadding(FVector2D(4, 4));
    auto Add = [this, Row](const FString& Text, const FString& Action, const FString& Tip)
    {
        UButton* B = TextButton(Text); B->SetToolTipText(FText::FromString(Tip));
        B->OnClicked.AddDynamic(this, &UEidoverseHUD::OnBuildClicked); ButtonIds.Add(B, FName(*Action));
        Row->AddChildToWrapBox(B);
    };
    Add(IFlag(E->Interact, TEXT("locked")) ? TEXT("\U0001F512 locked") : TEXT("lock"), TEXT("tool:lock"), TEXT("locked things can't be moved or removed until unlocked"));
    Add(IFlag(E->Interact, TEXT("guarded")) ? TEXT("\U0001F6E1 guarded") : TEXT("guard"), TEXT("tool:guard"), TEXT("guarded: only its placer or the world's owner can change it"));
    Add(TEXT("find"), TEXT("tool:find"), TEXT("how far, and where"));
    if (E->MountTo.IsEmpty()) Add(TEXT("attach to…"), TEXT("tool:attach"), TEXT("make it ride another thing, keeping its pose"));
    else Add(TEXT("detach from ") + E->MountTo, TEXT("tool:detach"), TEXT("stop riding, staying where it is"));
    BuildExtras->AddChildToVerticalBox(Row);
    if (E->Light.IsValid())
    {
        BuildExtras->AddChildToVerticalBox(Label(FString::Printf(TEXT("LIGHT · brightness %.0f · range %.0f m%s%s"), E->LightIntensity, E->LightRange,
            E->bLightKeep ? TEXT(" · kept lit") : TEXT(""), E->bLightDayAware ? TEXT("") : TEXT(" · burns at noon")), 9, FEidoverseTheme::Dim(), true))->SetPadding(FMargin(0, 6, 0, 2));
        auto* L = WidgetTree->ConstructWidget<UWrapBox>(); L->SetInnerSlotPadding(FVector2D(4, 4));
        auto AddL = [this, L](const FString& Text, const FString& Action)
        { UButton* B = TextButton(Text); B->OnClicked.AddDynamic(this, &UEidoverseHUD::OnBuildClicked); ButtonIds.Add(B, FName(*Action)); L->AddChildToWrapBox(B); };
        AddL(TEXT("dimmer"), TEXT("tool:light:dimmer")); AddL(TEXT("brighter"), TEXT("tool:light:brighter"));
        AddL(TEXT("nearer"), TEXT("tool:light:nearer")); AddL(TEXT("farther"), TEXT("tool:light:farther"));
        AddL(E->bLightKeep ? TEXT("✓ keep lit") : TEXT("keep lit"), TEXT("tool:light:keep"));
        AddL(E->bLightDayAware ? TEXT("burns at noon") : TEXT("✓ burns at noon"), TEXT("tool:light:noon"));
        for (const TPair<const TCHAR*, const TCHAR*>& C : {TPair<const TCHAR*, const TCHAR*>{TEXT("warm"), TEXT("ffd9a0")}, {TEXT("white"), TEXT("ffffff")}, {TEXT("red"), TEXT("ff5a4a")},
                                                           {TEXT("green"), TEXT("7dff8a")}, {TEXT("blue"), TEXT("6aa8ff")}, {TEXT("violet"), TEXT("c48aff")}})
            AddL(C.Key, FString(TEXT("tool:light:color:")) + C.Value);
        BuildExtras->AddChildToVerticalBox(L);
    }
}
