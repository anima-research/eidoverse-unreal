#include "EidoverseVoice.h"
#include "EidoverseVoiceWave.h"
#include "EidoverseVoiceMic.h"
#include "Dom/JsonObject.h"
#include "Containers/Queue.h"
#include "HAL/Runnable.h"
#include "HAL/RunnableThread.h"
#include "HAL/PlatformProcess.h"
#include "Misc/ScopeLock.h"
#include "UObject/Package.h"
#include <atomic>

DEFINE_LOG_CATEGORY_STATIC(LogEidoVoice, Log, All);

// ─── the speaker wave (render thread pulls the ring) ─────────────────────────────────────────
UEidoVoiceSpeakerWave::UEidoVoiceSpeakerWave(const FObjectInitializer& Init) : Super(Init)
{
    SetSampleRate(48000);
    NumChannels = 1;
    Duration = 10000.f;                     // INDEFINITELY_LOOPING_DURATION
    bLooping = false;
    SoundGroup = SOUNDGROUP_Voice;
    bCanProcessAsync = true;
    VirtualizationMode = EVirtualizationMode::PlayWhenSilent;
}

int32 UEidoVoiceSpeakerWave::OnGeneratePCMAudio(TArray<uint8>& OutAudio, int32 NumSamples)
{
    if (!Ring.IsValid() || NumSamples <= 0) return 0;
    OutAudio.SetNumUninitialized(NumSamples * sizeof(int16));
    Ring->Pull(reinterpret_cast<int16*>(OutAudio.GetData()), NumSamples);
    return NumSamples;
}

#if EIDOVERSE_VOICE_WEBRTC
THIRD_PARTY_INCLUDES_START
#include "PreWebRTCApi.h"
#include "api/peer_connection_interface.h"
#include "api/create_peerconnection_factory.h"
#include "api/audio_codecs/audio_decoder_factory_template.h"
#include "api/audio_codecs/audio_encoder_factory_template.h"
#include "api/audio_codecs/opus/audio_decoder_opus.h"
#include "api/audio_codecs/opus/audio_encoder_opus.h"
#include "api/audio_options.h"
#include "api/jsep.h"
#include "api/make_ref_counted.h"
#include "api/notifier.h"
#include "modules/audio_device/include/audio_device.h"
#include "rtc_base/ssl_adapter.h"
#include "rtc_base/thread.h"
#include "PostWebRTCApi.h"
#ifdef PF_MAX
#undef PF_MAX
#endif
THIRD_PARTY_INCLUDES_END

namespace
{
constexpr int32 Rate = 48000, Frame = 480;  // libwebrtc's 10 ms frame at 48 kHz

// Linear resampler on a stream (state carries across calls). Ratio = input rate / output rate.
struct FResampler
{
    double T = 0; float Prev = 0;
    void Run(const float* In, int32 N, double Ratio, TArray<float>& Out)
    {
        if (N <= 0) return;
        if (Ratio == 1.0 && T == 0) { Out.Append(In, N); Prev = In[N - 1]; return; }
        while (T < N)
        {
            const int32 I = int32(T); const float F = float(T - I);
            const float A = I == 0 ? Prev : In[I - 1], B = In[I];
            Out.Add(A + (B - A) * F); T += Ratio;
        }
        Prev = In[N - 1]; T -= N;
    }
};

// Single-producer/single-consumer float ring for the raw mic (RT I/O thread -> pump).
struct FMicRing
{
    static constexpr int32 Capacity = 96000 * 2;
    float Data[Capacity];
    std::atomic<uint64> Write{0}, Read{0};
    std::atomic<double> DeviceRate{48000};
    void Push(const float* In, int32 N)
    {
        const uint64 W = Write.load(std::memory_order_relaxed), R = Read.load(std::memory_order_acquire);
        if (W - R + N > Capacity) return;
        for (int32 I = 0; I < N; ++I) Data[(W + I) % Capacity] = In[I];
        Write.store(W + N, std::memory_order_release);
    }
    int32 Drain(TArray<float>& Out)
    {
        const uint64 W = Write.load(std::memory_order_acquire), R = Read.load(std::memory_order_relaxed);
        const int32 N = int32(W - R); const int32 Base = Out.Num(); Out.AddUninitialized(N);
        for (int32 I = 0; I < N; ++I) Out[Base + I] = Data[(R + I) % Capacity];
        Read.store(W, std::memory_order_release);
        return N;
    }
    void Clear() { Read.store(Write.load(std::memory_order_acquire), std::memory_order_release); }
    int32 Available() const { return int32(Write.load(std::memory_order_acquire) - Read.load(std::memory_order_relaxed)); }
    int32 Free() const { return Capacity - int32(Write.load(std::memory_order_relaxed) - Read.load(std::memory_order_acquire)); }
    void Pop(float* Out, int32 N)
    {
        const uint64 R = Read.load(std::memory_order_relaxed);
        for (int32 I = 0; I < N; ++I) Out[I] = Data[(R + I) % Capacity];
        Read.store(R + N, std::memory_order_release);
    }
};

// ─── custom AudioDeviceModule: no device, playout pulled by our pump ─────────────────────────
class FEidoVoiceAdm : public webrtc::AudioDeviceModule
{
public:
    void Pull()
    {
        FScopeLock L(&Cs);
        if (!Transport) return;
        int16 Mix[Frame * 2]; size_t Out = 0; int64_t Elapsed = -1, Ntp = -1;
        // The mix itself is discarded: pulling it is what makes NetEq decode and the per-track sinks fire.
        Transport->NeedMorePlayData(Frame, 2 * sizeof(int16), 2, Rate, Mix, Out, &Elapsed, &Ntp);
    }
    int32_t ActiveAudioLayer(AudioLayer* Layer) const override { *Layer = kDummyAudio; return 0; }
    int32_t RegisterAudioCallback(webrtc::AudioTransport* Cb) override { FScopeLock L(&Cs); Transport = Cb; return 0; }
    int32_t Init() override { bInit = true; return 0; }
    int32_t Terminate() override { FScopeLock L(&Cs); Transport = nullptr; bInit = false; bPlaying = false; return 0; }
    bool Initialized() const override { return bInit; }
    int16_t PlayoutDevices() override { return 1; }
    int16_t RecordingDevices() override { return 1; }
    int32_t PlayoutDeviceName(uint16_t, char Name[webrtc::kAdmMaxDeviceNameSize], char Guid[webrtc::kAdmMaxGuidSize]) override { Name[0] = 0; Guid[0] = 0; return 0; }
    int32_t RecordingDeviceName(uint16_t, char Name[webrtc::kAdmMaxDeviceNameSize], char Guid[webrtc::kAdmMaxGuidSize]) override { Name[0] = 0; Guid[0] = 0; return 0; }
    int32_t SetPlayoutDevice(uint16_t) override { return 0; }
    int32_t SetPlayoutDevice(WindowsDeviceType) override { return 0; }
    int32_t SetRecordingDevice(uint16_t) override { return 0; }
    int32_t SetRecordingDevice(WindowsDeviceType) override { return 0; }
    int32_t PlayoutIsAvailable(bool* Ok) override { *Ok = true; return 0; }
    int32_t InitPlayout() override { bPlayInit = true; return 0; }
    bool PlayoutIsInitialized() const override { return bPlayInit; }
    int32_t RecordingIsAvailable(bool* Ok) override { *Ok = true; return 0; }
    int32_t InitRecording() override { bRecInit = true; return 0; }
    bool RecordingIsInitialized() const override { return bRecInit; }
    int32_t StartPlayout() override { bPlaying = true; return 0; }
    int32_t StopPlayout() override { bPlaying = false; return 0; }
    bool Playing() const override { return bPlaying; }
    int32_t StartRecording() override { bRecording = true; return 0; }   // the mic reaches libwebrtc through FEidoVoiceSource, not here
    int32_t StopRecording() override { bRecording = false; return 0; }
    bool Recording() const override { return bRecording; }
    int32_t InitSpeaker() override { return 0; }
    bool SpeakerIsInitialized() const override { return true; }
    int32_t InitMicrophone() override { return 0; }
    bool MicrophoneIsInitialized() const override { return true; }
    int32_t SpeakerVolumeIsAvailable(bool* Ok) override { *Ok = false; return 0; }
    int32_t SetSpeakerVolume(uint32_t) override { return -1; }
    int32_t SpeakerVolume(uint32_t*) const override { return -1; }
    int32_t MaxSpeakerVolume(uint32_t*) const override { return -1; }
    int32_t MinSpeakerVolume(uint32_t*) const override { return -1; }
    int32_t MicrophoneVolumeIsAvailable(bool* Ok) override { *Ok = false; return 0; }
    int32_t SetMicrophoneVolume(uint32_t) override { return -1; }
    int32_t MicrophoneVolume(uint32_t*) const override { return -1; }
    int32_t MaxMicrophoneVolume(uint32_t*) const override { return -1; }
    int32_t MinMicrophoneVolume(uint32_t*) const override { return -1; }
    int32_t SpeakerMuteIsAvailable(bool* Ok) override { *Ok = false; return 0; }
    int32_t SetSpeakerMute(bool) override { return -1; }
    int32_t SpeakerMute(bool*) const override { return -1; }
    int32_t MicrophoneMuteIsAvailable(bool* Ok) override { *Ok = false; return 0; }
    int32_t SetMicrophoneMute(bool) override { return -1; }
    int32_t MicrophoneMute(bool*) const override { return -1; }
    int32_t StereoPlayoutIsAvailable(bool* Ok) const override { *Ok = true; return 0; }
    int32_t SetStereoPlayout(bool) override { return 0; }
    int32_t StereoPlayout(bool* On) const override { *On = true; return 0; }
    int32_t StereoRecordingIsAvailable(bool* Ok) const override { *Ok = false; return 0; }
    int32_t SetStereoRecording(bool) override { return 0; }
    int32_t StereoRecording(bool* On) const override { *On = false; return 0; }
    int32_t PlayoutDelay(uint16_t* Ms) const override { *Ms = 0; return 0; }
    bool BuiltInAECIsAvailable() const override { return false; }
    bool BuiltInAGCIsAvailable() const override { return false; }
    bool BuiltInNSIsAvailable() const override { return false; }
    int32_t EnableBuiltInAEC(bool) override { return -1; }
    int32_t EnableBuiltInAGC(bool) override { return -1; }
    int32_t EnableBuiltInNS(bool) override { return -1; }
private:
    mutable FCriticalSection Cs;
    webrtc::AudioTransport* Transport = nullptr;
    std::atomic<bool> bInit{false}, bPlayInit{false}, bRecInit{false}, bPlaying{false}, bRecording{false};
};

// ─── the published track's source: the pump pushes 10 ms mono frames straight to the senders ──
class FEidoVoiceSource : public webrtc::Notifier<webrtc::AudioSourceInterface>
{
public:
    SourceState state() const override { return kLive; }
    bool remote() const override { return false; }
    void AddSink(webrtc::AudioTrackSinkInterface* S) override { FScopeLock L(&Cs); Sinks.AddUnique(S); }
    void RemoveSink(webrtc::AudioTrackSinkInterface* S) override { FScopeLock L(&Cs); Sinks.Remove(S); }
    const cricket::AudioOptions options() const override
    {   // processing happens before us (Apple voice processing + our gate); APM never sees this path
        cricket::AudioOptions O; O.echo_cancellation = false; O.auto_gain_control = false; O.noise_suppression = false; O.highpass_filter = false;
        return O;
    }
    void Deliver(const int16* Pcm)
    {
        FScopeLock L(&Cs);
        for (webrtc::AudioTrackSinkInterface* S : Sinks) S->OnData(Pcm, 16, Rate, 1, Frame, absl::nullopt);
    }
private:
    FCriticalSection Cs;
    TArray<webrtc::AudioTrackSinkInterface*> Sinks;
};

// ─── one remote speaker: decoded PCM -> ring (for the wave) + 512-sample peak (for mouths) ──
struct FSpeakerSink : public webrtc::AudioTrackSinkInterface
{
    using webrtc::AudioTrackSinkInterface::OnData;
    TSharedPtr<FEidoVoiceRing, ESPMode::ThreadSafe> Ring = MakeShared<FEidoVoiceRing, ESPMode::ThreadSafe>();
    rtc::scoped_refptr<webrtc::AudioTrackInterface> Track;     // signaling thread
    std::atomic<float> Peak{0};
    float Abs[512] = {}; int32 AbsPos = 0;
    FResampler Res; TArray<float> Mono, Out48; TArray<int16> Pcm;

