#include "EidoverseActions.h"
#include "Algo/AllOf.h"
#include "Algo/AnyOf.h"

namespace
{
    TArray<FEidoAction>& Store() { static TArray<FEidoAction> S; return S; }
    bool Visible(const FEidoAction& A) { return !A.When || A.When(); }
    bool IsSplit(TCHAR C) { return FChar::IsWhitespace(C) || FString(TEXT("-_/·:▸.,()'\"")).Contains(FString(1, &C)); }
    TArray<FString> Words(const FString& T)
    {
        TArray<FString> Out; FString W;
        for (const TCHAR C : T) { if (IsSplit(C)) { if (!W.IsEmpty()) Out.Add(W); W.Empty(); } else W.AppendChar(C); }
        if (!W.IsEmpty()) Out.Add(W);
        return Out;
    }
    int32 Tier(const FString& Q, const FString& Text)
    {
        if (Text.IsEmpty()) return 0;
        const FString T = Text.ToLower();
        if (T == Q) return 1000;
        if (T.StartsWith(Q)) return 900 - FMath::Min(49, T.Len() - Q.Len());
        const TArray<FString> W = Words(T);
        for (const FString& X : W) if (X.StartsWith(Q)) return 700;
        TArray<FString> QW; Q.ParseIntoArrayWS(QW);
        if (QW.Num() > 1 && Algo::AllOf(QW, [&W](const FString& P) { return Algo::AnyOf(W, [&P](const FString& X) { return X.StartsWith(P); }); })) return 650;
        if (Q.Len() < 2) return 0;
        if (T.Contains(Q)) return 400;
        int32 I = 0, Start = -1, End = -1;
        for (int32 J = 0; J < T.Len() && I < Q.Len(); ++J) if (T[J] == Q[I]) { if (Start < 0) Start = J; End = J; ++I; }
        if (I < Q.Len()) return 0;
        return 100 + FMath::RoundToInt(99.0 * Q.Len() / (End - Start + 1));
    }
}

namespace EidoActions
{
void Register(const FEidoAction& A)
{
    if (A.Id.IsEmpty()) return;
    for (FEidoAction& P : Store())
        if (P.Id == A.Id)
        {
            if (!A.Title.IsEmpty()) P.Title = A.Title;
            if (A.Group != TEXT("other")) P.Group = A.Group;
            if (!A.Key.IsEmpty()) P.Key = A.Key; if (!A.Icon.IsEmpty()) P.Icon = A.Icon;
            if (!A.Detail.IsEmpty()) P.Detail = A.Detail; if (!A.Fill.IsEmpty()) P.Fill = A.Fill;
            for (const FString& K : A.Keywords) P.Keywords.AddUnique(K);
            if (A.Run) P.Run = A.Run; if (A.When) P.When = A.When;
            return;
        }
    if (A.Title.IsEmpty() || (!A.Run && A.Fill.IsEmpty())) return;
    Store().Add(A);
}
void Unregister(const FString& Id) { Store().RemoveAll([&Id](const FEidoAction& A) { return A.Id == Id; }); }
void Clear() { Store().Reset(); }

int32 Score(const FString& Query, const FEidoAction& A)
{
    const FString Q = Query.TrimStartAndEnd().ToLower();
    if (Q.IsEmpty()) return 1;
    int32 S = Tier(Q, A.Title);
    for (const FString& K : A.Keywords) { const int32 T = Tier(Q, K); if (T >= 650) S = FMath::Max(S, T - 100); }
    if (!A.Key.IsEmpty() && A.Key.ToLower() == Q) S = FMath::Max(S, 850);
    return S;
}

TArray<TPair<const FEidoAction*, int32>> List(const FString& Query, int32 Limit)
{
    struct FR { const FEidoAction* A; int32 S, Order; };
    TArray<FR> Out; int32 Order = 0;
    for (const FEidoAction& A : Store()) { ++Order; if (!Visible(A)) continue; const int32 S = Score(Query, A); if (S > 0) Out.Add({&A, S, Order}); }
    Out.StableSort([](const FR& X, const FR& Y) { return X.S != Y.S ? X.S > Y.S : X.A->Title.Len() != Y.A->Title.Len() ? X.A->Title.Len() < Y.A->Title.Len() : X.Order < Y.Order; });
    TArray<TPair<const FEidoAction*, int32>> R;
    for (int32 I = 0; I < Out.Num() && I < Limit; ++I) R.Emplace(Out[I].A, Out[I].S);
    return R;
}

bool Run(const FString& Id)
{
    for (const FEidoAction& A : Store()) if (A.Id == Id) { if (!Visible(A) || !A.Run) return false; A.Run(); return true; }
    return false;
}
}
