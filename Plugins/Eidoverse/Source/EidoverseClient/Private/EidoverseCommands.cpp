// Chat slash commands: one table, as the browser's commands/registry.js, so names,
// aliases, help and autocomplete come from one place. An unknown command is never
// posted to the room.
#include "EidoverseBridge.h"
#include "EidoverseUI.h"
#include "EidoverseScene.h"
#include "EidoverseAvatar.h"
#include "EidoverseClips.h"
#include "EidoverseSettings.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Kismet/GameplayStatics.h"
#include "IWebSocket.h"

namespace
{
    struct FCommandDef { const TCHAR* Name; TArray<const TCHAR*> Aliases; const TCHAR* Help; bool bListed; bool bAliasAsAction; };
    const TArray<FCommandDef>& Commands()
    {
        static const TArray<FCommandDef> Table = {
            {TEXT("w"), {TEXT("whisper")}, TEXT("/w <name> <message> — whisper, privately"), true, false},
            {TEXT("r"), {TEXT("reply")}, TEXT("reply to the last whisper you got"), true, false},
            {TEXT("name"), {TEXT("rename")}, TEXT("/name <new name> — change what the world calls you"), true, false},
            {TEXT("me"), {}, TEXT("describe an action — \"/me waves\""), true, false},
            {TEXT("emote"), {}, TEXT("/emote <name> — the emote bar's gestures (or number keys)"), true, false},
            {TEXT("sit"), {}, TEXT("sit down, on a seat if one is near"), true, false},
            {TEXT("push"), {TEXT("shove")}, TEXT("/push [name] [power] — shove someone within reach (their client decides); /push <thing> works the thing"), true, false},
            {TEXT("touch"), {TEXT("reach")}, TEXT("/touch [name] [point] [left|right] — rest a hand on someone; the hand tracks them until /letgo"), true, false},
            {TEXT("letgo"), {TEXT("release"), TEXT("clear_reach")}, TEXT("let go — lower your reaching hand (or just one: /letgo left|right)"), true, false},
            {TEXT("eyes"), {}, TEXT("/eyes close|open — hold your eyes shut, if your rig has eyelids"), true, false},
            {TEXT("pushable"), {}, TEXT("/pushable on|off — whether shoves and blasts can knock you over (on by default)"), true, false},
            {TEXT("mic"), {}, TEXT("/mic on|off — your microphone (V toggles; push-to-talk in settings)"), true, false},
            {TEXT("tts"), {}, TEXT("/tts on|off|<ws:// or http:// endpoint> — speak your messages with your own voice while the mic is off"), true, false},
            {TEXT("hear"), {TEXT("voices")}, TEXT("/hear on|off|hush — whether other people's voices reach you (off by default)"), true, false},
            {TEXT("posable"), {}, TEXT("/posable on|off — whether others may pose or animate your body (off by default)"), true, false},
            {TEXT("boom"), {TEXT("blast")}, TEXT("/boom [power] [radius] — a blast where you stand, you included (builder)"), true, false},
            {TEXT("kick"), {}, TEXT("/kick <name> [reason] — moderation: remove someone from this world (owner). Objects fly with /punt"), true, false},
            {TEXT("punt"), {TEXT("boot")}, TEXT("/punt [thing|person] [power] — boot an object into flight; a person's name is the ragdoll shove"), true, false},
            {TEXT("epoch"), {}, TEXT("/epoch [tickMs|off] — owner: hand this world's punt flights to the deterministic sim"), true, false},
            {TEXT("who"), {}, TEXT("list everyone present"), true, false},
            {TEXT("panels"), {}, TEXT("/panels <0.3–1> — panel opacity"), true, false},
            {TEXT("role"), {}, TEXT("what you may do here (or /role <name>)"), true, false},
            {TEXT("grant"), {}, TEXT("/grant <name> owner|builder|visitor [+gen|-gen] [+fly|-fly] — owner only"), true, false},
            {TEXT("ban"), {}, TEXT("/ban <name> [reason] — ban someone from this world (owner)"), true, false},
            {TEXT("unban"), {}, TEXT("/unban <name> — lift a ban here (owner)"), true, false},
            {TEXT("bans"), {}, TEXT("who is banned from this world"), true, false},
            {TEXT("gban"), {}, TEXT("/gban <name> [reason] — ban from ALL worlds (operator)"), true, false},
            {TEXT("gunban"), {}, TEXT("/gunban <name> — lift a global ban (operator)"), true, false},
            {TEXT("gbans"), {}, TEXT("list global bans (operator)"), true, false},
            {TEXT("fork"), {TEXT("copy")}, TEXT("/fork <new-name> — copy this world, history and all (owner)"), true, false},
            {TEXT("reset"), {TEXT("erase")}, TEXT("erase this world back to zero, archived not destroyed (owner)"), true, false},
            {TEXT("goto"), {}, TEXT("/goto <name> — where someone is"), true, false},
            {TEXT("go"), {}, TEXT("/go <world> — travel to another world"), true, false},
            {TEXT("presence"), {TEXT("away"), TEXT("busy"), TEXT("back")}, TEXT("/presence present|away|busy — how you show in People"), true, false},
            {TEXT("clear"), {}, TEXT("clear your chat log"), true, false},
            {TEXT("help"), {}, TEXT("open the help sheet"), true, false},
            {TEXT("flight"), {}, TEXT("why flight is (or is not) doing that \u2014 rig, capability, phase"), true, false},
            {TEXT("use"), {TEXT("pull"), TEXT("ring"), TEXT("open")}, TEXT("/use <thing> [action]"), false, true},
            {TEXT("mount"), {TEXT("attach")}, TEXT("/mount <thing> <onto> [slot]"), false, false},
            {TEXT("dismount"), {TEXT("detach")}, TEXT("/dismount <thing>"), false, false},
            {TEXT("debug"), {}, TEXT("/debug [n] — the world's flight recorder"), false, false},
            {TEXT("scripts"), {}, TEXT("list the behaviours bound in this world"), true, false},
            {TEXT("script"), {TEXT("unbind")}, TEXT("/script <id> \u2014 a behaviour's console; /unbind <id> removes it"), true, false},
            {TEXT("wave"), {TEXT("cheer"), TEXT("point")}, TEXT("emote shortcuts"), false, false},
        };
        return Table;
    }
    const FCommandDef* FindCommand(const FString& Word, bool& bViaAlias)
    {
        for (const FCommandDef& C : Commands())
        {
            if (Word.Equals(C.Name, ESearchCase::IgnoreCase)) { bViaAlias = false; return &C; }
            for (const TCHAR* A : C.Aliases) if (Word.Equals(A, ESearchCase::IgnoreCase)) { bViaAlias = true; return &C; }
        }
        return nullptr;
    }
    FString Bearing(double Dx, double Dz)
    {
        static const TCHAR* Dirs[] = {TEXT("N"), TEXT("NE"), TEXT("E"), TEXT("SE"), TEXT("S"), TEXT("SW"), TEXT("W"), TEXT("NW")};
        const double A = FMath::Fmod(FMath::Atan2(Dx, Dz) + 2 * PI, 2 * PI);
        return Dirs[int32(FMath::RoundToDouble(A / (PI / 4))) % 8];
    }
    bool IsNumber(const FString& S) { return !S.IsEmpty() && S.IsNumeric(); }
}