    void OnData(const void* Data, int Bits, int SampleRate, size_t Channels, size_t Frames) override
    {
        if (Bits != 16 || !Data || Channels == 0 || Frames == 0) return;
        const int16* S = static_cast<const int16*>(Data);
        Mono.SetNumUninitialized(int32(Frames));
        for (size_t I = 0; I < Frames; ++I)
        {
            int32 Sum = 0; for (size_t C = 0; C < Channels; ++C) Sum += S[I * Channels + C];
            Mono[int32(I)] = float(Sum) / float(Channels) / 32768.f;
        }
        Out48.Reset(); Res.Run(Mono.GetData(), Mono.Num(), double(SampleRate) / Rate, Out48);
        Pcm.SetNumUninitialized(Out48.Num());
        for (int32 I = 0; I < Out48.Num(); ++I)
        {
            const float V = Out48[I]; Abs[AbsPos] = FMath::Abs(V); AbsPos = (AbsPos + 1) & 511;
            Pcm[I] = int16(FMath::Clamp(V * 32767.f, -32768.f, 32767.f));
        }
        float P = 0; for (float A : Abs) P = FMath::Max(P, A);
        Peak.store(FMath::Min(P, 1.f), std::memory_order_relaxed);
        Ring->Push(Pcm.GetData(), Pcm.Num());
    }
};

const TCHAR* IceName(webrtc::PeerConnectionInterface::IceConnectionState S)
{
    using E = webrtc::PeerConnectionInterface;
    switch (S)
    {
    case E::kIceConnectionNew: return TEXT("new"); case E::kIceConnectionChecking: return TEXT("checking");
    case E::kIceConnectionConnected: return TEXT("connected"); case E::kIceConnectionCompleted: return TEXT("completed");
    case E::kIceConnectionFailed: return TEXT("failed"); case E::kIceConnectionDisconnected: return TEXT("disconnected");
    case E::kIceConnectionClosed: return TEXT("closed"); default: return TEXT("?");
    }
}
std::string Std(const FString& S) { return std::string(TCHAR_TO_UTF8(*S)); }
FString Ue(const std::string& S) { return FString(UTF8_TO_TCHAR(S.c_str())); }
}

struct FEidoVoice::FImpl : public TSharedFromThis<FEidoVoice::FImpl, ESPMode::ThreadSafe>
{
    using FPc = webrtc::PeerConnectionInterface;
    FEidoVoice& Owner;
    TFunction<void(TSharedRef<FJsonObject>)> SendSignal;
    explicit FImpl(FEidoVoice& InOwner, TFunction<void(TSharedRef<FJsonObject>)> InSend) : Owner(InOwner), SendSignal(MoveTemp(InSend)) {}

    // ── game thread ──
    TQueue<TFunction<void()>, EQueueMode::Mpsc> GameQueue;
    bool bStarted = false, bRecv = false, bWantMic = false, bPtt = false, bKeyDown = false, bVoiceProcessing = true, bNoRelay = false;
    bool bHavePos = false, bPermissionAsking = false;
    FVector Pos = FVector::ZeroVector;
    TSharedPtr<FJsonObject> Cred; FString SelfId, Incarnation; int64 MediaGen = -1;
    int32 Epoch = 0;
    FString Ice = TEXT("none");
    double StallAt = 0, RecoveryAt = 0, PosAt = 0, LastAnnounce = -10;
    int32 RecoveryAttempts = 0, SeenOnsets = 0;
    EMicPermission Permission = EMicPermission::Unknown;
    TUniquePtr<EidoVoiceMic::FCapture> Capture;
    TMap<FString, TObjectPtr<UEidoVoiceSpeakerWave>> Waves;

    // ── shared, lock-protected: speaker id -> sink (signaling attaches, game reads rings/levels) ──
    mutable FCriticalSection SpeakersCs;
    TMap<FString, TSharedPtr<FSpeakerSink>> Sinks;

    // ── media (created on first Start) ──
    std::unique_ptr<rtc::Thread> Signaling;
    rtc::scoped_refptr<FEidoVoiceAdm> Adm;
    rtc::scoped_refptr<FEidoVoiceSource> Source;
    rtc::scoped_refptr<webrtc::PeerConnectionFactoryInterface> Factory;
    rtc::scoped_refptr<webrtc::AudioTrackInterface> LocalTrack;

