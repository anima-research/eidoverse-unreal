#include "EidoverseVoiceMic.h"
#if PLATFORM_MAC
#include "Mac/MacSystemIncludes.h"
#pragma push_macro("FVector")
#define FVector FVectorWorkaround
#import <AVFoundation/AVFoundation.h>
#pragma pop_macro("FVector")

namespace EidoVoiceMic
{
EStatus Status()
{
    switch ([AVCaptureDevice authorizationStatusForMediaType:AVMediaTypeAudio])
    {
    case AVAuthorizationStatusAuthorized: return EStatus::Granted;
    case AVAuthorizationStatusNotDetermined: return EStatus::NotDetermined;
    default: return EStatus::Denied;    // Denied or Restricted: macOS would hand us silent buffers
    }
}

void Request(TFunction<void(bool)> Done)
{
    TFunction<void(bool)>* Heap = new TFunction<void(bool)>(MoveTemp(Done));
    [AVCaptureDevice requestAccessForMediaType:AVMediaTypeAudio completionHandler:^(BOOL bGranted) { (*Heap)(bGranted == YES); delete Heap; }];
}

TUniquePtr<FCapture> FCapture::Start(bool bVoiceProcessing, TFunction<void(const float*, int32, double)> OnAudio, FString& OutError)
{
    TUniquePtr<FCapture> C(new FCapture());
    AVAudioEngine* Engine = [[AVAudioEngine alloc] init];
    AVAudioInputNode* Input = Engine.inputNode;
    NSError* Err = nil;
    if (bVoiceProcessing)
    {
        if ([Input setVoiceProcessingEnabled:YES error:&Err]) C->bVoiceProcessingActive = true;
        else UE_LOG(LogTemp, Warning, TEXT("[voice] voice processing unavailable (%s) — capturing without echo cancellation"), Err ? *FString(Err.localizedDescription) : TEXT("?"));
        if (C->bVoiceProcessingActive)
        {
            // VPIO ducks every other app's output (the whole game) by default; keep that to a minimum.
            if (@available(macOS 14.0, *))
            {
                AVAudioVoiceProcessingOtherAudioDuckingConfiguration Duck;
                Duck.enableAdvancedDucking = NO; Duck.duckingLevel = AVAudioVoiceProcessingOtherAudioDuckingLevelMin;
                Input.voiceProcessingOtherAudioDuckingConfiguration = Duck;
            }
            Input.voiceProcessingAGCEnabled = YES;
        }
    }
    AVAudioFormat* Fmt = [Input outputFormatForBus:0];
    const double Rate = Fmt.sampleRate; const bool bInterleaved = Fmt.isInterleaved; const AVAudioChannelCount Channels = Fmt.channelCount;
    if (Rate <= 0 || Channels == 0) { OutError = TEXT("no microphone input device"); [Engine release]; return nullptr; }
    if (Fmt.commonFormat != AVAudioPCMFormatFloat32) { OutError = TEXT("unsupported microphone sample format"); [Engine release]; return nullptr; }
    TSharedPtr<TFunction<void(const float*, int32, double)>, ESPMode::ThreadSafe> Cb = MakeShared<TFunction<void(const float*, int32, double)>, ESPMode::ThreadSafe>(MoveTemp(OnAudio));
    TSharedPtr<TArray<float>, ESPMode::ThreadSafe> Scratch = MakeShared<TArray<float>, ESPMode::ThreadSafe>();
    Scratch->SetNumUninitialized(16384);
    AVAudioSinkNode* Sink = [[AVAudioSinkNode alloc] initWithReceiverBlock:^OSStatus(const AudioTimeStamp*, AVAudioFrameCount Frames, const AudioBufferList* List) {
        if (!List || List->mNumberBuffers == 0 || Frames == 0 || Frames > 16384) return noErr;
        const float* Src = static_cast<const float*>(List->mBuffers[0].mData);
        if (!Src) return noErr;
        if (bInterleaved && Channels > 1)
        {   // channel 0 of an interleaved buffer (voice processing hands back its own channel layout)
            float* Dst = Scratch->GetData();
            for (AVAudioFrameCount I = 0; I < Frames; ++I) Dst[I] = Src[I * Channels];
            Src = Dst;
        }
        (*Cb)(Src, int32(Frames), Rate);
        return noErr;
    }];
    [Engine attachNode:Sink];
    [Engine connect:Input to:Sink format:Fmt];
    [Engine prepare];
    if (![Engine startAndReturnError:&Err])
    {
        OutError = FString::Printf(TEXT("microphone failed to start: %s"), Err ? *FString(Err.localizedDescription) : TEXT("?"));
        [Sink release]; [Engine release];
        return nullptr;
    }
    C->Engine = Engine; C->Sink = Sink;
    return C;
}

FCapture::~FCapture()
{
    AVAudioEngine* E = static_cast<AVAudioEngine*>(Engine);
    if (E) { [E stop]; if (Sink) [E detachNode:static_cast<AVAudioSinkNode*>(Sink)]; }
    if (Sink) [static_cast<AVAudioSinkNode*>(Sink) release];
    if (E) [E release];
}
}
#endif
