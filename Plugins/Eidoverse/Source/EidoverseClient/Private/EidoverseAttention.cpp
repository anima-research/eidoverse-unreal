#include "EidoverseAttention.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/Guid.h"
#include "Sound/SoundWaveProcedural.h"

namespace EidoAttention
{
namespace
{
    // Number.isSafeInteger
    bool SafeInteger(const TSharedPtr<FJsonValue>& V, int64& Out)
    {
        if (!V.IsValid() || V->Type != EJson::Number) return false;
        const double N = V->AsNumber();
        if (!FMath::IsFinite(N) || FMath::Abs(N) > 9007199254740991.0 || FMath::FloorToDouble(N) != N) return false;
        Out = int64(N);
        return true;
    }
}

bool Mentions(const FString& Text, const FString& Me)
{
    if (Me.IsEmpty()) return false;
    const FString Lower = Text.ToLower(), Name = Me.ToLower();
    int32 From = 0;
    while (true)
    {
        const int32 At = Lower.Find(Name, ESearchCase::CaseSensitive, ESearchDir::FromStart, From);
        if (At == INDEX_NONE) return false;
        const bool bStart = At == 0 || !FChar::IsAlnum(Lower[At - 1]);
        const int32 End = At + Name.Len();
        const bool bEnd = End >= Lower.Len() || !FChar::IsAlnum(Lower[End]);
        if (bStart && bEnd) return true;
        From = At + 1;
    }
}

FSay ReadSay(const TSharedPtr<FJsonObject>& Entry)
{
    FSay S;
    if (!Entry.IsValid()) return S;
    Entry->TryGetStringField(TEXT("verb"), S.Verb);
    Entry->TryGetStringField(TEXT("actor"), S.Actor);
    Entry->TryGetBoolField(TEXT("replay"), S.bReplay);
    S.bSeqValid = SafeInteger(Entry->TryGetField(TEXT("seq")), S.Seq);
    const TSharedPtr<FJsonObject>* Args = nullptr;
    if (Entry->TryGetObjectField(TEXT("args"), Args))
    {
        (*Args)->TryGetStringField(TEXT("text"), S.Text);
        bool bSpoken = false;
        S.bSpoken = (*Args)->TryGetBoolField(TEXT("spoken"), bSpoken) && bSpoken;   // spoken === true
        int64 Utt = 0;
        if (SafeInteger((*Args)->TryGetField(TEXT("utt")), Utt)) S.Utt = Utt;
    }
    return S;
}

void FGate::Sync()
{
    // [scope, me]: a new world or a new name is a new context, with nothing carried over.
    const FString Next = (Scope ? Scope() : FString()) + TEXT("\n") + (Me ? Me() : FString());
    if (!bContext || Next != Context)
    {
        Context = Next; bContext = true;
        HighSeq = -1; Last = -1e300;
        Utterances.Reset(); UtteranceOrder.Reset(); Sessions.Reset();
    }
}

void FGate::Forget(const FString& Actor)
{
    Sessions.Remove(Actor);
    for (auto It = Utterances.CreateIterator(); It; ++It) if (It.Value() == Actor) It.RemoveCurrent();
    const FString Prefix = Actor + TEXT("\n");
    UtteranceOrder.RemoveAll([&Prefix](const FString& K) { return K.StartsWith(Prefix, ESearchCase::CaseSensitive); });
}

bool FGate::Admit()
{
    const double At = NowMs ? NowMs() : 0;
    if (At - Last < CooldownMs) return false;
    // A silent notice consumes the message's identity, not the audio cooldown.
    if (!Play || !Play()) return false;
    Last = At;
    return true;   // leading-edge coalescing; never queue a stale bell
}

void FGate::Participant(const FString& Id, const TOptional<FString>& Session, bool bArrived)
{
    Sync();
    if (Id.IsEmpty()) return;
    const TOptional<FString>* Known = Sessions.Find(Id);
    const bool bBoundary = Session.IsSet() ? !(Known && Known->IsSet() && Known->GetValue() == Session.GetValue()) : bArrived;
    if (bBoundary) Forget(Id);
    if (Session.IsSet() || !Sessions.Contains(Id)) Sessions.Add(Id, Session);
}

void FGate::Roster(const TArray<TPair<FString, TOptional<FString>>>& People)
{
    Sync();
    // A sender missing from the snapshot may be between two legs of one page: its memory stays until its
    // session changes (the utterance cache is bounded either way).
    for (const auto& P : People) Participant(P.Key, P.Value, false);
}

bool FGate::Say(const FSay& E)
{
    Sync();
    if (E.Verb != TEXT("say") || E.bReplay) return false;
    if (!E.bSeqValid || E.Seq <= HighSeq) return false;
    HighSeq = E.Seq;
    const FString MeNow = Me ? Me() : FString();
    if (E.Actor.IsEmpty() || E.Actor == TEXT("*") || E.Actor == MeNow || !(MentionsMe && MentionsMe(E.Text))) return false;
    if (E.bSpoken && E.Utt.IsSet())
    {
        const FString Key = E.Actor + TEXT("\n") + LexToString(E.Utt.GetValue());
        if (Utterances.Contains(Key)) return false;
        Utterances.Add(Key, E.Actor); UtteranceOrder.Add(Key);
        // Bounded session memory, not a timer: a long utterance's later flush stays consumed even after the
        // burst cooldown has passed.
        while (Utterances.Num() > UtteranceLimit && UtteranceOrder.Num())
        {
            Utterances.Remove(UtteranceOrder[0]);
            UtteranceOrder.RemoveAt(0);
        }
    }
    return Admit();
}

bool FGate::Whisper(const FWhisper& M)
{
    Sync();
    const FString MeNow = Me ? Me() : FString();
    if (M.bEcho || M.bReplay || M.From.IsEmpty() || M.From == MeNow || M.To != MeNow || !M.bTextIsString || M.Text.IsEmpty()) return false;
    return Admit();
}

TArray<int16> RenderTone(const FString& Sound, float Volume, int32 SampleRate)
{
    struct FNote { double Freq, Delay, Duration; };
    TArray<FNote> Notes;
    if (Sound == TEXT("chime")) Notes = {{660, 0, .18}, {880, .12, .22}};
    else if (Sound == TEXT("tap")) Notes = {{440, 0, .07}};
    else Notes = {{660, 0, .24}};
    double End = 0;
    for (const FNote& N : Notes) End = FMath::Max(End, N.Delay + N.Duration + .01);
    const int32 Count = FMath::CeilToInt(End * SampleRate);
    TArray<float> Mix; Mix.SetNumZeroed(Count);
    for (const FNote& N : Notes)
    {
        const int32 From = FMath::FloorToInt(N.Delay * SampleRate), To = FMath::Min(Count, FMath::CeilToInt((N.Delay + N.Duration + .01) * SampleRate));
        for (int32 I = From; I < To; ++I)
        {
            const double T = double(I) / SampleRate - N.Delay;
            // linearRamp 0 -> 1 over 8 ms, exponentialRamp to 0.001 at the duration, held after
            const double Env = T < .008 ? T / .008 : FMath::Pow(.001, FMath::Min(1.0, (T - .008) / (N.Duration - .008)));
            Mix[I] += float(FMath::Sin(2 * PI * N.Freq * T) * Env);
        }
    }
    const float Gain = FMath::Clamp(Volume, 0.f, 1.f) * .15f;
    TArray<int16> Out; Out.SetNumUninitialized(Count);
    for (int32 I = 0; I < Count; ++I) Out[I] = int16(FMath::Clamp(Mix[I] * Gain, -1.f, 1.f) * 32767);
    return Out;
}

bool PlayTone(UWorld* World, const FString& Sound, float Volume)
{
    if (!World || Volume <= 0) return false;
    constexpr int32 Rate = 48000;
    const TArray<int16> Pcm = RenderTone(Sound, Volume, Rate);
    if (Pcm.IsEmpty()) return false;
    USoundWaveProcedural* Wave = NewObject<USoundWaveProcedural>();
    Wave->SetSampleRate(Rate); Wave->NumChannels = 1; Wave->Duration = float(Pcm.Num()) / Rate;
    Wave->SoundGroup = SOUNDGROUP_Default; Wave->bLooping = false;
    Wave->QueueAudio(reinterpret_cast<const uint8*>(Pcm.GetData()), Pcm.Num() * sizeof(int16));
    UGameplayStatics::PlaySound2D(World, Wave);
    return true;
}

const FString& PageSession()
{
    // 32 hex digits: inside the server's 8-64 of [A-Za-z0-9_-]
    static const FString Session = FGuid::NewGuid().ToString(EGuidFormats::Digits).ToLower();
    return Session;
}
}
