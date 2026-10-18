// The World panel's access tab (eidoverse-worlds client/lib/accesspanel.js, #258): who may come into this world,
// and until when. Everything is the `grant` verb underneath (the wildcard's rung for the door, a named record per
// member), so the tab can do nothing /access, /grant, /revoke and an agent's world_access cannot, and the server is
// the only judge: refusals come back as toasts and show here too. The structure comes from the server's
// access-state, re-asked whenever a grant lands or someone arrives or leaves. Owners edit; everyone else reads.
#include "EidoverseUI.h"
#include "EidoverseBridge.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/EditableTextBox.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/ScrollBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/WrapBox.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

namespace
{
    struct FChoice { const TCHAR* Value; const TCHAR* Label; const TCHAR* What; };
    const FChoice Doors[] = {
        {TEXT("builder"), TEXT("open"), TEXT("anyone may come in and build")},
        {TEXT("visitor"), TEXT("visitors"), TEXT("anyone may come in; only those listed may build")},
        {TEXT("spectator"), TEXT("watch only"), TEXT("anyone may watch; only those listed may come in")},
        {TEXT("none"), TEXT("members only"), TEXT("invisible to everyone not listed")},
    };
    // `none` is a real grant too: an explicit "not this person", which matters in an open world
    const FChoice Roles[] = {{TEXT("owner"), TEXT("owner"), nullptr}, {TEXT("builder"), TEXT("builder"), nullptr}, {TEXT("visitor"), TEXT("visitor"), nullptr},
        {TEXT("spectator"), TEXT("watch only"), nullptr}, {TEXT("none"), TEXT("no access"), nullptr}};
    const FChoice Spans[] = {{TEXT(""), TEXT("no end"), nullptr}, {TEXT("1h"), TEXT("1 hour"), nullptr}, {TEXT("1d"), TEXT("1 day"), nullptr}, {TEXT("1w"), TEXT("1 week"), nullptr}};
    FString RoleLabel(const FString& R) { for (const FChoice& C : Roles) if (R == C.Value) return C.Label; return R; }
    const FChoice* DoorOf(const FString& R) { for (const FChoice& C : Doors) if (R == C.Value) return &C; return nullptr; }
    FString Left(double Until, double Now)
    {
        const double Ms = Until - Now;
        if (Ms <= 0) return TEXT("ending");
        if (Ms < 3600000) return FString::Printf(TEXT("%d min left"), FMath::Max(1, FMath::RoundToInt(Ms / 60000)));
        if (Ms < 2 * 86400000.0) return FString::Printf(TEXT("%d h left"), FMath::RoundToInt(Ms / 3600000));
        return TEXT("until ") + FDateTime::FromUnixTimestamp(int64(Until / 1000)).ToString(TEXT("%Y-%m-%d"));
    }
    FString Ago(double T, double Now)
    {
        const double D = Now - T;
        return D < 3600000 ? FString(TEXT("here recently")) : D < 86400000 ? FString::Printf(TEXT("seen %d h ago"), FMath::RoundToInt(D / 3600000))
            : FString::Printf(TEXT("seen %d d ago"), FMath::RoundToInt(D / 86400000));
    }
    double Num(const TSharedPtr<FJsonObject>& O, const TCHAR* K, double D = 0) { double V; return O && O->TryGetNumberField(K, V) ? V : D; }
    FString Str(const TSharedPtr<FJsonObject>& O, const TCHAR* K) { FString V; if (O) O->TryGetStringField(K, V); return V; }
    bool Flag(const TSharedPtr<FJsonObject>& O, const TCHAR* K) { bool V = false; return O && O->TryGetBoolField(K, V) && V; }
}

