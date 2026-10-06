#pragma once
#include "CoreMinimal.h"

// Speech-to-text (stt.js) on macOS's SFSpeechRecognizer, fed from the voice client's own
// mic capture (one device for voice and STT, unlike the browser). On-device recognition
// where the locale supports it. Implemented in EidoverseSTT.mm; inert elsewhere.
class FEidoStt
{
public:
    enum class EAuth : uint8 { Unknown, Granted, Denied, Unsupported };
    static EAuth Authorization();
    static void RequestAuthorization(TFunction<void(bool bGranted)> Done);   // Done: any thread
    FEidoStt();
    ~FEidoStt();
    bool Start(FString& OutError);        // begin listening (continuous: restarts itself)
    void Stop();
    bool IsRunning() const;
    bool IsOnDevice() const;
    void Feed(const float* Mono, int32 Num, double Rate);   // any thread (the voice pump)
    // Game thread: utterances finished since the last call (a 1.2 s pause or the
    // recognizer's own final), and the latest error text if any.
    void Poll(TArray<FString>& OutFinals, FString& OutError);
    struct FImpl;
private:
    TSharedPtr<FImpl, ESPMode::ThreadSafe> Impl;
};
