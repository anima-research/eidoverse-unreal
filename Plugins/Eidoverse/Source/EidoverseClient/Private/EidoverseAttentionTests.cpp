// Eidoverse.Attention: the native attention gate against eidoverse-worlds tools/attention-test.ts (the pure
// gating half), plus the tones and the page session.
#include "EidoverseAttention.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEidoverseAttention, "Eidoverse.Attention", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FEidoverseAttention::RunTest(const FString&)
{
    using namespace EidoAttention;
    double Now = 0; FString Me = TEXT("tester"), World = TEXT("a");
    FGate Gate;
    Gate.NowMs = [&Now]() { return Now; };
    Gate.Me = [&Me]() { return Me; };
    Gate.Scope = [&World]() { return World; };
    Gate.MentionsMe = [&Me](const FString& Text) { return Text.Contains(Me, ESearchCase::CaseSensitive); };
    Gate.Play = []() { return true; };
    int64 Seq = 0;
    auto Say = [&Seq](const FString& Text = TEXT("@tester hello"), const FString& Actor = TEXT("other"), TOptional<int64> Utt = {}, bool bSpoken = false)
    {
        FSay S; S.Verb = TEXT("say"); S.Seq = ++Seq; S.bSeqValid = true; S.Actor = Actor; S.Text = Text; S.bSpoken = bSpoken; S.Utt = Utt;
        return S;
    };
    auto Spoken = [&Say](const FString& Text, int64 Utt, const FString& Actor = TEXT("other")) { return Say(Text, Actor, Utt, true); };
    auto Tick = [&Now]() { Now += CooldownMs; };

    TestTrue(TEXT("live mention admitted"), Gate.Say(Say()));
    TestFalse(TEXT("burst coalesced"), Gate.Say(Say()));
    Tick();
    TestFalse(TEXT("ambient ignored"), Gate.Say(Say(TEXT("hello room"))));
    TestFalse(TEXT("self ignored"), Gate.Say(Say(TEXT("@tester"), Me)));
    { FSay R = Say(); R.bReplay = true; TestFalse(TEXT("replay ignored"), Gate.Say(R)); }
    { FSay C = Say(); C.Verb = TEXT("caption"); TestFalse(TEXT("non-speech ignored"), Gate.Say(C)); }
    const FSay E = Say();
    TestTrue(TEXT("next live mention admitted"), Gate.Say(E)); Tick();
    TestFalse(TEXT("duplicate sequence ignored after cooldown"), Gate.Say(E));
    TestTrue(TEXT("spoken mention admitted"), Gate.Say(Spoken(TEXT("@tester"), 7))); Tick();
    TestFalse(TEXT("same utterance continuation ignored after cooldown"), Gate.Say(Spoken(TEXT("@tester again"), 7)));
    TestTrue(TEXT("another actor may use the same utt"), Gate.Say(Spoken(TEXT("@tester"), 7, TEXT("third")))); Tick();
    TestFalse(TEXT("non-mention beginning stays eligible"), Gate.Say(Spoken(TEXT("hello"), 8)));
    TestTrue(TEXT("later directed sentence admitted"), Gate.Say(Spoken(TEXT("@tester"), 8))); Tick();
    FWhisper Dm; Dm.From = TEXT("other"); Dm.To = TEXT("tester"); Dm.Text = TEXT("hello");
    TestTrue(TEXT("incoming whisper admitted"), Gate.Whisper(Dm)); Tick();
    { FWhisper W = Dm; W.bReplay = true; TestFalse(TEXT("held whisper ignored"), Gate.Whisper(W)); }
    { FWhisper W = Dm; W.bEcho = true; TestFalse(TEXT("outgoing whisper ignored"), Gate.Whisper(W)); }
    { FWhisper W = Dm; W.To = TEXT("elsewhere"); TestFalse(TEXT("wrong-recipient whisper ignored"), Gate.Whisper(W)); }
    { FWhisper W = Dm; W.From = TEXT("tester"); TestFalse(TEXT("self whisper ignored"), Gate.Whisper(W)); }
    World = TEXT("b");
    { FSay S = Say(); S.Seq = 1; TestTrue(TEXT("sequence restarts in a new world"), Gate.Say(S)); }
    Me = TEXT("renamed");
    { FSay S = Say(TEXT("@renamed")); S.Seq = 2; TestTrue(TEXT("renaming updates mention and self matching"), Gate.Say(S)); }
    Tick();
    TestFalse(TEXT("old name no longer addressed"), Gate.Say(Say(TEXT("@tester"))));

    // Utterance counters are sender-local, scoped by the sender's page session.
    Me = TEXT("tester"); World = TEXT("sessions");
    using FPeople = TArray<TPair<FString, TOptional<FString>>>;
    auto One = [](const TCHAR* Session) { FPeople P; P.Add({TEXT("other"), Session ? TOptional<FString>(Session) : TOptional<FString>()}); return P; };
    Gate.Roster(One(TEXT("boot-a:1")));
    TestTrue(TEXT("first sender session owns its utterance"), Gate.Say(Spoken(TEXT("@tester"), 1))); Tick();
    Gate.Roster(One(TEXT("boot-a:1")));
    TestFalse(TEXT("unchanged session snapshot retains dedup"), Gate.Say(Spoken(TEXT("@tester"), 1)));
    Gate.Participant(TEXT("other"), FString(TEXT("boot-a:2")));
    TestTrue(TEXT("sender rejoin can reuse its utterance counter"), Gate.Say(Spoken(TEXT("@tester"), 1))); Tick();
    Gate.Roster(One(TEXT("boot-b:2")));
    TestTrue(TEXT("server restart cannot collide at the same generation"), Gate.Say(Spoken(TEXT("@tester"), 1))); Tick();
    Gate.Roster(FPeople());
    Gate.Participant(TEXT("other"), FString(TEXT("boot-b:3")));
    TestTrue(TEXT("back after leaving the roster with a new session is fresh"), Gate.Say(Spoken(TEXT("@tester"), 1))); Tick();
    Gate.Roster(One(nullptr));
    TestTrue(TEXT("first legacy sender without session can chime"), Gate.Say(Spoken(TEXT("@tester"), 2))); Tick();
    Gate.Roster(One(nullptr));
    TestFalse(TEXT("sessionless reconnect snapshot preserves dedup"), Gate.Say(Spoken(TEXT("@tester"), 2)));
    Gate.Participant(TEXT("other"), TOptional<FString>());
    TestTrue(TEXT("explicit sessionless arrival permits counter reuse"), Gate.Say(Spoken(TEXT("@tester"), 2))); Tick();
    Gate.Roster(One(TEXT("boot-c:9")));
    TestTrue(TEXT("known sender session establishes cache"), Gate.Say(Spoken(TEXT("@tester"), 3))); Tick();
    Gate.Roster(One(nullptr));
    TestFalse(TEXT("known to sessionless snapshot does not invent a lifetime"), Gate.Say(Spoken(TEXT("@tester"), 3)));
    Gate.Roster(One(TEXT("boot-c:9")));
    TestFalse(TEXT("later same known session retains dedup"), Gate.Say(Spoken(TEXT("@tester"), 3)));
    Gate.Participant(TEXT("other"), FString(TEXT("page-d")));
    TestTrue(TEXT("a new page forgets the old page's utterances"), Gate.Say(Spoken(TEXT("@tester"), 3))); Tick();
    Gate.Participant(TEXT("other"), FString(TEXT("page-d")));
    TestFalse(TEXT("a reconnect of the same page keeps its utterance memory"), Gate.Say(Spoken(TEXT("@tester"), 3)));
    Gate.Roster(FPeople());
    Gate.Participant(TEXT("other"), FString(TEXT("page-d")));
    TestFalse(TEXT("a snapshot that misses a reconnecting page keeps its memory"), Gate.Say(Spoken(TEXT("@tester"), 3)));
    Gate.Participant(TEXT("other"), FString(TEXT("page-e")));
    TestTrue(TEXT("a new page restarts the counter"), Gate.Say(Spoken(TEXT("@tester"), 3))); Tick();

    // A silent notice consumes identity, not the cooldown.
    bool bAudible = false;
    FGate Silent;
    Silent.NowMs = [&Now]() { return Now; }; Silent.Me = []() { return FString(TEXT("tester")); }; Silent.Scope = []() { return FString(TEXT("silent")); };
    Silent.MentionsMe = [](const FString&) { return true; }; Silent.Play = [&bAudible]() { return bAudible; };
    TestFalse(TEXT("silent spoken notice stays silent"), Silent.Say(Spoken(TEXT("@tester"), 4)));
    bAudible = true;
    TestFalse(TEXT("silent utterance never replays on enable"), Silent.Say(Spoken(TEXT("@tester"), 4)));
    TestTrue(TEXT("new utterance immediately eligible after silent notice"), Silent.Say(Spoken(TEXT("@tester"), 5)));

    // Reading a wire entry: only safe-integer seq/utt count, spoken must be true.
    {
        auto Entry = MakeShared<FJsonObject>(); auto Args = MakeShared<FJsonObject>();
        Entry->SetStringField(TEXT("verb"), TEXT("say")); Entry->SetStringField(TEXT("actor"), TEXT("other")); Entry->SetNumberField(TEXT("seq"), 41.5);
        Args->SetStringField(TEXT("text"), TEXT("@tester")); Args->SetBoolField(TEXT("spoken"), true); Args->SetNumberField(TEXT("utt"), 9);
        Entry->SetObjectField(TEXT("args"), Args);
        const FSay S = ReadSay(Entry);
        TestFalse(TEXT("a fractional seq is not a position"), S.bSeqValid);
        TestTrue(TEXT("spoken utterance read"), S.bSpoken && S.Utt.IsSet() && S.Utt.GetValue() == 9);
    }

    // The mention rule: @name or the whole word, any case; never inside another word.
    TestTrue(TEXT("@name"), Mentions(TEXT("hi @Tester!"), TEXT("tester")));
    TestTrue(TEXT("bare whole word"), Mentions(TEXT("tester, look"), TEXT("tester")));
    TestFalse(TEXT("not inside a word"), Mentions(TEXT("protesters"), TEXT("tester")));

    // Tones: lengths, an attack from silence, the volume ceiling, silence at zero.
    const TArray<int16> Soft = RenderTone(TEXT("soft"), 1, 48000), Chime = RenderTone(TEXT("chime"), 1, 48000), Tap = RenderTone(TEXT("tap"), 1, 48000);
    TestEqual(TEXT("soft lasts 0.25 s"), Soft.Num(), 12000);
    TestEqual(TEXT("chime lasts 0.35 s"), Chime.Num(), 16800);
    TestEqual(TEXT("tap lasts 0.08 s"), Tap.Num(), 3840);
    TestEqual(TEXT("starts from silence"), int32(Soft[0]), 0);
    int32 Peak = 0; for (int16 V : Soft) Peak = FMath::Max(Peak, FMath::Abs(int32(V)));
    TestTrue(TEXT("peaks near volume x 0.15"), Peak > 0.13f * 32767 && Peak <= 0.151f * 32767);
    int32 Quiet = 0; for (int16 V : RenderTone(TEXT("soft"), 0, 48000)) Quiet = FMath::Max(Quiet, FMath::Abs(int32(V)));
    TestEqual(TEXT("zero volume is silent"), Quiet, 0);

    // The page session fits the server's 8-64 of [A-Za-z0-9_-], and is one id for the run.
    const FString& Session = PageSession();
    bool bShape = Session.Len() >= 8 && Session.Len() <= 64;
    for (TCHAR C : Session) bShape &= FChar::IsAlnum(C) || C == '_' || C == '-';
    TestTrue(TEXT("page session shape"), bShape);
    TestEqual(TEXT("page session is stable"), PageSession(), Session);
    return true;
}
#endif
