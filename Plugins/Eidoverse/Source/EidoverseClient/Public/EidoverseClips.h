#pragma once
#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "EidoverseClips.generated.h"

// VRM Animation (.vrma) clips, as the browser plays them (@pixiv/three-vrm-animation).
// Keys are stored the way three-vrm's loader stores them: humanoid rotations as
// normalized deltas from the clip's own rest pose (parentWorldRest * q *
// inverse(boneWorldRest)), hips translation in world space, all in glTF axes
// (Y up, metres). Retargeting onto a body happens in UEidoverseAvatarMesh.
struct FEidoClipTrack
{
    TArray<float> Times;
    TArray<FQuat> Rotations;
};

struct EIDOVERSECLIENT_API FEidoClip
{
    FString Name;
    float Duration = 0;
    TMap<FName, FEidoClipTrack> Bones;   // VRM 1.0 humanoid bone names
    TArray<float> HipsTimes;
    TArray<FVector> HipsPositions;       // world, glTF axes, metres
    float RestHipsHeight = 1;            // the clip's rest hips height (glTF Y)
    float Takeoff = 0;                   // jump: where the hips rise back through rest (avatar.js clipTakeoff)

    bool Parse(const TArray<uint8>& Glb, FString& Error);
    bool SampleRotation(FName Bone, float Time, FQuat& Out) const;
    bool SampleHips(float Time, FVector& Out) const;
};

// The browser's clip vocabulary (shared/clipdefs.js) and playback rules.
namespace EidoClipRules
{
    // Slot -> library file name (CLIP_FILES); other slots load the file of the same name.
    EIDOVERSECLIENT_API FString FileForSlot(const FString& Slot);
    // Missing clip -> what to play instead (avatar.js CLIP_FALLBACK); empty when none.
    EIDOVERSECLIENT_API FString Fallback(const FString& Slot);
    // Natural speed (m/s) of a locomotion clip (CLIP_SPEED); 0 for the rest.
    EIDOVERSECLIENT_API float NaturalSpeed(const FString& Slot);
    // timeScale = clamp(speed / natural, 0.6, 1.6), else 1.
    EIDOVERSECLIENT_API float RateFor(const FString& Slot, float Speed);
    // locomotion_clip.js selectClip, desktop subset.
    EIDOVERSECLIENT_API FString SelectLocomotion(bool bJumped, float AirborneSeconds, bool bWantMove, float Speed, const FString& Posture = FString());
}

// Shared by every body: the server's roster (/animations) and emote table (/defs),
// with clips downloaded on first use and parsed on a worker thread.
UCLASS()
class EIDOVERSECLIENT_API UEidoverseClipLibrary : public UObject
{
    GENERATED_BODY()
public:
    // http(s)://host[:port] of the server; resets everything when it changes.
    void Configure(const FString& HttpBase);
    // The clip for a slot (or a plain clip file name), following the fallback chain
    // while the preferred one loads. Null when nothing playable is ready yet.
    TSharedPtr<const FEidoClip> Resolve(const FString& Slot, FString* OutPlayedSlot = nullptr);
    // Starts the preferred slot loading without asking for a fallback.
    void Prefetch(const FString& Slot);
    // Exactly this clip file (no fallback); starts loading it and returns null until ready.
    TSharedPtr<const FEidoClip> Find(const FString& File);
    // Clip file for an emote name (defs/animations/_emotes.json), or empty.
    FString EmoteClip(const FString& Emote) const;
    TArray<FString> EmoteNames() const { TArray<FString> K; Emotes.GetKeys(K); K.Sort(); return K; }
    int32 ReadyCount() const { return Ready.Num(); }
private:
    FString Base;
    bool bRosterRequested = false, bRosterReady = false;
    TMap<FString, FString> Roster;           // file name -> library path (with ?v=)
    TMap<FString, FString> Emotes;           // emote -> clip file
    TMap<FString, TSharedPtr<const FEidoClip>> Ready;   // file name -> clip
    TSet<FString> Loading, Failed;
    TArray<FString> Waiting;                 // requested before the roster arrived
    uint32 Generation = 0;
    void RequestRoster();
    void Download(const FString& File);
};
