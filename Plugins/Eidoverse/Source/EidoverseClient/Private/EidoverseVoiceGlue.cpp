// Voice in the world (voicesfubridge.js / voicemouths.js / mictoggle.js): FEidoVoice
// carries the SFU leg; this side forwards its signalling over the world socket,
// plays each speaker from their avatar (2D, linear 3→20 m rolloff x voice volume,
// eased over 0.7 s), drives mouths from levels, and owns V and the voice prefs.
// Off by default in both directions: hearing needs consent, the mic needs a click.
#include "EidoverseBridge.h"
#include "EidoverseVoice.h"
#include "EidoverseUI.h"
#include "EidoverseAvatar.h"
#include "EidoverseSettings.h"
#include "Components/AudioComponent.h"
#include "Sound/SoundWaveProcedural.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Dom/JsonObject.h"
#include "EidoverseSTT.h"
#include "Async/Async.h"

void AEidoverseBridge::EnsureVoice()
{
    if (Voice) return;
    TWeakObjectPtr<AEidoverseBridge> Weak(this);
    Voice = MakeShared<FEidoVoice>([Weak](TSharedRef<FJsonObject> Msg) { if (Weak.IsValid()) Weak->Send(Msg); });
    Voice->OnSpeakerAdded = [Weak](const FString& Id, USoundWaveProcedural* Wave)
    {
        if (!Weak.IsValid() || !Wave) return;
        AEidoverseBridge& B = *Weak;
        FVoiceSpeaker& S = B.VoiceSpeakers.FindOrAdd(Id);
        if (S.Audio.IsValid()) S.Audio->DestroyComponent();
        // On their avatar when we have one (follows them), else on the bridge until they appear.
        AActor* Host = B.Peers.Contains(Id) && B.Peers[Id].Actor.IsValid() ? B.Peers[Id].Actor.Get() : &B;
        auto* A = NewObject<UAudioComponent>(Host);
        A->bAutoActivate = false; A->bAllowSpatialization = false; A->bIsUISound = false;
        A->SetSound(Wave);
        A->SetVolumeMultiplier(0);
        if (Host->GetRootComponent()) A->SetupAttachment(Host->GetRootComponent());
        A->RegisterComponent();
        A->Play();
        S.Audio = A; S.Volume = 0; S.NextTarget = 0;
    };
    Voice->OnSpeakerRemoved = [Weak](const FString& Id)
    {
        if (!Weak.IsValid()) return;
        if (FVoiceSpeaker* S = Weak->VoiceSpeakers.Find(Id)) if (S->Audio.IsValid()) S->Audio->DestroyComponent();
        Weak->VoiceSpeakers.Remove(Id);
        if (FEidoversePeer* P = Weak->Peers.Find(Id)) if (P->AvatarMesh.IsValid()) P->AvatarMesh->SetVoiceLevel(-1);
    };
    Voice->OnNotice = [Weak](const FString& Line) { if (Weak.IsValid()) Weak->Narrate(Line); };
}

void AEidoverseBridge::ApplyVoicePrefs()
{
    EnsureVoice();
    Voice->SetPushToTalk(EidoSettings::PushToTalk());
    Voice->SetMicSensitivity(EidoSettings::MicFloor());
    Voice->SetVoiceProcessing(EidoSettings::VoiceProcessing());
    Voice->SetReceiveConsent(EidoSettings::HearVoices());
    Voice->SetSynthEnabled(IsTtsOn());
}

void AEidoverseBridge::VoiceJoined()
{
    EnsureVoice();
    if (bSpectator) return;
    ApplyVoicePrefs();
    Voice->Start();
}

void AEidoverseBridge::VoiceLost()
{
    if (Stt) { Stt->Stop(); if (Voice) Voice->SetMicTap(nullptr); }
    if (Voice) Voice->Stop();
    for (auto& Pair : VoiceSpeakers) if (Pair.Value.Audio.IsValid()) Pair.Value.Audio->DestroyComponent();
    VoiceSpeakers.Reset();
}

bool AEidoverseBridge::ForwardVoiceSignal(const FString& Type, const TSharedPtr<FJsonObject>& J)
{
    static const TSet<FString> Types = {TEXT("relay-cred"), TEXT("sfu-offer"), TEXT("sfu-route"), TEXT("sfu-ice"), TEXT("surface-transition"),
                                        TEXT("voice-service"), TEXT("voice-consent"), TEXT("voice-moderated"), TEXT("error")};
    if (!Voice || !Types.Contains(Type)) return false;
    Voice->OnSignal(J);
    return Type != TEXT("error");   // errors still reach the person through the usual path
}

void AEidoverseBridge::SetMicOn(bool bOn)
{
    EnsureVoice();
    Voice->SetMicEnabled(bOn);
    if (HUD) HUD->Hint(bOn ? (EidoSettings::PushToTalk() ? TEXT("mic on — hold V to talk") : TEXT("mic on — V to turn off")) : TEXT("mic off"), 2.5f);
}

