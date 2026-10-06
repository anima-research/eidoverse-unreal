// Bring-your-own-voice TTS (tts.js / tts-chunk.js / browservoice.js): my own says,
// while TTS is on and the mic is off, become speech on the voice leg. The text is
// shaped for the larynx (no code, links, markup or emoji) and cut into synthesis-
// sized chunks; each chunk goes to a voice endpoint (WebSocket synth protocol, or
// HTTP POST returning audio), and the results play in order on the published track
// with a local sidetone. Off by default.
#include "EidoverseTTS.h"
#include "EidoverseBridge.h"
#include "EidoverseVoice.h"
#include "EidoverseUI.h"
#include "EidoverseSettings.h"
#include "Internationalization/Regex.h"
#include "WebSocketsModule.h"
#include "IWebSocket.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Misc/Base64.h"
#include "Components/AudioComponent.h"
#include "Sound/SoundWaveProcedural.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "HAL/IConsoleManager.h"

bool EidoDecodeAudioBytes(const TArray<uint8>& Bytes, const FString& Ext, TArray<int16>& OutMono, int32& OutRate);

namespace
{
    TAutoConsoleVariable<int32> CVarTts(TEXT("Eidoverse.Audio.Tts"), 0, TEXT("1: speak my own says with my TTS voice while the mic is off (off by default)."));
    TAutoConsoleVariable<FString> CVarTtsEndpoint(TEXT("Eidoverse.Audio.TtsEndpoint"), TEXT("ws://127.0.0.1:8927"),
        TEXT("Voice endpoint: ws(s):// speaks the synth protocol; http(s):// takes POST {text} and returns audio."));
    TAutoConsoleVariable<float> CVarTtsVolume(TEXT("Eidoverse.Audio.TtsVolume"), 1.f, TEXT("Hearing my own TTS (sidetone gain is 0.8 x this)."));

    // Replace every match; "$1" in the replacement is the first capture group.
    FString ReplaceAll(const FString& In, const FString& Pattern, const FString& With)
    {
        const FRegexPattern P(Pattern);
        FRegexMatcher M(P, In);
        FString Out; int32 Last = 0;
        while (M.FindNext())
        {
            const int32 B = M.GetMatchBeginning(), E = M.GetMatchEnding();
            Out += In.Mid(Last, B - Last);
            Out += With.Replace(TEXT("$1"), *M.GetCaptureGroup(1));
            Last = E;
            if (E == B) { if (E < In.Len()) Out += In[E]; Last = E + 1; }   // never stall on an empty match
        }
        return Out + In.Mid(Last);
    }

    const TCHAR* Soft = TEXT("(?:[,;:—–][\"'’”)\\]»]*\\s)|(?:\\s(?=(?:and|but|so|or|yet|because|which|while|though|although)\\s))");

    FString ShapeContinuation(const FString& Chunk)
    {
        return Chunk.Len() && FString(TEXT(",;:—–.!?…")).Contains(Chunk.Right(1)) ? Chunk : Chunk + TEXT(",");
    }

    TArray<int32> SoftEnds(const FString& S)
    {
        TArray<int32> Ends; FRegexMatcher M(FRegexPattern(Soft), S);
        while (M.FindNext()) Ends.Add(M.GetMatchEnding());
        return Ends;
    }

    TArray<FString> SplitLong(const FString& Sentence)
    {
        if (Sentence.Len() <= 90) return {Sentence};
        TArray<FString> Chunks; FString Rest = Sentence;
        while (Rest.Len() > 90)
        {
            int32 Best = -1;
            for (const int32 End : SoftEnds(Rest)) { if (End < 35) continue; if (Rest.Len() - End < 35) break; Best = End; }
            if (Best < 0) break;
            Chunks.Add(ShapeContinuation(Rest.Left(Best).TrimStartAndEnd()));
            Rest = Rest.Mid(Best);
        }
        if (!Rest.TrimStartAndEnd().IsEmpty()) Chunks.Add(Rest.TrimStartAndEnd());
        return Chunks;
    }

    TArray<FString> FastFirst(const FString& Chunk)
    {
        if (Chunk.Len() <= 64) return {Chunk};
        int32 Cut = -1;
        for (const int32 End : SoftEnds(Chunk)) { if (End < 20) continue; if (End > 64) break; Cut = End; }
        if (Cut < 0)
        {
            const int32 Sp = Chunk.Left(65).Find(TEXT(" "), ESearchCase::CaseSensitive, ESearchDir::FromEnd);
            Cut = Sp > 20 ? Sp + 1 : -1;
        }
        if (Cut < 0 || Chunk.Len() - Cut < 20) return {Chunk};
        return {ShapeContinuation(Chunk.Left(Cut).TrimStartAndEnd()), Chunk.Mid(Cut).TrimStartAndEnd()};
    }

