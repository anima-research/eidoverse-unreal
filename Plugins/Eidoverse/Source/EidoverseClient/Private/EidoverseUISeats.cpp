// Seat-anchor gizmos (seatedit.js:105-277): while building with a thing selected, each
// of its seats shows as a diamond with a cone pointing where a sitter faces (gold, the
// picked one mint at 1.6x). A click within min(0.3, 0.06 + 0.015 x distance) m of one
// picks it; Q/E face it (Shift fine), Del removes, a drag slides it along the surface,
// Esc lets go. Every gesture sends the whole merged sockets map with its undo.
#include "EidoverseUI.h"
#include "EidoverseBridge.h"
#include "EidoverseScene.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

namespace
{
    struct FSeatMark { FString Slot; FVector At; FVector Forward; };
    bool SeatMarks(const AEidoverseScene* Scene, const FString& Id, const FString& Dragged, const FVector& DragLocal, TArray<FSeatMark>& Out)
    {
        const FEidoverseSceneItem* E = Scene->GetItems().Find(Id);
        const TSharedPtr<FJsonObject>* Sockets = nullptr;
        if (!E || !E->Actor.IsValid() || !E->Interact || !E->Interact->TryGetObjectField(TEXT("sockets"), Sockets)) return false;
        const FTransform T = E->Actor->GetActorTransform();
        for (const auto& Pair : (*Sockets)->Values)
        {
            const TSharedPtr<FJsonObject> S = Pair.Value.IsValid() ? Pair.Value->AsObject() : nullptr; if (!S) continue;
            const TArray<TSharedPtr<FJsonValue>>* P = nullptr; FVector Local(0);
            if (S->TryGetArrayField(TEXT("pos"), P) && P->Num() == 3) Local = FVector((*P)[0]->AsNumber(), (*P)[1]->AsNumber(), (*P)[2]->AsNumber());
            if (FString(Pair.Key) == Dragged) Local = DragLocal;
            double Yaw = 0; S->TryGetNumberField(TEXT("yaw"), Yaw);
            // Socket yaw is relative to the thing; the thing's wire yaw is minus its UE yaw.
            const double W = Yaw - FMath::DegreesToRadians(T.Rotator().Yaw);
            Out.Add({FString(Pair.Key), T.TransformPosition(AEidoverseBridge::FromWirePosition(Local)), AEidoverseBridge::FromWirePosition(FVector(FMath::Sin(W), 0, FMath::Cos(W))).GetSafeNormal()});
        }
        return true;
    }
    float RayDistanceMetres(const FVector& Origin, const FVector& Dir, const FVector& P)
    {
        const float Along = FVector::DotProduct(P - Origin, Dir);
        return Along <= 0 ? 1e9f : FVector::Dist(Origin + Dir * Along, P) / 100.f;
    }
}

int32 UEidoverseHUD::GetSeatGizmoCount() const
{
    int32 N = 0; for (const auto& C : SeatGizmoParts) N += C && C->IsVisible(); return N / 2;
}

