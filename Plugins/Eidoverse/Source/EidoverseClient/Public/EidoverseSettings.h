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
    EIDOVERSECLIENT_API void Set(const TCHAR* Name, const FString& Value);
    EIDOVERSECLIENT_API void Load();
    EIDOVERSECLIENT_API void Save();
    EIDOVERSECLIENT_API void ApplyInterfaceScale();
}