    bool IsRealBoundary(const FString& Head)
    {
        static const TSet<FString> Abbrevs = {TEXT("mr"), TEXT("mrs"), TEXT("ms"), TEXT("dr"), TEXT("prof"), TEXT("sr"), TEXT("jr"), TEXT("st"), TEXT("mt"), TEXT("ft"),
            TEXT("vs"), TEXT("etc"), TEXT("eg"), TEXT("e.g"), TEXT("ie"), TEXT("i.e"), TEXT("cf"), TEXT("ca"), TEXT("approx"), TEXT("no"), TEXT("vol"), TEXT("fig"),
            TEXT("dept"), TEXT("est"), TEXT("min"), TEXT("max"), TEXT("misc"), TEXT("jan"), TEXT("feb"), TEXT("mar"), TEXT("apr"), TEXT("jun"), TEXT("jul"),
            TEXT("aug"), TEXT("sep"), TEXT("sept"), TEXT("oct"), TEXT("nov"), TEXT("dec")};
        FString T = ReplaceAll(ReplaceAll(Head, TEXT("\\s+$"), TEXT("")), TEXT("[.!?…\"')\\]»]+$"), TEXT(""));
        T.TrimStartAndEndInline();
        TArray<FString> Words; T.ParseIntoArrayWS(Words);
        const FString Last = Words.Num() ? Words.Last() : FString();
        FString Key = Last.ToLower(); while (Key.EndsWith(TEXT("."))) Key.LeftChopInline(1);
        if (Abbrevs.Contains(Key)) return false;
        if (Last.Len() == 1 && FChar::IsUpper(Last[0])) return false;   // J. R. R. Tolkien
        return true;
    }
}

namespace EidoTts
{
FString SpokenForm(const FString& Text)
{
    FString S = Text;
    S = ReplaceAll(S, TEXT("```[\\s\\S]*?(?:```|$)"), TEXT(" "));                // fenced code never speaks
    S = ReplaceAll(S, TEXT("\\[([^\\]]{1,80})\\]\\((?:[^)]+)\\)"), TEXT("$1"));    // [label](url) -> label
    S = ReplaceAll(S, TEXT("(?i)\\bhttps?://\\S+"), TEXT("link"));                // bare URLs -> "link"
    S = ReplaceAll(S, TEXT("[*_`#]+"), TEXT(""));
    S = ReplaceAll(S, TEXT("[\\p{Extended_Pictographic}\\x{FE0F}\\x{200D}]"), TEXT(""));
    S = ReplaceAll(S, TEXT("[→⇒↔▸▶◦‣·•]+"), TEXT(", "));
    S = ReplaceAll(S, TEXT("(?:->|=>|::)+"), TEXT(", "));
    S = ReplaceAll(S, TEXT("(?<=[a-zA-Z])[:](?=[a-zA-Z])"), TEXT(", "));           // world:staging -> pause; 6:04 stays
    S = ReplaceAll(S, TEXT("\\s[|~]+\\s"), TEXT(", "));
    S = ReplaceAll(S, TEXT("\\s{2,}"), TEXT(" "));
    S = ReplaceAll(S, TEXT("(?:, )+(?=, )"), TEXT(""));
    return S.TrimStartAndEnd();
}

TArray<FString> Chunks(const FString& Text)
{
    const FString Spoken = SpokenForm(Text);
    if (Spoken.IsEmpty()) return {};
    TArray<FString> Sentences; int32 Pos = 0;
    FRegexMatcher M(FRegexPattern(TEXT("[.!?…]+[\"'’”)\\]»]*\\s")), Spoken);
    while (M.FindNext())
    {
        const int32 End = M.GetMatchEnding();
        if (!IsRealBoundary(Spoken.Mid(Pos, End - Pos))) continue;
        Sentences.Add(Spoken.Mid(Pos, End - Pos).TrimStartAndEnd());
        Pos = End;
    }
    if (!Spoken.Mid(Pos).TrimStartAndEnd().IsEmpty()) Sentences.Add(Spoken.Mid(Pos).TrimStartAndEnd());
    TArray<FString> Glued;
    for (const FString& S : Sentences) { if (Glued.Num() && Glued.Last().Len() < 20) Glued.Last() += TEXT(" ") + S; else Glued.Add(S); }
    TArray<FString> Out;
    for (const FString& G : Glued) Out.Append(SplitLong(G));
    if (!Out.Num()) return Out;
    TArray<FString> First = FastFirst(Out[0]);
    Out.RemoveAt(0);
    First.Append(Out);
    return First;
}

TArray<float> To48k(const TArray<int16>& Pcm, int32 Rate)
{
    TArray<float> Out;
    if (!Pcm.Num() || Rate < 4000 || Rate > 192000) return Out;
    const double Step = double(Rate) / 48000.0;
    const int32 N = int32(Pcm.Num() / Step);
    Out.SetNumUninitialized(N);
    for (int32 I = 0; I < N; ++I)
    {
        const double At = I * Step; const int32 A = int32(At); const double F = At - A;
        const float S0 = Pcm[FMath::Min(A, Pcm.Num() - 1)] / 32768.f, S1 = Pcm[FMath::Min(A + 1, Pcm.Num() - 1)] / 32768.f;
        Out[I] = float(S0 + (S1 - S0) * F);
    }
    return Out;
}
}