bool UEidoverseHUD::TickSeatGizmos(APlayerController* PC, AEidoverseScene* Scene, bool bShift)
{
    const FString Id = GetSelectedEntity();
    if (Id != SeatSelOn) { SeatSel.Empty(); SeatSelOn = Id; bSeatDragArmed = bSeatDragging = false; }
    TArray<FSeatMark> Marks;
    const bool bHave = !Id.IsEmpty() && IsFrameOpen(TEXT("build")) && SeatMarks(Scene, Id, bSeatDragging ? SeatSel : FString(), SeatDragLocal, Marks);
    if (!SeatSel.IsEmpty() && !Marks.ContainsByPredicate([&](const FSeatMark& M) { return M.Slot == SeatSel; })) SeatSel.Empty();

    // Draw: a diamond and a cone per seat, pooled on a visible holder actor.
    if (!SeatGizmoHolder && bHave)
    {
        FActorSpawnParameters SP; SP.ObjectFlags |= RF_Transient;
        SeatGizmoHolder = GetWorld()->SpawnActor<AActor>(AActor::StaticClass(), FTransform::Identity, SP);
        if (SeatGizmoHolder) { auto* Root = NewObject<USceneComponent>(SeatGizmoHolder); SeatGizmoHolder->SetRootComponent(Root); Root->RegisterComponent(); }
    }
    UMaterialInterface* Unlit = LoadObject<UMaterialInterface>(nullptr, TEXT("/Eidoverse/UI/M_EidoverseOverlay.M_EidoverseOverlay"));
    while (SeatGizmoHolder && SeatGizmoParts.Num() < Marks.Num() * 2)
    {
        const bool bCone = SeatGizmoParts.Num() % 2 == 1;
        auto* C = NewObject<UStaticMeshComponent>(SeatGizmoHolder);
        C->SetupAttachment(SeatGizmoHolder->GetRootComponent()); SeatGizmoHolder->AddInstanceComponent(C);
        C->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, bCone ? TEXT("/Engine/BasicShapes/Cone.Cone") : TEXT("/Engine/BasicShapes/Cube.Cube")));
        if (Unlit) C->SetMaterial(0, UMaterialInstanceDynamic::Create(Unlit, C));
        C->SetCollisionEnabled(ECollisionEnabled::NoCollision); C->SetCastShadow(false); C->SetMobility(EComponentMobility::Movable);
        C->RegisterComponent();
        SeatGizmoParts.Add(C);
    }
    for (int32 I = 0; I < SeatGizmoParts.Num(); ++I)
    {
        UStaticMeshComponent* C = SeatGizmoParts[I]; if (!C) continue;
        const int32 M = I / 2;
        const bool bOn = bHave && M < Marks.Num();
        C->SetVisibility(bOn);
        if (!bOn) continue;
        const bool bSel = Marks[M].Slot == SeatSel;
        const float K = bSel ? 1.6f : 1.f;
        if (auto* MI = Cast<UMaterialInstanceDynamic>(C->GetMaterial(0))) MI->SetVectorParameterValue(TEXT("Color"), bSel ? FLinearColor(FColor(0x8f, 0xe8, 0xc8)) : FLinearColor(FColor(0xf5, 0xc9, 0x6b)));
        const FRotator Face = Marks[M].Forward.Rotation();
        if (I % 2 == 0) C->SetWorldTransform(FTransform(FRotator(45, Face.Yaw, 45), Marks[M].At, FVector(.09f * K)));
        else C->SetWorldTransform(FTransform(FRotator(-90, Face.Yaw, 0), Marks[M].At + Marks[M].Forward * 12 * K, FVector(.05f * K, .05f * K, .1f * K)));
    }
    if (!bHave) return false;

    float MX = 0, MY = 0; PC->GetMousePosition(MX, MY);
    const FVector2D Mouse(MX, MY);
    FVector Origin, Dir; PC->DeprojectMousePositionToWorld(Origin, Dir);
    // Pick: the nearest marker within the distance-scaled tolerance takes the click.
    if (bPointerMode && PC->WasInputKeyJustPressed(EKeys::LeftMouseButton))
    {
        int32 Best = INDEX_NONE; float BestD = 1e9f;
        for (int32 M = 0; M < Marks.Num(); ++M)
        {
            const float Tol = FMath::Min(.3f, .06f + .015f * float(FVector::Dist(Origin, Marks[M].At) / 100));
            const float D = RayDistanceMetres(Origin, Dir, Marks[M].At);
            if (D < Tol && D < BestD) { BestD = D; Best = M; }
        }
        if (Best != INDEX_NONE)
        {
            SeatSel = Marks[Best].Slot; bSeatDragArmed = true; bSeatDragging = false; SeatDragStart = Mouse;
            Hint(SeatSel + TEXT(" — drag to move · Q/E face · Del remove · Esc done"), 4);
            return true;
        }
        SeatSel.Empty();
        return false;   // the click falls through to selecting/dragging things
    }
    const FEidoverseSceneItem* E = Scene->GetItems().Find(Id);
    if (bSeatDragArmed && E && E->Actor.IsValid())
    {
        if (PC->IsInputKeyDown(EKeys::LeftMouseButton))
        {
            if (!bSeatDragging && FVector2D::Distance(Mouse, SeatDragStart) > 4) bSeatDragging = true;
            FHitResult Hit;
            if (bSeatDragging && PC->GetHitResultUnderCursor(ECC_Visibility, true, Hit) && Scene->EntityId(Hit.GetActor()) == Id)
                SeatDragLocal = AEidoverseBridge::ToWirePosition(E->Actor->GetActorTransform().InverseTransformPosition(Hit.ImpactPoint));
            return true;
        }
        // Release: commit a slide that happened.
        bSeatDragArmed = false;
        if (bSeatDragging)
        {
            bSeatDragging = false;
            const TSharedPtr<FJsonObject>* Have = nullptr; const TSharedPtr<FJsonObject>* Slot = nullptr;
            if (E->Interact && E->Interact->TryGetObjectField(TEXT("sockets"), Have) && (*Have)->TryGetObjectField(SeatSel, Slot))
            {
                auto Next = MakeShared<FJsonObject>(**Have); auto S = MakeShared<FJsonObject>(**Slot);
                auto R3 = [](double V) { return MakeShared<FJsonValueNumber>(FMath::RoundToDouble(V * 1000) / 1000); };
                S->SetArrayField(TEXT("pos"), {R3(SeatDragLocal.X), R3(SeatDragLocal.Y), R3(SeatDragLocal.Z)});
                Next->SetObjectField(SeatSel, S);
                SendSockets(Id, Next, *Have);
            }
        }
        return true;
    }
    if (SeatSel.IsEmpty()) return false;
    // Keys belong to the picked seat (not the thing) until Esc.
    if (PC->WasInputKeyJustPressed(EKeys::Escape)) { SeatSel.Empty(); return true; }
    if (PC->WasInputKeyJustPressed(EKeys::Q)) { BuildToolAction(FString::Printf(TEXT("tool:seat:turn:%s:%s"), *SeatSel, bShift ? TEXT("1.146") : TEXT("15"))); return true; }
    if (PC->WasInputKeyJustPressed(EKeys::E)) { BuildToolAction(FString::Printf(TEXT("tool:seat:turn:%s:%s"), *SeatSel, bShift ? TEXT("-1.146") : TEXT("-15"))); return true; }
    if (PC->WasInputKeyJustPressed(EKeys::Delete) || PC->WasInputKeyJustPressed(EKeys::BackSpace)) { BuildToolAction(TEXT("tool:seat:remove:") + SeatSel); SeatSel.Empty(); return true; }
    return false;
}
