#pragma once
#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

class UWorld;

// Private attention (eidoverse-worlds client/lib/attention-policy.js + attention.js): a short local tone for
// a LIVE public say that mentions me and for a whisper to me. Separate from chat rendering, which also renders
// history; nothing is sent, no TTS or world volume, never positional. Default off; missed notices are never
// queued. Captions are visual performance: the durable say is the single attention source for an utterance.
namespace EidoAttention
{
    inline constexpr double CooldownMs = 3000;
    inline constexpr int32 UtteranceLimit = 1024;

    /** chat.js mentionRx: @name, or the bare name as a whole word, case-insensitive. */
    EIDOVERSECLIENT_API bool Mentions(const FString& Text, const FString& Me);

    /** One live log entry, as the gate reads it. bSeqValid / Utt only when they were safe integers. */
    struct FSay
    {
        FString Verb, Actor, Text;
        int64 Seq = -1; bool bSeqValid = false;
        bool bReplay = false, bSpoken = false;
        TOptional<int64> Utt;
    };
    EIDOVERSECLIENT_API FSay ReadSay(const TSharedPtr<FJsonObject>& Entry);

    struct FWhisper { FString From, To, Text; bool bTextIsString = true, bEcho = false, bReplay = false; };

    /** createAttentionGate: dedup by seq and by (sender, utterance) within the sender's page session,
     *  a 3 s leading-edge cooldown, and a world/name context that resets everything when it changes. */
    class EIDOVERSECLIENT_API FGate
    {
    public:
        TFunction<FString()> Me, Scope;
        TFunction<bool(const FString&)> MentionsMe;
        TFunction<double()> NowMs;
        TFunction<bool()> Play;   // true only when a tone actually sounded

        /** An arrival (bArrived) or a snapshot record. A changed session is a new sender page, whose utterance
         *  counter starts again; the same session again is a reconnect of the same page. An arrival without a
         *  session (an older server) counts as a boundary; a snapshot without one preserves the cache. */
        void Participant(const FString& Id, const TOptional<FString>& Session, bool bArrived = true);
        void Roster(const TArray<TPair<FString, TOptional<FString>>>& People);
        bool Say(const FSay& Entry);
        bool Whisper(const FWhisper& Msg);

    private:
        FString Context;
        bool bContext = false;
        int64 HighSeq = -1;
        double Last = -1e300;
        TMap<FString, FString> Utterances;   // key [actor, utt] -> actor
        TArray<FString> UtteranceOrder;      // insertion order, for the bounded memory
        TMap<FString, TOptional<FString>> Sessions;
        void Sync();
        void Forget(const FString& Actor);
        bool Admit();
    };

    /** attention.js tone(): soft 660 Hz 0.24 s; chime 660 then 880 Hz; tap 440 Hz 0.07 s; an 8 ms attack and an
     *  exponential decay to 0.001, at volume x 0.15. Mono 16-bit PCM. */
    EIDOVERSECLIENT_API TArray<int16> RenderTone(const FString& Sound, float Volume, int32 SampleRate = 48000);
    /** Plays RenderTone in 2-D (never positional). False when nothing could play. */
    EIDOVERSECLIENT_API bool PlayTone(UWorld* World, const FString& Sound, float Volume);

    /** This run's presence session (net.js PAGE_SESSION): one id for as long as the client runs, sent with every
     *  join, so listeners can tell a reconnect (the same counter goes on) from a new client (it starts again). */
    EIDOVERSECLIENT_API const FString& PageSession();
}