// ---------------------------------------------------------------- the bridge side

bool AEidoverseBridge::IsTtsOn() const { return CVarTts.GetValueOnGameThread() != 0; }

void AEidoverseBridge::SetTtsEnabled(bool bOn)
{
    EidoSettings::Set(TEXT("Eidoverse.Audio.Tts"), bOn ? TEXT("1") : TEXT("0"));
    EnsureVoice();
    Voice->SetSynthEnabled(bOn);
    if (!bOn) { TtsWaiting.Reset(); TtsDone.Reset(); TtsSentAt.Reset(); TtsPlayNext = TtsSerial; if (TtsSocket) { TtsSocket->Close(); TtsSocket.Reset(); } bTtsReady = false; }
    Narrate(bOn ? FString::Printf(TEXT("your messages will be spoken with your voice (%s) while the mic is off"), *CVarTtsEndpoint.GetValueOnGameThread())
                : FString(TEXT("TTS off")));
}

void AEidoverseBridge::SpeakOwnSay(const FString& Text)
{
    // speakOwnSays: not when TTS is off, the mic is live, or nobody could hear it.
    if (!IsTtsOn() || !Voice || Voice->IsMicLive()) return;
    const FString TtsUrl = CVarTtsEndpoint.GetValueOnGameThread().TrimStartAndEnd();
    const double Now = FPlatformTime::Seconds();
    for (const FString& Chunk : EidoTts::Chunks(Text))
    {
        const int32 Id = ++TtsSerial;
        TtsSentAt.Add(Id, Now);
        if (TtsUrl.StartsWith(TEXT("http://")) || TtsUrl.StartsWith(TEXT("https://")))
        {
            auto Req = FHttpModule::Get().CreateRequest();
            Req->SetURL(TtsUrl); Req->SetVerb(TEXT("POST")); Req->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
            auto Body = MakeShared<FJsonObject>(); Body->SetStringField(TEXT("text"), Chunk);
            FString Json; FJsonSerializer::Serialize(Body, TJsonWriterFactory<>::Create(&Json)); Req->SetContentAsString(Json);
            Req->SetTimeout(15);
            TWeakObjectPtr<AEidoverseBridge> Weak(this);
            Req->OnProcessRequestComplete().BindLambda([Weak, Id](FHttpRequestPtr, FHttpResponsePtr Resp, bool bOk)
            {
                if (!Weak.IsValid()) return;
                TArray<int16> Pcm; int32 Rate = 0;
                if (bOk && Resp && Resp->GetResponseCode() == 200)
                {
                    const TArray<uint8>& Bytes = Resp->GetContent();
                    const bool bWav = Bytes.Num() > 12 && Bytes[0] == 'R' && Bytes[1] == 'I' && Bytes[2] == 'F' && Bytes[3] == 'F';
                    EidoDecodeAudioBytes(Bytes, bWav ? TEXT("wav") : TEXT("mp3"), Pcm, Rate);
                }
                Weak->TtsDone.Add(Id, EidoTts::To48k(Pcm, Rate));
            });
            Req->ProcessRequest();
            continue;
        }
        TtsWaiting.Emplace(Id, Chunk);
    }
    if (TtsWaiting.Num()) EnsureTtsSocket(TtsUrl);
}