    // ── signaling thread only ──
    struct FObserver;
    rtc::scoped_refptr<FPc> Pc;
    TUniquePtr<FObserver> Observer;
    int32 PcEpoch = -1;
    TArray<FString> RouteQueue;
    std::vector<std::unique_ptr<webrtc::IceCandidateInterface>> PendingIce;
    std::string StreamId = "self";

    // ── pump thread (and atomics read anywhere) ──
    struct FPump : public FRunnable
    {
        FImpl& Self; std::atomic<bool> bRun{true};
        explicit FPump(FImpl& In) : Self(In) {}
        uint32 Run() override
        {
            double Next = FPlatformTime::Seconds();
            while (bRun)
            {
                const double Now = FPlatformTime::Seconds();
                if (Now < Next) { FPlatformProcess::Sleep(float(FMath::Min(Next - Now, .005))); continue; }
                Next = Now - Next > .1 ? Now + .01 : Next + .01;
                Self.PumpTick();
            }
            return 0;
        }
        void Stop() override { bRun = false; }
    };
    TUniquePtr<FPump> Pump; TUniquePtr<FRunnableThread> PumpThread;
    TSharedPtr<FMicRing, ESPMode::ThreadSafe> MicRing = MakeShared<FMicRing, ESPMode::ThreadSafe>();
    std::atomic<bool> bMicLive{false}, bMuted{false}, aPtt{false}, aKeyDown{false}, bWantPublish{false}, bSpeaking{false};
    std::atomic<int32> MicGen{0}, Onsets{0};
    std::atomic<float> Threshold{0.063f}, MicPeak{0}, MicRmsDb{-100}, ToneHz{0}, ToneAmp{.5f};
    // synth lane (game thread feeds the ring from the backlog; the pump drains it)
    TSharedPtr<FMicRing, ESPMode::ThreadSafe> SynthRing = MakeShared<FMicRing, ESPMode::ThreadSafe>();
    TArray<float> SynthBacklog;
    std::atomic<bool> bSynthOn{false};
    std::atomic<float> SynthPeak{0};
    FCriticalSection TapCs; TFunction<void(const float*, int32, double)> MicTap;
    // pump-owned
    int32 PumpMicGen = -1; FResampler MicRes; TArray<float> MicRaw, Mic48; double TonePhase = 0;
    float Last512[512] = {}; int32 LastPos = 0;
    TArray<float> Delay; int32 DelayPos = 0; float Gain = 0;
    int64 Clock = 0; int32 TickAccum = 0;
    bool bAbove = false; int64 OpenUntil = 0, OpenedAt = 0, LastOnset = -100000; bool bAnnounced = false;
    float Noise = .01f; int32 Settle = 0; float Blocks[6]; int32 BlockIdx = 0, BlocksFilled = 0; float BlockMin = 1e9f; int64 BlockStart = 0;

    // ═══ media bring-up / teardown ═══
    bool EnsureMedia()
    {
        if (Factory) return true;
        static bool bSsl = false; if (!bSsl) { rtc::InitializeSSL(); bSsl = true; }
        Signaling = rtc::Thread::CreateWithSocketServer();
        Signaling->SetName("EidoVoiceSignaling", nullptr);
        Signaling->Start();
        Adm = rtc::make_ref_counted<FEidoVoiceAdm>();
        Source = rtc::make_ref_counted<FEidoVoiceSource>();
        Factory = webrtc::CreatePeerConnectionFactory(nullptr, nullptr, Signaling.get(), Adm,
            webrtc::CreateAudioEncoderFactory<webrtc::AudioEncoderOpus>(), webrtc::CreateAudioDecoderFactory<webrtc::AudioDecoderOpus>(),
            nullptr, nullptr, nullptr, nullptr);
        if (!Factory) { UE_LOG(LogEidoVoice, Error, TEXT("[voice] libwebrtc factory failed")); Signaling->Stop(); Signaling.reset(); return false; }
        webrtc::PeerConnectionFactoryInterface::Options O; O.network_ignore_mask = 0;   // allow loopback (local servers)
        Factory->SetOptions(O);
        LocalTrack = Factory->CreateAudioTrack("eido-mic", Source.get());
        Pump = MakeUnique<FPump>(*this);
        PumpThread.Reset(FRunnableThread::Create(Pump.Get(), TEXT("EidoVoicePump"), 0, TPri_AboveNormal));
        return true;
    }
    void Shutdown()
    {
        StopCapture();
        if (Signaling) Signaling->BlockingCall([this] { ClosePcOnSignaling(); });
        if (Pump) { Pump->Stop(); PumpThread->WaitForCompletion(); PumpThread.Reset(); Pump.Reset(); }
        if (Signaling) Signaling->BlockingCall([this] { LocalTrack = nullptr; Factory = nullptr; Source = nullptr; Adm = nullptr; });
        if (Signaling) { Signaling->Stop(); Signaling.reset(); }
        ClearGameSpeakers();
    }
    void PostSig(TFunction<void(FImpl&)> Fn)
    {
        if (!Signaling) return;
        Signaling->PostTask([W = TWeakPtr<FImpl, ESPMode::ThreadSafe>(AsShared()), F = MoveTemp(Fn)]() mutable { if (auto P = W.Pin()) F(*P); });
    }
    void PostGame(TFunction<void(FImpl&)> Fn)
    {
        GameQueue.Enqueue([W = TWeakPtr<FImpl, ESPMode::ThreadSafe>(AsShared()), F = MoveTemp(Fn)] { if (auto P = W.Pin()) F(*P); });
    }

    // ═══ game thread: signalling ═══
    void Send(TSharedRef<FJsonObject> O) { if (SendSignal) SendSignal(O); }
    void SendType(const TCHAR* Type) { TSharedRef<FJsonObject> O = MakeShared<FJsonObject>(); O->SetStringField(TEXT("type"), Type); Send(O); }
    void SendConsent() { TSharedRef<FJsonObject> O = MakeShared<FJsonObject>(); O->SetStringField(TEXT("type"), TEXT("voice-consent")); O->SetBoolField(TEXT("recv"), bRecv); Send(O); }
    void RequestCred() { SendType(TEXT("relay-cred")); SendConsent(); }
    void Notice(const FString& Line) { UE_LOG(LogEidoVoice, Log, TEXT("[voice] %s"), *Line); if (Owner.OnNotice) Owner.OnNotice(Line); }

