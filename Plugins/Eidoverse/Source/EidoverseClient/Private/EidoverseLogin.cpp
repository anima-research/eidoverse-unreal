#include "EidoverseBridge.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Serialization/JsonSerializer.h"
#include "Dom/JsonObject.h"
#include "HAL/PlatformProcess.h"
#include "EidoverseSecureStore.h"
#include "Misc/App.h"
#include "Misc/DateTime.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "HAL/IConsoleManager.h"
#include "EngineUtils.h"
#endif

namespace
{
const FString Production=TEXT("https://eidoverse.animalabs.ai");
bool IsHexSecret(const FString& S)
{
    if(S.Len()!=64)return false;
    for(TCHAR C:S)if(!((C>='a'&&C<='f')||(C>='0'&&C<='9')))return false;
    return true;
}
TSharedRef<IHttpRequest,ESPMode::ThreadSafe> Post(const FString& URL,const FString& Key=TEXT(""),const FString& Value=TEXT(""))
{
    auto Request=FHttpModule::Get().CreateRequest();Request->SetURL(URL);Request->SetVerb(TEXT("POST"));
    Request->SetOption(HttpRequestOptions::EnableCookies,TEXT("false"));Request->SetTimeout(15);
    Request->SetHeader(TEXT("Content-Type"),TEXT("application/json"));
    auto J=MakeShared<FJsonObject>();if(!Key.IsEmpty())J->SetStringField(Key,Value);
    FString Body;FJsonSerializer::Serialize(J,TJsonWriterFactory<>::Create(&Body));Request->SetContentAsString(Body);return Request;
}
bool ReadJSON(FHttpResponsePtr Response,TSharedPtr<FJsonObject>& Out)
{
    return Response&&Response->GetContent().Num()<=8192&&FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Response->GetContentAsString()),Out)&&Out.IsValid();
}
}
bool AEidoverseBridge::ParseProductionSession(const FString& Header,FString& Out)
{
    Out.Empty();FString Cookie,Rest;Header.Split(TEXT(";"),&Cookie,&Rest);if(Cookie.IsEmpty())Cookie=Header;
    if(!Cookie.StartsWith(TEXT("ew_sess="))||!IsHexSecret(Cookie.Mid(8)))return false;
    Out=Cookie.Mid(8);return true;
}
bool AEidoverseBridge::IsPairingCode(const FString& Code)
{
    if(Code.Len()!=11||Code[5]!='-')return false;
    for(int32 I=0;I<Code.Len();++I)if(I!=5&&!((Code[I]>='A'&&Code[I]<='F')||(Code[I]>='0'&&Code[I]<='9')))return false;
    return true;
}
void AEidoverseBridge::BeginProductionLogin()
{
    LogoutProduction();LoginService=Production;BeginLogin();
}
void AEidoverseBridge::BeginLogin()
{
    bAuthenticating=true;LoginStatus=TEXT("Preparing Discord sign-in…");
    PairingExpires=FPlatformTime::Seconds()+300;
    const uint32 Attempt=++AuthGeneration;TWeakObjectPtr<AEidoverseBridge> Weak(this);
    AuthRequest=Post(LoginService+TEXT("/native/start"));
    AuthRequest->OnProcessRequestComplete().BindLambda([Weak,Attempt](FHttpRequestPtr Request,FHttpResponsePtr Response,bool OK)
    {
        Request->SetContent(TArray<uint8>());
        if(!Weak.IsValid()||Weak->AuthGeneration!=Attempt)return;
        auto& B=*Weak.Get();B.AuthRequest.Reset();TSharedPtr<FJsonObject> J;
        if(!OK||!Response||Response->GetResponseCode()!=200||Response->GetEffectiveURL()!=B.LoginService+TEXT("/native/start")||!ReadJSON(Response,J))
        {
            B.FinishLoginAttempt(Response&&(Response->GetResponseCode()==404||Response->GetResponseCode()==503)?
                TEXT("Discord sign-in needs the Eidoverse server pairing update. No token is required from you."):
                TEXT("Could not start Discord sign-in. Check your connection and try again."));return;
        }
        FString Secret,Code;double Expiry=0;
        if(!J->TryGetStringField(TEXT("device_code"),Secret)||!IsHexSecret(Secret)||!J->TryGetStringField(TEXT("user_code"),Code)||!IsPairingCode(Code)||
            !J->TryGetNumberField(TEXT("expires_in"),Expiry)||!FMath::IsFinite(Expiry)||Expiry<1||Expiry>300)
        {B.FinishLoginAttempt(TEXT("Unexpected sign-in response. Please try again later."));return;}
        B.DeviceCode=Secret;B.PairingCode=Code;B.PairingExpires=FPlatformTime::Seconds()+Expiry;B.NextAuthPoll=FPlatformTime::Seconds()+2;
        B.LoginStatus=TEXT("Code: ")+Code+TEXT(" — sign in with Discord in your browser and approve only this matching code. Expires in 5 minutes.");
        B.OpenProductionLoginBrowser();
    });
    if(!AuthRequest->ProcessRequest())FinishLoginAttempt(TEXT("Could not start sign-in. Check the network."));
}
void AEidoverseBridge::OpenProductionLoginBrowser()
{
    if(bAuthenticating&&IsPairingCode(PairingCode)&&LoginService==Production)
        FPlatformProcess::LaunchURL(*(Production+TEXT("/native#code=")+PairingCode),nullptr,nullptr);
}
void AEidoverseBridge::FinishLoginAttempt(const FString& Message)
{
    ++AuthGeneration;
    if(AuthRequest){AuthRequest->OnProcessRequestComplete().Unbind();AuthRequest->CancelRequest();AuthRequest->SetContent(TArray<uint8>());AuthRequest.Reset();}
    bAuthenticating=false;DeviceCode.Empty();PairingCode.Empty();PairingExpires=NextAuthPoll=0;LoginStatus=Message;
}
void AEidoverseBridge::TickProductionLogin(double Now)
{
    if(!bAuthenticating)return;
    if(Now>=PairingExpires){LogoutProduction();LoginStatus=TEXT("Sign-in expired. Click Sign in with Discord to start again.");return;}
    if(AuthRequest||DeviceCode.IsEmpty()||Now<NextAuthPoll)return;
    const uint32 Attempt=AuthGeneration;TWeakObjectPtr<AEidoverseBridge> Weak(this);
    AuthRequest=Post(LoginService+TEXT("/native/poll"),TEXT("device_code"),DeviceCode);
    AuthRequest->OnProcessRequestComplete().BindLambda([Weak,Attempt](FHttpRequestPtr Request,FHttpResponsePtr Response,bool OK)
    {
        Request->SetContent(TArray<uint8>());
        if(!Weak.IsValid()||Weak->AuthGeneration!=Attempt)return;
        auto& B=*Weak.Get();B.AuthRequest.Reset();B.NextAuthPoll=FPlatformTime::Seconds()+2;
        if(!OK||!Response){B.NextAuthPoll+=3;return;}
        if(Response->GetEffectiveURL()!=B.LoginService+TEXT("/native/poll")){B.FinishLoginAttempt(TEXT("Unexpected sign-in redirect. Please try again later."));return;}
        const int32 Status=Response->GetResponseCode();
        if(Status==202)return;
        if(Status==429||Status>=500){B.NextAuthPoll+=3;return;}
        if(Status!=200){B.FinishLoginAttempt(TEXT("Sign-in was denied, expired, or cancelled. Start again with Discord."));return;}
        TSharedPtr<FJsonObject> J;FString Session,Name;double Expiry=0;
        if(!ReadJSON(Response,J)||!ParseProductionSession(Response->GetHeader(TEXT("Set-Cookie")),Session)||
            !J->TryGetStringField(TEXT("name"),Name)||Name.IsEmpty()||Name.Len()>256||
            !J->TryGetNumberField(TEXT("expires_in"),Expiry)||!FMath::IsFinite(Expiry)||Expiry<1||Expiry>43200)
        {B.FinishLoginAttempt(TEXT("Invalid sign-in response. Please start again."));return;}
        B.ProductionSession=Session;B.ProductionSessionExpires=FPlatformTime::Seconds()+Expiry;
        B.LoginWorldScope=TEXT("water");J->TryGetStringField(TEXT("world_scope"),B.LoginWorldScope);
        B.LoginName=Name.Left(64);B.SaveProductionSession();B.FinishLoginAttempt(TEXT("Signed in as ")+B.LoginName+(B.LoginWorldScope==TEXT("*")?TEXT(". All accessible worlds approved."):TEXT(". Water-only session.")));
    });
    if(!AuthRequest->ProcessRequest()){AuthRequest.Reset();NextAuthPoll=Now+5;}
}
void AEidoverseBridge::LogoutProduction()
{
    if(!DeviceCode.IsEmpty())Post(LoginService+TEXT("/native/cancel"),TEXT("device_code"),DeviceCode)->ProcessRequest();
    FinishLoginAttempt(TEXT("Signed out. Session credentials are not saved."));
    if(Endpoint==TEXT("wss://eidoverse.animalabs.ai/ws"))Disconnect();
    if(!ProductionSession.IsEmpty())
    {
        auto Request=Post(LoginService+TEXT("/logout"));Request->SetHeader(TEXT("Cookie"),TEXT("ew_sess=")+ProductionSession);Request->ProcessRequest();
    }
    ProductionSession.Empty();ProductionSessionExpires=0;LoginName.Empty();LoginWorldScope.Empty();
    if(!FApp::IsUnattended())EidoSecureStore::Delete(TEXT("production"));
}
void AEidoverseBridge::JoinProduction()
{
    if(!HasProductionSession()||LoginService!=Production||FPlatformTime::Seconds()>=ProductionSessionExpires)
    {LoginStatus=TEXT("Sign in before joining production.");return;}
    Endpoint=TEXT("wss://eidoverse.animalabs.ai/ws");ParticipantName=LoginName;
    AvatarLibraryBase=TEXT("https://eidoverse.animalabs.ai/library/");
    JoinToken.Empty();IdentityToken.Empty();SwitchWorld(WorldName);
}
#if WITH_DEV_AUTOMATION_TESTS
void AEidoverseBridge::BeginLocalLoginTest()
{
    LogoutProduction();LoginService=TEXT("http://127.0.0.1:18996");BeginLogin();
}
static FAutoConsoleCommandWithWorld NativeLoginProbe(TEXT("Eidoverse.TestDiscordPairing"),TEXT("Test pairing against loopback fixture only; never opens Discord or joins production."),
    FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* World)
    {
        if(!World||!World->IsGameWorld())return;
        TActorIterator<AEidoverseBridge> It(World);if(It){It->OpenChat();It->BeginLocalLoginTest();}
    }));
