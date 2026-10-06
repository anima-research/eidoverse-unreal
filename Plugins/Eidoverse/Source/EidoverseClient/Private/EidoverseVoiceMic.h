#pragma once
#include "CoreMinimal.h"

// macOS microphone: TCC permission + AVAudioEngine input (voice processing = Apple's AEC/NS/AGC).
// Implemented in EidoverseVoiceMic.mm; non-Mac builds get the stubs in EidoverseVoice.cpp.
namespace EidoVoiceMic
{
    enum class EStatus : uint8 { NotDetermined, Granted, Denied, Unsupported };
    EStatus Status();
    // Shows the system prompt when undetermined. Done runs on an arbitrary thread.
    void Request(TFunction<void(bool bGranted)> Done);

    // A running capture. OnAudio runs on the real-time I/O thread with mono float samples at the
    // device rate: copy and return, never block. Destroying it stops the device (light goes off).
    class FCapture
    {
    public:
        static TUniquePtr<FCapture> Start(bool bVoiceProcessing, TFunction<void(const float* Mono, int32 Frames, double Rate)> OnAudio, FString& OutError);
        ~FCapture();
        bool bVoiceProcessingActive = false;
    private:
        FCapture() = default;
        void* Engine = nullptr;     // AVAudioEngine*, retained
        void* Sink = nullptr;       // AVAudioSinkNode*, retained
    };
}