    void OnSignal(const TSharedPtr<FJsonObject>& Msg)
    {
        FString Type; if (!Msg.IsValid() || !Msg->TryGetStringField(TEXT("type"), Type)) return;
        if (Type == TEXT("relay-cred")) OnCred(Msg);
        else if (Type == TEXT("sfu-route")) { FString S; if (Msg->TryGetStringField(TEXT("speaker"), S)) PostSig([S](FImpl& I) { I.RouteQueue.Add(S); }); }
        else if (Type == TEXT("sfu-offer"))
        {
            FString Sdp; if (!Msg->TryGetStringField(TEXT("sdp"), Sdp) || !Cred) return;
            PostSig([Sdp, E = Epoch](FImpl& I) { I.OnOfferSig(Std(Sdp), E); });
        }
        else if (Type == TEXT("sfu-ice"))
        {
            const TSharedPtr<FJsonObject>* C; if (!Msg->TryGetObjectField(TEXT("candidate"), C)) return;
            FString Cand, Mid; double Idx = 0; (*C)->TryGetStringField(TEXT("candidate"), Cand); (*C)->TryGetStringField(TEXT("sdpMid"), Mid); (*C)->TryGetNumberField(TEXT("sdpMLineIndex"), Idx);
            if (!Cand.IsEmpty()) PostSig([Cand, Mid, Idx, E = Epoch](FImpl& I) { I.OnRemoteIceSig(Std(Mid), int(Idx), Std(Cand), E); });
        }
        else if (Type == TEXT("surface-transition"))
        {
            FString Surface, Id; Msg->TryGetStringField(TEXT("surface"), Surface);
            if (!Msg->TryGetStringField(TEXT("id"), Id)) Msg->TryGetStringField(TEXT("actor"), Id);
            if ((Surface != TEXT("voice") && Surface != TEXT("voice-relay")) || Id.IsEmpty() || Id == SelfId) return;
            if (!Msg->HasField(TEXT("gen")) || Msg->HasTypedField<EJson::Null>(TEXT("gen"))) { DropSpeaker(Id); return; }
            bool bKnown; { FScopeLock L(&SpeakersCs); bKnown = Sinks.Contains(Id); }
            if (bKnown) EnsureWave(Id);                 // a returning speaker: its route (and our track) survives
            if (Ice == TEXT("connected") || Ice == TEXT("completed")) SendType(TEXT("sfu-want-negotiate"));
        }
        else if (Type == TEXT("voice-service"))
        {
            FString State, Inc; Msg->TryGetStringField(TEXT("state"), State); Msg->TryGetStringField(TEXT("incarnation"), Inc);
            if (State == TEXT("degraded")) Notice(TEXT("voice: the server's voice service is degraded"));
            // A new incarnation strands every credential: one clean fresh join.
            if (bStarted && Cred && !Inc.IsEmpty() && Inc != Incarnation && State == TEXT("live")) RequestCred();
        }
        else if (Type == TEXT("voice-consent"))
        {
            bool bApplied = false; Msg->TryGetBoolField(TEXT("applied"), bApplied); FString Note; Msg->TryGetStringField(TEXT("note"), Note);
            UE_LOG(LogEidoVoice, Log, TEXT("[voice] consent ack recv=%d applied=%d %s"), Msg->GetBoolField(TEXT("recv")), bApplied, *Note);
        }
        else if (Type == TEXT("error"))
        {
            FString Err; Msg->TryGetStringField(TEXT("error"), Err);
            if (Err.Contains(TEXT("no voice relay"))) { bNoRelay = true; Notice(TEXT("this world has no voice relay — voice is unavailable here")); }
            else if (Err.StartsWith(TEXT("voice leg refused"))) { Notice(TEXT("voice: ") + Err); MediaDead(TEXT("refused")); }
            else if (Err.Contains(TEXT("relay-cred")) || Err.Contains(TEXT("muting someone"))) Notice(TEXT("voice: ") + Err);
        }
    }

    void OnCred(const TSharedPtr<FJsonObject>& Msg)
    {
        if (!bStarted || !EnsureMedia()) return;
        Cred = Msg; bNoRelay = false;
        Msg->TryGetStringField(TEXT("id"), SelfId); Msg->TryGetStringField(TEXT("incarnation"), Incarnation);
        double G = -1; if (!Msg->TryGetNumberField(TEXT("mediaGen"), G)) Msg->TryGetNumberField(TEXT("gen"), G); MediaGen = int64(G);
        TArray<std::pair<std::vector<std::string>, std::pair<std::string, std::string>>> Servers;
        const TArray<TSharedPtr<FJsonValue>>* Arr;
        if (Msg->TryGetArrayField(TEXT("iceServers"), Arr))
            for (const TSharedPtr<FJsonValue>& V : *Arr)
            {
                const TSharedPtr<FJsonObject>* S; if (!V->TryGetObject(S)) continue;
                std::vector<std::string> Urls; FString U; const TArray<TSharedPtr<FJsonValue>>* UA;
                if ((*S)->TryGetStringField(TEXT("urls"), U)) Urls.push_back(Std(U));
                else if ((*S)->TryGetArrayField(TEXT("urls"), UA)) for (const TSharedPtr<FJsonValue>& X : *UA) Urls.push_back(Std(X->AsString()));
                FString User, Pass; (*S)->TryGetStringField(TEXT("username"), User); (*S)->TryGetStringField(TEXT("credential"), Pass);
                Servers.Add({ Urls, { Std(User), Std(Pass) } });
            }
        ++Epoch; Ice = TEXT("new"); StallAt = 0;
        ClearGameSpeakers();
        const std::string Sid = Std(SelfId.IsEmpty() ? TEXT("self") : SelfId);
        PostSig([Servers, Sid, E = Epoch](FImpl& I) { I.ConnectSig(Servers, Sid, E); });
        UE_LOG(LogEidoVoice, Log, TEXT("[voice] credential %s gen %lld (%d ICE servers)"), *SelfId, MediaGen, Servers.Num());
        // Honour a mic pressed before the credential landed (voicesfubridge.js relay-cred replay).
        if (bWantMic && !Capture) StartMicFlow();
        SyncPublishIntent();
        SendType(TEXT("sfu-want-negotiate"));       // the server offers; we only ask
    }

    void SendAnswer(const std::string& Sdp, int32 E)
    {
        if (E != Epoch || !Cred) return;
        TSharedRef<FJsonObject> O = MakeShared<FJsonObject>(), C = MakeShared<FJsonObject>();
        O->SetStringField(TEXT("type"), TEXT("sfu-answer")); O->SetStringField(TEXT("sdp"), Ue(Sdp)); O->SetNumberField(TEXT("gen"), double(MediaGen));
        for (const TCHAR* K : { TEXT("world"), TEXT("id"), TEXT("incarnation"), TEXT("nonce") }) { FString V; Cred->TryGetStringField(K, V); C->SetStringField(K, V); }
        double Pg = -1; Cred->TryGetNumberField(TEXT("primaryGen"), Pg); C->SetNumberField(TEXT("primaryGen"), Pg); C->SetNumberField(TEXT("mediaGen"), double(MediaGen));
        O->SetObjectField(TEXT("cred"), C);
        Send(O);
    }

    void OnIceState(const FString& S, int32 E)
    {
        if (E != Epoch) return;
        Ice = S; UE_LOG(LogEidoVoice, Log, TEXT("[voice] ICE %s"), *S);
        if (S == TEXT("checking")) StallAt = FPlatformTime::Seconds() + 8;
        else if (S == TEXT("connected") || S == TEXT("completed")) { StallAt = 0; RecoveryAttempts = 0; RecoveryAt = 0; }
        else if (S == TEXT("failed")) { StallAt = 0; Notice(TEXT("voice: connection failed — reconnecting voice…")); MediaDead(TEXT("ice-failed")); }
        else if (S == TEXT("closed")) StallAt = 0;
    }
    void MediaDead(const TCHAR* Why)
    {
        if (RecoveryAt > 0 || !bStarted) return;
        if (RecoveryAttempts >= 3) { UE_LOG(LogEidoVoice, Error, TEXT("[voice] media path dead (%s) — giving up after 3 recovery attempts"), Why); return; }
        static const double Delays[3] = { 2, 8, 20 };
        const double D = Delays[RecoveryAttempts++];
        UE_LOG(LogEidoVoice, Warning, TEXT("[voice] media path dead (%s) — re-joining voice in %.0fs (attempt %d/3)"), Why, D, RecoveryAttempts);
        RecoveryAt = FPlatformTime::Seconds() + D;
    }

    // ── speakers (game thread) ──
    void EnsureWave(const FString& Id)
    {
        if (Waves.Contains(Id)) return;
        TSharedPtr<FEidoVoiceRing, ESPMode::ThreadSafe> Ring;
        { FScopeLock L(&SpeakersCs); if (const TSharedPtr<FSpeakerSink>* S = Sinks.Find(Id)) Ring = (*S)->Ring; }
        if (!Ring) return;
        UEidoVoiceSpeakerWave* W = NewObject<UEidoVoiceSpeakerWave>(GetTransientPackage(), NAME_None, RF_Transient);
        W->Ring = Ring;
        Waves.Add(Id, W);
        UE_LOG(LogEidoVoice, Log, TEXT("[voice] speaker %s"), *Id);
        if (Owner.OnSpeakerAdded) Owner.OnSpeakerAdded(Id, W);
    }
    void DropSpeaker(const FString& Id)
    {
        if (!Waves.Remove(Id)) return;
        UE_LOG(LogEidoVoice, Log, TEXT("[voice] speaker %s left"), *Id);
        if (Owner.OnSpeakerRemoved) Owner.OnSpeakerRemoved(Id);
    }
    void ClearGameSpeakers() { TArray<FString> Ids; Waves.GetKeys(Ids); for (const FString& Id : Ids) DropSpeaker(Id); }

