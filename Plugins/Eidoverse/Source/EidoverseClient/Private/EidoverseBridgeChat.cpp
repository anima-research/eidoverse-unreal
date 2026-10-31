// Slice B's hook (Plugins/Eidoverse/Bridge/SLICES.md): bridge/say and bridge/narration (Bridge/src/chat.ts).
#include "EidoverseBridge.h"
#include "EidoverseClips.h"
#include "EidoverseAttention.h"
#include "Dom/JsonObject.h"

const TCHAR* AEidoverseBridge::NoDirectMessages=TEXT("Direct messages aren't in world chat any more: speech here is public and arrival-only.");

void AEidoverseBridge::OnChatMsg(const FString& Type,const TSharedPtr<FJsonObject>& J)
{
    if(Type==TEXT("bridge/narration")){FString Text;if(J->TryGetStringField(TEXT("text"),Text))Narrate(Text.Left(400));return;}
    // bridge/say {seq, actor, name, text, spoken?, utt?, t0?, ts}
    FString Name,Text;double SeqN=-1,UttN=0;bool bSpoken=false;
    if(!J->TryGetStringField(TEXT("name"),Name)||!J->TryGetStringField(TEXT("text"),Text)||!J->TryGetNumberField(TEXT("seq"),SeqN)||SeqN<0)return;
    Name=Name.Left(64);Text=Text.Left(4000);J->TryGetBoolField(TEXT("spoken"),bSpoken);
    const FString Utt=bSpoken&&J->TryGetNumberField(TEXT("utt"),UttN)?FString::Printf(TEXT("%.0f"),UttN):FString();
    const int64 Seq=int64(SeqN)*1000;   // a history page's numbering (engine server/worldlog.ts ChatLine: seq × 1000)
    if(RenderedChatSeqs.Contains(Seq)||Seq<=ChatTrimmedThrough)return;RenderedChatSeqs.Add(Seq);
    EidoAttention::FSay S;S.Verb=TEXT("say");S.Actor=Name;S.Text=Text;S.Seq=Seq;S.bSeqValid=true;S.bSpoken=bSpoken;
    if(!Utt.IsEmpty())S.Utt=int64(UttN);
    AttentionGate.Say(S);
    AddChatLine(Name,Text,false,Utt,bSpoken,Seq);
    if(!bSpoken)NoteSpeaker(Name,Text);   // spoken says were performed live by their captions
}
FString AEidoverseBridge::PeerOnceAnim(const FString& Id) const
{
    const FEidoversePeer* P=Peers.Find(Id);return P?P->ActiveEmote:FString();
}
TArray<FString> AEidoverseBridge::ClipEmoteNames() const
{
    return ClipLibrary?ClipLibrary->EmoteNames():TArray<FString>();
}