#endif

// The production sign-in survives restarts in the Keychain (EidoverseSecureStore):
// the session cookie, its expiry (Unix seconds), display name and approved worlds.
// Native sessions last at most 12 hours; a restored one is checked with /whoami
// before use, and anything the server no longer accepts is deleted.
void AEidoverseBridge::SaveProductionSession() const
{
    if(FApp::IsUnattended()||ProductionSession.IsEmpty())return;
    auto J=MakeShared<FJsonObject>();
    J->SetStringField(TEXT("session"),ProductionSession);
    J->SetNumberField(TEXT("expires"),double(FDateTime::UtcNow().ToUnixTimestamp())+(ProductionSessionExpires-FPlatformTime::Seconds()));
    J->SetStringField(TEXT("name"),LoginName);
    J->SetStringField(TEXT("scope"),LoginWorldScope);
    FString Text;FJsonSerializer::Serialize(J,TJsonWriterFactory<TCHAR,TCondensedJsonPrintPolicy<TCHAR>>::Create(&Text));
    EidoSecureStore::Save(TEXT("production"),Text);
}
void AEidoverseBridge::RestoreProductionSession()
{
    if(FApp::IsUnattended()||HasProductionSession()||bAuthenticating)return;
    FString Text;if(!EidoSecureStore::Load(TEXT("production"),Text))return;
    TSharedPtr<FJsonObject> J;FString Session,Name,Scope;double Expires=0;
    if(!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text),J)||!J.IsValid()||!J->TryGetStringField(TEXT("session"),Session)||
        !ParseProductionSession(TEXT("ew_sess=")+Session,Session)||!J->TryGetNumberField(TEXT("expires"),Expires))
    {EidoSecureStore::Delete(TEXT("production"));return;}
    J->TryGetStringField(TEXT("name"),Name);J->TryGetStringField(TEXT("scope"),Scope);
    const double Remaining=Expires-double(FDateTime::UtcNow().ToUnixTimestamp());
    if(Remaining<60){EidoSecureStore::Delete(TEXT("production"));LoginStatus=TEXT("Your saved sign-in expired. Sign in with Discord again.");return;}
    LoginStatus=TEXT("Checking your saved sign-in\u2026");
    auto Request=FHttpModule::Get().CreateRequest();Request->SetURL(LoginService+TEXT("/whoami"));Request->SetVerb(TEXT("GET"));
    Request->SetOption(HttpRequestOptions::EnableCookies,TEXT("false"));Request->SetTimeout(15);
    Request->SetHeader(TEXT("Cookie"),TEXT("ew_sess=")+Session);
    TWeakObjectPtr<AEidoverseBridge> Weak(this);
    Request->OnProcessRequestComplete().BindLambda([Weak,Session,Name,Scope,Remaining](FHttpRequestPtr,FHttpResponsePtr Response,bool bOk)
    {
        AEidoverseBridge* B=Weak.Get();if(!B||B->HasProductionSession()||B->bAuthenticating)return;
        if(!bOk||!Response){B->LoginStatus=TEXT("Couldn't check your saved sign-in (offline?). Sign in with Discord, or try again later.");return;}
        if(Response->GetResponseCode()!=200)
        {EidoSecureStore::Delete(TEXT("production"));B->LoginStatus=TEXT("Your saved sign-in is no longer valid. Sign in with Discord again.");return;}
        TSharedPtr<FJsonObject> Who;FString WhoName;
        if(ReadJSON(Response,Who))Who->TryGetStringField(TEXT("name"),WhoName);
        B->ProductionSession=Session;B->ProductionSessionExpires=FPlatformTime::Seconds()+Remaining;
        B->LoginName=(WhoName.IsEmpty()?Name:WhoName).Left(64);B->LoginWorldScope=Scope.IsEmpty()?TEXT("water"):Scope;
        B->LoginStatus=TEXT("Signed in as ")+B->LoginName+TEXT(" (saved sign-in).");
    });
    Request->ProcessRequest();
}
