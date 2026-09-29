#include "EidoverseLoadTrace.h"
#include "HAL/IConsoleManager.h"
#include "RHI.h"
#include "RenderTimer.h"

namespace
{
    struct FStage { int32 Count = 0; double Total = 0, Max = 0; };
    TMap<FString, FStage> Stages;
    TArray<FString> ThisFrame;
    int32 Hitches = 0;
    double WorstFrame = 0;

    TAutoConsoleVariable<float> CVarHitchMs(TEXT("Eidoverse.LoadTrace.HitchMs"), 100.f,
        TEXT("Frames longer than this log EIDOVERSE_HITCH with the load stages that ran in them (0 = off)."));

    FAutoConsoleCommand CmdStats(TEXT("Eidoverse.LoadStats"), TEXT("Log per-stage asset loading costs since the last reset."),
        FConsoleCommandDelegate::CreateLambda([] { UE_LOG(LogTemp, Warning, TEXT("EIDOVERSE_LOADSTATS %s"), *EidoLoadTrace::Summary()); }));
    FAutoConsoleCommand CmdReset(TEXT("Eidoverse.LoadStatsReset"), TEXT("Reset the asset loading statistics."),
        FConsoleCommandDelegate::CreateLambda([] { EidoLoadTrace::Reset(); }));
}

void EidoLoadTrace::Note(const TCHAR* Stage, const FString& What, double Ms)
{
    check(IsInGameThread());
    FStage& S = Stages.FindOrAdd(Stage);
    ++S.Count; S.Total += Ms; S.Max = FMath::Max(S.Max, Ms);
    if (Ms >= 1.0) ThisFrame.Add(FString::Printf(TEXT("%s %.0fms %s"), Stage, Ms, *What.Right(60)));
}

void EidoLoadTrace::Tick(float DeltaSeconds)
{
    const double Ms = DeltaSeconds * 1000.0;
    const float Threshold = CVarHitchMs.GetValueOnGameThread();
    if (Threshold > 0 && Ms > Threshold)
    {
        ++Hitches; WorstFrame = FMath::Max(WorstFrame, Ms);
        const double Gpu = FPlatformTime::ToMilliseconds(RHIGetGPUFrameCycles());
        UE_LOG(LogTemp, Warning, TEXT("EIDOVERSE_HITCH frame=%.0fms gpu=%.0fms game=%.0fms render=%.0fms | %s"), Ms, Gpu,
            FPlatformTime::ToMilliseconds(GGameThreadTime), FPlatformTime::ToMilliseconds(GRenderThreadTime),
            ThisFrame.IsEmpty() ? TEXT("(no load stages)") : *FString::Join(ThisFrame, TEXT("; ")));
    }
    ThisFrame.Reset();
}

void EidoLoadTrace::Reset()
{
    Stages.Reset(); ThisFrame.Reset(); Hitches = 0; WorstFrame = 0;
}

FString EidoLoadTrace::Summary()
{
    TArray<FString> Parts;
    TArray<FString> Keys; Stages.GetKeys(Keys); Keys.Sort();
    for (const FString& K : Keys)
    {
        const FStage& S = Stages[K];
        Parts.Add(FString::Printf(TEXT("\"%s\":{\"n\":%d,\"total_ms\":%.0f,\"max_ms\":%.0f}"), *K, S.Count, S.Total, S.Max));
    }
    return FString::Printf(TEXT("{\"hitches\":%d,\"worst_frame_ms\":%.0f,\"stages\":{%s}}"), Hitches, WorstFrame, *FString::Join(Parts, TEXT(",")));
}
