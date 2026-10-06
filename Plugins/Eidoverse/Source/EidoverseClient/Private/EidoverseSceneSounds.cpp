// Placed sounds (client/lib/sounds.js, shared/sound.js): {src, playing, loop, volume,
// radius, t0}. Fetched from the library, decoded once (AudioToolbox: mp3/m4a/wav/aiff;
// Vorbis: ogg), played from the entity through a procedural wave so loops and the shared
// playhead (t0) are exact. Distance law: WebAudio 'inverse', refDistance 1.5, rolloff 1.5,
// held at maxDistance = radius. Volume x the world volume setting.
#include "EidoverseScene.h"
#include "EidoverseSettings.h"
#include "Dom/JsonObject.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Components/AudioComponent.h"
#include "Sound/SoundWaveProcedural.h"
#include "Sound/SoundAttenuation.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "Async/Async.h"
#if PLATFORM_MAC
#include "Mac/MacSystemIncludes.h"
#pragma push_macro("FVector")
#define FVector FVectorWorkaround
#include <AudioToolbox/AudioToolbox.h>
#pragma pop_macro("FVector")
#endif
#include "vorbis/vorbisfile.h"

struct FEidoPcm { TArray<int16> Samples; int32 Rate = 44100, Channels = 1; double Duration() const { return Channels > 0 && Rate > 0 ? double(Samples.Num() / Channels) / Rate : 0; } };

struct FEidoSoundPlayer
{
    FString Src, Key;
    TSharedPtr<FEidoPcm> Pcm;
    TWeakObjectPtr<UAudioComponent> Comp;
    TStrongObjectPtr<USoundWaveProcedural> Wave;
    TSharedPtr<int64, ESPMode::ThreadSafe> Cursor = MakeShared<int64, ESPMode::ThreadSafe>(0);
    TSharedPtr<bool, ESPMode::ThreadSafe> bLoop = MakeShared<bool, ESPMode::ThreadSafe>(true);
};

namespace
{
    FString SStr(const TSharedPtr<FJsonObject>& O, const TCHAR* K) { FString S; if (O) O->TryGetStringField(K, S); return S; }
    double SNum(const TSharedPtr<FJsonObject>& O, const TCHAR* K, double D) { double N; return O && O->TryGetNumberField(K, N) && FMath::IsFinite(N) ? N : D; }
    bool SFlag(const TSharedPtr<FJsonObject>& O, const TCHAR* K, bool D) { bool B = D; if (O) O->TryGetBoolField(K, B); return B; }

    // ov_open_callbacks over a memory buffer.
    struct FMemOgg { const TArray<uint8>* Bytes; int64 Pos = 0; };
    size_t OggRead(void* Dst, size_t Size, size_t N, void* Src)
    { FMemOgg* M = (FMemOgg*)Src; const int64 Want = int64(Size * N), Have = FMath::Min<int64>(Want, M->Bytes->Num() - M->Pos); if (Have <= 0) return 0; FMemory::Memcpy(Dst, M->Bytes->GetData() + M->Pos, Have); M->Pos += Have; return size_t(Have / Size); }
    int OggSeek(void* Src, ogg_int64_t Off, int Whence) { FMemOgg* M = (FMemOgg*)Src; int64 P = Whence == SEEK_SET ? Off : Whence == SEEK_CUR ? M->Pos + Off : M->Bytes->Num() + Off; if (P < 0 || P > M->Bytes->Num()) return -1; M->Pos = P; return 0; }
    long OggTell(void* Src) { return long(((FMemOgg*)Src)->Pos); }
    int OggClose(void*) { return 0; }

