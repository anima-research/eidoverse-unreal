// Slice D's hook (Plugins/Eidoverse/Bridge/SLICES.md): bridge/access-state and bridge/access-directory (Bridge/src/access.ts), and the tab's writes.
#include "EidoverseBridge.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

namespace
{
    FString AxStr(const TSharedPtr<FJsonObject>& O, const TCHAR* K) { FString S; if (O) O->TryGetStringField(K, S); return S; }
    TSharedPtr<FJsonObject> AxObj(const TSharedPtr<FJsonObject>& O, const TCHAR* K) { const TSharedPtr<FJsonObject>* V = nullptr; return O && O->TryGetObjectField(K, V) ? *V : nullptr; }
    TSharedRef<FJsonObject> SetOp(const FString& Component, const TSharedRef<FJsonObject>& Value)
    { auto A = MakeShared<FJsonObject>(); A->SetStringField(TEXT("id"), TEXT("@world")); A->SetStringField(TEXT("component"), Component); A->SetObjectField(TEXT("value"), Value); return A; }
    const TCHAR* IdentityPrefix = TEXT("u-");
}

void AEidoverseBridge::OnAccessMsg(const FString& Type,const TSharedPtr<FJsonObject>& J)
{
    if (Type == TEXT("bridge/access-state")) { AccessStateWire = J; AccessState = AccessTabState(J); ++AccessSerial; return; }
    if (Type == TEXT("bridge/access-directory"))
    {
        FString Q; J->TryGetStringField(TEXT("q"), Q);
        if (Q != AccessQuery) return;   // an answer to an older keystroke
        AccessResults.Reset();
        const TArray<TSharedPtr<FJsonValue>>* R = nullptr;
        if (J->TryGetArrayField(TEXT("results"), R)) for (const auto& V : *R) if (const TSharedPtr<FJsonObject> O = V->AsObject()) if (AccessResults.Num() < 40) AccessResults.Add(O);
        const FString Err = AxStr(J, TEXT("error")); if (!Err.IsEmpty()) AccessError = Err;
        ++AccessResultsSerial;
    }
}

bool AEidoverseBridge::AccessMayEdit() const
{
    // the door is an owner's; listing people is grantRoles (roles.js). The authority still judges every write.
    const TSharedPtr<FJsonObject> Rights = AxObj(AccessStateWire, TEXT("rights"));
    if (AxStr(Rights, TEXT("role")) == TEXT("owner") || AxStr(AxObj(AccessStateWire, TEXT("you")), TEXT("role")) == TEXT("owner")) return true;
    const TArray<TSharedPtr<FJsonValue>>* P = nullptr;
    if (Rights && Rights->TryGetArrayField(TEXT("permissions"), P)) for (const auto& V : *P) if (V->AsString() == TEXT("grantRoles")) return true;
    return false;
}

void AEidoverseBridge::AccessReplied(const TSharedPtr<FJsonObject>& Reply)
{
    // a refusal shows in the tab as well as a toast; the bridge pushes the new state itself on success
    bool bOk = false; if (Reply && Reply->TryGetBoolField(TEXT("ok"), bOk) && bOk) { AccessError.Empty(); return; }
    const TSharedPtr<FJsonObject> R = AxObj(Reply, TEXT("refused")).IsValid() ? AxObj(Reply, TEXT("refused")) : Reply;
    AccessError = AxStr(R, TEXT("reason")).Left(300);
    ShowRefusal(Reply);
}

bool AEidoverseBridge::SendAccessDoor(const FString& Door)
{
    // access.js doorWrites: the door is @world access (+ default-role for the open doors), the access's other fields kept
    auto Access = MakeShared<FJsonObject>();
    if (const TSharedPtr<FJsonObject> Was = AxObj(AccessStateWire, TEXT("access")))
        for (const auto& F : Was->Values) if (F.Key != TEXT("join") && F.Key != TEXT("watch")) Access->SetField(F.Key, F.Value);
    TArray<TSharedPtr<FJsonValue>> Ops;
    if (Door == TEXT("invite-only") || Door == TEXT("watch"))
    {
        Access->SetStringField(TEXT("join"), TEXT("listed"));
        if (Door == TEXT("watch")) Access->SetStringField(TEXT("watch"), TEXT("anyone"));
        Ops.Add(MakeShared<FJsonValueObject>(SetOp(TEXT("access"), Access)));
    }
    else if (Door == TEXT("open") || Door == TEXT("public"))
    {
        Access->SetStringField(TEXT("join"), TEXT("anyone"));
        auto DefaultRole = MakeShared<FJsonObject>(); DefaultRole->SetStringField(TEXT("role"), Door == TEXT("open") ? TEXT("builder") : TEXT("member"));
        Ops.Add(MakeShared<FJsonValueObject>(SetOp(TEXT("access"), Access)));
        Ops.Add(MakeShared<FJsonValueObject>(SetOp(TEXT("default-role"), DefaultRole)));
    }
    else return false;
    AccessError.Empty();
    if (Ops.Num() == 1) return !Request(TEXT("set"), Ops[0]->AsObject().ToSharedRef(), [this](const TSharedPtr<FJsonObject>& R) { AccessReplied(R); }).IsEmpty();
    // batch ops are {verb, args} (requests.js batch)
    TArray<TSharedPtr<FJsonValue>> Steps;
    for (const auto& Op : Ops) { auto O = MakeShared<FJsonObject>(); O->SetStringField(TEXT("verb"), TEXT("set")); O->SetObjectField(TEXT("args"), Op->AsObject()); Steps.Add(MakeShared<FJsonValueObject>(O)); }
    auto Batch = MakeShared<FJsonObject>(); Batch->SetArrayField(TEXT("ops"), Steps);
    return !Request(TEXT("batch"), Batch, [this](const TSharedPtr<FJsonObject>& R) { AccessReplied(R); }).IsEmpty();
}