    // ── mic (game thread) ──
    void StartMicFlow()
    {
        if (Capture || !Cred) return;               // the device starts once there is a leg to publish on
        switch (EidoVoiceMic::Status())
        {
        case EidoVoiceMic::EStatus::Granted: Permission = EMicPermission::Granted; StartCapture(); break;
        case EidoVoiceMic::EStatus::NotDetermined:
            if (bPermissionAsking) break;
            bPermissionAsking = true;
            EidoVoiceMic::Request([W = TWeakPtr<FImpl, ESPMode::ThreadSafe>(AsShared())](bool bOk) {
                if (auto P = W.Pin()) P->PostGame([bOk](FImpl& I) {
                    I.bPermissionAsking = false; I.Permission = bOk ? EMicPermission::Granted : EMicPermission::Denied;
                    if (!bOk) { I.Notice(TEXT("microphone permission denied — allow it in System Settings › Privacy & Security › Microphone")); I.bWantMic = false; I.SyncPublishIntent(); }
                    else if (I.bWantMic) I.StartCapture();
                });
            });
            break;
        case EidoVoiceMic::EStatus::Denied:
            Permission = EMicPermission::Denied; bWantMic = false; SyncPublishIntent();
            Notice(TEXT("microphone permission denied — allow it in System Settings › Privacy & Security › Microphone"));
            break;
        default: Permission = EMicPermission::Unsupported; bWantMic = false; SyncPublishIntent(); Notice(TEXT("no microphone support on this platform")); break;
        }
    }
    void StartCapture()
    {
        if (Capture || !bWantMic) return;
        MicRing->Clear();
        TSharedPtr<FMicRing, ESPMode::ThreadSafe> R = MicRing;
        FString Err;
        Capture = EidoVoiceMic::FCapture::Start(bVoiceProcessing, [R](const float* Mono, int32 N, double DevRate) { R->DeviceRate.store(DevRate, std::memory_order_relaxed); R->Push(Mono, N); }, Err);
        if (!Capture) { Notice(TEXT("voice: ") + Err); bWantMic = false; SyncPublishIntent(); return; }
        UE_LOG(LogEidoVoice, Log, TEXT("[voice] mic live (voice processing %s)"), Capture->bVoiceProcessingActive ? TEXT("on") : TEXT("off"));
        MicGen.fetch_add(1); bMicLive = true;
        SyncPublishIntent();
    }
    void StopCapture()
    {
        if (!Capture) return;
        bMicLive = false; Capture.Reset();         // AVAudioEngine stop: the hardware light goes off
        UE_LOG(LogEidoVoice, Log, TEXT("[voice] mic device stopped"));
    }
    void SyncPublishIntent()
    {
        const bool bWant = bWantMic || ToneHz.load() > 0 || bSynthOn.load();
        bWantPublish = bWant;
        if (bWant && Cred) PostSig([E = Epoch](FImpl& I) { I.EnsurePublishedSig(E); });
    }

    void Tick()
    {
        TFunction<void()> Fn; while (GameQueue.Dequeue(Fn)) Fn();
        if (SynthBacklog.Num())
        {
            const int32 N = FMath::Min(SynthBacklog.Num(), SynthRing->Free() - 1);
            if (N > 0) { SynthRing->Push(SynthBacklog.GetData(), N); SynthBacklog.RemoveAt(0, N, EAllowShrinking::No); }
        }
        const double Now = FPlatformTime::Seconds();
        if (StallAt > 0 && Now > StallAt && (Ice == TEXT("checking") || Ice == TEXT("new")))
        {
            StallAt = 0;
            UE_LOG(LogEidoVoice, Error, TEXT("[voice] ICE STALLED — no media path after 8s"));
            Notice(TEXT("voice: no media path to the server — mic is on but nobody can hear you"));
            MediaDead(TEXT("ice-stalled"));
        }
        if (RecoveryAt > 0 && Now >= RecoveryAt) { RecoveryAt = 0; if (bStarted) RequestCred(); }
        if (bHavePos && (Ice == TEXT("connected") || Ice == TEXT("completed")) && Now - PosAt >= .5)
        {
            PosAt = Now;
            TSharedRef<FJsonObject> O = MakeShared<FJsonObject>(); O->SetStringField(TEXT("type"), TEXT("sfu-pos"));
            O->SetNumberField(TEXT("x"), FMath::RoundToDouble(Pos.X * 100) / 100); O->SetNumberField(TEXT("y"), FMath::RoundToDouble(Pos.Y * 100) / 100); O->SetNumberField(TEXT("z"), FMath::RoundToDouble(Pos.Z * 100) / 100);
            Send(O);
        }
        const int32 On = Onsets.load();
        if (On != SeenOnsets) { SeenOnsets = On; AnnounceMic(); }
    }
    void AnnounceMic()
    {   // micstate.js: the 🎙 onset, at most once per 1.5 s (level onset or PTT press)
        const double Now = FPlatformTime::Seconds();
        if (Now - LastAnnounce <= 1.5) return;
        LastAnnounce = Now;
        TSharedRef<FJsonObject> O = MakeShared<FJsonObject>(); O->SetStringField(TEXT("type"), TEXT("typing")); O->SetStringField(TEXT("state"), TEXT("mic")); Send(O);
    }

    // ═══ signaling thread ═══
    struct FObserver : public webrtc::PeerConnectionObserver
    {
        FImpl& Self; int32 E;
        FObserver(FImpl& In, int32 InE) : Self(In), E(InE) {}
        void OnSignalingChange(FPc::SignalingState) override {}
        void OnDataChannel(rtc::scoped_refptr<webrtc::DataChannelInterface>) override {}
        void OnRenegotiationNeeded() override {}    // the server owns every offer
        void OnIceGatheringChange(FPc::IceGatheringState) override {}
        void OnStandardizedIceConnectionChange(FPc::IceConnectionState S) override
        {
            const FString Name = IceName(S); const int32 Ep = E;
            Self.PostGame([Name, Ep](FImpl& I) { I.OnIceState(Name, Ep); });
        }
        void OnIceCandidate(const webrtc::IceCandidateInterface* C) override
        {
            std::string Sdp; if (!C || !C->ToString(&Sdp)) return;
            const FString Cand = Ue(Sdp), Mid = Ue(C->sdp_mid()); const int32 Idx = C->sdp_mline_index(), Ep = E;
            Self.PostGame([Cand, Mid, Idx, Ep](FImpl& I) {
                if (Ep != I.Epoch) return;
                TSharedRef<FJsonObject> O = MakeShared<FJsonObject>(), J = MakeShared<FJsonObject>();
                J->SetStringField(TEXT("candidate"), Cand); J->SetStringField(TEXT("sdpMid"), Mid); J->SetNumberField(TEXT("sdpMLineIndex"), Idx);
                O->SetStringField(TEXT("type"), TEXT("sfu-ice")); O->SetObjectField(TEXT("candidate"), J); O->SetNumberField(TEXT("gen"), double(I.MediaGen));
                I.Send(O);
            });
        }
        void OnTrack(rtc::scoped_refptr<webrtc::RtpTransceiverInterface> T) override { if (E == Self.PcEpoch) Self.OnTrackSig(T); }
    };
    struct FSrd : public webrtc::SetRemoteDescriptionObserverInterface
    {
        TWeakPtr<FImpl, ESPMode::ThreadSafe> W; int32 E;
        void OnSetRemoteDescriptionComplete(webrtc::RTCError Err) override { if (auto P = W.Pin()) P->AfterRemoteSig(Err, E); }
    };
    struct FSld : public webrtc::SetLocalDescriptionObserverInterface
    {
        TWeakPtr<FImpl, ESPMode::ThreadSafe> W; int32 E;
        void OnSetLocalDescriptionComplete(webrtc::RTCError Err) override { if (auto P = W.Pin()) P->AfterLocalSig(Err, E); }
    };