void AEidoverseBridge::SetHearVoices(bool bOn)
{
    EidoSettings::Set(TEXT("Eidoverse.Audio.HearVoices"), bOn ? TEXT("1") : TEXT("0"));
    EnsureVoice();
    Voice->SetReceiveConsent(bOn);
    if (!bOn) { bVoiceHushed = false; for (auto& Pair : VoiceSpeakers) { Pair.Value.Target = 0; Pair.Value.NextTarget = 0; } }
    Narrate(bOn ? TEXT("you can hear people's voices now") : TEXT("voices off — nobody's audio reaches you"));
}

bool AEidoverseBridge::IsMicOn() const { return Voice && Voice->IsMicEnabled(); }
FString AEidoverseBridge::VoiceDiagnostic() const { return Voice ? Voice->DescribeState() : TEXT("voice not started"); }
float AEidoverseBridge::GetVoiceLevel(const FString& Id) const { return !Voice ? 0.f : Id == AssignedIdentity ? Voice->GetMicLevel() : Voice->GetSpeakerLevel(Id); }
float AEidoverseBridge::GetMicRmsDb() const { return Voice ? Voice->GetMicRmsDb() : -100.f; }
void AEidoverseBridge::SetVoiceTestTone(float Hz, float Amplitude) { EnsureVoice(); Voice->SetTestTone(Hz, Amplitude); }
TArray<FString> AEidoverseBridge::GetVoiceSpeakers() const { TArray<FString> Out; VoiceSpeakers.GetKeys(Out); return Out; }

void AEidoverseBridge::TickVoice(float Dt)
{
    if (!Voice) return;
    Voice->Tick(Dt);
    TickTts(FPlatformTime::Seconds());
    TickStt();
    const APawn* Me = UGameplayStatics::GetPlayerPawn(this, 0);
    if (Me) Voice->SetListenerPosition(ToWirePosition(Me->GetActorLocation() - OriginCentimeters));
    // V: toggle the mic, or talk while held in push-to-talk (never while typing).
    if (APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0))
    {
        const bool bTyping = HUD && HUD->IsChatFocused();
        const bool bShift = PC->IsInputKeyDown(EKeys::LeftShift) || PC->IsInputKeyDown(EKeys::RightShift);
        if (EidoSettings::PushToTalk())
        {
            if (!Voice->IsMicEnabled() && !bTyping && PC->WasInputKeyJustPressed(EKeys::V) && !bShift) SetMicOn(true);
            Voice->SetTalkKeyDown(!bTyping && PC->IsInputKeyDown(EKeys::V) && FApp::HasFocus());
        }
        else if (!bTyping && !bShift && PC->WasInputKeyJustPressed(EKeys::V)) SetMicOn(!Voice->IsMicEnabled());
    }
    // Speakers: re-home the sink onto their avatar once it exists; volume by distance.
    const double Now = FPlatformTime::Seconds();
    for (auto& Pair : VoiceSpeakers)
    {
        FVoiceSpeaker& S = Pair.Value;
        UAudioComponent* A = S.Audio.Get(); if (!A) continue;
        const FEidoversePeer* P = Peers.Find(Pair.Key);
        AActor* Body = P && P->Actor.IsValid() ? P->Actor.Get() : nullptr;
        if (Body && A->GetOwner() != Body && Body->GetRootComponent())
            A->AttachToComponent(Body->GetRootComponent(), FAttachmentTransformRules::SnapToTargetNotIncludingScale);
        if (Now >= S.NextTarget)
        {
            S.NextTarget = Now + .3;
            const float D = Body && Me ? float(FVector::Dist(Body->GetActorLocation(), Me->GetActorLocation()) / 100) : 1e9f;
            // Consent withdrawn: silent here at once, whatever the wire still carries.
            S.Target = EidoSettings::HearVoices() ? FEidoVoice::TargetVolume(D, bVoiceHushed, EidoSettings::VoiceVolume()) : 0.f;
        }
        S.Volume = FEidoVoice::StepVolume(S.Volume, S.Target, Dt);
        A->SetVolumeMultiplier(FMath::Max(S.Volume, .0001f));
    }
    // Mouths (voicemouths.js): others from their decoded level, mine from the raw mic while live.
    for (auto& Pair : Peers)
    {
        UEidoverseAvatarMesh* M = Pair.Value.AvatarMesh.Get(); if (!M) continue;
        const bool bVoice = VoiceSpeakers.Contains(Pair.Key);
        const float L = bVoice ? Voice->GetSpeakerLevel(Pair.Key) : -1.f;
        M->SetVoiceLevel(L);
        if (L > .045f) { Pair.Value.SpeakingUntil = FMath::Max(Pair.Value.SpeakingUntil, Now + .6); M->NoteSpeaking(Now + .6); }
    }
    if (UEidoverseAvatarMesh* M = SelfBody.AvatarMesh.Get())
    {
        const bool bLive = Voice->IsMicLive() && !Voice->IsMuted();
        const bool bSynth = !bLive && Voice->IsSynthPlaying();   // my TTS voice moves my mouth too
        const float L = bLive ? Voice->GetMicLevel() : bSynth ? Voice->GetSynthLevel() : -1.f;
        if (bSynth && L > .045f) { SelfSpeakingUntil = FMath::Max(SelfSpeakingUntil, Now + .4); M->NoteSpeaking(Now + .4); }
        M->SetVoiceLevel(L);
        if (bLive && Voice->IsSpeaking() && L > .045f) { SelfSpeakingUntil = FMath::Max(SelfSpeakingUntil, Now + .4); M->NoteSpeaking(Now + .4); }
    }
}

