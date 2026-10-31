// Starts Plugins/Eidoverse/Bridge (packaged: Binaries/<Platform>/Bridge/eido-bridge; a development tree: bun Bridge/src/main.ts).
// Handshake: a random token on its stdin, {"port": P} back on stdout, then ws://127.0.0.1:P/ws with the subprotocol
// eido-bridge.<token>. It exits when its stdin closes, so it never outlives this process (Bridge/README.md).
#include "EidoverseSidecar.h"
#include "IWebSocket.h"
#include "WebSocketsModule.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/Guid.h"
#include "Misc/Paths.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace
{
    const TCHAR* BridgeName = TEXT("eido-bridge");
    const TCHAR* Subprotocol = TEXT("eido-bridge.");
    // 64 hex characters from four random GUIDs (the bridge takes 32-256 of [A-Za-z0-9_-])
    FString NewToken()
    {
        FString T;
        for (int32 I = 0; I < 4; ++I) T += FGuid::NewGuid().ToString(EGuidFormats::Digits).Left(16);
        return T.ToLower();
    }
    FString Quote(const FString& S) { return TEXT("\"") + S + TEXT("\""); }
}

TArray<FEidoverseSidecar::FCommand> FEidoverseSidecar::Candidates() const
{
    TArray<FCommand> List;
    const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("Eidoverse"));
    if (!Plugin) return List;
    const FString Base = FPaths::ConvertRelativePathToFull(Plugin->GetBaseDir());
#if PLATFORM_WINDOWS
    const FString Ext = TEXT(".exe");
#else
    const FString Ext;
#endif
    // EIDOVERSE_BRIDGE=dev: the sources first (an unsigned local executable may be refused by the OS)
    const bool bDevFirst = FPlatformMisc::GetEnvironmentVariable(TEXT("EIDOVERSE_BRIDGE")) == TEXT("dev");
    FCommand Packaged{ Base / TEXT("Binaries") / FPlatformProcess::GetBinariesSubdirectory() / TEXT("Bridge") / (FString(BridgeName) + Ext), FString() };
    FString Bun = FPlatformMisc::GetEnvironmentVariable(TEXT("BUN"));
    if (Bun.IsEmpty())
    {
        FString Home = FPlatformMisc::GetEnvironmentVariable(TEXT("HOME"));
        if (Home.IsEmpty()) Home = FPlatformMisc::GetEnvironmentVariable(TEXT("USERPROFILE"));
        Bun = Home / TEXT(".bun/bin/bun") + Ext;
    }
    const FString Script = Base / TEXT("Bridge/src/main.ts");
    FCommand Dev{ Bun, Quote(Script) };
    const bool bPackaged = FPaths::FileExists(Packaged.Exe), bDev = FPaths::FileExists(Bun) && FPaths::FileExists(Script);
    if (bDevFirst && bDev) List.Add(Dev);
    if (bPackaged) List.Add(Packaged);
    if (!bDevFirst && bDev) List.Add(Dev);
    return List;
}

bool FEidoverseSidecar::IsRunning() const { return Proc.IsValid() && FPlatformProcess::IsProcRunning(const_cast<FProcHandle&>(Proc)); }

bool FEidoverseSidecar::Ensure(FString& OutError)
{
    if (IsRunning()) return true;
    Close();
    Pending = Candidates();
    if (Pending.IsEmpty()) { OutError = TEXT("no local bridge: build it (Plugins/Eidoverse/Bridge/build.sh), or install bun for the development bridge"); return false; }
    while (!Pending.IsEmpty())
    {
        const FCommand C = Pending[0]; Pending.RemoveAt(0);
        if (Launch(C, OutError)) return true;
    }
    return false;
}