    void ClosePcOnSignaling()
    {
        {
            FScopeLock L(&SpeakersCs);
            for (auto& KV : Sinks) if (KV.Value->Track) { KV.Value->Track->RemoveSink(KV.Value.Get()); KV.Value->Track = nullptr; }
            Sinks.Empty();
        }
        if (Pc) { Pc->Close(); Pc = nullptr; }
        Observer.Reset(); PendingIce.clear(); RouteQueue.Reset(); PcEpoch = -1;
    }
    void ConnectSig(const TArray<std::pair<std::vector<std::string>, std::pair<std::string, std::string>>>& Servers, const std::string& Sid, int32 E)
    {
        ClosePcOnSignaling();                       // a reconnect closes its predecessor first
        StreamId = Sid;
        FPc::RTCConfiguration Cfg;
        Cfg.sdp_semantics = webrtc::SdpSemantics::kUnifiedPlan;
        Cfg.bundle_policy = FPc::kBundlePolicyMaxBundle;
        for (const auto& S : Servers) { FPc::IceServer I; I.urls = S.first; I.username = S.second.first; I.password = S.second.second; Cfg.servers.push_back(I); }
        Observer = MakeUnique<FObserver>(*this, E);
        auto R = Factory->CreatePeerConnectionOrError(Cfg, webrtc::PeerConnectionDependencies(Observer.Get()));
        if (!R.ok()) { UE_LOG(LogEidoVoice, Error, TEXT("[voice] peer connection failed: %s"), *Ue(R.error().message())); Observer.Reset(); return; }
        Pc = R.MoveValue(); PcEpoch = E;
    }
    void OnOfferSig(const std::string& Sdp, int32 E)
    {
        if (!Pc || E != PcEpoch) return;
        webrtc::SdpParseError Err;
        std::unique_ptr<webrtc::SessionDescriptionInterface> D = webrtc::CreateSessionDescription(webrtc::SdpType::kOffer, Sdp, &Err);
        if (!D) { UE_LOG(LogEidoVoice, Error, TEXT("[voice] bad offer: %s"), *Ue(Err.description)); return; }
        auto Obs = rtc::make_ref_counted<FSrd>(); Obs->W = AsShared(); Obs->E = E;
        Pc->SetRemoteDescription(std::move(D), Obs);
    }
    void AfterRemoteSig(webrtc::RTCError Err, int32 E)
    {
        if (!Pc || E != PcEpoch) return;
        if (!Err.ok()) { UE_LOG(LogEidoVoice, Error, TEXT("[voice] setRemoteDescription: %s"), *Ue(Err.message())); return; }
        for (auto& C : PendingIce) Pc->AddIceCandidate(C.get());
        PendingIce.clear();
        auto Obs = rtc::make_ref_counted<FSld>(); Obs->W = AsShared(); Obs->E = E;
        Pc->SetLocalDescription(Obs);               // implicit createAnswer
    }
    void AfterLocalSig(webrtc::RTCError Err, int32 E)
    {
        if (!Pc || E != PcEpoch) return;
        if (!Err.ok()) { UE_LOG(LogEidoVoice, Error, TEXT("[voice] setLocalDescription: %s"), *Ue(Err.message())); return; }
        std::string Sdp; if (const webrtc::SessionDescriptionInterface* L = Pc->local_description()) L->ToString(&Sdp);
        PostGame([Sdp, E](FImpl& I) { I.SendAnswer(Sdp, E); });
        EnsurePublishedSig(E);                      // rescue / first publish onto the floor (after the answer, then re-ask)
    }
    void OnRemoteIceSig(const std::string& Mid, int Idx, const std::string& Cand, int32 E)
    {
        if (!Pc || E != PcEpoch) return;
        webrtc::SdpParseError Err;
        std::unique_ptr<webrtc::IceCandidateInterface> C(webrtc::CreateIceCandidate(Mid, Idx, Cand, &Err));
        if (!C) return;
        if (!Pc->remote_description()) { PendingIce.push_back(std::move(C)); return; }   // early candidates are not garbage
        Pc->AddIceCandidate(C.get());
    }
    void OnTrackSig(rtc::scoped_refptr<webrtc::RtpTransceiverInterface> T)
    {
        if (!T || T->media_type() != cricket::MEDIA_TYPE_AUDIO) return;
        rtc::scoped_refptr<webrtc::RtpReceiverInterface> Rx = T->receiver();
        const std::vector<std::string> Ids = Rx->stream_ids();
        FString ViaQueue; if (RouteQueue.Num()) ViaQueue = RouteQueue[0], RouteQueue.RemoveAt(0);
        const FString ViaMsid = Ids.empty() ? FString() : Ue(Ids[0]);
        const FString Id = (!ViaMsid.IsEmpty() && ViaMsid != TEXT("default")) ? ViaMsid : ViaQueue;
        if (!ViaMsid.IsEmpty() && !ViaQueue.IsEmpty() && ViaMsid != TEXT("default") && ViaMsid != ViaQueue)
            UE_LOG(LogEidoVoice, Warning, TEXT("[voice] route identity mismatch — msid %s vs queue %s (trusting msid)"), *ViaMsid, *ViaQueue);
        if (Id.IsEmpty() || Ue(StreamId) == Id) return;
        rtc::scoped_refptr<webrtc::MediaStreamTrackInterface> Tr = Rx->track();
        if (!Tr || Tr->kind() != webrtc::MediaStreamTrackInterface::kAudioKind) return;
        rtc::scoped_refptr<webrtc::AudioTrackInterface> Audio(static_cast<webrtc::AudioTrackInterface*>(Tr.get()));
        {
            FScopeLock L(&SpeakersCs);
            TSharedPtr<FSpeakerSink>& S = Sinks.FindOrAdd(Id);
            if (!S) S = MakeShared<FSpeakerSink>();
            if (S->Track != Audio) { if (S->Track) S->Track->RemoveSink(S.Get()); Audio->AddSink(S.Get()); S->Track = Audio; }
        }
        PostGame([Id](FImpl& I) { I.EnsureWave(Id); });
    }
    // voicesfu.js's transceiver hunt: our sendonly sender wins (a swap), else the inactive floor the
    // server offered for our mic (replaceTrack + sendonly), addTrack only when neither exists; then ask.
    void EnsurePublishedSig(int32 E)
    {
        if (!Pc || E != PcEpoch || !bWantPublish || !Pc->remote_description()) return;
        using D = webrtc::RtpTransceiverDirection;
        const auto Ts = Pc->GetTransceivers();
        for (const auto& T : Ts)
        {
            if (T->media_type() != cricket::MEDIA_TYPE_AUDIO || T->stopped()) continue;
            const absl::optional<D> Cur = T->current_direction();
            const bool bMine = (Cur && *Cur == D::kSendOnly) || (!Cur && (T->direction() == D::kSendOnly || T->direction() == D::kSendRecv))
                || (T->direction() == D::kSendOnly && T->sender()->track() == LocalTrack);   // already moved, renegotiation pending
            if (!bMine) continue;
            if (T->sender()->track() != LocalTrack) T->sender()->SetTrack(LocalTrack.get());
            return;
        }
        for (const auto& T : Ts)
        {
            if (T->media_type() != cricket::MEDIA_TYPE_AUDIO || T->stopped()) continue;
            const absl::optional<D> Cur = T->current_direction();
            if (!((Cur && *Cur == D::kInactive) || T->direction() == D::kInactive)) continue;
            T->sender()->SetTrack(LocalTrack.get());
            T->sender()->SetStreams({ StreamId });
            const webrtc::RTCError R = T->SetDirectionWithError(D::kSendOnly);
            if (!R.ok()) { UE_LOG(LogEidoVoice, Error, TEXT("[voice] floor direction: %s"), *Ue(R.message())); return; }
            UE_LOG(LogEidoVoice, Log, TEXT("[voice] publishing on the floor m-line (mid %s)"), T->mid() ? *Ue(*T->mid()) : TEXT("?"));
            PostGame([E](FImpl& I) { if (E == I.Epoch) I.SendType(TEXT("sfu-want-negotiate")); });
            return;
        }
        auto R = Pc->AddTrack(LocalTrack, { StreamId });
        UE_LOG(LogEidoVoice, Log, TEXT("[voice] no floor offered — addTrack (%s)"), R.ok() ? TEXT("ok") : *Ue(R.error().message()));
        if (R.ok()) PostGame([E](FImpl& I) { if (E == I.Epoch) I.SendType(TEXT("sfu-want-negotiate")); });
    }