TArray<FString> AEidoverseBridge::CommandCompletions(const FString& Prefix) const
{
    TArray<FString> Out;
    for (const FCommandDef& C : Commands())
        if (C.bListed && FString(C.Name).StartsWith(Prefix, ESearchCase::IgnoreCase)) Out.Add(FString::Printf(TEXT("/%s\t%s"), C.Name, C.Help));
    return Out;
}

FVector AEidoverseBridge::MyWirePosition() const
{
    const APawn* Me = UGameplayStatics::GetPlayerPawn(this, 0);
    if (!Me) return FVector::ZeroVector;
    const bool bLand = Me->IsA<AEidoverseExplorer>();
    return ToWirePosition(Me->GetActorLocation() - OriginCentimeters - (bLand ? FVector(0, 0, 88) : FVector::ZeroVector));
}

bool AEidoverseBridge::HandleInteractionCommand(const FString& Text)
{
    TArray<FString> A; Text.ParseIntoArrayWS(A);
    if (A.IsEmpty() || !A[0].StartsWith(TEXT("/"))) return false;
    const FString Word = A[0].Mid(1);
    const FString Rest = Text.TrimStart().Mid(A[0].Len()).TrimStartAndEnd();
    TArray<FString> Args(A.GetData() + 1, A.Num() - 1);
    bool bViaAlias = false;
    const FCommandDef* Def = FindCommand(Word, bViaAlias);
    InteractionStatus.Empty();
    if (!Def) { Narrate(FString::Printf(TEXT("unknown command /%s — try /help"), *Word)); return true; }
    const FString Name = Def->Name;
    auto Usage = [this, Def]() { Narrate(FString::Printf(TEXT("usage: %s"), Def->Help)); };
    auto Verb = [this](const FString& V, const TSharedRef<FJsonObject>& Args2) { return SendVerb(V, Args2); };
    auto Obj = []() { return MakeShared<FJsonObject>(); };

    if (Name == TEXT("w"))
    {
        if (Args.IsEmpty()) { Usage(); return true; }
        const FString To = Args[0], Body = Rest.Mid(To.Len()).TrimStartAndEnd();
        if (Body.IsEmpty()) { if (HUD) HUD->OpenDm(To); return true; }   // a DM tab, as chat.js
        if (!Whisper(To, Body)) Narrate(TEXT("couldn't send that whisper"));
    }
    else if (Name == TEXT("r"))
    {
        if (LastWhisperFrom.IsEmpty()) { Narrate(TEXT("nobody has whispered you yet")); return true; }
        if (Rest.IsEmpty()) { if (HUD) HUD->OpenDm(LastWhisperFrom); return true; }
        Whisper(LastWhisperFrom, Rest);
    }
    else if (Name == TEXT("name"))
    {
        if (Rest.IsEmpty()) Narrate(FString::Printf(TEXT("you are %s — /name <new name> to change it"), *AssignedIdentity));
        else Narrate(TEXT("renaming mid-session isn't supported yet — set your name at the door (disconnect, then join with the new name)"));
    }
    else if (Name == TEXT("me")) { if (Rest.IsEmpty()) Usage(); else Say(FString::Printf(TEXT("* %s %s"), *AssignedIdentity, *Rest)); }
    else if (Name == TEXT("who"))
    {
        TArray<FString> Here; for (const auto& P : Peers) Here.Add(P.Key); Here.Sort();
        Narrate(Here.IsEmpty() ? TEXT("just you") : TEXT("here now: ") + FString::Join(Here, TEXT(", ")));
    }
    else if (Name == TEXT("clear")) { ChatHistory.Reset(); if (HUD) HUD->ClearChat(); }
    else if (Name == TEXT("help")) { if (HUD) HUD->ToggleHelp(); }
    else if (Name == TEXT("flight")) { TArray<FString> Lines; FlightDiagnostic().ParseIntoArrayLines(Lines); for (const FString& L : Lines) Narrate(L); }
    else if (Name == TEXT("panels"))
    {
        const float V = Args.Num() ? FCString::Atof(*Args[0]) : -1;
        if (V < .3f || V > 1.f) { Narrate(FString::Printf(TEXT("usage: /panels <0.3–1> — panel opacity (current %.2f)"), EidoSettings::PanelOpacity())); return true; }
        EidoSettings::Set(TEXT("Eidoverse.UI.PanelOpacity"), FString::SanitizeFloat(V));
        if (HUD) HUD->RefreshPanelOpacity();
        Narrate(FString::Printf(TEXT("panels at %.0f%% opacity"), V * 100));
    }
    else if (Name == TEXT("emote") || Name == TEXT("wave"))
    {
        const FString E = Name == TEXT("wave") ? Word.ToLower() : Rest.ToLower();
        TArray<FString> Known; if (ClipLibrary) Known = ClipLibrary->EmoteNames();
        if (E.IsEmpty() || (!Known.IsEmpty() && !Known.Contains(E))) { Narrate(TEXT("emotes: ") + FString::Join(Known, TEXT(", "))); return true; }
        Emote(E);
    }
    else if (Name == TEXT("sit"))
    {
        auto* Explorer = Cast<AEidoverseExplorer>(UGameplayStatics::GetPlayerPawn(this, 0));
        if (!Explorer) return true;
        if (!SitOnNamed(Explorer, Rest)) Explorer->Posture = TEXT("sit");
    }
    else if (Name == TEXT("push"))
    {
        FString Target; float Power = 2.2f;
        for (const FString& S : Args) { if (IsNumber(S)) Power = FCString::Atof(*S); else Target = S; }
        if (!Target.IsEmpty() && DynamicScene && DynamicScene->EntityActor(Target)) { UseEntity(Target, TEXT("push")); return true; }
        ShovePerson(Target, Power, TEXT("push"));
    }
    else if (Name == TEXT("punt"))
    {
        FString Target; float Power = -1;
        for (const FString& S : Args) { if (IsNumber(S)) Power = FCString::Atof(*S); else Target = S; }
        if (!Target.IsEmpty() && !(DynamicScene && DynamicScene->EntityActor(Target)) && Peers.Contains(Target))
        { ShovePerson(Target, Power > 0 ? Power : 2.6f, TEXT("punt")); return true; }
        PuntNearest(Target, Power > 0 ? Power : 4.f);
    }
    else if (Name == TEXT("touch"))
    {
        // /touch [name|me] [point words] [left|right]: nearest person within 6 m by default.
        TArray<FString> W = Args; bool bLeft = false;
        if (W.Num() && (W.Last() == TEXT("left") || W.Last() == TEXT("right"))) { bLeft = W.Last() == TEXT("left"); W.Pop(); }
        FString Who;
        if (W.Num() && (Peers.Contains(W[0]) || W[0] == TEXT("me"))) { Who = W[0] == TEXT("me") ? AssignedIdentity : W[0]; W.RemoveAt(0); }
        if (Who.IsEmpty())
        {
            const FVector Me = MyWirePosition(); double Best = 6;
            for (const auto& P : Peers) if (P.Value.Actor.IsValid())
            { const FVector Pw = ToWirePosition(P.Value.Actor->GetActorLocation() - OriginCentimeters); const double D = FVector::Dist(Pw, Me); if (D < Best) { Best = D; Who = P.Key; } }
        }
        if (Who.IsEmpty()) { Narrate(TEXT("nobody within reach to touch")); return true; }
        FString Point = FString::Join(W, TEXT("_")).ToLower();
        if (Point.IsEmpty()) Point = TEXT("shoulder_l"); else if (Point == TEXT("head")) Point = TEXT("head_top");
        if (ReachTo(Who, Point, bLeft)) Narrate(FString::Printf(TEXT("you reach for %s's %s (%s hand)…"), *Who, *Point, bLeft ? TEXT("left") : TEXT("right")));
    }
    else if (Name == TEXT("letgo")) { ClearReach(); Narrate(TEXT("you let go")); }
    else if (Name == TEXT("eyes"))
    {
        auto* M = SelfBody.AvatarMesh.Get();
        const bool bShut = Args.Num() ? Args[0] == TEXT("close") : !(M && M->EyesHeldShut());
        if (!M || !M->SetEyesHeldShut(bShut)) { Narrate(TEXT("this body has no eyelids — nothing to close")); return true; }
        Narrate(bShut ? TEXT("you close your eyes") : TEXT("you open your eyes"));
    }
    else if (Name == TEXT("pushable") || Name == TEXT("posable"))
    {
        const bool bPush = Name == TEXT("pushable");
        const TCHAR* Var = bPush ? TEXT("Eidoverse.Consent.Pushable") : TEXT("Eidoverse.Consent.Posable");
        if (Args.Num() && (Args[0] == TEXT("on") || Args[0] == TEXT("off"))) EidoSettings::Set(Var, Args[0] == TEXT("on") ? TEXT("1") : TEXT("0"));
        if (bPush) Narrate(EidoSettings::Pushable() ? TEXT("shoves and blasts CAN knock you over (/pushable off to stop them)") : TEXT("shoves and blasts can NOT knock you over (/pushable on to allow)"));
        else Narrate(EidoSettings::Posable() ? TEXT("others MAY pose and animate your body (/posable off to stop them)") : TEXT("others may NOT pose your body (/posable on to allow)"));
    }
    else if (Name == TEXT("mic"))
    {
        if (Args.Num() && (Args[0] == TEXT("on") || Args[0] == TEXT("off"))) SetMicOn(Args[0] == TEXT("on"));
        else Narrate(FString::Printf(TEXT("mic %s · %s"), IsMicOn() ? TEXT("on") : TEXT("off"), *VoiceDiagnostic()));
    }
    else if (Name == TEXT("tts"))
    {
        if (Args.Num() && (Args[0] == TEXT("on") || Args[0] == TEXT("off"))) SetTtsEnabled(Args[0] == TEXT("on"));
        else if (Args.Num() && Args[0].Contains(TEXT("://"))) { EidoSettings::Set(TEXT("Eidoverse.Audio.TtsEndpoint"), Args[0].Left(300)); Narrate(TEXT("TTS endpoint: ") + Args[0].Left(300)); }
        else Narrate(IsTtsOn() ? TEXT("TTS is on (/tts off to stop)") : TEXT("TTS is off (/tts on, /tts <endpoint>)"));
    }
    else if (Name == TEXT("hear"))
    {
        if (Args.Num() && (Args[0] == TEXT("on") || Args[0] == TEXT("off"))) SetHearVoices(Args[0] == TEXT("on"));
        else if (Args.Num() && Args[0] == TEXT("hush")) { bVoiceHushed = !bVoiceHushed; Narrate(bVoiceHushed ? TEXT("voices hushed") : TEXT("voices unhushed")); }
        else Narrate(EidoSettings::HearVoices() ? TEXT("you hear voices (/hear off to stop, /hear hush to quiet them)") : TEXT("voices are off (/hear on to connect)"));
    }
    else if (Name == TEXT("boom"))
    {
        const FVector P = MyWirePosition(); auto Arg = Obj();
        Arg->SetArrayField(TEXT("at"), {MakeShared<FJsonValueNumber>(P.X), MakeShared<FJsonValueNumber>(P.Y), MakeShared<FJsonValueNumber>(P.Z)});
        if (Args.Num() > 0 && IsNumber(Args[0])) Arg->SetNumberField(TEXT("power"), FCString::Atod(*Args[0]));
        if (Args.Num() > 1 && IsNumber(Args[1])) Arg->SetNumberField(TEXT("radius"), FCString::Atod(*Args[1]));
        Verb(TEXT("force"), Arg);
    }
    else if (Name == TEXT("kick") || Name == TEXT("ban"))
    {
        if (Args.IsEmpty()) { Narrate(Name == TEXT("kick") ? TEXT("usage: /kick <name> [reason] — they can rejoin; /ban keeps them out") : TEXT("usage: /ban <name> [reason] — a kick that sticks — /unban lifts it")); return true; }
        auto Arg = Obj(); Arg->SetStringField(TEXT("id"), Args[0]);
        const FString Reason = Rest.Mid(Args[0].Len()).TrimStartAndEnd();
        if (!Reason.IsEmpty()) Arg->SetStringField(TEXT("reason"), Reason.Left(200));
        Verb(Name, Arg);
    }
    else if (Name == TEXT("unban")) { if (Args.IsEmpty()) Usage(); else { auto Arg = Obj(); Arg->SetStringField(TEXT("id"), Args[0]); Verb(TEXT("unban"), Arg); } }
    else if (Name == TEXT("bans")) SendRaw(TEXT("world-bans"), Obj());
    else if (Name == TEXT("gban") || Name == TEXT("gunban"))
    {
        if (Args.IsEmpty()) { Usage(); return true; }
        auto Arg = Obj(); Arg->SetStringField(TEXT("id"), Args[0]);
        const FString Reason = Rest.Mid(Args[0].Len()).TrimStartAndEnd();
        if (Name == TEXT("gban") && !Reason.IsEmpty()) Arg->SetStringField(TEXT("reason"), Reason.Left(200));
        SendRaw(Name == TEXT("gban") ? TEXT("global-ban") : TEXT("global-unban"), Arg);
    }
    else if (Name == TEXT("gbans")) SendRaw(TEXT("global-bans"), Obj());
    else if (Name == TEXT("role"))
    {
        const FString Who = Rest.IsEmpty() ? AssignedIdentity : Rest;
        if (Who == AssignedIdentity)
        {
            if (bWorldOpen) Narrate(TEXT("this world is open — everyone here can build"));
            else Narrate(WorldRole.IsEmpty() || WorldRole == TEXT("visitor") ? FString::Printf(TEXT("%s holds no role here (visitor)"), *Who) : FString::Printf(TEXT("%s: %s%s"), *Who, *WorldRole, bCanGenerate ? TEXT(" +gen") : TEXT("")));
        }
        else Narrate(FString::Printf(TEXT("%s's role isn't known to this client — ask an owner, or /role for your own"), *Who));
    }
    else if (Name == TEXT("grant"))
    {
        FString Role, Gen, Fly;
        for (int32 I = 1; I < Args.Num(); ++I)
        {
            const FString L = Args[I].ToLower();
            if (L == TEXT("owner") || L == TEXT("builder") || L == TEXT("visitor")) Role = L;
            else if (L == TEXT("+gen") || L == TEXT("-gen")) Gen = L;
            else if (L == TEXT("+fly") || L == TEXT("-fly")) Fly = L;
        }
        if (Args.IsEmpty() || (Role.IsEmpty() && Gen.IsEmpty() && Fly.IsEmpty())) { Usage(); return true; }
        auto Arg = Obj(); Arg->SetStringField(TEXT("id"), Args[0]);
        if (!Role.IsEmpty()) Arg->SetStringField(TEXT("role"), Role);
        if (!Gen.IsEmpty()) Arg->SetBoolField(TEXT("gen"), Gen == TEXT("+gen"));
        if (!Fly.IsEmpty()) Arg->SetBoolField(TEXT("fly"), Fly == TEXT("+fly"));
        Verb(TEXT("grant"), Arg);
    }
    else if (Name == TEXT("epoch"))
    {
        auto Arg = Obj();
        if (Args.Num() && (Args[0] == TEXT("off") || Args[0] == TEXT("leave") || Args[0] == TEXT("exit") || Args[0] == TEXT("none")))
        { Arg->SetField(TEXT("sim"), MakeShared<FJsonValueNull>()); Narrate(TEXT("asking to leave the sim epoch — flights are released where they stand")); }
        else
        {
            Arg->SetStringField(TEXT("sim"), SimId.IsEmpty() ? TEXT("eidosim@0.5.0") : SimId);
            Arg->SetNumberField(TEXT("tickMs"), Args.Num() && IsNumber(Args[0]) ? FMath::Clamp(FCString::Atoi(*Args[0]), 16, 1000) : 66);
            Narrate(TEXT("asking to enter the deterministic sim epoch — punts will replay identically everywhere"));
        }
        Verb(TEXT("epoch"), Arg);
    }
    else if (Name == TEXT("fork"))
    {
        if (Rest.IsEmpty()) { Narrate(TEXT("usage: /fork <new-name> — copies this world into a new one")); return true; }
        if (!IsWorldName(Rest)) { Narrate(FString::Printf(TEXT("“%s” won't do as a world name — letters, digits, - and _ only"), *Rest)); return true; }
        auto Arg = Obj(); Arg->SetStringField(TEXT("to"), Rest); SendRaw(TEXT("world-fork"), Arg);
    }
    else if (Name == TEXT("reset"))
    {
        if (Rest != WorldName) { Narrate(FString::Printf(TEXT("this erases “%s” back to zero — everything built and said here goes to the archive. if you mean it: /reset %s"), *WorldName, *WorldName)); return true; }
        auto Arg = Obj(); Arg->SetStringField(TEXT("name"), WorldName); SendRaw(TEXT("world-reset"), Arg);
    }
    else if (Name == TEXT("goto"))
    {
        const FEidoversePeer* P = nullptr; FString Found;
        for (const auto& Pair : Peers) if (Pair.Key.Equals(Rest, ESearchCase::IgnoreCase)) { P = &Pair.Value; Found = Pair.Key; }
        if (!P || !P->Actor.IsValid()) { Narrate(FString::Printf(TEXT("no one here called “%s”"), *Rest)); return true; }
        const FVector Me = MyWirePosition(), Them = ToWirePosition(P->Actor->GetActorLocation() - OriginCentimeters);
        const FString Line = FString::Printf(TEXT("%s is %.0fm away, bearing %s"), *Found, FVector::Dist(Me, Them), *Bearing(Them.X - Me.X, Them.Z - Me.Z));
        if (HUD) HUD->Hint(Line, 5); else Narrate(Line);
    }
    else if (Name == TEXT("go"))
    {
        if (!IsWorldName(Rest)) { Narrate(TEXT("usage: /go <world>")); return true; }
        SwitchWorld(Rest);
    }
    else if (Name == TEXT("presence"))
    {
        FString V = bViaAlias ? Word.ToLower() : Rest.ToLower();
        if (V == TEXT("back")) V = TEXT("present");
        if (V != TEXT("present") && V != TEXT("away") && V != TEXT("busy")) { Narrate(FString::Printf(TEXT("you show as %s — /presence present|away|busy"), *CurrentPresence())); return true; }
        SetPresence(V); Narrate(TEXT("you now show as ") + V);
    }
    else if (Name == TEXT("use"))
    {
        // An alias is the action: /pull lever1 = /use lever1 pull.
        if (Args.IsEmpty()) { Narrate(TEXT("usage: /use <thing> [action] — e.g. /push swing1")); return true; }
        if (DynamicScene && !DynamicScene->EntityActor(Args[0])) { Narrate(FString::Printf(TEXT("nothing here called “%s”"), *Args[0])); return true; }
        UseEntity(Args[0], bViaAlias ? Word.ToLower() : (Args.Num() > 1 ? Args[1] : TEXT("use")));
    }
    else if (Name == TEXT("mount"))
    {
        if (Args.Num() < 2) { Narrate(TEXT("usage: /mount <thing> <onto> [slot] — parents one thing to another, keeping its pose")); return true; }
        if (!DynamicScene || !DynamicScene->EntityActor(Args[0]) || !DynamicScene->EntityActor(Args[1])) { Narrate(TEXT("both things must exist (and be loaded) here")); return true; }
        AttachEntity(Args[0], Args[1], Args.Num() > 2 ? Args[2] : FString());
    }
    else if (Name == TEXT("dismount"))
    {
        if (Args.IsEmpty()) { Usage(); return true; }
        DetachEntity(Args[0]);
    }
    else if (Name == TEXT("scripts"))
    {
        auto Arg = Obj(); DebugReqId = FString::Printf(TEXT("d%d"), ++DebugRequests);
        Arg->SetStringField(TEXT("reqId"), DebugReqId); Arg->SetBoolField(TEXT("behaviors"), true);
        SendRaw(TEXT("debug"), Arg);
    }
    else if (Name == TEXT("script"))
    {
        if (Args.IsEmpty()) { Usage(); return true; }
        if (bViaAlias) { auto Arg = Obj(); Arg->SetStringField(TEXT("id"), Args[0]); Arg->SetBoolField(TEXT("remove"), true); Verb(TEXT("behavior"), Arg); return true; }
        auto Arg = Obj(); DebugReqId = FString::Printf(TEXT("d%d"), ++DebugRequests);
        Arg->SetStringField(TEXT("reqId"), DebugReqId); Arg->SetStringField(TEXT("behavior"), Args[0]); Arg->SetNumberField(TEXT("limit"), 40);
        SendRaw(TEXT("debug"), Arg);
    }
    else if (Name == TEXT("debug"))
    {
        auto Arg = Obj(); DebugReqId = FString::Printf(TEXT("d%d"), ++DebugRequests);
        Arg->SetStringField(TEXT("reqId"), DebugReqId);
        Arg->SetNumberField(TEXT("limit"), FMath::Clamp(Args.Num() ? FCString::Atoi(*Args[0]) : 12, 1, 50));
        SendRaw(TEXT("debug"), Arg);
    }
    if (!InteractionStatus.IsEmpty() && InteractionStatus != TEXT("Action requested; awaiting server.")) Narrate(InteractionStatus);
    return true;
}

