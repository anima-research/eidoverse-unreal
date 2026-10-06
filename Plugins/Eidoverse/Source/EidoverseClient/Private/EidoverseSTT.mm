#include "EidoverseSTT.h"
#if PLATFORM_MAC
#include "Mac/MacSystemIncludes.h"
#pragma push_macro("FVector")
#define FVector FVectorWorkaround
#import <AVFoundation/AVFoundation.h>
#import <Speech/Speech.h>
#pragma pop_macro("FVector")
#include "HAL/PlatformTime.h"

struct FEidoStt::FImpl
{
    FCriticalSection Cs;
    SFSpeechRecognizer* Recognizer = nil;               // retained
    SFSpeechAudioBufferRecognitionRequest* Request = nil; // retained, swapped per task
    SFSpeechRecognitionTask* Task = nil;                 // retained
    FString Partial, Error; double PartialAt = 0, TaskAt = 0;
    bool bRunning = false, bTaskEnded = false, bOnDevice = false;
    int32 TaskGen = 0;
    TArray<FString> Ready;

    void EndTaskLocked()
    {
        if (Request) { [Request endAudio]; [Request release]; Request = nil; }
        if (Task) { [Task cancel]; [Task release]; Task = nil; }
    }
    bool BeginTaskLocked(FString& OutError, TSharedPtr<FImpl, ESPMode::ThreadSafe> Self)
    {
        EndTaskLocked();
        Request = [[SFSpeechAudioBufferRecognitionRequest alloc] init];
        Request.shouldReportPartialResults = YES;
        bOnDevice = Recognizer.supportsOnDeviceRecognition;
        if (bOnDevice) Request.requiresOnDeviceRecognition = YES;
        const int32 Gen = ++TaskGen;
        bTaskEnded = false; TaskAt = FPlatformTime::Seconds(); Partial.Empty();
        TWeakPtr<FImpl, ESPMode::ThreadSafe> Weak = Self;
        Task = [[Recognizer recognitionTaskWithRequest:Request resultHandler:^(SFSpeechRecognitionResult* Result, NSError* Err)
        {
            TSharedPtr<FImpl, ESPMode::ThreadSafe> I = Weak.Pin(); if (!I) return;
            FScopeLock L(&I->Cs);
            if (Gen != I->TaskGen) return;
            if (Result)
            {
                I->Partial = FString(Result.bestTranscription.formattedString);
                I->PartialAt = FPlatformTime::Seconds();
                if (Result.isFinal) { if (!I->Partial.TrimStartAndEnd().IsEmpty()) I->Ready.Add(I->Partial); I->Partial.Empty(); I->bTaskEnded = true; }
            }
            if (Err)
            {
                // 1110 "no speech detected" and cancellations are the normal end of a quiet task.
                const NSInteger Code = Err.code;
                if (Code != 1110 && Code != 216 && Code != 301) I->Error = FString(Err.localizedDescription);
                I->bTaskEnded = true;
            }
        }] retain];
        if (!Task) { OutError = TEXT("speech recognizer would not start"); return false; }
        return true;
    }
};

FEidoStt::EAuth FEidoStt::Authorization()
{
    switch ([SFSpeechRecognizer authorizationStatus])
    {
    case SFSpeechRecognizerAuthorizationStatusAuthorized: return EAuth::Granted;
    case SFSpeechRecognizerAuthorizationStatusNotDetermined: return EAuth::Unknown;
    default: return EAuth::Denied;
    }
}
void FEidoStt::RequestAuthorization(TFunction<void(bool)> Done)
{
    TFunction<void(bool)>* Heap = new TFunction<void(bool)>(MoveTemp(Done));
    [SFSpeechRecognizer requestAuthorization:^(SFSpeechRecognizerAuthorizationStatus S) { (*Heap)(S == SFSpeechRecognizerAuthorizationStatusAuthorized); delete Heap; }];
}
FEidoStt::FEidoStt() : Impl(MakeShared<FImpl, ESPMode::ThreadSafe>()) {}
FEidoStt::~FEidoStt() { Stop(); FScopeLock L(&Impl->Cs); if (Impl->Recognizer) { [Impl->Recognizer release]; Impl->Recognizer = nil; } }

bool FEidoStt::Start(FString& OutError)
{
    FScopeLock L(&Impl->Cs);
    if (Impl->bRunning) return true;
    if (!Impl->Recognizer) Impl->Recognizer = [[SFSpeechRecognizer alloc] initWithLocale:[NSLocale currentLocale]];
    if (!Impl->Recognizer) Impl->Recognizer = [[SFSpeechRecognizer alloc] init];
    if (!Impl->Recognizer || !Impl->Recognizer.isAvailable) { OutError = TEXT("speech recognition isn't available for this language"); return false; }
    if (!Impl->BeginTaskLocked(OutError, Impl)) return false;
    Impl->bRunning = true;
    return true;
}
void FEidoStt::Stop()
{
    FScopeLock L(&Impl->Cs);
    Impl->bRunning = false; ++Impl->TaskGen;
    Impl->EndTaskLocked();
}
bool FEidoStt::IsRunning() const { return Impl->bRunning; }
bool FEidoStt::IsOnDevice() const { return Impl->bOnDevice; }

void FEidoStt::Feed(const float* Mono, int32 Num, double Rate)
{
    SFSpeechAudioBufferRecognitionRequest* Req = nil;
    { FScopeLock L(&Impl->Cs); if (!Impl->bRunning || !Impl->Request) return; Req = [Impl->Request retain]; }
    AVAudioFormat* Format = [[AVAudioFormat alloc] initWithCommonFormat:AVAudioPCMFormatFloat32 sampleRate:Rate channels:1 interleaved:NO];
    AVAudioPCMBuffer* Buf = [[AVAudioPCMBuffer alloc] initWithPCMFormat:Format frameCapacity:AVAudioFrameCount(Num)];
    if (Buf)
    {
        FMemory::Memcpy(Buf.floatChannelData[0], Mono, Num * sizeof(float));
        Buf.frameLength = AVAudioFrameCount(Num);
        [Req appendAudioPCMBuffer:Buf];
        [Buf release];
    }
    [Format release];
    [Req release];
}

void FEidoStt::Poll(TArray<FString>& OutFinals, FString& OutError)
{
    FScopeLock L(&Impl->Cs);
    const double Now = FPlatformTime::Seconds();
    // A pause ends an utterance (continuous dictation); tasks also roll over before
    // Apple's one-minute limit, and restart after their own end.
    const bool bPause = !Impl->Partial.TrimStartAndEnd().IsEmpty() && Now - Impl->PartialAt > 1.2;
    if (bPause) { Impl->Ready.Add(Impl->Partial); Impl->Partial.Empty(); }
    OutFinals = MoveTemp(Impl->Ready); Impl->Ready.Reset();
    OutError = MoveTemp(Impl->Error); Impl->Error.Empty();
    if (Impl->bRunning && (bPause || Impl->bTaskEnded || Now - Impl->TaskAt > 50))
    {
        FString Err;
        if (!Impl->BeginTaskLocked(Err, Impl)) { OutError = Err; Impl->bRunning = false; }
    }
}
#endif