UWidget* UEidoverseHUD::BuildWorldAccess()
{
    auto* Scroll = WidgetTree->ConstructWidget<UScrollBox>();
    auto* Column = WidgetTree->ConstructWidget<UVerticalBox>();
    Scroll->AddChild(Column);
    AccessDoor = WidgetTree->ConstructWidget<UVerticalBox>();
    Column->AddChildToVerticalBox(AccessDoor);
    AccessMembers = WidgetTree->ConstructWidget<UVerticalBox>();
    Column->AddChildToVerticalBox(AccessMembers)->SetPadding(FMargin(0, 6, 0, 0));
    // "add someone": built once, so a repaint never takes the search box from someone typing in it
    auto* Add = WidgetTree->ConstructWidget<UVerticalBox>();
    AccessAddSection = Add;
    Add->AddChildToVerticalBox(Label(TEXT("ADD SOMEONE"), 9, FEidoverseTheme::Dim(), true))->SetPadding(FMargin(0, 10, 0, 4));
    AccessSearch = Input(TEXT("a name — people who have signed in, or an agent"));
    AccessSearch->OnTextChanged.AddDynamic(this, &UEidoverseHUD::OnAccessSearchChanged);
    AccessSearch->OnTextCommitted.AddDynamic(this, &UEidoverseHUD::OnAccessSearchCommitted);
    Add->AddChildToVerticalBox(AccessSearch);
    AccessAddRole = WidgetTree->ConstructWidget<UWrapBox>(); AccessAddRole->SetInnerSlotPadding(FVector2D(4, 4));
    AccessAddSpan = WidgetTree->ConstructWidget<UWrapBox>(); AccessAddSpan->SetInnerSlotPadding(FVector2D(4, 4));
    Add->AddChildToVerticalBox(AccessAddRole)->SetPadding(FMargin(0, 4, 0, 0));
    Add->AddChildToVerticalBox(AccessAddSpan)->SetPadding(FMargin(0, 4, 0, 0));
    AccessResultsBox = WidgetTree->ConstructWidget<UVerticalBox>();
    Add->AddChildToVerticalBox(AccessResultsBox)->SetPadding(FMargin(0, 4, 0, 0));
    AccessHere = WidgetTree->ConstructWidget<UVerticalBox>();
    Add->AddChildToVerticalBox(AccessHere)->SetPadding(FMargin(0, 6, 0, 0));
    Column->AddChildToVerticalBox(Add);
    AccessErrorLine = Label(TEXT(""), 10, FEidoverseTheme::Attention());
    Column->AddChildToVerticalBox(AccessErrorLine)->SetPadding(FMargin(0, 8, 0, 0));
    return Scroll;
}

UButton* UEidoverseHUD::AccessButton(const FString& Text, const FString& Action, bool bOn)
{
    UButton* B = TextButton(Text);
    if (bOn) B->SetStyle(FEidoverseTheme::Button(FEidoverseTheme::Brand().CopyWithNewOpacity(.16f), FEidoverseTheme::Hover(), 6, FEidoverseTheme::Brand().CopyWithNewOpacity(.6f)));
    B->OnClicked.AddDynamic(this, &UEidoverseHUD::OnAccessClicked);
    AccessActions.Add(B, Action);
    return B;
}

void UEidoverseHUD::RefreshAccess(double Now)
{
    if (!Bridge || !AccessDoor) return;
    const bool bOpen = IsFrameOpen("world") && WorldTab == 5;
    if (!bOpen) { bAccessShown = false; return; }
    // opening the tab asks; so does a grant landing or someone arriving or leaving (settled 150 ms), and the relative
    // times ("2 h left") stay true while it is open
    if (!bAccessShown) { bAccessShown = true; AccessArmed.Empty(); AccessOpenRow.Empty(); Bridge->AccessError.Empty(); Bridge->RequestAccessState(); AccessAskAt = 0; AccessPaintedSerial = -1; }
    if (Bridge->AccessDirty != AccessDirtySeen) { AccessDirtySeen = Bridge->AccessDirty; AccessAskAt = Now + .15; }
    if (AccessAskAt > 0 && Now >= AccessAskAt) { AccessAskAt = 0; Bridge->RequestAccessState(); }
    const int32 Serial = Bridge->AccessSerial * 7919 + Bridge->AccessResultsSerial * 31 + GetTypeHash(Bridge->AccessError);
    if (Serial == AccessPaintedSerial && Now < AccessRepaintAt) return;
    AccessPaintedSerial = Serial; AccessRepaintAt = Now + 30;
    PaintAccess();
}

