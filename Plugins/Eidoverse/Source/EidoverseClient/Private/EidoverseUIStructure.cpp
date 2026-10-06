// The griddled-building editor (client/lib/structure_ui.js), with the pure edits of
// shared/structure_edit.js ported one for one: data in, data out, the whole building
// re-sent as one comp, undo keeps the previous value. Last write wins, as on the web.
#include "EidoverseUI.h"
#include "EidoverseBridge.h"
#include "EidoverseScene.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/WrapBox.h"
#include "Components/TextBlock.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "GameFramework/PlayerController.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/Guid.h"

namespace StructEdit
{
    using FArr = TArray<TSharedPtr<FJsonValue>>;
    TSharedPtr<FJsonObject> Empty()
    {
        auto D = MakeShared<FJsonObject>();
        D->SetNumberField(TEXT("tile"), 1); D->SetNumberField(TEXT("wallH"), 2.8); D->SetNumberField(TEXT("wallT"), .15);
        D->SetObjectField(TEXT("labels"), MakeShared<FJsonObject>());
        auto L = MakeShared<FJsonObject>(); L->SetNumberField(TEXT("y"), 0);
        L->SetArrayField(TEXT("tiles"), {}); L->SetArrayField(TEXT("walls"), {}); L->SetArrayField(TEXT("apertures"), {});
        D->SetArrayField(TEXT("levels"), {MakeShared<FJsonValueObject>(L)});
        return D;
    }
    TSharedPtr<FJsonObject> Clone(const TSharedPtr<FJsonObject>& D)
    {
        if (!D) return Empty();
        FString S; FJsonSerializer::Serialize(D.ToSharedRef(), TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&S));
        TSharedPtr<FJsonObject> Out; FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(S), Out); return Out ? Out : Empty();
    }
    FString ToString(const TSharedPtr<FJsonObject>& D)
    { FString S; if (D) FJsonSerializer::Serialize(D.ToSharedRef(), TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&S)); return S; }
    TSharedPtr<FJsonObject> Level(const TSharedPtr<FJsonObject>& D, int32 I)
    {
        FArr Levels; const FArr* Have = nullptr; if (D->TryGetArrayField(TEXT("levels"), Have)) Levels = *Have;
        while (Levels.Num() <= I)
        {
            auto L = MakeShared<FJsonObject>(); double Y = 0; D->TryGetNumberField(TEXT("wallH"), Y);
            L->SetNumberField(TEXT("y"), Levels.Num() * (Y > 0 ? Y : 2.8));
            L->SetArrayField(TEXT("tiles"), {}); L->SetArrayField(TEXT("walls"), {}); L->SetArrayField(TEXT("apertures"), {});
            Levels.Add(MakeShared<FJsonValueObject>(L));
        }
        D->SetArrayField(TEXT("levels"), Levels);
        return Levels[I]->AsObject();
    }
    FArr Get(const TSharedPtr<FJsonObject>& L, const TCHAR* K) { const FArr* A = nullptr; return L->TryGetArrayField(K, A) ? *A : FArr(); }
    int32 At(const TSharedPtr<FJsonValue>& V, int32 I) { const FArr* A = nullptr; return V->TryGetArray(A) && A->IsValidIndex(I) ? int32((*A)[I]->AsNumber()) : INT32_MIN; }
    TSharedPtr<FJsonValue> Tuple(std::initializer_list<int32> N, const FString& Tail = FString())
    {
        FArr A; for (int32 X : N) A.Add(MakeShared<FJsonValueNumber>(X));
        if (!Tail.IsEmpty()) A.Add(MakeShared<FJsonValueString>(Tail));
        return MakeShared<FJsonValueArray>(A);
    }
    bool SameEdge(const TSharedPtr<FJsonValue>& W, int32 Axis, int32 X, int32 Z) { return At(W, 0) == Axis && At(W, 1) == X && At(W, 2) == Z; }
    struct FGridEdge { int32 Axis, X, Z; double D; };
    // pickEdges: every edge of the cell under a grid-local point, nearest first.
    TArray<FGridEdge> PickEdges(const TSharedPtr<FJsonObject>& G, double Lx, double Lz)
    {
        double T = 1; G->TryGetNumberField(TEXT("tile"), T);
        const int32 Cx = FMath::FloorToInt(Lx / T), Cz = FMath::FloorToInt(Lz / T);
        const double U = Lx / T - Cx, V = Lz / T - Cz, R = UE_INV_SQRT_2;
        TArray<FGridEdge> E = {{0, Cx, Cz, V * T}, {0, Cx, Cz + 1, (1 - V) * T}, {1, Cx, Cz, U * T}, {1, Cx + 1, Cz, (1 - U) * T},
                           {2, Cx, Cz, FMath::Abs(U - V) * R * T}, {3, Cx, Cz, FMath::Abs(U + V - 1) * R * T}};
        E.StableSort([](const FGridEdge& A, const FGridEdge& B) { return A.D < B.D; });
        return E;
    }
    FIntPoint PickCell(const TSharedPtr<FJsonObject>& G, double Lx, double Lz)
    { double T = 1; G->TryGetNumberField(TEXT("tile"), T); return {FMath::FloorToInt(Lx / T), FMath::FloorToInt(Lz / T)}; }
    TSharedPtr<FJsonObject> AddWall(const TSharedPtr<FJsonObject>& Data, const FGridEdge& E, int32 Lv)
    {
        auto D = Clone(Data); auto L = Level(D, Lv); FArr W = Get(L, TEXT("walls"));
        if (W.ContainsByPredicate([&](const TSharedPtr<FJsonValue>& X) { return SameEdge(X, E.Axis, E.X, E.Z); })) return D;
        if (E.Axis >= 2) W.RemoveAll([&](const TSharedPtr<FJsonValue>& X) { return At(X, 0) >= 2 && At(X, 1) == E.X && At(X, 2) == E.Z; });
        W.Add(Tuple({E.Axis, E.X, E.Z})); L->SetArrayField(TEXT("walls"), W); return D;
    }
    TSharedPtr<FJsonObject> SetAperture(const TSharedPtr<FJsonObject>& Data, const FGridEdge& E, const FString& Kind, int32 Lv)
    {
        auto D = Clone(Data); auto L = Level(D, Lv);
        if (!Get(L, TEXT("walls")).ContainsByPredicate([&](const TSharedPtr<FJsonValue>& X) { return SameEdge(X, E.Axis, E.X, E.Z); })) return D;
        FArr A = Get(L, TEXT("apertures")); A.RemoveAll([&](const TSharedPtr<FJsonValue>& X) { return SameEdge(X, E.Axis, E.X, E.Z); });
        if (Kind == TEXT("door") || Kind == TEXT("window") || Kind == TEXT("arch")) A.Add(Tuple({E.Axis, E.X, E.Z}, Kind));
        L->SetArrayField(TEXT("apertures"), A); return D;
    }
    TSharedPtr<FJsonObject> SetTile(const TSharedPtr<FJsonObject>& Data, FIntPoint C, int32 Lv)
    {
        auto D = Clone(Data); auto L = Level(D, Lv); FArr T = Get(L, TEXT("tiles"));
        T.RemoveAll([&](const TSharedPtr<FJsonValue>& X) { return At(X, 0) == C.X && At(X, 1) == C.Y; });
        T.Add(Tuple({C.X, C.Y}, TEXT("floor"))); L->SetArrayField(TEXT("tiles"), T); return D;
    }
    TSharedPtr<FJsonObject> DrawRoom(const TSharedPtr<FJsonObject>& Data, FIntPoint A, FIntPoint B, int32 Lv)
    {
        auto D = Clone(Data);
        const int32 X0 = FMath::Min(A.X, B.X), X1 = FMath::Max(A.X, B.X), Z0 = FMath::Min(A.Y, B.Y), Z1 = FMath::Max(A.Y, B.Y);
        for (int32 X = X0; X <= X1; ++X) for (int32 Z = Z0; Z <= Z1; ++Z) D = SetTile(D, {X, Z}, Lv);
        for (int32 X = X0; X <= X1; ++X) { D = AddWall(D, {0, X, Z0, 0}, Lv); D = AddWall(D, {0, X, Z1 + 1, 0}, Lv); }
        for (int32 Z = Z0; Z <= Z1; ++Z) { D = AddWall(D, {1, X0, Z, 0}, Lv); D = AddWall(D, {1, X1 + 1, Z, 0}, Lv); }
        return D;
    }
    TSharedPtr<FJsonObject> EraseRoom(const TSharedPtr<FJsonObject>& Data, FIntPoint A, FIntPoint B, int32 Lv)
    {
        auto D = Clone(Data); auto L = Level(D, Lv);
        const int32 X0 = FMath::Min(A.X, B.X), X1 = FMath::Max(A.X, B.X), Z0 = FMath::Min(A.Y, B.Y), Z1 = FMath::Max(A.Y, B.Y);
        FArr T = Get(L, TEXT("tiles"));
        T.RemoveAll([&](const TSharedPtr<FJsonValue>& X) { const int32 Cx = At(X, 0), Cz = At(X, 1); return Cx >= X0 && Cx <= X1 && Cz >= Z0 && Cz <= Z1; });
        L->SetArrayField(TEXT("tiles"), T);
        TSet<FIntPoint> Floored; for (const auto& X : T) Floored.Add({At(X, 0), At(X, 1)});
        // a wall survives if either side still has a floor: it is someone else's wall
        FArr W = Get(L, TEXT("walls"));
        W.RemoveAll([&](const TSharedPtr<FJsonValue>& X)
        {
            const int32 Axis = At(X, 0), Ex = At(X, 1), Ez = At(X, 2);
            if (Axis >= 2) return !Floored.Contains({Ex, Ez});
            const FIntPoint Pa = Axis == 0 ? FIntPoint(Ex, Ez - 1) : FIntPoint(Ex - 1, Ez);
            return !(Floored.Contains(Pa) || Floored.Contains({Ex, Ez}));
        });
        L->SetArrayField(TEXT("walls"), W);
        FArr Ap = Get(L, TEXT("apertures"));
        Ap.RemoveAll([&](const TSharedPtr<FJsonValue>& X) { return !W.ContainsByPredicate([&](const TSharedPtr<FJsonValue>& Y) { return SameEdge(Y, At(X, 0), At(X, 1), At(X, 2)); }); });
        L->SetArrayField(TEXT("apertures"), Ap);
        return D;
    }
}
using namespace StructEdit;

