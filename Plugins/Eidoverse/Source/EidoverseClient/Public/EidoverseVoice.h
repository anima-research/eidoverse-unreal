#pragma once
#include "CoreMinimal.h"
#include "UObject/GCObject.h"

class FJsonObject;
class USoundWaveProcedural;

// Native voice client for the Eidoverse in-process SFU (VOICE_TRANSPORT=sfu): the port of
// client/lib/voicesfu.js + voicesfubridge.js + micgate.js/micstate.js (parity/audio.md §1-5).
//
// Signalling rides the world WebSocket: the integrator forwards every incoming message whose
// type is relay-cred, sfu-offer, sfu-route, sfu-ice, surface-transition, voice-service,
// voice-consent or error to OnSignal(), and puts every object handed to SendSignal on the socket
// (each already carries "type"). The server always offers; this only answers.
//
// Media is UE's bundled libwebrtc (M109) with a custom AudioDeviceModule, so NetEq (jitter
// buffer, loss concealment, Opus FEC) does the receive work. A 10 ms "pump" thread pulls the
// playout mix (discarded) so the per-speaker sinks fire, and pushes the gated mic / test tone
// into the published track. The mic is AVAudioEngine input with Apple voice processing (AEC/NS/
// AGC); the RMS gate is micgate.js run on the sample clock.
//
// Threading: every public method is GAME THREAD only. Tick() must be called every frame (it
// sends queued signals, raises speaker callbacks, runs the ICE-stall/recovery timers and the
// 500 ms sfu-pos). Levels are lock-free snapshots written by the audio threads.
//
// Playback: one USoundWaveProcedural per remote speaker (mono 48 kHz, fed from a lock-free ring
// on the audio render thread, so it keeps playing at any frame rate). Put it on a non-spatialized
// UAudioComponent attached to that speaker's avatar and drive SetVolumeMultiplier with
// TargetVolume()/StepVolume() (3 -> 20 m rolloff, hush, voice volume). Waves are kept alive by
// this object until OnSpeakerRemoved; the component's own reference keeps one alive after that.
class EIDOVERSECLIENT_API FEidoVoice : public FGCObject
{
public:
    enum class EMicPermission : uint8 { Unknown, Granted, Denied, Unsupported };

    explicit FEidoVoice(TFunction<void(TSharedRef<FJsonObject>)> InSendSignal);
    virtual ~FEidoVoice() override;

    void OnSignal(const TSharedPtr<FJsonObject>& Msg);
    void Start();                                   // after the world join: relay-cred + voice-consent
    void Stop();                                    // close the peer connection, stop the mic device, drop speakers
    void Tick(float Dt);

    void SetReceiveConsent(bool bRecv);             // default false (privacy rule); sends voice-consent
    void SetMicEnabled(bool bOn);                   // on: asks permission, starts the device, publishes; off: stops the device
    void SetMuted(bool bMute);                      // closes the gate at once; device keeps running
    void SetPushToTalk(bool bMode);
    void SetTalkKeyDown(bool bDown);                // V held (call false on focus loss too)
    void SetMicSensitivity(float MicFloor0to1);     // web micFloor 0..0.2 (default .12 = -24 dBFS)
    void SetListenerPosition(const FVector& WireMetres);   // world frame, metres (ToWirePosition)
    void SetVoiceProcessing(bool bOn);              // Apple VPIO echo cancellation; default on, applies on next mic start
    // Publish a synthetic sine (Hz>0) instead of the mic, bypassing the gate — tests/TTS stand-in. 0 = off.
    void SetTestTone(float Hz, float Amplitude = .5f);

    // ── state (game thread) ──
    bool IsConnected() const;                       // ICE connected/completed
    bool IsMicEnabled() const;                      // intent
    bool IsMicLive() const;                         // device capturing
    bool IsMuted() const;
    bool IsSpeaking() const;                        // the gate is open (level+hang, or PTT key) and the mic is live/unmuted
    EMicPermission GetMicPermission() const;
    float GetMicLevel() const;                      // raw peak |x| of the last 512 mic samples, before the gate; 0 if off/muted
    float GetMicRmsDb() const;                      // raw RMS in dBFS (-100 floor), for the sensitivity meter
    float GetGateThresholdDb() const;               // -60 + floor/0.2*60
    float GetSpeakerLevel(const FString& SpeakerId) const;   // peak of the last 512 decoded samples, 0..1
    TMap<FString, float> GetSpeakerLevels() const;
    TMap<FString, USoundWaveProcedural*> GetSpeakerWaves() const;
    FString GetSelfId() const;                      // our id as the credential names it
    FString DescribeState() const;                  // one-line diagnostics

    // ── callbacks (game thread, from Tick or the calls above) ──
    TFunction<void(const FString& SpeakerId, USoundWaveProcedural* Wave)> OnSpeakerAdded;
    TFunction<void(const FString& SpeakerId)> OnSpeakerRemoved;
    TFunction<void(const FString& Line)> OnNotice;  // person-visible lines (chat "*" lines in the web client)

    // ── rolloff helpers (voicesfubridge.js): target every 300 ms, approach at 1/0.7 per second ──
    static float TargetVolume(float DistanceMetres, bool bHushed, float VoiceVolume)
    {
        return bHushed ? 0.f : FMath::Clamp(1.f - (DistanceMetres - 3.f) / 17.f, 0.f, 1.f) * VoiceVolume;
    }
    static float StepVolume(float Current, float Target, float Dt)
    {
        const float Step = Dt / .7f, D = Target - Current;
        return FMath::Abs(D) <= Step ? Target : Current + FMath::Sign(D) * Step;
    }

    virtual void AddReferencedObjects(FReferenceCollector& Collector) override;
    virtual FString GetReferencerName() const override { return TEXT("FEidoVoice"); }

    struct FImpl;
private:
    TSharedPtr<FImpl, ESPMode::ThreadSafe> Impl;
};