    // ═══ pump thread: playout pull + the send path (gate / tone / silence) ═══
    void PumpTick()
    {
        if (Adm) Adm->Pull();
        if (!Source) return;
        int16 Pcm[Frame];
        const float Hz = ToneHz.load(std::memory_order_relaxed);
        if (Hz > 0)
        {   // synthetic source: bypasses the gate, like a synth lane in micstate.js
            const float A = ToneAmp.load(std::memory_order_relaxed);
            for (int32 I = 0; I < Frame; ++I) { Pcm[I] = int16(FMath::Clamp(A * float(FMath::Sin(TonePhase)), -1.f, 1.f) * 32767.f); TonePhase += 2.0 * UE_DOUBLE_PI * Hz / Rate; }
            TonePhase = FMath::Fmod(TonePhase, 2.0 * UE_DOUBLE_PI);
            MicRing->Clear();
            Source->Deliver(Pcm);
            return;
        }
        if (!bMicLive.load() && bSynthOn.load() && SynthRing->Available() >= Frame)
        {   // synthesized speech stands in for the mic while it is off (tts.js)
            float F[Frame]; SynthRing->Pop(F, Frame);
            float Peak = 0;
            for (int32 I = 0; I < Frame; ++I) { Peak = FMath::Max(Peak, FMath::Abs(F[I])); Pcm[I] = int16(FMath::Clamp(F[I], -1.f, 1.f) * 32767.f); }
            SynthPeak = Peak;
            Source->Deliver(Pcm);
            return;
        }
        SynthPeak = 0;
        if (!bMicLive.load())
        {   // keep the published track's clock running: silence, exactly what a disabled browser track sends
            FMemory::Memzero(Pcm, sizeof(Pcm)); MicPeak = 0; MicRmsDb = -100; bSpeaking = false;
            Source->Deliver(Pcm);
            return;
        }
        const int32 Gen = MicGen.load();
        if (Gen != PumpMicGen) { PumpMicGen = Gen; ResetGate(); }
        MicRaw.Reset(); MicRing->Drain(MicRaw);
        { FScopeLock L(&TapCs); if (MicTap && MicRaw.Num()) MicTap(MicRaw.GetData(), MicRaw.Num(), MicRing->DeviceRate.load()); }
        MicRes.Run(MicRaw.GetData(), MicRaw.Num(), MicRing->DeviceRate.load() / Rate, Mic48);
        int32 Done = 0;
        while (Mic48.Num() - Done >= Frame) { GateFrame(Mic48.GetData() + Done, Pcm); Source->Deliver(Pcm); Done += Frame; }
        if (Done) Mic48.RemoveAt(0, Done, EAllowShrinking::No);
        if (Mic48.Num() > Rate / 2) Mic48.Reset();  // runaway backlog (should not happen): stay live
    }
    void ResetGate()
    {   // makeOnsetGate().start(): re-measure the room on every start, lane starts CLOSED
        bAbove = false; OpenUntil = 0; bAnnounced = false; Noise = .01f; Settle = 0;
        for (float& B : Blocks) B = 1e9f;
        BlockIdx = BlocksFilled = 0; BlockMin = 1e9f; BlockStart = Clock / 48;
        Delay.SetNumZeroed(Rate / 20); DelayPos = 0; Gain = 0;      // 50 ms lookahead
        FMemory::Memzero(Last512, sizeof(Last512)); LastPos = 0; MicRes = FResampler(); Mic48.Reset(); MicRing->Clear();
    }
    void GateFrame(const float* In, int16* Out)
    {
        for (int32 I = 0; I < Frame; ++I) { Last512[LastPos] = In[I]; LastPos = (LastPos + 1) & 511; }
        float Sq = 0, Peak = 0; for (float V : Last512) { Sq += V * V; Peak = FMath::Max(Peak, FMath::Abs(V)); }
        const float Rms = FMath::Sqrt(Sq / 512);
        const bool bMicOn = bMicLive.load() && !bMuted.load();
        MicPeak = bMicOn ? FMath::Min(Peak, 1.f) : 0.f;
        MicRmsDb = bMicOn ? FMath::Max(-100.f, 20.f * FMath::LogX(10.f, FMath::Max(Rms, 1e-6f))) : -100.f;
        Clock += Frame;
        const int64 Now = Clock / 48;               // ms on the sample clock
        if ((TickAccum += Frame) >= 960) { TickAccum -= 960; GateTick(bMicOn ? Rms : 0.f, Now); }
        // decide(): the held key in PTT mode, else level + hang; the lane's authority (live, unmuted) on top
        const bool bOpen = aPtt.load() ? aKeyDown.load() : (bAbove || Now <= OpenUntil);
        const bool bWant = bMicOn && bOpen;
        bSpeaking = bWant;
        const float Target = bWant ? 1.f : 0.f;
        const float Coef = 1.f - FMath::Exp(-1.f / ((bWant ? .008f : .12f) * Rate));   // attack τ 8 ms, release τ 120 ms
        const int32 DN = Delay.Num();
        for (int32 I = 0; I < Frame; ++I)
        {
            const float Delayed = Delay[DelayPos]; Delay[DelayPos] = In[I]; DelayPos = (DelayPos + 1) % DN;
            Gain += (Target - Gain) * Coef;
            Out[I] = int16(FMath::Clamp(Delayed * Gain, -1.f, 1.f) * 32767.f);
        }
    }
    void GateTick(float Lv, int64 Now)
    {   // micgate.js makeOnsetGate tick(), verbatim in shape
        if (Lv <= 0) { bAbove = false; OpenUntil = 0; return; }
        if (Settle < 8) { ++Settle; Noise += (Lv - Noise) * (Lv < Noise ? .3f : .5f); return; }   // measuring, still CLOSED
        if (Lv < BlockMin) BlockMin = Lv;
        if (Now - BlockStart >= 400)
        {
            BlockStart = Now; Blocks[BlockIdx] = BlockMin; BlockIdx = (BlockIdx + 1) % 6;
            if (BlocksFilled < 6) ++BlocksFilled;
            BlockMin = 1e9f;
        }
        float Floor = BlockMin; for (int32 I = 0; I < BlocksFilled; ++I) Floor = FMath::Min(Floor, Blocks[I]);
        Noise = FMath::Max(1e-5f, Floor);
        const float OnT = Threshold.load(), OffT = OnT * .7f;
        if (Lv >= OnT)
        {
            OpenUntil = Now + 700;
            if (!bAbove) { bAbove = true; OpenedAt = Now; bAnnounced = false; }
            if (!aPtt.load() && !bAnnounced && Now - OpenedAt >= 60 && Now - LastOnset > 1500) { bAnnounced = true; LastOnset = Now; Onsets.fetch_add(1); }
        }
        else if (bAbove && Lv < OffT && Now > OpenUntil) bAbove = false;
    }
};

// ═══ FEidoVoice facade ═══
FEidoVoice::FEidoVoice(TFunction<void(TSharedRef<FJsonObject>)> InSendSignal)
    : Impl(MakeShared<FImpl, ESPMode::ThreadSafe>(*this, MoveTemp(InSendSignal))) { SetMicSensitivity(.12f); }
FEidoVoice::~FEidoVoice() { OnSpeakerAdded = nullptr; OnSpeakerRemoved = nullptr; OnNotice = nullptr; Impl->Shutdown(); Impl.Reset(); }