void UEidoverseHUD::AddStructureRows(const FString& Id)
{
    // structure_ui.js TOOLS, plus level, undo and done.
    BuildExtras->AddChildToVerticalBox(Label(FString::Printf(TEXT("BUILDING · level %d%s"), StructLevel, StructTool.IsEmpty() ? TEXT("") : *(TEXT(" · tool: ") + StructTool)), 9, FEidoverseTheme::Dim(), true))->SetPadding(FMargin(0, 6, 0, 2));
    auto* Row = WidgetTree->ConstructWidget<UWrapBox>(); Row->SetInnerSlotPadding(FVector2D(4, 4));
    auto Add = [this, Row](const FString& Text, const FString& Action, const FString& Tip)
    { UButton* B = TextButton(Text); B->SetToolTipText(FText::FromString(Tip)); B->OnClicked.AddDynamic(this, &UEidoverseHUD::OnBuildClicked); ButtonIds.Add(B, FName(*Action)); Row->AddChildToWrapBox(B); };
    Add(TEXT("▭ room"), TEXT("struct:tool:room"), TEXT("drag a rectangle: floor and walls right round it"));
    Add(TEXT("│ wall"), TEXT("struct:tool:wall"), TEXT("click an edge — or the middle of a cell for a diagonal"));
    Add(TEXT("door"), TEXT("struct:tool:door"), TEXT("click a wall"));
    Add(TEXT("window"), TEXT("struct:tool:window"), TEXT("click a wall"));
    Add(TEXT("▦ floor"), TEXT("struct:tool:floor"), TEXT("click a cell"));
    Add(TEXT("⌫ erase"), TEXT("struct:tool:erase"), TEXT("drag to clear floor; shared walls survive"));
    Add(TEXT("level −"), TEXT("struct:level:-1"), TEXT("edit the level below"));
    Add(TEXT("level +"), TEXT("struct:level:1"), TEXT("edit the level above"));
    Add(TEXT("undo"), TEXT("struct:undo"), TEXT("undo the last building edit"));
    if (!StructTool.IsEmpty()) Add(TEXT("done"), TEXT("struct:done"), TEXT("stop editing the building"));
    BuildExtras->AddChildToVerticalBox(Row);
}

