// Slice C's hook (Plugins/Eidoverse/Bridge/SLICES.md): the build and world writes as bridge/do (Bridge/src/writes.ts), and OnWriteReply, which re-keys a create's $b<n> by the reply's ids.
#include "EidoverseBridge.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace EidoWrites
{
    TSharedPtr<FJsonValue> Round(double X, double K) { return MakeShared<FJsonValueNumber>(FMath::RoundToDouble(X * K) / K); }
    FString Str(const TSharedPtr<FJsonObject>& O, const TCHAR* Key) { FString S; if (O) O->TryGetStringField(Key, S); return S; }
}

void AEidoverseBridge::OnWriteReply(const FString& Type,const TSharedPtr<FJsonObject>& J)
{
    // a create's answer: its $b<n> is the minted id from here on (ResolveBuildId); refused, it never will be
    FString Ref;if(!J||!PendingCreates.RemoveAndCopyValue(EidoWrites::Str(J,TEXT("req")),Ref))return;
    const TSharedPtr<FJsonObject>* Reply=nullptr;const TSharedPtr<FJsonObject>* Ids=nullptr;FString Minted;
    if(Type==TEXT("bridge/done")&&J->TryGetObjectField(TEXT("reply"),Reply)&&(*Reply)->TryGetObjectField(TEXT("ids"),Ids))(*Ids)->TryGetStringField(Ref,Minted);
    if(Minted.IsEmpty()){RefusedRefs.Add(Ref);if(PendingSelect==Ref)PendingSelect.Empty();return;}
    MintedIds.Add(Ref,Minted);
    if(PendingSelect==Ref)PendingSelect=Minted;
}

FString AEidoverseBridge::ResolveBuildId(const FString& Id) const { const FString* M=MintedIds.Find(Id);return M?*M:Id; }

TSharedRef<FJsonObject> AEidoverseBridge::TransformFields(const FVector& PosWire,double Yaw,double Scale)
{
    // requests.js create: a whole transform, q a heading about +Y (rot.js qFromYaw)
    using namespace EidoWrites;
    auto T=MakeShared<FJsonObject>();
    T->SetArrayField(TEXT("pos"),{Round(PosWire.X,1000),Round(PosWire.Y,1000),Round(PosWire.Z,1000)});
    T->SetArrayField(TEXT("q"),{Round(0,1),Round(FMath::Sin(Yaw/2),1e7),Round(0,1),Round(FMath::Cos(Yaw/2),1e7)});
    if(!FMath::IsNearlyEqual(Scale,1.0)){const TSharedPtr<FJsonValue> S=Round(Scale,1000);T->SetArrayField(TEXT("scale"),{S,S,S});}
    return T;
}

TSharedRef<FJsonObject> AEidoverseBridge::PlaceFields(const FString& Id,const FVector& PosWire,double Yaw,double Scale)
{
    using namespace EidoWrites;
    auto P=MakeShared<FJsonObject>();
    P->SetArrayField(TEXT("pos"),{Round(PosWire.X,1000),Round(PosWire.Y,1000),Round(PosWire.Z,1000)});
    P->SetField(TEXT("yaw"),Round(Yaw,10000));P->SetField(TEXT("scale"),Round(Scale,1000));
    auto F=MakeShared<FJsonObject>();F->SetStringField(TEXT("id"),Id);F->SetObjectField(TEXT("patch"),P);
    return F;
}

FString AEidoverseBridge::BuildCreate(const FString& Model,const FVector& PosWire,double Yaw,double Scale,const TSharedPtr<FJsonObject>& Components,bool bSelect,TFunction<void(const TSharedPtr<FJsonObject>&)> Done)
{
    // a request-local placeholder names it (ADAPTER-DESIGN §3.5) until OnWriteReply learns the minted id
    const FString Ref=FString::Printf(TEXT("$b%d"),++BuildRefSerial);
    auto F=MakeShared<FJsonObject>();F->SetStringField(TEXT("ref"),Ref);
    if(!Model.IsEmpty())F->SetStringField(TEXT("model"),Model);
    F->SetObjectField(TEXT("transform"),TransformFields(PosWire,Yaw,Scale));
    if(Components)F->SetObjectField(TEXT("components"),Components);
    const FString Req=BridgeDo(TEXT("create"),F,MoveTemp(Done));
    if(Req.IsEmpty())return FString();
    PendingCreates.Add(Req,Ref);if(bSelect)PendingSelect=Ref;
    return Ref;
}