    TSharedPtr<FEidoPcm> DecodeOgg(const TArray<uint8>& Bytes)
    {
        FMemOgg Mem{&Bytes}; OggVorbis_File F; ov_callbacks Cb{OggRead, OggSeek, OggClose, OggTell};
        if (ov_open_callbacks(&Mem, &F, nullptr, 0, Cb) != 0) return nullptr;
        vorbis_info* Info = ov_info(&F, -1);
        auto Pcm = MakeShared<FEidoPcm>(); Pcm->Rate = Info ? Info->rate : 44100; Pcm->Channels = FMath::Clamp(Info ? Info->channels : 1, 1, 2);
        char Buffer[8192]; int Section = 0;
        for (;;)
        {
            const long Got = ov_read(&F, Buffer, sizeof(Buffer), 0, 2, 1, &Section);
            if (Got <= 0) break;
            const int32 At = Pcm->Samples.Num(); Pcm->Samples.AddUninitialized(Got / 2); FMemory::Memcpy(Pcm->Samples.GetData() + At, Buffer, Got);
            if (Pcm->Samples.Num() > 48000 * 2 * 600) break;   // ten minutes of stereo is plenty
        }
        ov_clear(&F);
        return Pcm->Samples.Num() ? TSharedPtr<FEidoPcm>(Pcm) : TSharedPtr<FEidoPcm>();
    }

    TSharedPtr<FEidoPcm> DecodeWithAudioToolbox(const TArray<uint8>& Bytes, const FString& Ext)
    {
#if PLATFORM_MAC
        // ExtAudioFile reads from a file: stage the bytes in the saved cache.
        const FString Path = FPaths::ProjectSavedDir() / TEXT("SoundCache") / (FMD5::HashBytes(Bytes.GetData(), Bytes.Num()) + TEXT(".") + Ext);
        if (!IFileManager::Get().FileExists(*Path) && !FFileHelper::SaveArrayToFile(Bytes, *Path)) return nullptr;
        const FString Full = FPaths::ConvertRelativePathToFull(Path);
        CFURLRef Url = CFURLCreateFromFileSystemRepresentation(nullptr, (const UInt8*)TCHAR_TO_UTF8(*Full), strlen(TCHAR_TO_UTF8(*Full)), false);
        ExtAudioFileRef File = nullptr;
        const OSStatus Open = ExtAudioFileOpenURL(Url, &File);
        CFRelease(Url);
        if (Open != noErr || !File) return nullptr;
        AudioStreamBasicDescription In{}; UInt32 Size = sizeof(In);
        ExtAudioFileGetProperty(File, kExtAudioFileProperty_FileDataFormat, &Size, &In);
        auto Pcm = MakeShared<FEidoPcm>(); Pcm->Rate = int32(In.mSampleRate > 0 ? In.mSampleRate : 44100); Pcm->Channels = FMath::Clamp(int32(In.mChannelsPerFrame), 1, 2);
        AudioStreamBasicDescription Out{}; Out.mSampleRate = Pcm->Rate; Out.mFormatID = kAudioFormatLinearPCM;
        Out.mFormatFlags = kAudioFormatFlagIsSignedInteger | kAudioFormatFlagIsPacked; Out.mBitsPerChannel = 16; Out.mChannelsPerFrame = Pcm->Channels;
        Out.mBytesPerFrame = 2 * Pcm->Channels; Out.mFramesPerPacket = 1; Out.mBytesPerPacket = Out.mBytesPerFrame;
        if (ExtAudioFileSetProperty(File, kExtAudioFileProperty_ClientDataFormat, sizeof(Out), &Out) != noErr) { ExtAudioFileDispose(File); return nullptr; }
        TArray<int16> Chunk; Chunk.SetNumUninitialized(4096 * Pcm->Channels);
        for (;;)
        {
            AudioBufferList List; List.mNumberBuffers = 1; List.mBuffers[0].mNumberChannels = Pcm->Channels;
            List.mBuffers[0].mDataByteSize = Chunk.Num() * 2; List.mBuffers[0].mData = Chunk.GetData();
            UInt32 Frames = 4096;
            if (ExtAudioFileRead(File, &Frames, &List) != noErr || Frames == 0) break;
            Pcm->Samples.Append(Chunk.GetData(), Frames * Pcm->Channels);
            if (Pcm->Samples.Num() > 48000 * 2 * 600) break;
        }
        ExtAudioFileDispose(File);
        return Pcm->Samples.Num() ? TSharedPtr<FEidoPcm>(Pcm) : TSharedPtr<FEidoPcm>();
#else
        return nullptr;
#endif
    }

