#pragma once
#include "CoreMinimal.h"
#include "Sound/SoundWaveProcedural.h"
#include <atomic>
#include "EidoverseVoiceWave.generated.h"

// Single-producer / single-consumer PCM ring (int16 mono 48 kHz). The producer is the voice pump
// thread (libwebrtc's per-track sink), the consumer the audio render thread (the wave below).
struct FEidoVoiceRing
{
    static constexpr int32 Capacity = 48000;        // 1 s; the writer drops when full
    static constexpr int32 Prime = 960;             // 20 ms cushion before (re)starting
    static constexpr int32 MaxBacklog = 9600;       // > 200 ms queued: the reader skips to Keep
    static constexpr int32 Keep = 2880;             // 60 ms
    int16 Data[Capacity];
    std::atomic<uint64> Write{0}, Read{0};
    bool bPrimed = false;                           // reader-owned

    void Push(const int16* In, int32 N)
    {
        const uint64 W = Write.load(std::memory_order_relaxed), R = Read.load(std::memory_order_acquire);
        if (W - R + N > Capacity) return;           // nobody is draining (no component playing): drop, never block
        for (int32 I = 0; I < N; ++I) Data[(W + I) % Capacity] = In[I];
        Write.store(W + N, std::memory_order_release);
    }
    void Pull(int16* Out, int32 N)
    {
        const uint64 W = Write.load(std::memory_order_acquire); uint64 R = Read.load(std::memory_order_relaxed);
        int64 Avail = int64(W - R);
        if (Avail > MaxBacklog) { R = W - Keep; Avail = Keep; }     // drift/backlog: jump back to live
        if (!bPrimed && Avail < Prime) { FMemory::Memzero(Out, N * sizeof(int16)); Read.store(R, std::memory_order_release); return; }
        bPrimed = true;
        const int32 Take = int32(FMath::Min<int64>(Avail, N));
        for (int32 I = 0; I < Take; ++I) Out[I] = Data[(R + I) % Capacity];
        if (Take < N) { FMemory::Memzero(Out + Take, (N - Take) * sizeof(int16)); bPrimed = false; }   // underflow: re-prime
        Read.store(R + Take, std::memory_order_release);
    }
};

// A remote speaker's voice as a playable sound: mono, 48 kHz, generated on the audio render
// thread from the speaker's ring. Owned (GC-wise) by FEidoVoice while the speaker is present.
UCLASS(Transient)
class UEidoVoiceSpeakerWave : public USoundWaveProcedural
{
    GENERATED_BODY()
public:
    UEidoVoiceSpeakerWave(const FObjectInitializer& Init);
    TSharedPtr<FEidoVoiceRing, ESPMode::ThreadSafe> Ring;
    virtual int32 OnGeneratePCMAudio(TArray<uint8>& OutAudio, int32 NumSamples) override;
};
