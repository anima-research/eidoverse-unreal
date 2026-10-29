// The local bridge process (Plugins/Eidoverse/Bridge): launch, token on stdin, its port from stdout, restart, shutdown.
// The only file that knows how to start it or speak its handshake (ADAPTER-DESIGN §1.1, guard G3).
#pragma once
#include "CoreMinimal.h"
#include "HAL/PlatformProcess.h"

class IWebSocket;

class FEidoverseSidecar
{
public:
    ~FEidoverseSidecar() { Stop(); }
    /** Start the bridge unless it is running. False (and why) when none can be started. */
    bool Ensure(FString& OutError);
    /** Polled each tick: true once the bridge has said its port and is still running. */
    bool Ready();
    bool IsRunning() const;
    /** A socket to the bridge, carrying the launch token (not yet connected). Only valid once Ready(). */
    TSharedPtr<IWebSocket> OpenSocket() const;
    /** Close its stdin (it exits on EOF), then end it if it hasn't. */
    void Stop();
    /** What runs: the packaged executable, or bun on the sources (a development tree). */
    FString Describe() const { return Running; }
    int32 Port = 0;
    int32 Launches = 0;
private:
    struct FCommand { FString Exe, Args; };
    TArray<FCommand> Candidates() const;
    bool Launch(const FCommand& C, FString& OutError);
    void Close();
    FProcHandle Proc;
    void* StdinRead = nullptr; void* StdinWrite = nullptr; void* StdoutRead = nullptr; void* StdoutWrite = nullptr;
    FString Token, Out, Running;
    TArray<FCommand> Pending;   // what to try next if this one dies before saying its port
    double LaunchedAt = 0;
};