    // WebAudio 'inverse': g(d) = ref / (ref + rolloff (max(d, ref) - ref)), d clamped at maxDistance.
    float InverseGain(double Metres, double Radius) { const double D = FMath::Clamp(Metres, 1.5, FMath::Max(1.5, Radius)); return float(1.5 / (1.5 + 1.5 * (D - 1.5))); }
}

void AEidoverseScene::TickSounds()
{
    const float WorldVolume = EidoSettings::WorldVolume();
    for (auto& Pair : Items)
    {
        FEidoverseSceneItem& E = Pair.Value;
        const FString Src = SStr(E.Sound, TEXT("src"));
        const bool bPlaying = SFlag(E.Sound, TEXT("playing"), true);
        if (Src.IsEmpty() || !E.Actor.IsValid())
        {
            if (E.SoundPlayer) { if (UAudioComponent* C = E.SoundPlayer->Comp.Get()) C->DestroyComponent(); E.SoundPlayer.Reset(); }
            continue;
        }
        // Fetch and decode once per path, off the game thread.
        TSharedPtr<FEidoPcm>* Cached = SoundCache.Find(Src);
        if (!Cached)
        {
            if (SoundLoading.Contains(Src) || LibraryBase.IsEmpty()) continue;
            SoundLoading.Add(Src);
            TWeakObjectPtr<AEidoverseScene> Weak(this);
            auto Request = FHttpModule::Get().CreateRequest();
            Request->SetURL(LibraryBase + Src); Request->SetVerb(TEXT("GET")); Request->SetTimeout(60);
            const FString Ext = FPaths::GetExtension(Src).ToLower();
            Request->OnProcessRequestComplete().BindLambda([Weak, Src, Ext](FHttpRequestPtr, FHttpResponsePtr Response, bool bOk)
            {
                if (!bOk || !Response.IsValid() || Response->GetResponseCode() != 200 || Response->GetContent().Num() > 48 * 1024 * 1024)
                { if (AEidoverseScene* S = Weak.Get()) { S->SoundCache.Add(Src, nullptr); S->SoundLoading.Remove(Src); } return; }
                TArray<uint8> Bytes = Response->GetContent();
                Async(EAsyncExecution::ThreadPool, [Weak, Src, Ext, Bytes = MoveTemp(Bytes)]()
                {
                    TSharedPtr<FEidoPcm> Pcm = Ext == TEXT("ogg") ? DecodeOgg(Bytes) : DecodeWithAudioToolbox(Bytes, Ext);
                    AsyncTask(ENamedThreads::GameThread, [Weak, Src, Pcm]()
                    {
                        AEidoverseScene* S = Weak.Get(); if (!S) return;
                        S->SoundCache.Add(Src, Pcm); S->SoundLoading.Remove(Src);
                        if (!Pcm) UE_LOG(LogTemp, Warning, TEXT("EIDOVERSE sound could not be decoded here: %s"), *Src);
                    });
                });
            });
            Request->ProcessRequest();
            continue;
        }
        const TSharedPtr<FEidoPcm> Pcm = *Cached;
        if (!Pcm) continue;
        if (!E.SoundPlayer || E.SoundPlayer->Src != Src)
        {
            if (E.SoundPlayer) if (UAudioComponent* Old = E.SoundPlayer->Comp.Get()) Old->DestroyComponent();
            auto P = MakeShared<FEidoSoundPlayer>(); P->Src = Src; P->Pcm = Pcm;
            USoundWaveProcedural* Wave = NewObject<USoundWaveProcedural>();
            Wave->SetSampleRate(Pcm->Rate); Wave->NumChannels = Pcm->Channels; Wave->Duration = INDEFINITELY_LOOPING_DURATION; Wave->SoundGroup = SOUNDGROUP_Default; Wave->bLooping = false;
            // Feed on demand from the decoded buffer, wrapping when the sound loops.
            TWeakPtr<FEidoPcm> WeakPcm = Pcm; auto Cursor = P->Cursor; auto bLoop = P->bLoop;
            Wave->OnSoundWaveProceduralUnderflow = FOnSoundWaveProceduralUnderflow::CreateLambda([WeakPcm, Cursor, bLoop](USoundWaveProcedural* W, int32 SamplesRequired)
            {
                TSharedPtr<FEidoPcm> Data = WeakPcm.Pin(); if (!Data || Data->Samples.IsEmpty()) return;
                TArray<int16> Out; Out.Reserve(SamplesRequired);
                int64 C = *Cursor;
                while (Out.Num() < SamplesRequired)
                {
                    if (C >= Data->Samples.Num()) { if (!*bLoop) break; C = 0; }
                    const int32 Take = int32(FMath::Min<int64>(SamplesRequired - Out.Num(), Data->Samples.Num() - C));
                    Out.Append(Data->Samples.GetData() + C, Take); C += Take;
                }
                *Cursor = C;
                if (Out.Num()) W->QueueAudio((const uint8*)Out.GetData(), Out.Num() * sizeof(int16));
            });
            P->Wave = TStrongObjectPtr<USoundWaveProcedural>(Wave);
            UAudioComponent* C = NewObject<UAudioComponent>(E.Actor.Get());
            E.Actor->AddInstanceComponent(C); C->SetupAttachment(E.Actor->GetRootComponent());
            C->bAutoActivate = false; C->bAllowSpatialization = true; C->bOverrideAttenuation = true; C->SetSound(Wave);
            C->RegisterComponent();
            P->Comp = C; P->Key.Empty();
            E.SoundPlayer = P;
        }
        FEidoSoundPlayer& P = *E.SoundPlayer;
        UAudioComponent* C = P.Comp.Get(); if (!C) continue;
        *P.bLoop = SFlag(E.Sound, TEXT("loop"), true);
        const double Radius = FMath::Clamp(SNum(E.Sound, TEXT("radius"), 12), 1., 200.), Volume = FMath::Clamp(SNum(E.Sound, TEXT("volume"), .8), 0., 1.);
        const double T0 = SNum(E.Sound, TEXT("t0"), 0);
        const FString Key = FString::Printf(TEXT("%d|%d|%.3f|%.3f|%.0f"), bPlaying, *P.bLoop, Radius, Volume, T0);
        if (Key != P.Key)
        {
            P.Key = Key;
            // The distance law as a custom curve over 0..500 m (flat past the radius).
            FSoundAttenuationSettings& A = C->AttenuationOverrides;
            A.bAttenuate = true; A.bSpatialize = true; A.AttenuationShape = EAttenuationShape::Sphere;
            A.AttenuationShapeExtents = FVector(0); A.FalloffDistance = 50000; A.DistanceAlgorithm = EAttenuationDistanceModel::Custom;
            FRichCurve& Curve = *A.CustomAttenuationCurve.GetRichCurve(); Curve.Reset();
            for (double M : {0., 1.5, 2., 3., 4., 6., 8., 12., 16., 24., 32., 48., 64., 100., 150., 200., 500.}) Curve.AddKey(float(M / 500), InverseGain(M, Radius));
            C->SetVolumeMultiplier(float(Volume) * WorldVolume);
            if (bPlaying)
            {
                // The shared playhead: (now - t0) into the sound, wrapped when looping.
                double Offset = 0;
                if (T0 > 0) { const double Now = double(FDateTime::UtcNow().ToUnixTimestamp()) * 1000 + FDateTime::UtcNow().GetMillisecond(); Offset = FMath::Max(0., (Now - T0) / 1000); }
                const double Dur = Pcm->Duration();
                if (*P.bLoop && Dur > 0) Offset = FMath::Fmod(Offset, Dur); else Offset = FMath::Min(Offset, Dur);
                *P.Cursor = int64(Offset * Pcm->Rate) * Pcm->Channels;
                if (P.Wave) P.Wave->ResetAudio();
                C->Play();
            }
            else C->Stop();
        }
        else C->SetVolumeMultiplier(float(Volume) * WorldVolume);
    }
}