// ---------------------------------------------------------------- speech-to-text
namespace { TAutoConsoleVariable<int32> CVarSttConsent(TEXT("Eidoverse.Audio.SttConsent"), -1,
    TEXT("Speech-to-text consent: -1 never asked, 0 no, 1 yes (your mic audio goes to Apple's recognizer, on-device when supported).")); }

bool AEidoverseBridge::IsSttRunning() const { return Stt && Stt->IsRunning(); }

void AEidoverseBridge::SetSttConsent(bool bOn)
{
    EidoSettings::Set(TEXT("Eidoverse.Audio.SttConsent"), bOn ? TEXT("1") : TEXT("0"));
    Narrate(bOn ? TEXT("speech-to-text on: while your mic is live, what you say becomes chat (Apple's recognizer — on-device when your language supports it)")
                : TEXT("speech-to-text off"));
}

void AEidoverseBridge::TickStt()
{
    // stt.js: runs only with consent, a live mic, and a recognizer; finals become says.
    const bool bWant = CVarSttConsent.GetValueOnGameThread() == 1 && Voice && Voice->IsMicLive() && !Voice->IsMuted() && bJoined;
    if (!bWant)
    {
        if (Stt && Stt->IsRunning()) { Stt->Stop(); Voice->SetMicTap(nullptr); }
        return;
    }
    if (!Stt) Stt = MakeShared<FEidoStt>();
    if (!Stt->IsRunning())
    {
        const FEidoStt::EAuth Auth = FEidoStt::Authorization();
        if (Auth == FEidoStt::EAuth::Unknown)
        {
            if (!bSttAuthPending)
            {
                bSttAuthPending = true;
                TWeakObjectPtr<AEidoverseBridge> Weak(this);
                FEidoStt::RequestAuthorization([Weak](bool bOk) { AsyncTask(ENamedThreads::GameThread, [Weak, bOk]() {
                    if (!Weak.IsValid()) return;
                    Weak->bSttAuthPending = false;
                    if (!bOk) { Weak->Narrate(TEXT("speech-to-text: permission denied (System Settings → Privacy → Speech Recognition)")); EidoSettings::Set(TEXT("Eidoverse.Audio.SttConsent"), TEXT("0")); }
                }); });
            }
            return;
        }
        if (Auth != FEidoStt::EAuth::Granted) { Narrate(TEXT("speech-to-text isn't permitted on this machine")); EidoSettings::Set(TEXT("Eidoverse.Audio.SttConsent"), TEXT("0")); return; }
        FString Err;
        if (!Stt->Start(Err)) { Narrate(TEXT("speech-to-text: ") + Err); EidoSettings::Set(TEXT("Eidoverse.Audio.SttConsent"), TEXT("0")); return; }
        TWeakPtr<FEidoStt> WeakStt = Stt;
        Voice->SetMicTap([WeakStt](const float* Data, int32 Num, double Rate) { if (TSharedPtr<FEidoStt> S = WeakStt.Pin()) S->Feed(Data, Num, Rate); });
    }
    TArray<FString> Finals; FString Err;
    Stt->Poll(Finals, Err);
    if (!Err.IsEmpty()) Narrate(TEXT("speech-to-text: ") + Err.Left(200));
    for (const FString& F : Finals)
    {
        const FString Text = F.TrimStartAndEnd().Left(1000);
        if (Text.IsEmpty()) continue;
        auto A = MakeShared<FJsonObject>();
        A->SetStringField(TEXT("text"), TEXT("\U0001F399 ") + Text);
        A->SetBoolField(TEXT("voiced"), true); A->SetBoolField(TEXT("spoken"), true); A->SetNumberField(TEXT("utt"), ++SttUtt);
        auto J = MakeShared<FJsonObject>();
        J->SetStringField(TEXT("type"), TEXT("verb")); J->SetStringField(TEXT("verb"), TEXT("say")); J->SetObjectField(TEXT("args"), A);
        Send(J);
    }
}