bool AEidoverseBridge::PuntNearest(const FString& Named, float Power)
{
    if (!DynamicScene) return false;
    const FVector Me = MyWirePosition();
    FString Id = Named; double Dist = 1e9;
    for (const auto& Pair : DynamicScene->GetItems())
    {
        const AActor* A = Pair.Value.Actor.Get(); if (!A) continue;
        const FVector C = ToWirePosition(A->GetComponentsBoundingBox().GetCenter() - OriginCentimeters);
        const double D = FVector2D(C.X - Me.X, C.Z - Me.Z).Size();
        if (!Named.IsEmpty()) { if (Pair.Key == Named) { Dist = D; break; } continue; }
        if (Pair.Value.Kind == TEXT("light") || !Pair.Value.bCollide || Pair.Value.Motion) continue;
        if (D < 2.5 && D < Dist) { Dist = D; Id = Pair.Key; }
    }
    if (Id.IsEmpty() || (!Named.IsEmpty() && !DynamicScene->EntityActor(Named))) { Narrate(Named.IsEmpty() ? TEXT("nothing at your feet to kick") : FString::Printf(TEXT("nothing here called “%s”"), *Named)); return false; }
    const FEidoverseSceneItem* Item = DynamicScene->GetItems().Find(Id);
    if (Item && (Item->Motion || Item->Kind == TEXT("light"))) { Narrate(FString::Printf(TEXT("%s is animated by its motion — it can't be kicked loose (yet)"), *Id)); return false; }
    if (Dist > 3.3) { Narrate(FString::Printf(TEXT("%s is too far away to kick"), *Id)); return false; }
    return PuntEntity(Id, FMath::Clamp(Power, 1.f, 10.f));
}