void FEidoVoice::OnSignal(const TSharedPtr<FJsonObject>& Msg) { Impl->OnSignal(Msg); }
void FEidoVoice::Start()
{
    FImpl& I = *Impl;
    if (I.bStarted) return;
    if (!I.EnsureMedia()) { I.Notice(TEXT("voice: WebRTC failed to initialise")); return; }
    I.bStarted = true; I.RecoveryAttempts = 0; I.RecoveryAt = 0;
    I.RequestCred();                                // once per join: relay-cred + the current consent
}
void FEidoVoice::Stop()
{
    FImpl& I = *Impl;
    I.bStarted = false; I.Cred.Reset(); I.MediaGen = -1; ++I.Epoch; I.Ice = TEXT("none"); I.StallAt = I.RecoveryAt = 0;
    I.StopCapture();                                // intent (bWantMic) survives for the next Start
    if (I.Signaling) I.Signaling->BlockingCall([&I] { I.ClosePcOnSignaling(); });
    I.ClearGameSpeakers();
}
void FEidoVoice::Tick(float) { Impl->Tick(); }
void FEidoVoice::SetReceiveConsent(bool bRecv) { Impl->bRecv = bRecv; if (Impl->bStarted) Impl->SendConsent(); }
void FEidoVoice::SetMicEnabled(bool bOn)
{
    FImpl& I = *Impl;
    I.bWantMic = bOn;
    if (bOn) { if (I.bStarted) I.StartMicFlow(); }
    else I.StopCapture();
    I.SyncPublishIntent();
}
void FEidoVoice::SetMuted(bool bMute) { Impl->bMuted = bMute; }
void FEidoVoice::SetPushToTalk(bool bMode) { Impl->bPtt = bMode; Impl->aPtt = bMode; if (!bMode) { Impl->bKeyDown = false; Impl->aKeyDown = false; } }
void FEidoVoice::SetTalkKeyDown(bool bDown)
{
    FImpl& I = *Impl;
    if (bDown == I.bKeyDown) return;
    I.bKeyDown = bDown; I.aKeyDown = bDown;
    if (bDown && I.bPtt && I.bMicLive.load() && !I.bMuted.load()) I.AnnounceMic();   // the press is the gesture
}
void FEidoVoice::SetMicSensitivity(float MicFloor0to1)
{
    const float Db = -60.f + FMath::Clamp(MicFloor0to1 / .2f, 0.f, 1.f) * 60.f;
    Impl->Threshold = FMath::Pow(10.f, Db / 20.f);
}
void FEidoVoice::SetListenerPosition(const FVector& WireMetres) { Impl->Pos = WireMetres; Impl->bHavePos = !WireMetres.ContainsNaN(); }
void FEidoVoice::SetVoiceProcessing(bool bOn) { Impl->bVoiceProcessing = bOn; }
void FEidoVoice::SetTestTone(float Hz, float Amplitude) { Impl->ToneAmp = Amplitude; Impl->ToneHz = FMath::Max(0.f, Hz); Impl->SyncPublishIntent(); }
void FEidoVoice::SetSynthEnabled(bool bOn) { Impl->bSynthOn = bOn; if (!bOn) ClearSynth(); Impl->SyncPublishIntent(); }
void FEidoVoice::QueueSynth(const TArray<float>& Pcm48k) { if (Impl->bSynthOn.load()) Impl->SynthBacklog.Append(Pcm48k); }
void FEidoVoice::ClearSynth() { Impl->SynthBacklog.Reset(); Impl->SynthRing->Clear(); }
bool FEidoVoice::IsSynthPlaying() const { return Impl->SynthBacklog.Num() > 0 || Impl->SynthRing->Available() > 0; }
float FEidoVoice::GetSynthLevel() const { return Impl->SynthPeak.load(); }
void FEidoVoice::SetMicTap(TFunction<void(const float*, int32, double)> Tap) { FScopeLock L(&Impl->TapCs); Impl->MicTap = MoveTemp(Tap); }

bool FEidoVoice::IsConnected() const { return Impl->Ice == TEXT("connected") || Impl->Ice == TEXT("completed"); }
bool FEidoVoice::IsMicEnabled() const { return Impl->bWantMic; }
bool FEidoVoice::IsMicLive() const { return Impl->bMicLive.load(); }
bool FEidoVoice::IsMuted() const { return Impl->bMuted.load(); }
bool FEidoVoice::IsSpeaking() const { return Impl->bSpeaking.load(); }
FEidoVoice::EMicPermission FEidoVoice::GetMicPermission() const { return Impl->Permission; }
float FEidoVoice::GetMicLevel() const { return Impl->bMicLive.load() && !Impl->bMuted.load() ? Impl->MicPeak.load() : 0.f; }
float FEidoVoice::GetMicRmsDb() const { return Impl->MicRmsDb.load(); }
float FEidoVoice::GetGateThresholdDb() const { return 20.f * FMath::LogX(10.f, FMath::Max(Impl->Threshold.load(), 1e-6f)); }
float FEidoVoice::GetSpeakerLevel(const FString& SpeakerId) const
{
    if (!Impl->Waves.Contains(SpeakerId)) return 0.f;
    FScopeLock L(&Impl->SpeakersCs);
    const TSharedPtr<FSpeakerSink>* S = Impl->Sinks.Find(SpeakerId);
    return S ? (*S)->Peak.load(std::memory_order_relaxed) : 0.f;
}
TMap<FString, float> FEidoVoice::GetSpeakerLevels() const
{
    TMap<FString, float> Out;
    for (const auto& KV : Impl->Waves) Out.Add(KV.Key, GetSpeakerLevel(KV.Key));
    return Out;
}
TMap<FString, USoundWaveProcedural*> FEidoVoice::GetSpeakerWaves() const
{
    TMap<FString, USoundWaveProcedural*> Out;
    for (const auto& KV : Impl->Waves) Out.Add(KV.Key, KV.Value.Get());
    return Out;
}
FString FEidoVoice::GetSelfId() const { return Impl->SelfId; }
FString FEidoVoice::DescribeState() const
{
    const FImpl& I = *Impl;
    return FString::Printf(TEXT("voice %s ice=%s gen=%lld speakers=%d mic=%s%s%s recv=%d"), I.bStarted ? TEXT("started") : TEXT("stopped"), *I.Ice, I.MediaGen,
        I.Waves.Num(), I.bMicLive.load() ? TEXT("live") : I.bWantMic ? TEXT("wanted") : TEXT("off"), I.bMuted.load() ? TEXT(" muted") : TEXT(""),
        I.ToneHz.load() > 0 ? TEXT(" tone") : I.bSynthOn.load() ? TEXT(" synth") : TEXT(""), I.bRecv);
}
void FEidoVoice::AddReferencedObjects(FReferenceCollector& Collector)
{
    if (Impl) for (auto& KV : Impl->Waves) Collector.AddReferencedObject(KV.Value);
}

#else   // no libwebrtc on this platform: an inert client that says so once

struct FEidoVoice::FImpl { TMap<FString, TObjectPtr<UEidoVoiceSpeakerWave>> Waves; bool bTold = false; };
FEidoVoice::FEidoVoice(TFunction<void(TSharedRef<FJsonObject>)>) : Impl(MakeShared<FImpl, ESPMode::ThreadSafe>()) {}
FEidoVoice::~FEidoVoice() {}
void FEidoVoice::OnSignal(const TSharedPtr<FJsonObject>&) {}
void FEidoVoice::Start() { if (!Impl->bTold && OnNotice) OnNotice(TEXT("voice is not supported on this platform")); Impl->bTold = true; }
void FEidoVoice::Stop() {}
void FEidoVoice::Tick(float) {}
void FEidoVoice::SetReceiveConsent(bool) {}
void FEidoVoice::SetMicEnabled(bool) {}
void FEidoVoice::SetMuted(bool) {}
void FEidoVoice::SetPushToTalk(bool) {}
void FEidoVoice::SetTalkKeyDown(bool) {}
void FEidoVoice::SetMicSensitivity(float) {}
void FEidoVoice::SetListenerPosition(const FVector&) {}
void FEidoVoice::SetVoiceProcessing(bool) {}
void FEidoVoice::SetTestTone(float, float) {}
void FEidoVoice::SetSynthEnabled(bool) {}
void FEidoVoice::QueueSynth(const TArray<float>&) {}
void FEidoVoice::ClearSynth() {}
bool FEidoVoice::IsSynthPlaying() const { return false; }
float FEidoVoice::GetSynthLevel() const { return 0; }
void FEidoVoice::SetMicTap(TFunction<void(const float*, int32, double)>) {}
bool FEidoVoice::IsConnected() const { return false; }
bool FEidoVoice::IsMicEnabled() const { return false; }
bool FEidoVoice::IsMicLive() const { return false; }
bool FEidoVoice::IsMuted() const { return false; }
bool FEidoVoice::IsSpeaking() const { return false; }
FEidoVoice::EMicPermission FEidoVoice::GetMicPermission() const { return EMicPermission::Unsupported; }
float FEidoVoice::GetMicLevel() const { return 0; }
float FEidoVoice::GetMicRmsDb() const { return -100; }
float FEidoVoice::GetGateThresholdDb() const { return -24; }
float FEidoVoice::GetSpeakerLevel(const FString&) const { return 0; }
TMap<FString, float> FEidoVoice::GetSpeakerLevels() const { return {}; }
TMap<FString, USoundWaveProcedural*> FEidoVoice::GetSpeakerWaves() const { return {}; }
FString FEidoVoice::GetSelfId() const { return FString(); }
FString FEidoVoice::DescribeState() const { return TEXT("voice unsupported"); }
void FEidoVoice::AddReferencedObjects(FReferenceCollector&) {}
#endif

#if !PLATFORM_MAC
namespace EidoVoiceMic
{
EStatus Status() { return EStatus::Unsupported; }
void Request(TFunction<void(bool)> Done) { Done(false); }
TUniquePtr<FCapture> FCapture::Start(bool, TFunction<void(const float*, int32, double)>, FString& OutError) { OutError = TEXT("no microphone support on this platform"); return nullptr; }
FCapture::~FCapture() {}
}
#endif