bool FEidoverseSidecar::Launch(const FCommand& C, FString& OutError)
{
    Close();
    FPlatformProcess::CreatePipe(StdoutRead, StdoutWrite);
    FPlatformProcess::CreatePipe(StdinRead, StdinWrite, true);
    uint32 Pid = 0;
    const FString Dir = FPaths::GetPath(C.Args.IsEmpty() ? C.Exe : C.Args.TrimQuotes());
    Proc = FPlatformProcess::CreateProc(*C.Exe, *C.Args, false, true, true, &Pid, 0, *Dir, StdoutWrite, StdinRead);
    if (!Proc.IsValid())
    {
        OutError = FString::Printf(TEXT("couldn't start %s"), *C.Exe); Close();
        UE_LOG(LogTemp, Warning, TEXT("EIDOVERSE bridge: %s (the OS refused it, or it isn't executable)"), *OutError);
        return false;
    }
    Token = NewToken();
    const FTCHARToUTF8 Line(*(Token + TEXT("\n")));
    int32 Wrote = 0;
    FPlatformProcess::WritePipe(StdinWrite, reinterpret_cast<const uint8*>(Line.Get()), Line.Length(), &Wrote);
    Running = C.Args.IsEmpty() ? C.Exe : C.Exe + TEXT(" ") + C.Args;
    LaunchedAt = FPlatformTime::Seconds(); ++Launches;
    UE_LOG(LogTemp, Display, TEXT("EIDOVERSE bridge starting: %s (pid %u)"), *Running, Pid);
    return true;
}

bool FEidoverseSidecar::Ready()
{
    if (!Proc.IsValid()) return false;
    if (!IsRunning())
    {
        Out += FPlatformProcess::ReadPipe(StdoutRead);
        UE_LOG(LogTemp, Warning, TEXT("EIDOVERSE bridge exited: %s%s"), *Running, Port ? TEXT("") : TEXT(" (before saying its port)"));
        FString Ignored; if (!Port && !Pending.IsEmpty()) { const FCommand C = Pending[0]; Pending.RemoveAt(0); Launch(C, Ignored); }
        else Close();
        return false;
    }
    if (Port) return true;
    Out += FPlatformProcess::ReadPipe(StdoutRead);
    // the port line may follow a runtime's own warning on the shared pipe (bun on Linux: JSC's signal-30 note); say those, keep reading
    int32 Nl;
    while (Out.FindChar('\n', Nl))
    {
        const FString Line = Out.Left(Nl).TrimStartAndEnd(); Out.RightChopInline(Nl + 1);
        TSharedPtr<FJsonObject> J; double P = 0;
        if (Line.StartsWith(TEXT("{")) && FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Line), J) && J && J->TryGetNumberField(TEXT("port"), P) && P > 0 && P < 65536)
        {
            Port = int32(P);
            UE_LOG(LogTemp, Display, TEXT("EIDOVERSE bridge ready on 127.0.0.1:%d (%.1f s)"), Port, FPlatformTime::Seconds() - LaunchedAt);
            return true;
        }
        if (!Line.IsEmpty()) UE_LOG(LogTemp, Warning, TEXT("EIDOVERSE bridge said %s"), *Line.Left(200));
    }
    if (FPlatformTime::Seconds() - LaunchedAt > 15) { UE_LOG(LogTemp, Warning, TEXT("EIDOVERSE bridge never said its port in 15 s; stopping it")); Stop(); }
    return false;
}

TSharedPtr<IWebSocket> FEidoverseSidecar::OpenSocket() const
{
    if (!Port) return nullptr;
    FModuleManager::LoadModuleChecked<FWebSocketsModule>(TEXT("WebSockets"));
    return FWebSocketsModule::Get().CreateWebSocket(FString::Printf(TEXT("ws://127.0.0.1:%d/ws"), Port), FString(Subprotocol) + Token);
}

void FEidoverseSidecar::Stop()
{
    if (Proc.IsValid())
    {
        // EOF on its stdin is its signal to exit; then a short grace before ending it
        if (StdinWrite) { FPlatformProcess::ClosePipe(nullptr, StdinWrite); StdinWrite = nullptr; }
        const double Until = FPlatformTime::Seconds() + 1.5;
        while (IsRunning() && FPlatformTime::Seconds() < Until) FPlatformProcess::Sleep(.02f);
        if (IsRunning()) FPlatformProcess::TerminateProc(Proc, true);
    }
    Close();
}

void FEidoverseSidecar::Close()
{
    if (Proc.IsValid()) FPlatformProcess::CloseProc(Proc);
    Proc.Reset();
    if (StdoutRead || StdoutWrite) FPlatformProcess::ClosePipe(StdoutRead, StdoutWrite);
    if (StdinRead || StdinWrite) FPlatformProcess::ClosePipe(StdinRead, StdinWrite);
    StdoutRead = StdoutWrite = StdinRead = StdinWrite = nullptr;
    Port = 0; Out.Empty(); Token.Empty();
}