void UEidoverseHUD::PaintAccess()
{
    for (auto It = AccessActions.CreateIterator(); It; ++It) if (!It->Key.IsValid()) It.RemoveCurrent();
    AccessDoor->ClearChildren(); AccessMembers->ClearChildren(); AccessResultsBox->ClearChildren(); AccessHere->ClearChildren();
    AccessAddRole->ClearChildren(); AccessAddSpan->ClearChildren();
    const TSharedPtr<FJsonObject> S = Bridge->AccessState;
    auto Cap = [this](UVerticalBox* Box, const FString& Text) { Box->AddChildToVerticalBox(Label(Text.ToUpper(), 9, FEidoverseTheme::Dim(), true))->SetPadding(FMargin(0, 8, 0, 4)); };
    auto Note = [this](UVerticalBox* Box, const FString& Text, bool bWarn = false)
    { auto* T = Label(Text, 10, bWarn ? FEidoverseTheme::Attention() : FEidoverseTheme::Dim()); T->SetAutoWrapText(true); Box->AddChildToVerticalBox(T)->SetPadding(FMargin(0, 1, 0, 2)); };
    AccessErrorLine->SetText(FText::FromString(Bridge->AccessError));
    if (!S.IsValid() || !Bridge->bJoined)
    {
        Note(AccessDoor, Bridge->bJoined ? TEXT("asking the world…") : TEXT("join a world to see who may come in"));
        AccessAddSection->SetVisibility(ESlateVisibility::Collapsed);
        return;
    }
    const bool bEdit = Flag(S, TEXT("mayEdit")), bOwned = Flag(S, TEXT("owned"));
    const FString DoorRung = Str(S, TEXT("door"));
    const double Now = FDateTime::UtcNow().ToUnixTimestamp() * 1000.0;
    const TArray<TSharedPtr<FJsonValue>>* HereArr = nullptr; S->TryGetArrayField(TEXT("here"), HereArr);
    const int32 HereCount = HereArr ? HereArr->Num() : 0;

    // ---- the door ----
    Cap(AccessDoor, FString::Printf(TEXT("who can come into %s"), *Str(S, TEXT("world"))));
    if (!bOwned) Note(AccessDoor, TEXT("This world has no owner, so it is open: anyone may come in and build. An operator can make someone its owner."));
    else if (bEdit)
    {
        auto* Row = WidgetTree->ConstructWidget<UWrapBox>(); Row->SetInnerSlotPadding(FVector2D(4, 4));
        for (const FChoice& D : Doors)
        {
            UButton* B = AccessButton(D.Label, FString(TEXT("door:")) + D.Value, DoorRung == D.Value);
            B->SetToolTipText(FText::FromString(D.What));
            Row->AddChildToWrapBox(B);
        }
        AccessDoor->AddChildToVerticalBox(Row);
        const FChoice* Cur = DoorOf(DoorRung);
        Note(AccessDoor, Cur ? Cur->What : *(TEXT("everyone else: ") + DoorRung));
        if (!AccessArmed.IsEmpty())
        {
            const FChoice* Armed = DoorOf(AccessArmed);
            Note(AccessDoor, FString::Printf(TEXT("%d %s listed and will have to leave%s. Click “%s” again to go ahead, or add them below first."),
                HereCount, HereCount == 1 ? TEXT("person here isn't") : TEXT("people here aren't"), AccessArmed == TEXT("spectator") ? TEXT(" (they may watch)") : TEXT(""),
                Armed ? Armed->Label : *AccessArmed), true);
        }
    }
    else
    {
        const FChoice* Cur = DoorOf(DoorRung);
        Note(AccessDoor, Cur ? FString::Printf(TEXT("%s: %s"), Cur->Label, Cur->What) : TEXT("everyone else: ") + DoorRung);
        const TSharedPtr<FJsonObject>* You = nullptr; S->TryGetObjectField(TEXT("you"), You);
        const FString Role = You ? Str(*You, TEXT("role")) : FString();
        const double Until = You ? Num(*You, TEXT("until")) : 0;
        Note(AccessDoor, FString::Printf(TEXT("you are a %s%s here%s"), Role == TEXT("spectator") ? TEXT("watcher") : *Role,
            Until > 0 ? *FString::Printf(TEXT(" (%s)"), *Left(Until, Now)) : TEXT(""), bOwned ? TEXT(" — its owners decide who comes in") : TEXT("")));
    }

    // ---- members ----
    const TArray<TSharedPtr<FJsonValue>>* MembersArr = nullptr; S->TryGetArrayField(TEXT("members"), MembersArr);
    const int32 NumMembers = MembersArr ? MembersArr->Num() : 0;
    Cap(AccessMembers, FString::Printf(TEXT("listed by name (%d)"), NumMembers));
    if (!NumMembers) Note(AccessMembers, TEXT("nobody yet"));
    if (MembersArr) for (const auto& V : *MembersArr)
    {
        const TSharedPtr<FJsonObject> M = V->AsObject(); if (!M) continue;
        const FString Id = Str(M, TEXT("id")), Role = Str(M, TEXT("role")), Kind = Str(M, TEXT("kind"));
        // edit BY SUBJECT when the record has one: its name is a display name, which can look like something else
        const FString Who = Str(M, TEXT("sub")).IsEmpty() ? Id : Str(M, TEXT("sub"));
        const double Until = Num(M, TEXT("until"));
        auto* Line = WidgetTree->ConstructWidget<UHorizontalBox>();
        auto* Txt = WidgetTree->ConstructWidget<UVerticalBox>();
        auto* Name = Label(Id == Bridge->AssignedIdentity ? Id + TEXT(" (you)") : Id, 11); Name->SetAutoWrapText(false);
        Txt->AddChildToVerticalBox(Name);
        TArray<FString> Bits;
        if (!bEdit) Bits.Add(RoleLabel(Role));
        if (Kind == TEXT("agent")) Bits.Add(TEXT("agent"));
        if (Flag(M, TEXT("here"))) Bits.Add(TEXT("here now"));
        if (Until > 0) Bits.Add(Left(Until, Now));
        if (Bits.Num()) { auto* B2 = Label(FString::Join(Bits, TEXT(" · ")), 10, FEidoverseTheme::Dim()); Txt->AddChildToVerticalBox(B2); }
        if (Kind == TEXT("name") && DoorRung != TEXT("builder") && DoorRung != TEXT("visitor"))
        { auto* W = Label(TEXT("listed by name only: lets in an agent with this name, not a person. Remove and add them again once they have signed in."), 10, FEidoverseTheme::Attention()); W->SetAutoWrapText(true); Txt->AddChildToVerticalBox(W); }
        Line->AddChildToHorizontalBox(Txt)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
        if (bEdit)
        {
            Line->AddChildToHorizontalBox(AccessButton(RoleLabel(Role) + TEXT(" ▾"), TEXT("role:") + Who, AccessOpenRow == TEXT("role:") + Who))->SetPadding(FMargin(4, 0, 0, 0));
            if (Role != TEXT("owner")) Line->AddChildToHorizontalBox(AccessButton((Until > 0 ? Left(Until, Now) : FString(TEXT("no end"))) + TEXT(" ▾"), TEXT("span:") + Who, AccessOpenRow == TEXT("span:") + Who))->SetPadding(FMargin(4, 0, 0, 0));
            UButton* Rm = AccessButton(TEXT("✕"), TEXT("rm:") + Who);
            Rm->SetToolTipText(FText::FromString(FString::Printf(TEXT("take %s off the list"), *Id)));
            Line->AddChildToHorizontalBox(Rm)->SetPadding(FMargin(4, 0, 0, 0));
        }
        AccessMembers->AddChildToVerticalBox(Line)->SetPadding(FMargin(0, 2));
        // an open choice strip under its row: pick to grant, the strip closes
        if (bEdit && AccessOpenRow == TEXT("role:") + Who)
        {
            auto* Strip = WidgetTree->ConstructWidget<UWrapBox>(); Strip->SetInnerSlotPadding(FVector2D(4, 4));
            for (const FChoice& R : Roles) Strip->AddChildToWrapBox(AccessButton(R.Label, FString::Printf(TEXT("setrole:%s|%s"), R.Value, *Who), Role == R.Value));
            AccessMembers->AddChildToVerticalBox(Strip)->SetPadding(FMargin(12, 0, 0, 4));
        }
        if (bEdit && AccessOpenRow == TEXT("span:") + Who)
        {
            auto* Strip = WidgetTree->ConstructWidget<UWrapBox>(); Strip->SetInnerSlotPadding(FVector2D(4, 4));
            for (const FChoice& P : Spans) Strip->AddChildToWrapBox(AccessButton(P.Label, FString::Printf(TEXT("setspan:%s|%s"), FString(P.Value).IsEmpty() ? TEXT("none") : P.Value, *Who)));
            AccessMembers->AddChildToVerticalBox(Strip)->SetPadding(FMargin(12, 0, 0, 4));
        }
    }

    // ---- add someone (owners), and here now, not listed ----
    AccessAddSection->SetVisibility(bEdit && bOwned ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
    if (!(bEdit && bOwned)) return;
    AccessAddRole->AddChildToWrapBox(Label(TEXT("as"), 10, FEidoverseTheme::Dim()));
    for (const FChoice& R : Roles) AccessAddRole->AddChildToWrapBox(AccessButton(R.Label, FString(TEXT("addrole:")) + R.Value, AccessAddRoleValue == R.Value));
    AccessAddSpan->AddChildToWrapBox(Label(TEXT("for"), 10, FEidoverseTheme::Dim()));
    for (const FChoice& P : Spans) AccessAddSpan->AddChildToWrapBox(AccessButton(P.Label, FString(TEXT("addspan:")) + (FString(P.Value).IsEmpty() ? TEXT("none") : P.Value), AccessAddSpanValue == P.Value));
    const FString Q = AccessSearch->GetText().ToString().TrimStartAndEnd();
    bool bExact = false;
    if (!Q.IsEmpty() && Bridge->AccessQuery.TrimStartAndEnd() == Q)
        for (const TSharedPtr<FJsonObject>& R : Bridge->AccessResults)
        {
            const FString RName = Str(R, TEXT("name"));
            bExact |= RName.Equals(Q, ESearchCase::IgnoreCase);
            auto* Line = WidgetTree->ConstructWidget<UHorizontalBox>();
            auto* Txt = WidgetTree->ConstructWidget<UVerticalBox>();
            Txt->AddChildToVerticalBox(Label(RName, 11));
            Txt->AddChildToVerticalBox(Label(FString::Printf(TEXT("%s · %s"), *Str(R, TEXT("kind")), *Ago(Num(R, TEXT("last")), Now)), 10, FEidoverseTheme::Dim()));
            Line->AddChildToHorizontalBox(Txt)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
            // by SUBJECT: the pick binds to exactly this person, whatever else shares the name
            Line->AddChildToHorizontalBox(AccessButton(TEXT("add"), TEXT("add:") + Str(R, TEXT("sub"))));
            AccessResultsBox->AddChildToVerticalBox(Line)->SetPadding(FMargin(0, 2));
        }
    if (!Q.IsEmpty() && !bExact)
    {
        Note(AccessResultsBox, Bridge->AccessResults.Num() ? TEXT("not who you mean?") : *FString::Printf(TEXT("nobody called “%s” has signed in yet"), *Q));
        Note(AccessResultsBox, TEXT("an agent can be added by its name; a person needs to sign in once (opening any world) before you can add them"));
        AccessResultsBox->AddChildToVerticalBox(AccessButton(FString::Printf(TEXT("add “%s” by name"), *Q), TEXT("addname")));
    }
    if (HereArr && HereCount)
    {
        Cap(AccessHere, FString::Printf(TEXT("here now, not listed (%d)"), HereCount));
        Note(AccessHere, DoorRung == TEXT("builder") || DoorRung == TEXT("visitor")
            ? TEXT("they are here because the world is open — closing it would remove them unless you add them") : TEXT("they can stay until they leave"));
        auto* Chips = WidgetTree->ConstructWidget<UWrapBox>(); Chips->SetInnerSlotPadding(FVector2D(4, 4));
        int32 Verified = 0;
        for (const auto& V : *HereArr)
        {
            const TSharedPtr<FJsonObject> P = V->AsObject(); if (!P) continue;
            const FString Id = Str(P, TEXT("id")), Sub = Str(P, TEXT("sub"));
            UButton* B = AccessButton(TEXT("+ ") + Id, TEXT("here:") + (Sub.IsEmpty() ? Id : Sub));
            if (!Flag(P, TEXT("verified")))
            { B->SetIsEnabled(false); B->SetToolTipText(FText::FromString(Id + TEXT(" came in with a typed name — they can't be let into a closed world until they sign in"))); }
            else { ++Verified; B->SetToolTipText(FText::FromString(FString::Printf(TEXT("add %s as %s"), *Id, *RoleLabel(AccessAddRoleValue)))); }
            Chips->AddChildToWrapBox(B);
        }
        if (Verified > 1) Chips->AddChildToWrapBox(AccessButton(FString::Printf(TEXT("add all %d"), Verified), TEXT("hereall")));
        AccessHere->AddChildToVerticalBox(Chips);
    }
}

void UEidoverseHUD::AccessGrant(const FString& Id, const FString& Role, const FString& Span, bool bRevoke, bool bClearEnd)
{
    if (!Bridge || Id.IsEmpty()) return;
    auto A = MakeShared<FJsonObject>(); A->SetStringField(TEXT("id"), Id);
    if (bRevoke) A->SetBoolField(TEXT("revoke"), true);
    if (!Role.IsEmpty()) A->SetStringField(TEXT("role"), Role);
    if (!Span.IsEmpty()) A->SetStringField(TEXT("for"), Span);
    else if (bClearEnd) A->SetField(TEXT("until"), MakeShared<FJsonValueNull>());
    Bridge->SendAccessGrant(A);
}

void UEidoverseHUD::OnAccessClicked()
{
    for (const auto& Pair : AccessActions) if (Pair.Key.IsValid() && Pair.Key->IsHovered()) { AccessAction(Pair.Value); return; }
}

void UEidoverseHUD::AccessAction(const FString& Action)
{
    if (!Bridge || Action.IsEmpty()) return;
    const TSharedPtr<FJsonObject> S = Bridge->AccessState;
    FString Head, Tail; if (!Action.Split(TEXT(":"), &Head, &Tail)) Head = Action;
    if (Head == TEXT("door") && S.IsValid())
    {
        if (Str(S, TEXT("door")) == Tail) return;
        // closing removes whoever is here and not listed: say so, and ask twice
        const TArray<TSharedPtr<FJsonValue>>* Here = nullptr; S->TryGetArrayField(TEXT("here"), Here);
        const bool bLosing = (Tail == TEXT("none") || Tail == TEXT("spectator")) && Here && Here->Num() > 0;
        if (bLosing && AccessArmed != Tail) { AccessArmed = Tail; PaintAccess(); return; }
        AccessArmed.Empty();
        AccessGrant(TEXT("*"), Tail);
    }
    else if (Head == TEXT("role") || Head == TEXT("span")) AccessOpenRow = AccessOpenRow == Action ? FString() : Action;
    else if (Head == TEXT("setrole")) { FString Role, Who; Tail.Split(TEXT("|"), &Role, &Who); AccessOpenRow.Empty(); AccessGrant(Who, Role); }
    else if (Head == TEXT("setspan")) { FString Span, Who; Tail.Split(TEXT("|"), &Span, &Who); AccessOpenRow.Empty(); AccessGrant(Who, FString(), Span == TEXT("none") ? FString() : Span, false, Span == TEXT("none")); }
    else if (Head == TEXT("rm")) AccessGrant(Tail, FString(), FString(), true);
    else if (Head == TEXT("addrole")) AccessAddRoleValue = Tail;
    else if (Head == TEXT("addspan")) AccessAddSpanValue = Tail == TEXT("none") ? FString() : Tail;
    else if (Head == TEXT("add") || Head == TEXT("here")) { AccessGrant(Tail, AccessAddRoleValue, AccessAddSpanValue); if (Head == TEXT("add")) { AccessSearch->SetText(FText::GetEmpty()); Bridge->RequestAccessDirectory(FString()); } }
    else if (Head == TEXT("addname")) OnAccessSearchCommitted(AccessSearch->GetText(), ETextCommit::OnEnter);
    else if (Head == TEXT("hereall") && S.IsValid())
    {
        const TArray<TSharedPtr<FJsonValue>>* Here = nullptr; S->TryGetArrayField(TEXT("here"), Here);
        if (Here) for (const auto& V : *Here) if (const TSharedPtr<FJsonObject> P = V->AsObject()) if (Flag(P, TEXT("verified")))
            AccessGrant(Str(P, TEXT("sub")).IsEmpty() ? Str(P, TEXT("id")) : Str(P, TEXT("sub")), AccessAddRoleValue, AccessAddSpanValue);
    }
    PaintAccess();
}

void UEidoverseHUD::OnAccessSearchChanged(const FText& Text)
{
    if (Bridge) Bridge->RequestAccessDirectory(Text.ToString().TrimStartAndEnd());
}

void UEidoverseHUD::OnAccessSearchCommitted(const FText& Text, ETextCommit::Type Kind)
{
    const FString Name = Text.ToString().TrimStartAndEnd();
    if (Kind != ETextCommit::OnEnter || Name.IsEmpty() || !Bridge) return;
    AccessGrant(Name, AccessAddRoleValue, AccessAddSpanValue);   // by name: an agent's, or someone not yet seen
    AccessSearch->SetText(FText::GetEmpty());
    Bridge->RequestAccessDirectory(FString());
    PaintAccess();
}
