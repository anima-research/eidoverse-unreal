// Travel between worlds. The browser moves by URL (?world=name); a native client has none, so it reads the
// server's map (GET /worlds: every world with a log, and who is in the loaded ones) and travels by leaving one
// world and joining the next on the same server, as the same signed-in person (wire: travel = leave A, join B).
#include "EidoverseBridge.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Dom/JsonObject.h"

void AEidoverseBridge::RefreshWorlds(const FString& Server)
{
    const FString SocketUrl = Server.IsEmpty() ? Endpoint : Server.TrimStartAndEnd();
    const FString Base = HttpBaseOf(SocketUrl);
    if (Base.IsEmpty()) { WorldsStatus = TEXT("no server to ask"); return; }
    // one request at a time per server; a different server starts over
    if (bWorldsPending && WorldsServer == Base) return;
    if (WorldsServer != Base) { KnownWorlds.Reset(); ++WorldsSerial; }
    WorldsServer = Base; bWorldsPending = true; WorldsFetchedAt = FPlatformTime::Seconds();
    auto Request = FHttpModule::Get().CreateRequest();
    Request->SetURL(Base + TEXT("/worlds")); Request->SetVerb(TEXT("GET")); Request->SetTimeout(10);
    Request->SetOption(HttpRequestOptions::EnableCookies, TEXT("false"));
    // the public server's map is asked as the signed-in person, as the browser's page would be
    if (HasProductionSession() && SocketUrl == ProductionEndpoint) Request->SetHeader(TEXT("Cookie"), TEXT("ew_sess=") + ProductionSession);
    TWeakObjectPtr<AEidoverseBridge> Weak(this);
    Request->OnProcessRequestComplete().BindLambda([Weak, Base](FHttpRequestPtr, FHttpResponsePtr Response, bool bOk)
    {
        AEidoverseBridge* B = Weak.Get();
        if (!B || B->WorldsServer != Base) return;
        B->bWorldsPending = false;
        TSharedPtr<FJsonObject> J; const TArray<TSharedPtr<FJsonValue>>* List = nullptr;
        if (!bOk || !Response || Response->GetResponseCode() != 200 || Response->GetContent().Num() > 2 * 1024 * 1024
            || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Response->GetContentAsString()), J) || !J || !J->TryGetArrayField(TEXT("worlds"), List))
        { B->WorldsStatus = TEXT("couldn't read this server's worlds"); return; }
        TArray<FEidoverseWorldInfo> Worlds;
        for (const auto& V : *List)
        {
            const TSharedPtr<FJsonObject> O = V.IsValid() ? V->AsObject() : nullptr; if (!O) continue;
            FEidoverseWorldInfo W; O->TryGetStringField(TEXT("name"), W.Name);
            if (!IsWorldName(W.Name)) continue;
            O->TryGetBoolField(TEXT("loaded"), W.bLoaded);
            const TArray<TSharedPtr<FJsonValue>>* Names = nullptr;
            if (O->TryGetArrayField(TEXT("people"), Names)) for (const auto& N : *Names) if (W.People.Num() < 64) W.People.Add(N->AsString().Left(64));
            if (O->TryGetArrayField(TEXT("agents"), Names)) for (const auto& N : *Names) if (W.Agents.Num() < 64) W.Agents.Add(N->AsString().Left(64));
            Worlds.Add(MoveTemp(W));
            if (Worlds.Num() >= 500) break;
        }
        // busiest first, then by name
        Worlds.Sort([](const FEidoverseWorldInfo& A, const FEidoverseWorldInfo& C)
        {
            const int32 NA = A.People.Num() + A.Agents.Num(), NC = C.People.Num() + C.Agents.Num();
            return NA != NC ? NA > NC : A.Name < C.Name;
        });
        int32 Present = 0; for (const auto& W : Worlds) Present += W.People.Num() + W.Agents.Num();
        B->KnownWorlds = MoveTemp(Worlds); ++B->WorldsSerial;
        B->WorldsStatus = FString::Printf(TEXT("%d world%s · %d here now"), B->KnownWorlds.Num(), B->KnownWorlds.Num() == 1 ? TEXT("") : TEXT("s"), Present);
    });
    if (!Request->ProcessRequest()) { bWorldsPending = false; WorldsStatus = TEXT("couldn't ask this server"); }
}

bool AEidoverseBridge::TravelTo(const FString& World)
{
    const FString W = World.TrimStartAndEnd();
    if (!IsWorldName(W)) { ToastText(TEXT("World names are letters, numbers, - and _."), true); return false; }
    if (!bJoined && !bWanted) { ToastText(TEXT("Join a world first: travel starts from one."), true); return false; }
    if (bJoined && W == WorldName) { ToastText(FString::Printf(TEXT("You're in “%s” already."), *W)); return false; }
    Narrate(FString::Printf(TEXT("travelling to “%s”…"), *W));
    bRenderDynamicWorld = true;
    const bool bOk = SwitchWorld(W);
    if (bOk) SaveClientSettings();
    return bOk;
}
