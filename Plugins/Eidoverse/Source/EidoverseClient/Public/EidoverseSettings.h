#pragma once
#include "CoreMinimal.h"

// Client preferences, as console variables (scriptable, Blueprint-readable via
// the settings frame). Persisted per user by UEidoverseHUD in GameUserSettings.
namespace EidoSettings
{
    EIDOVERSECLIENT_API float LookSensitivity();   // Eidoverse.Look.Sensitivity, multiplier
    EIDOVERSECLIENT_API bool InvertLook();         // Eidoverse.Look.InvertY
    EIDOVERSECLIENT_API bool Nameplates();         // Eidoverse.UI.Nameplates
    EIDOVERSECLIENT_API bool Bubbles();            // Eidoverse.UI.Bubbles
    EIDOVERSECLIENT_API float PanelOpacity();      // Eidoverse.UI.PanelOpacity, 0.3..1 (web --panel-a)
    EIDOVERSECLIENT_API float InterfaceScale();    // Eidoverse.UI.Scale, 0.75..1.5
    EIDOVERSECLIENT_API bool Lumen();              // Eidoverse.World.Lumen, applies on the next join
    EIDOVERSECLIENT_API bool Posable();            // Eidoverse.Consent.Posable (default off)
    EIDOVERSECLIENT_API bool Pushable();           // Eidoverse.Consent.Pushable (default on)
    EIDOVERSECLIENT_API float WorldVolume();       // Eidoverse.Audio.World
    EIDOVERSECLIENT_API float VoiceVolume();       // Eidoverse.Audio.Voices
    EIDOVERSECLIENT_API bool HearVoices();         // Eidoverse.Audio.HearVoices (receive consent, default off)
    EIDOVERSECLIENT_API float MicFloor();          // Eidoverse.Audio.MicFloor, 0..0.2 (web micFloor, default .12 = -24 dBFS)
    EIDOVERSECLIENT_API bool PushToTalk();         // Eidoverse.Audio.PushToTalk (default off): V held opens the mic
    EIDOVERSECLIENT_API bool VoiceProcessing();    // Eidoverse.Audio.VoiceProcessing: OS echo cancellation (default on)
    // Attention (attention.js): a short local tone for a live mention or a whisper to me. Default off; its own
    // volume (default 20%), independent of world and voice volume; soft | chime | tap.
    EIDOVERSECLIENT_API bool AttentionSound();     // Eidoverse.Audio.Attention
    EIDOVERSECLIENT_API float AttentionVolume();   // Eidoverse.Audio.AttentionVolume, 0..1
    EIDOVERSECLIENT_API FString AttentionTone();   // Eidoverse.Audio.AttentionTone
    // Video (videopanel.js / governor.js): render scale auto|1|.85|.7, shadows off or
    // 1024|2048|4096, particle tier auto|med|low (count x1|.5|.25), avatar detail
    // auto|full|half|low (far-body animation rate x1|1|2|4).
    EIDOVERSECLIENT_API float RenderScale();       // Eidoverse.Video.RenderScale, 0 = auto
    EIDOVERSECLIENT_API int32 ShadowRes();         // Eidoverse.Video.Shadows, 0 = off
    EIDOVERSECLIENT_API float ParticleScale();     // Eidoverse.Video.Particles 0 auto, 1 med, 2 low
    EIDOVERSECLIENT_API int32 ParticleTier();
    EIDOVERSECLIENT_API int32 AvatarDetail();      // Eidoverse.Video.AvatarDetail 0 auto, 1 full, 2 half, 3 low
    EIDOVERSECLIENT_API int32 AvatarLodBias();
    EIDOVERSECLIENT_API void ApplyVideo();
    EIDOVERSECLIENT_API void Set(const TCHAR* Name, const FString& Value);
    EIDOVERSECLIENT_API void Load();
    EIDOVERSECLIENT_API void Save();
    EIDOVERSECLIENT_API void ApplyInterfaceScale();
}
