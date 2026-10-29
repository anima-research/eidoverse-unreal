// Slice A's hook (Plugins/Eidoverse/Bridge/SLICES.md): bridge/scene v2, the render model the bridge folds (Bridge/src/scene.ts).
#include "EidoverseBridge.h"
#include "EidoverseScene.h"
#include "Dom/JsonObject.h"
#include "GameFramework/Pawn.h"
#include "Kismet/GameplayStatics.h"

void AEidoverseBridge::OnSceneMsg(const FString& Type,const TSharedPtr<FJsonObject>& J)
{
    // held by me: my own hand draws it (TickSelfHeld); held by another: the bridge has put it at their hand (ADAPTER-DESIGN §3.6)
    const TArray<TSharedPtr<FJsonValue>>* Ts=nullptr;
    if(!J->HasField(TEXT("error"))&&J->TryGetArrayField(TEXT("transforms"),Ts))
    {
        TArray<TSharedPtr<FJsonValue>> Kept;TSet<FString> Mine;
        for(const auto& V:*Ts)
        {
            const TSharedPtr<FJsonObject>* T=nullptr;const TSharedPtr<FJsonObject>* H=nullptr;FString By,Id;
            if(V->TryGetObject(T)&&(*T)->TryGetObjectField(TEXT("held"),H)&&(*H)->TryGetStringField(TEXT("by"),By)&&!By.IsEmpty()&&By==AssignedIdentity&&(*T)->TryGetStringField(TEXT("id"),Id))Mine.Add(Id);
            else Kept.Add(V);
        }
        if(DynamicScene)for(const FString& Id:SelfHeld)if(!Mine.Contains(Id))DynamicScene->HoldTransform(Id,false);
        SelfHeld=MoveTemp(Mine);J->SetArrayField(TEXT("transforms"),Kept);
    }
    if(J->HasField(TEXT("seats")))OnBodiesMsg(Type,J);   // seats[] are slice D's
    ReceiveDynamicScene(J);
}
void AEidoverseBridge::TickSelfHeld()
{
    // display only: my feet as SendPose has them, plus HAND_Y (engine shared/protocol/physconst.js), as Bridge/src/bodies.ts places a held thing
    const APawn* Me=UGameplayStatics::GetPlayerPawn(this,0);if(!DynamicScene||!Me)return;
    const FVector Hand=Me->GetActorLocation()-FVector(0,0,85)+FromWirePosition(FVector(0,1.1,0));
    for(const FString& Id:SelfHeld)if(const AActor* A=DynamicScene->EntityActor(Id))
    {FTransform T=A->GetActorTransform();T.SetLocation(Hand);DynamicScene->HoldTransform(Id,true);DynamicScene->SetLocalTransform(Id,T);}
}