bool AEidoverseBridge::SendAccessRole(const FString& Who,const FString& NewRole,double UntilMs)
{
    if (Who.IsEmpty() || NewRole.IsEmpty()) return false;
    AccessError.Empty();
    auto Done = [this](const TSharedPtr<FJsonObject>& R) { AccessReplied(R); };
    if (!Who.StartsWith(IdentityPrefix))
    {
        // a name: the bridge resolves it to the identity roles key on, or says who it can't (writes.ts grant)
        auto F = MakeShared<FJsonObject>(); F->SetStringField(TEXT("who"), Who); F->SetStringField(TEXT("role"), NewRole);
        if (UntilMs > 0) F->SetNumberField(TEXT("until"), FMath::RoundToDouble(UntilMs));
        return !BridgeDo(TEXT("grant"), F, Done).IsEmpty();
    }
    // the whole grant, so a span cleared is cleared (Bridge/test/fixtures/access.ts grant)
    auto Value = MakeShared<FJsonObject>(); Value->SetStringField(TEXT("role"), NewRole);
    if (UntilMs > 0) Value->SetNumberField(TEXT("until"), FMath::RoundToDouble(UntilMs));
    return !Request(TEXT("set"), SetOp(TEXT("roles:") + Who, Value), Done).IsEmpty();
}

bool AEidoverseBridge::SendAccessRevoke(const FString& Who)
{
    if (Who.IsEmpty()) return false;
    AccessError.Empty();
    auto Done = [this](const TSharedPtr<FJsonObject>& R) { AccessReplied(R); };
    if (!Who.StartsWith(IdentityPrefix)) { auto F = MakeShared<FJsonObject>(); F->SetStringField(TEXT("who"), Who); return !BridgeDo(TEXT("revoke"), F, Done).IsEmpty(); }
    auto A = MakeShared<FJsonObject>(); A->SetStringField(TEXT("id"), TEXT("@world")); A->SetStringField(TEXT("component"), TEXT("roles:") + Who);
    return !Request(TEXT("delete"), A, Done).IsEmpty();
}

// ---- the access tab reads and writes main's shapes (EidoverseUIAccess.cpp); through the bridge they are translated here ----
namespace
{
    // the tab's door rungs ↔ access.js DOORS
    const TCHAR* DoorPairs[][2] = {{TEXT("builder"), TEXT("open")}, {TEXT("visitor"), TEXT("public")}, {TEXT("spectator"), TEXT("watch")}, {TEXT("none"), TEXT("invite-only")}};
    FString TabDoor(const FString& Door) { for (const auto& P : DoorPairs) if (Door == P[1]) return P[0]; return Door; }
    FString WireDoor(const FString& Rung) { for (const auto& P : DoorPairs) if (Rung == P[0]) return P[1]; return FString(); }
    double SpanMs(const FString& S)   // "1h", "2d", "1w", "30m" (access.js durationMs, the forms the tab and /grant use)
    {
        const FString T = S.TrimStartAndEnd().ToLower(); int32 I = 0; while (I < T.Len() && (FChar::IsDigit(T[I]) || T[I] == '.')) ++I;
        const double N = FCString::Atod(*T.Left(I)); const FString U = T.Mid(I).TrimStart();
        const double Ms = N * (U.StartsWith(TEXT("m")) ? 60000.0 : U.StartsWith(TEXT("h")) ? 3600000.0 : U.StartsWith(TEXT("d")) ? 86400000.0 : U.StartsWith(TEXT("w")) ? 604800000.0 : 0);
        return I > 0 && Ms > 0 && Ms <= 366 * 86400000.0 ? Ms : 0;
    }
}

