#include "EidoverseSettings.h"
#include "HAL/IConsoleManager.h"
#include "Misc/ConfigCacheIni.h"
#include "Engine/UserInterfaceSettings.h"

namespace
{
    TAutoConsoleVariable<float> CVarSensitivity(TEXT("Eidoverse.Look.Sensitivity"), 1.f, TEXT("Mouselook sensitivity multiplier."));
    TAutoConsoleVariable<int32> CVarInvert(TEXT("Eidoverse.Look.InvertY"), 0, TEXT("1: invert vertical mouselook."));
    TAutoConsoleVariable<int32> CVarPlates(TEXT("Eidoverse.UI.Nameplates"), 1, TEXT("Show names over participants."));
    TAutoConsoleVariable<int32> CVarBubbles(TEXT("Eidoverse.UI.Bubbles"), 1, TEXT("Show chat bubbles over participants."));
    TAutoConsoleVariable<float> CVarOpacity(TEXT("Eidoverse.UI.PanelOpacity"), .78f, TEXT("Panel background opacity, 0.3..1."));
    TAutoConsoleVariable<int32> CVarLumen(TEXT("Eidoverse.World.Lumen"), 1, TEXT("Use hardware Lumen for server worlds when available (next join)."));
    TAutoConsoleVariable<float> CVarScale(TEXT("Eidoverse.UI.Scale"), 1.f, TEXT("Interface size multiplier for the game viewport, 0.75..1.5."));
    TAutoConsoleVariable<int32> CVarPosable(TEXT("Eidoverse.Consent.Posable"), 0, TEXT("1: others may pose you or play animations on your body (web: posable, default off)."));
    TAutoConsoleVariable<int32> CVarPushable(TEXT("Eidoverse.Consent.Pushable"), 1, TEXT("1: shoves and blasts may knock you over (web: pushable, default on)."));
    TAutoConsoleVariable<float> CVarWorldVolume(TEXT("Eidoverse.Audio.World"), 1.f, TEXT("World sound volume, 0..1."));
    TAutoConsoleVariable<float> CVarVoiceVolume(TEXT("Eidoverse.Audio.Voices"), 1.f, TEXT("Voice volume, 0..1."));
    TAutoConsoleVariable<int32> CVarHearVoices(TEXT("Eidoverse.Audio.HearVoices"), 0, TEXT("1: connect to other people's voices (receive consent; web recvVoice, default off)."));
    TAutoConsoleVariable<float> CVarMicFloor(TEXT("Eidoverse.Audio.MicFloor"), .12f, TEXT("Voice-activation sensitivity, 0..0.2 (threshold dB = -60 + floor/0.2*60)."));
    TAutoConsoleVariable<int32> CVarPtt(TEXT("Eidoverse.Audio.PushToTalk"), 0, TEXT("1: the mic opens only while V is held."));
    TAutoConsoleVariable<int32> CVarVpio(TEXT("Eidoverse.Audio.VoiceProcessing"), 1, TEXT("1: OS echo cancellation / noise suppression on the mic (next mic start)."));
    const TCHAR* Section = TEXT("/Script/EidoverseClient.Settings");
    const TCHAR* Names[] = {TEXT("Eidoverse.Look.Sensitivity"), TEXT("Eidoverse.Look.InvertY"), TEXT("Eidoverse.UI.Nameplates"),
                            TEXT("Eidoverse.UI.Bubbles"), TEXT("Eidoverse.UI.PanelOpacity"), TEXT("Eidoverse.UI.Scale"), TEXT("Eidoverse.World.Lumen"),
                            TEXT("Eidoverse.Consent.Posable"), TEXT("Eidoverse.Consent.Pushable"), TEXT("Eidoverse.Audio.World"), TEXT("Eidoverse.Audio.Voices"),
                            TEXT("Eidoverse.Audio.HearVoices"), TEXT("Eidoverse.Audio.MicFloor"), TEXT("Eidoverse.Audio.PushToTalk"), TEXT("Eidoverse.Audio.VoiceProcessing")};
}

namespace EidoSettings
{
float LookSensitivity() { return FMath::Clamp(CVarSensitivity.GetValueOnGameThread(), .1f, 5.f); }
bool InvertLook() { return CVarInvert.GetValueOnGameThread() != 0; }
bool Nameplates() { return CVarPlates.GetValueOnGameThread() != 0; }
bool Bubbles() { return CVarBubbles.GetValueOnGameThread() != 0; }
float PanelOpacity() { return FMath::Clamp(CVarOpacity.GetValueOnGameThread(), .3f, 1.f); }
bool Lumen() { return CVarLumen.GetValueOnGameThread() != 0; }
bool Posable() { return CVarPosable.GetValueOnGameThread() != 0; }
bool Pushable() { return CVarPushable.GetValueOnGameThread() != 0; }
float WorldVolume() { return FMath::Clamp(CVarWorldVolume.GetValueOnGameThread(), 0.f, 1.f); }
float VoiceVolume() { return FMath::Clamp(CVarVoiceVolume.GetValueOnGameThread(), 0.f, 1.f); }
bool HearVoices() { return CVarHearVoices.GetValueOnGameThread() != 0; }
float MicFloor() { return FMath::Clamp(CVarMicFloor.GetValueOnGameThread(), 0.f, .2f); }
bool PushToTalk() { return CVarPtt.GetValueOnGameThread() != 0; }
bool VoiceProcessing() { return CVarVpio.GetValueOnGameThread() != 0; }
float InterfaceScale() { return FMath::Clamp(CVarScale.GetValueOnGameThread(), .75f, 1.5f); }

void Set(const TCHAR* Name, const FString& Value)
{
    if (IConsoleVariable* V = IConsoleManager::Get().FindConsoleVariable(Name)) V->Set(*Value, ECVF_SetByGameSetting);
    Save();
}

void Load()
{
    for (const TCHAR* Name : Names)
    {
        FString Value;
        if (GConfig->GetString(Section, Name, Value, GGameUserSettingsIni))
            if (IConsoleVariable* V = IConsoleManager::Get().FindConsoleVariable(Name)) V->Set(*Value, ECVF_SetByGameSetting);
    }
    ApplyInterfaceScale();
}

void Save()
{
    for (const TCHAR* Name : Names)
        if (IConsoleVariable* V = IConsoleManager::Get().FindConsoleVariable(Name))
            GConfig->SetString(Section, Name, *V->GetString(), GGameUserSettingsIni);
    GConfig->Flush(false, GGameUserSettingsIni);
}

void ApplyInterfaceScale()
{
    // Game-viewport UI only (DPI curve x ApplicationScale); the editor is unaffected.
    GetMutableDefault<UUserInterfaceSettings>()->ApplicationScale = InterfaceScale();
}
}