bool UEidoverseHUD::StructureAction(const FString& Action)
{
    if (!Action.StartsWith(TEXT("struct:"))) return false;
    AEidoverseScene* Scene = Bridge ? Bridge->GetScene() : nullptr;
    if (!Scene) return true;
    if (Action == TEXT("struct:new"))
    {
        // structure_ui.js "+ new": a building where you aim, then an empty plan.
        APlayerController* PC = GetOwningPlayer(); APawn* Pawn = PC ? PC->GetPawn() : nullptr; if (!Pawn) return true;
        const FVector Ahead = Pawn->GetActorLocation() + FRotator(0, PC->GetControlRotation().Yaw, 0).Vector() * 400;
        const FVector W = AEidoverseBridge::ToWirePosition(Ahead - Scene->GetSceneOrigin());
        const FString Id = FString::Printf(TEXT("house-%d_%d"), FMath::RoundToInt(W.X), FMath::RoundToInt(W.Z));
        auto Args = MakeShared<FJsonObject>();
        Args->SetStringField(TEXT("id"), Id); Args->SetStringField(TEXT("lib"), TEXT("eidoverse/assets/models/crate_large_red.glb"));
        Args->SetArrayField(TEXT("pos"), {MakeShared<FJsonValueNumber>(FMath::RoundToDouble(W.X)), MakeShared<FJsonValueNumber>(0), MakeShared<FJsonValueNumber>(FMath::RoundToDouble(W.Z))});
        Args->SetNumberField(TEXT("yaw"), 0);
        if (!Bridge->SendVerb(TEXT("spawn"), Args)) return true;
        SendComp(Id, TEXT("structure"), Empty());
        StructTarget = Id; StructData = Empty(); StructTool = TEXT("room"); StructLevel = 0; StructUndo.Reset();
        SelectEntity(Id);
        Hint(TEXT("new building — drag a room on the ground"), 4);
        return true;
    }
    const FString Id = GetSelectedEntity();
    if (Id.IsEmpty()) return true;
    if (Action.StartsWith(TEXT("struct:tool:")))
    {
        StructTool = Action.Mid(12);
        if (StructTarget != Id) { StructTarget = Id; StructData.Reset(); StructUndo.Reset(); StructInspectSerial = Bridge->InspectSerial; Bridge->RequestInspect(Id); }
        Hint(TEXT("building tool: ") + StructTool, 2);
    }
    else if (Action.StartsWith(TEXT("struct:level:"))) StructLevel = FMath::Clamp(StructLevel + FCString::Atoi(*Action.Mid(13)), 0, 7);
    else if (Action == TEXT("struct:done")) StructTool.Empty();
    else if (Action == TEXT("struct:undo") && StructUndo.Num())
    {
        TSharedPtr<FJsonObject> Prev; FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(StructUndo.Pop()), Prev);
        if (Prev) { StructData = Prev; SendComp(StructTarget, TEXT("structure"), Prev); }
    }
    ExtrasFor.Empty();
    return true;
}

