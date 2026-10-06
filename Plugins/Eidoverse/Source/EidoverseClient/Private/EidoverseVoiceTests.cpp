#include "EidoverseVoice.h"
#include "Misc/AutomationTest.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "WebSocketsModule.h"
#include "IWebSocket.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformProcess.h"
#include "Misc/Paths.h"
#include "Tests/AutomationCommon.h"
#include "Sound/SoundWaveProcedural.h"

// End to end against a LOCAL Eidoverse server running VOICE_TRANSPORT=sfu (default
// ws://127.0.0.1:8994/ws, world water-preview; override with EIDO_VOICE_TEST_URL / _WORLD).
// Tools/eidoverse/voice_bot.ts joins as "voicebot" and publishes a 440 Hz tone; this test joins
// as "unrealvoice" over a raw WebSocket, consents to receive, drives FEidoVoice, and requires
//   1. the voicebot speaker track with a decoded level > 0.05, and
//   2. the reverse path: a 660 Hz test tone published natively (gate bypassed) that the bot decodes
//      at a mean level above -30 dBFS.
namespace
{
struct FVoiceLoopback
{
    TSharedPtr<IWebSocket> Ws;
    TUniquePtr<FEidoVoice> Voice;
    FProcHandle Bot; void* ReadPipe = nullptr; void* WritePipe = nullptr; FString BotOut;
    double T0 = 0, Last = 0; bool bJoined = false, bHeard = false, bReverse = false, bReverseChecked = false;
    float MaxLevel = 0, WavePeak = 0; double HeardAt = 0; FString World, ReverseReport;
    TArray<FString> Speakers;
    void Close()
    {
        if (Voice) { Voice->Stop(); Voice.Reset(); }
        if (Ws) { Ws->OnMessage().Clear(); Ws->Close(); Ws.Reset(); }
        if (Bot.IsValid()) { if (FPlatformProcess::IsProcRunning(Bot)) FPlatformProcess::TerminateProc(Bot, true); FPlatformProcess::CloseProc(Bot); }
        if (ReadPipe || WritePipe) { FPlatformProcess::ClosePipe(ReadPipe, WritePipe); ReadPipe = WritePipe = nullptr; }
    }
};
FString ToJson(const TSharedRef<FJsonObject>& O)
{
    FString S; const TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> W = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&S);
    FJsonSerializer::Serialize(O, W); return S;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEidoverseVoiceLoopback, "Eidoverse.VoiceLoopback", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FEidoverseVoiceLoopback::RunTest(const FString&)
{
    // pure math first: the gate threshold mapping and the rolloff helpers
    TestEqual(TEXT("rolloff full at 3 m"), FEidoVoice::TargetVolume(3, false, 1), 1.f);
    TestEqual(TEXT("rolloff silent at 20 m"), FEidoVoice::TargetVolume(20, false, 1), 0.f);
    TestTrue(TEXT("rolloff halfway"), FMath::IsNearlyEqual(FEidoVoice::TargetVolume(11.5f, false, .8f), .4f, 1e-4f));
    TestEqual(TEXT("hush"), FEidoVoice::TargetVolume(0, true, 1), 0.f);
    TestTrue(TEXT("60 ms step = 60/700"), FMath::IsNearlyEqual(FEidoVoice::StepVolume(0, 1, .06f), .06f / .7f, 1e-5f));
    {
        FEidoVoice Probe([](TSharedRef<FJsonObject>) {});
        TestTrue(TEXT("default threshold -24 dBFS"), FMath::IsNearlyEqual(Probe.GetGateThresholdDb(), -24.f, .01f));
        Probe.SetMicSensitivity(0); TestTrue(TEXT("floor 0 = -60 dB"), FMath::IsNearlyEqual(Probe.GetGateThresholdDb(), -60.f, .01f));
    }

    const FString Url = FPlatformMisc::GetEnvironmentVariable(TEXT("EIDO_VOICE_TEST_URL")).IsEmpty() ? TEXT("ws://127.0.0.1:8994/ws") : FPlatformMisc::GetEnvironmentVariable(TEXT("EIDO_VOICE_TEST_URL"));
    const FString WorldName = FPlatformMisc::GetEnvironmentVariable(TEXT("EIDO_VOICE_TEST_WORLD")).IsEmpty() ? TEXT("water-preview") : FPlatformMisc::GetEnvironmentVariable(TEXT("EIDO_VOICE_TEST_WORLD"));
    FString BunPath = FPlatformMisc::GetEnvironmentVariable(TEXT("BUN"));
    if (BunPath.IsEmpty()) BunPath = FPaths::Combine(FPlatformMisc::GetEnvironmentVariable(TEXT("HOME")), TEXT(".bun/bin/bun"));
    const FString Script = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectDir(), TEXT("Tools/eidoverse/voice_bot.ts")));
    if (!FPaths::FileExists(BunPath) || !FPaths::FileExists(Script)) { AddError(FString::Printf(TEXT("need bun (%s) and %s"), *BunPath, *Script)); return false; }

    TSharedPtr<FVoiceLoopback> S = MakeShared<FVoiceLoopback>();
    S->World = WorldName;
    FPlatformProcess::CreatePipe(S->ReadPipe, S->WritePipe);
    const FString Args = FString::Printf(TEXT("\"%s\" %s %s voicebot 32 --tone=440"), *Script, *Url, *WorldName);
    S->Bot = FPlatformProcess::CreateProc(*BunPath, *Args, false, true, true, nullptr, 0, nullptr, S->WritePipe);
    if (!S->Bot.IsValid()) { AddError(TEXT("could not launch voice_bot.ts")); S->Close(); return false; }

    FModuleManager::LoadModuleChecked<FWebSocketsModule>(TEXT("WebSockets"));
    S->Ws = FWebSocketsModule::Get().CreateWebSocket(Url, TEXT(""));
    TWeakPtr<FVoiceLoopback> W = S;
    S->Voice = MakeUnique<FEidoVoice>([W](TSharedRef<FJsonObject> O) { if (auto P = W.Pin()) if (P->Ws && P->Ws->IsConnected()) P->Ws->Send(ToJson(O)); });
    S->Voice->OnSpeakerAdded = [W](const FString& Id, USoundWaveProcedural*) { if (auto P = W.Pin()) P->Speakers.AddUnique(Id); };
    S->Voice->OnNotice = [](const FString& L) { UE_LOG(LogTemp, Display, TEXT("[voice-test] notice: %s"), *L); };
    S->Ws->OnConnected().AddLambda([W] {
        auto P = W.Pin(); if (!P) return;
        TSharedRef<FJsonObject> J = MakeShared<FJsonObject>();
        J->SetStringField(TEXT("type"), TEXT("join")); J->SetStringField(TEXT("world"), P->World); J->SetStringField(TEXT("id"), TEXT("unrealvoice"));
        J->SetStringField(TEXT("avatar"), TEXT("eidoverse/assets/vrms/shino.vrm"));
        P->Ws->Send(ToJson(J));
    });
    S->Ws->OnConnectionError().AddLambda([](const FString& E) { UE_LOG(LogTemp, Error, TEXT("[voice-test] socket error: %s"), *E); });
    S->Ws->OnMessage().AddLambda([W](const FString& Text) {
        auto P = W.Pin(); if (!P || !P->Voice) return;
        TSharedPtr<FJsonObject> M; if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), M) || !M) return;
        const FString Type = M->GetStringField(TEXT("type"));
        if (Type == TEXT("snapshot") && !P->bJoined)
        {
            P->bJoined = true;
            P->Voice->SetReceiveConsent(true);
            P->Voice->SetListenerPosition(FVector(0, 0, 3));
            P->Voice->SetTestTone(660.f, .5f);
            P->Voice->Start();
        }
        static const TSet<FString> VoiceTypes = { TEXT("relay-cred"), TEXT("sfu-offer"), TEXT("sfu-route"), TEXT("sfu-ice"), TEXT("surface-transition"), TEXT("voice-service"), TEXT("voice-consent"), TEXT("error") };
        if (VoiceTypes.Contains(Type)) P->Voice->OnSignal(M);
    });
    S->T0 = S->Last = FPlatformTime::Seconds();
    S->Ws->Connect();

    ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this, S]() -> bool {
        const double Now = FPlatformTime::Seconds();
        if (S->Voice) S->Voice->Tick(float(Now - S->Last));
        S->Last = Now;
        S->BotOut += FPlatformProcess::ReadPipe(S->ReadPipe);
        if (S->Voice)
        {
            const float L = S->Voice->GetSpeakerLevel(TEXT("voicebot"));
            S->MaxLevel = FMath::Max(S->MaxLevel, L);
            if (S->bHeard && S->WavePeak <= 0 && Now - S->HeardAt > .5)
            {   // the playback half: what the audio render thread would pull from the speaker's wave
                if (USoundWaveProcedural* Wave = S->Voice->GetSpeakerWaves().FindRef(TEXT("voicebot")))
                {
                    TArray<int16> Pcm; Pcm.SetNumZeroed(1024);
                    for (int32 K = 0; K < 4; ++K)
                    {
                        Wave->GeneratePCMData(reinterpret_cast<uint8*>(Pcm.GetData()), Pcm.Num());
                        for (int16 V : Pcm) S->WavePeak = FMath::Max(S->WavePeak, FMath::Abs(float(V)) / 32768.f);
                    }
                    S->WavePeak = FMath::Max(S->WavePeak, 1e-6f);
                }
            }
            if (L > .05f && !S->bHeard) { S->bHeard = true; S->HeardAt = Now; UE_LOG(LogTemp, Display, TEXT("[voice-test] heard voicebot at level %.3f after %.1fs — %s"), L, Now - S->T0, *S->Voice->DescribeState()); }
        }
        const bool bBotRunning = FPlatformProcess::IsProcRunning(S->Bot);
        if (!bBotRunning) S->BotOut += FPlatformProcess::ReadPipe(S->ReadPipe);
        if (!S->bReverseChecked && S->BotOut.Contains(TEXT("{\"heard\":")))
        {
            FString Line; for (const FString& Ln : [&] { TArray<FString> A; S->BotOut.ParseIntoArrayLines(A); return A; }()) if (Ln.StartsWith(TEXT("{\"heard\":"))) Line = Ln;
            TSharedPtr<FJsonObject> J; const TSharedPtr<FJsonObject>* H; const TSharedPtr<FJsonObject>* U;
            if (!Line.IsEmpty() && FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Line), J) && J && J->TryGetObjectField(TEXT("heard"), H) && (*H)->TryGetObjectField(TEXT("unrealvoice"), U))
            {
                double MeanDb = -200, Packets = 0; (*U)->TryGetNumberField(TEXT("meanDb"), MeanDb); (*U)->TryGetNumberField(TEXT("packets"), Packets);
                S->bReverse = MeanDb > -30; S->ReverseReport = FString::Printf(TEXT("bot decoded unrealvoice: %d packets, mean %.1f dBFS"), int32(Packets), MeanDb);
                S->bReverseChecked = true;
            }
            else if (!bBotRunning) { S->bReverseChecked = true; S->ReverseReport = TEXT("bot never heard unrealvoice"); }
        }
        const bool bTimeout = Now - S->T0 > 50;
        if ((S->bReverseChecked && !bBotRunning) || bTimeout)
        {
            UE_LOG(LogTemp, Display, TEXT("[voice-test] bot output:\n%s"), *S->BotOut.Right(3000));
            TestTrue(TEXT("joined the world"), S->bJoined);
            TestTrue(FString::Printf(TEXT("speaker track for voicebot (speakers: %s)"), *FString::Join(S->Speakers, TEXT(","))), S->Speakers.Contains(TEXT("voicebot")));
            TestTrue(FString::Printf(TEXT("voicebot level > 0.05 (max %.3f)"), S->MaxLevel), S->MaxLevel > .05f);
            TestTrue(FString::Printf(TEXT("speaker wave yields PCM (peak %.3f)"), S->WavePeak), S->WavePeak > .05f);
            TestTrue(FString::Printf(TEXT("reverse path: %s"), *S->ReverseReport), S->bReverse);
            AddInfo(FString::Printf(TEXT("voicebot max level %.3f, wave peak %.3f; %s"), S->MaxLevel, S->WavePeak, *S->ReverseReport));
            if (S->Voice) AddInfo(S->Voice->DescribeState());
            if (bTimeout) AddError(TEXT("timed out"));
            S->Close();
            return true;
        }
        return false;
    }));
    return true;
}
#endif