TSharedPtr<FJsonObject> AEidoverseBridge::AccessTabState(const TSharedPtr<FJsonObject>& W) const
{
    auto S = MakeShared<FJsonObject>();
    S->SetStringField(TEXT("world"), AxStr(W, TEXT("world")));
    S->SetStringField(TEXT("door"), TabDoor(AxStr(W, TEXT("door"))));
    S->SetBoolField(TEXT("owned"), true);   // a world of this protocol always has its founder's owner grant
    S->SetBoolField(TEXT("mayEdit"), AccessMayEdit());
    const TSharedPtr<FJsonObject> WYou = AxObj(W, TEXT("you"));
    auto You = MakeShared<FJsonObject>(); You->SetStringField(TEXT("role"), AxStr(WYou, TEXT("role")).IsEmpty() ? AxStr(W, TEXT("defaultRole")) : AxStr(WYou, TEXT("role")));
    double Until = 0; if (WYou && WYou->TryGetNumberField(TEXT("until"), Until)) You->SetNumberField(TEXT("until"), Until);
    S->SetObjectField(TEXT("you"), You);
    // members and here-now by identity (sub), shown by name
    TArray<TSharedPtr<FJsonValue>> Members, Here; const TArray<TSharedPtr<FJsonValue>>* A = nullptr;
    if (W->TryGetArrayField(TEXT("members"), A)) for (const auto& V : *A) if (const TSharedPtr<FJsonObject> M = V->AsObject())
    {
        auto O = MakeShared<FJsonObject>(); O->SetStringField(TEXT("id"), AxStr(M, TEXT("name")).IsEmpty() ? AxStr(M, TEXT("id")) : AxStr(M, TEXT("name")));
        O->SetStringField(TEXT("sub"), AxStr(M, TEXT("id"))); O->SetStringField(TEXT("role"), AxStr(M, TEXT("role"))); O->SetStringField(TEXT("kind"), TEXT("person"));
        double U = 0; if (M->TryGetNumberField(TEXT("until"), U)) O->SetNumberField(TEXT("until"), U);
        bool bHere = false; M->TryGetBoolField(TEXT("here"), bHere); O->SetBoolField(TEXT("here"), bHere);
        Members.Add(MakeShared<FJsonValueObject>(O));
    }
    if (W->TryGetArrayField(TEXT("here"), A)) for (const auto& V : *A) if (const TSharedPtr<FJsonObject> H = V->AsObject())
    {
        auto O = MakeShared<FJsonObject>(); O->SetStringField(TEXT("id"), AxStr(H, TEXT("name")).IsEmpty() ? AxStr(H, TEXT("id")) : AxStr(H, TEXT("name")));
        O->SetStringField(TEXT("sub"), AxStr(H, TEXT("identity"))); O->SetBoolField(TEXT("verified"), !AxStr(H, TEXT("identity")).IsEmpty());
        Here.Add(MakeShared<FJsonValueObject>(O));
    }
    S->SetArrayField(TEXT("members"), Members); S->SetArrayField(TEXT("here"), Here);
    return S;
}

bool AEidoverseBridge::SendAccessGrantBridged(const TSharedRef<FJsonObject>& Args)
{
    // main's grant {id, role?, for?, until: null?, revoke?} → the door, roles:<identity>, or its delete
    const FString Id = AxStr(Args, TEXT("id")); bool bRevoke = false; Args->TryGetBoolField(TEXT("revoke"), bRevoke);
    FString GrantRole = AxStr(Args, TEXT("role"));
    if (Id == TEXT("*")) { const FString Door = WireDoor(GrantRole); if (Door.IsEmpty()) return false; return SendAccessDoor(Door); }
    if (bRevoke) return SendAccessRevoke(Id);
    if (GrantRole == TEXT("visitor")) GrantRole = TEXT("member");   // roles.js RENAMED_ROLES
    if (GrantRole == TEXT("spectator") || GrantRole == TEXT("none"))
    { AccessError = TEXT("that isn't a role in this world's protocol: a ban or a mute keeps someone out; the door decides who may watch"); ToastText(AccessError, true); return false; }
    // a span alone keeps the member's role; a role alone keeps their end, unless it is cleared (until: null)
    TSharedPtr<FJsonObject> Was; const TArray<TSharedPtr<FJsonValue>>* M = nullptr;
    if (AccessStateWire && AccessStateWire->TryGetArrayField(TEXT("members"), M)) for (const auto& V : *M) if (const auto O = V->AsObject()) if (AxStr(O, TEXT("id")) == Id) Was = O;
    if (GrantRole.IsEmpty()) GrantRole = AxStr(Was, TEXT("role"));
    const FString For = AxStr(Args, TEXT("for")); double Until = 0;
    if (!For.IsEmpty()) { const double Ms = SpanMs(For); if (Ms <= 0) { AccessError = TEXT("for how long? 30m, 2h, 1d, 1w (a year at most)"); return false; } Until = FDateTime::UtcNow().ToUnixTimestamp() * 1000.0 + Ms; }
    else if (!Args->HasField(TEXT("until")) && Was) Was->TryGetNumberField(TEXT("until"), Until);
    return SendAccessRole(Id, GrantRole, Until);
}