FString AEidoverseBridge::BuildPlace(const FString& Id,const FVector& PosWire,double Yaw,double Scale)
{
    return BridgeDo(TEXT("setTransform"),PlaceFields(ResolveBuildId(Id),PosWire,Yaw,Scale));
}

FString AEidoverseBridge::BuildSet(const FString& Id,const FString& Component,const TSharedPtr<FJsonValue>& Value,bool bMerge,TFunction<void(const TSharedPtr<FJsonObject>&)> Done)
{
    // no value (or null) takes the component off
    auto F=MakeShared<FJsonObject>();F->SetStringField(TEXT("id"),ResolveBuildId(Id));F->SetStringField(TEXT("component"),Component);
    const bool bUnset=!Value.IsValid()||Value->IsNull();
    if(!bUnset){F->SetField(TEXT("value"),Value);if(bMerge)F->SetBoolField(TEXT("merge"),true);}
    return BridgeDo(bUnset?TEXT("unset"):TEXT("set"),F,MoveTemp(Done));
}

FString AEidoverseBridge::BuildRemove(const FString& Id)
{
    auto F=MakeShared<FJsonObject>();F->SetStringField(TEXT("id"),ResolveBuildId(Id));
    return BridgeDo(TEXT("remove"),F);
}

FString AEidoverseBridge::BridgeDoJson(const FString& Fn,const FString& FieldsJson)
{
    TSharedPtr<FJsonObject> F;
    if(!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(FieldsJson),F)||!F)return FString();
    FString Id;if(F->TryGetStringField(TEXT("id"),Id))F->SetStringField(TEXT("id"),ResolveBuildId(Id));   // an undo of a create names its $b<n>
    return BridgeDo(Fn,F.ToSharedRef());
}

bool AEidoverseBridge::InspectThroughBridge(const FString& Id)
{
    // the authority's inspect, through the bridge (ids and names in view spelling); the inspector reads its bag as `comp`
    auto F=MakeShared<FJsonObject>();F->SetStringField(TEXT("id"),ResolveBuildId(Id));
    TWeakObjectPtr<AEidoverseBridge> Weak(this);
    return !BridgeDo(TEXT("inspect"),F,[Weak,Id](const TSharedPtr<FJsonObject>& R)
    {
        AEidoverseBridge* B=Weak.Get();if(!B)return;
        auto Out=MakeShared<FJsonObject>();Out->SetStringField(TEXT("id"),Id);
        bool bOk=false;const TSharedPtr<FJsonObject>* Comp=nullptr;const TSharedPtr<FJsonObject>* Refused=nullptr;
        if(R&&R->TryGetBoolField(TEXT("ok"),bOk)&&bOk&&R->TryGetObjectField(TEXT("components"),Comp))
        {
            auto Bag=MakeShared<FJsonObject>(**Comp);Bag->RemoveField(TEXT("transform"));   // the pose has its own fields
            Out->SetObjectField(TEXT("comp"),Bag);
            for(const TCHAR* K:{TEXT("versions"),TEXT("touched"),TEXT("derived")}){const TSharedPtr<FJsonObject>* O=nullptr;if(R->TryGetObjectField(K,O))Out->SetObjectField(K,*O);}
        }
        else
        {
            const TSharedPtr<FJsonObject> Why=R&&R->TryGetObjectField(TEXT("refused"),Refused)?*Refused:R;
            Out->SetStringField(TEXT("error"),EidoWrites::Str(Why,TEXT("code"))==TEXT("not-found")?FString(TEXT("not_found")):EidoWrites::Str(Why,TEXT("reason")).Left(300));
        }
        B->InspectedId=Id;B->InspectReply=Out;++B->InspectSerial;
    }).IsEmpty();
}