bool UEidoverseHUD::TickStructure(APlayerController* PC, AEidoverseScene* Scene)
{
    if (StructTool.IsEmpty() || StructTarget.IsEmpty()) return false;
    // The editable plan comes from the inspect reply (the folded comp bag).
    if (!StructData && Bridge->InspectedId == StructTarget && Bridge->InspectSerial != StructInspectSerial && Bridge->InspectReply)
    {
        const TSharedPtr<FJsonObject>* Comp = nullptr; const TSharedPtr<FJsonObject>* S = nullptr;
        StructData = Bridge->InspectReply->TryGetObjectField(TEXT("comp"), Comp) && (*Comp)->TryGetObjectField(TEXT("structure"), S) ? Clone(*S) : Empty();
    }
    if (!StructData || !bPointerMode) return true;
    FVector Pos; double Yaw, Scale; FString Lib; bool bAnimated;
    if (!Scene->GetEntityPose(StructTarget, Pos, Yaw, Scale, Lib, bAnimated)) return true;
    // The mouse ray meets the level's floor plane; localizePoint into the grid.
    FVector Start, Dir; if (!PC->DeprojectMousePositionToWorld(Start, Dir)) return true;
    const FVector O = AEidoverseBridge::ToWirePosition(Start - Scene->GetSceneOrigin()), D = AEidoverseBridge::ToWirePosition(Dir * 100);
    double LevelY = 0; Level(StructData, StructLevel)->TryGetNumberField(TEXT("y"), LevelY);
    const double PlaneY = Pos.Y + LevelY * Scale;
    if (FMath::Abs(D.Y) < 1e-6) return true;
    const double T = (PlaneY - O.Y) / D.Y; if (T <= 0) return true;
    const FVector Hit = O + D * T;
    const double Dx = Hit.X - Pos.X, Dz = Hit.Z - Pos.Z, C = FMath::Cos(Yaw), N = FMath::Sin(Yaw);
    const double Lx = (Dx * C - Dz * N) / Scale, Lz = (Dx * N + Dz * C) / Scale;
    const FIntPoint Cell = PickCell(StructData, Lx, Lz);
    auto Commit = [&](const TSharedPtr<FJsonObject>& Next)
    {
        if (!Next || ToString(Next) == ToString(StructData)) return;
        StructUndo.Add(ToString(StructData)); if (StructUndo.Num() > 40) StructUndo.RemoveAt(0);
        StructData = Next; SendComp(StructTarget, TEXT("structure"), Next);
    };
    const bool bDragTool = StructTool == TEXT("room") || StructTool == TEXT("erase");
    if (PC->WasInputKeyJustPressed(EKeys::LeftMouseButton))
    {
        if (bDragTool) { bStructDragging = true; StructFrom = Cell; return true; }
        if (StructTool == TEXT("wall")) Commit(AddWall(StructData, PickEdges(StructData, Lx, Lz)[0], StructLevel));
        else if (StructTool == TEXT("floor")) Commit(SetTile(StructData, Cell, StructLevel));
        else if (StructTool == TEXT("door") || StructTool == TEXT("window"))
        {
            // pickWalledEdge: the nearest edge that actually carries a wall.
            const FArr Walls = Get(Level(StructData, StructLevel), TEXT("walls"));
            for (const FGridEdge& E : PickEdges(StructData, Lx, Lz))
                if (Walls.ContainsByPredicate([&](const TSharedPtr<FJsonValue>& W) { return SameEdge(W, E.Axis, E.X, E.Z); })) { Commit(SetAperture(StructData, E, StructTool, StructLevel)); break; }
        }
        return true;
    }
    if (bStructDragging && !PC->IsInputKeyDown(EKeys::LeftMouseButton))
    {
        bStructDragging = false;
        Commit(StructTool == TEXT("room") ? DrawRoom(StructData, StructFrom, Cell, StructLevel) : EraseRoom(StructData, StructFrom, Cell, StructLevel));
    }
    if (bStructDragging) Hint(FString::Printf(TEXT("%s %d×%d"), *StructTool, FMath::Abs(Cell.X - StructFrom.X) + 1, FMath::Abs(Cell.Y - StructFrom.Y) + 1), .3f);
    return true;
}

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
// Conformance with shared/structure_edit.js: the same edits must give the same plan
// (reference output from bun on 2026-10-06; see struct_ref in the session notes).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEidoverseStructureEditTest, "Eidoverse.StructureEdit", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FEidoverseStructureEditTest::RunTest(const FString&)
{
    auto D = Empty();
    D = DrawRoom(D, {0, 0}, {2, 1}, 0);
    D = DrawRoom(D, {3, 0}, {4, 1}, 0);
    D = AddWall(D, {2, 1, 0, 0}, 0);
    D = SetAperture(D, {0, 1, 0, 0}, TEXT("door"), 0);
    const FString AfterRooms = ToString(Level(D, 0));
    D = EraseRoom(D, {3, 0}, {4, 1}, 0);
    const FString AfterErase = ToString(Level(D, 0));
    TestEqual(TEXT("rooms"), AfterRooms, FString(TEXT("{\"y\":0,\"tiles\":[[0,0,\"floor\"],[0,1,\"floor\"],[1,0,\"floor\"],[1,1,\"floor\"],[2,0,\"floor\"],[2,1,\"floor\"],[3,0,\"floor\"],[3,1,\"floor\"],[4,0,\"floor\"],[4,1,\"floor\"]],\"walls\":[[0,0,0],[0,0,2],[0,1,0],[0,1,2],[0,2,0],[0,2,2],[1,0,0],[1,3,0],[1,0,1],[1,3,1],[0,3,0],[0,3,2],[0,4,0],[0,4,2],[1,5,0],[1,5,1],[2,1,0]],\"apertures\":[[0,1,0,\"door\"]]}")));
    TestEqual(TEXT("erase keeps shared walls"), AfterErase, FString(TEXT("{\"y\":0,\"tiles\":[[0,0,\"floor\"],[0,1,\"floor\"],[1,0,\"floor\"],[1,1,\"floor\"],[2,0,\"floor\"],[2,1,\"floor\"]],\"walls\":[[0,0,0],[0,0,2],[0,1,0],[0,1,2],[0,2,0],[0,2,2],[1,0,0],[1,3,0],[1,0,1],[1,3,1],[2,1,0]],\"apertures\":[[0,1,0,\"door\"]]}")));
    const TArray<FGridEdge> P = PickEdges(D, .3, .9);
    TArray<FString> Got; for (const FGridEdge& E : P) Got.Add(FString::Printf(TEXT("%d,%d,%d"), E.Axis, E.X, E.Z));
    TestEqual(TEXT("pick order"), FString::Join(Got, TEXT(" ")), FString(TEXT("0,0,1 3,0,0 1,0,0 2,0,0 1,1,0 0,0,0")));
    return true;
}
#endif