void AEidoverseBridge::EnsureTtsSocket(const FString& Url)
{
    if (TtsSocket) { if (bTtsReady) FlushTts(); return; }
    if (!(Url.StartsWith(TEXT("ws://")) || Url.StartsWith(TEXT("wss://")))) { Narrate(TEXT("TTS endpoint must be ws(s):// or http(s)://")); TtsWaiting.Reset(); return; }
    TtsSocket = FWebSocketsModule::Get().CreateWebSocket(Url);
    TtsConnectAt = FPlatformTime::Seconds();
    TWeakObjectPtr<AEidoverseBridge> Weak(this);
    TtsSocket->OnConnected().AddLambda([Weak]()
    {
        if (!Weak.IsValid()) return;
        auto Hello = MakeShared<FJsonObject>();
        Hello->SetStringField(TEXT("type"), TEXT("hello")); Hello->SetStringField(TEXT("name"), TEXT("voicesource")); Hello->SetStringField(TEXT("world"), TEXT(""));
        FString J; FJsonSerializer::Serialize(Hello, TJsonWriterFactory<>::Create(&J)); Weak->TtsSocket->Send(J);
        Weak->bTtsReady = true; Weak->FlushTts();
    });
    TtsSocket->OnMessage().AddLambda([Weak](const FString& Msg)
    {
        if (!Weak.IsValid()) return;
        TSharedPtr<FJsonObject> J;
        if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Msg), J) || !J) return;
        FString Type; J->TryGetStringField(TEXT("type"), Type);
        if (Type != TEXT("synth-result")) return;
        double Id = 0, Rate = 22050; J->TryGetNumberField(TEXT("id"), Id); J->TryGetNumberField(TEXT("sampleRate"), Rate);
        FString B64; J->TryGetStringField(TEXT("pcm"), B64);
        TArray<uint8> Bytes; FBase64::Decode(B64, Bytes);
        TArray<int16> Pcm; Pcm.SetNumUninitialized(Bytes.Num() / 2);
        if (Pcm.Num()) FMemory::Memcpy(Pcm.GetData(), Bytes.GetData(), Pcm.Num() * 2);   // s16le
        Weak->TtsDone.Add(int32(Id), EidoTts::To48k(Pcm, int32(Rate)));
    });
    auto Lost = [Weak]()
    {
        if (!Weak.IsValid()) return;
        Weak->bTtsReady = false; Weak->TtsSocket.Reset();
        if (Weak->TtsWaiting.Num()) Weak->Narrate(TEXT("TTS: the voice endpoint isn't answering"));
        Weak->TtsWaiting.Reset();
    };
    TtsSocket->OnConnectionError().AddLambda([Lost](const FString&) { Lost(); });
    TtsSocket->OnClosed().AddLambda([Lost](int32, const FString&, bool) { Lost(); });
    TtsSocket->Connect();
}

void AEidoverseBridge::FlushTts()
{
    if (!TtsSocket || !bTtsReady) return;
    for (const auto& W : TtsWaiting)
    {
        auto J = MakeShared<FJsonObject>();
        J->SetStringField(TEXT("type"), TEXT("synth")); J->SetNumberField(TEXT("id"), W.Key); J->SetStringField(TEXT("text"), W.Value);
        FString S; FJsonSerializer::Serialize(J, TJsonWriterFactory<>::Create(&S)); TtsSocket->Send(S);
    }
    TtsWaiting.Reset();
}

void AEidoverseBridge::TickTts(double Now)
{
    // 4 s to connect; 15 s per chunk; play strictly in order, skipping the lost.
    if (TtsSocket && !bTtsReady && Now - TtsConnectAt > 4)
    { TtsSocket->Close(); TtsSocket.Reset(); if (TtsWaiting.Num()) Narrate(TEXT("TTS: couldn't reach the voice endpoint")); TtsWaiting.Reset(); }
    while (TtsPlayNext < TtsSerial)
    {
        const int32 Next = TtsPlayNext + 1;
        if (TArray<float>* Pcm = TtsDone.Find(Next))
        {
            if (Pcm->Num() && Voice)
            {
                Voice->QueueSynth(*Pcm);
                ++TtsSpokenChunks;
                // Sidetone: hear my own voice at 0.8 x volume.
                if (!TtsSidetone)
                {
                    TtsSidetone = NewObject<USoundWaveProcedural>(this);
                    TtsSidetone->SetSampleRate(48000); TtsSidetone->NumChannels = 1; TtsSidetone->Duration = INDEFINITELY_LOOPING_DURATION; TtsSidetone->bLooping = false;
                    TtsSidetoneAudio = NewObject<UAudioComponent>(this); TtsSidetoneAudio->bAllowSpatialization = false; TtsSidetoneAudio->SetSound(TtsSidetone);
                    TtsSidetoneAudio->RegisterComponent(); TtsSidetoneAudio->Play();
                }
                TtsSidetoneAudio->SetVolumeMultiplier(FMath::Max(.0001f, .8f * FMath::Clamp(CVarTtsVolume.GetValueOnGameThread(), 0.f, 1.f)));
                TArray<int16> S16; S16.SetNumUninitialized(Pcm->Num());
                for (int32 I = 0; I < Pcm->Num(); ++I) S16[I] = int16(FMath::Clamp((*Pcm)[I], -1.f, 1.f) * 32767.f);
                TtsSidetone->QueueAudio(reinterpret_cast<const uint8*>(S16.GetData()), S16.Num() * 2);
            }
            TtsDone.Remove(Next); TtsSentAt.Remove(Next); ++TtsPlayNext;
            continue;
        }
        const double* At = TtsSentAt.Find(Next);
        if (!At || Now - *At > 15) { TtsSentAt.Remove(Next); ++TtsPlayNext; continue; }
        break;
    }
}
