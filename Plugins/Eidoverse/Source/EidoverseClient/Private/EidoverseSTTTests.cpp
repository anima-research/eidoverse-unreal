// STT end to end without a microphone: macOS `say` audio (Saved/EidoTest/stt_hello.aiff)
// fed through FEidoStt::Feed in 10 ms frames, as the voice pump does. Skips (with a
// warning) when speech recognition isn't authorized for this process.
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "EidoverseSTT.h"
#include "Tests/AutomationCommon.h"

bool EidoDecodeAudioBytes(const TArray<uint8>& Bytes, const FString& Ext, TArray<int16>& OutMono, int32& OutRate);

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEidoverseSttTest, "Eidoverse.SttFeed", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEidoverseSttTest::RunTest(const FString&)
{
    if (FEidoStt::Authorization() != FEidoStt::EAuth::Granted) { AddWarning(TEXT("speech recognition not authorized for this process: skipped")); return true; }
    TArray<uint8> Bytes; TArray<int16> Pcm; int32 Rate = 0;
    if (!FFileHelper::LoadFileToArray(Bytes, *FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("EidoTest/stt_hello.aiff"))) || !EidoDecodeAudioBytes(Bytes, TEXT("aiff"), Pcm, Rate))
    { AddError(TEXT("no test audio (say -o Saved/EidoTest/stt_hello.aiff ...)")); return false; }
    TSharedPtr<FEidoStt> Stt = MakeShared<FEidoStt>();
    FString Err; if (!Stt->Start(Err)) { AddError(Err); return false; }
    TSharedPtr<TArray<FString>> Heard = MakeShared<TArray<FString>>();
    TSharedPtr<int32> Fed = MakeShared<int32>(0);
    const double T0 = FPlatformTime::Seconds();
    ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this, Stt, Pcm, Rate, Heard, Fed, T0]() -> bool
    {
        // Real time: feed what the clock says should have arrived, then a second of silence.
        const int32 Due = int32((FPlatformTime::Seconds() - T0) * Rate);
        TArray<float> F;
        while (*Fed < Due && *Fed < Pcm.Num() + Rate * 2)
        {
            const int32 N = FMath::Min(Rate / 100, Due - *Fed);
            F.SetNumUninitialized(N);
            for (int32 I = 0; I < N; ++I) F[I] = (*Fed + I) < Pcm.Num() ? Pcm[*Fed + I] / 32768.f : 0.f;
            Stt->Feed(F.GetData(), N, Rate); *Fed += N;
        }
        TArray<FString> Finals; FString E; Stt->Poll(Finals, E);
        Heard->Append(Finals);
        if (!E.IsEmpty()) AddInfo(TEXT("recognizer: ") + E);
        if (Heard->Num() || FPlatformTime::Seconds() - T0 > 20)
        {
            Stt->Stop();
            AddInfo(FString::Printf(TEXT("heard [%s] on-device=%d"), *FString::Join(*Heard, TEXT(" | ")), Stt->IsOnDevice()));
            TestTrue(TEXT("recognized something"), Heard->Num() > 0 && FString::Join(*Heard, TEXT(" ")).Contains(TEXT("test"), ESearchCase::IgnoreCase));
            return true;
        }
        return false;
    }));
    return true;
}
