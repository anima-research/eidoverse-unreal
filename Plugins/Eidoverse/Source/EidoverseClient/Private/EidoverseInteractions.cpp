#include "EidoverseBridge.h"
#include "EidoverseUI.h"
#include "EidoverseScene.h"
#include "EidoverseAvatar.h"
#include "Dom/JsonObject.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "Kismet/GameplayStatics.h"

namespace InteractionWire
{
FString Str(const TSharedPtr<FJsonObject>& O,const TCHAR* K){FString S;if(O)O->TryGetStringField(K,S);return S;}
TSharedPtr<FJsonObject> Obj(const TSharedPtr<FJsonObject>& O,const TCHAR* K){const TSharedPtr<FJsonObject>* V=nullptr;return O&&O->TryGetObjectField(K,V)?*V:nullptr;}
double Num(const TSharedPtr<FJsonObject>& O,const TCHAR* K,double D=0){double N;return O&&O->TryGetNumberField(K,N)&&FMath::IsFinite(N)?N:D;}
bool Vec(const TSharedPtr<FJsonObject>& O,const TCHAR* K,FVector& V)
{const TArray<TSharedPtr<FJsonValue>>* A=nullptr;if(!O||!O->TryGetArrayField(K,A)||A->Num()!=3)return false;double N[3];for(int I=0;I<3;++I)if(!(*A)[I]->TryGetNumber(N[I])||!FMath::IsFinite(N[I])||FMath::Abs(N[I])>1e6)return false;V=FVector(N[0],N[1],N[2]);return true;}
TArray<TSharedPtr<FJsonValue>> Array(std::initializer_list<double> V){TArray<TSharedPtr<FJsonValue>> A;for(double N:V)A.Add(MakeShared<FJsonValueNumber>(N));return A;}
bool Flag(const TSharedPtr<FJsonObject>& O,const TCHAR* K){bool V=false;if(O)O->TryGetBoolField(K,V);return V;}
}
using namespace InteractionWire;

AActor* AEidoverseScene::EntityActor(const FString& Id) const{const auto* E=Items.Find(Id);return E?E->Actor.Get():nullptr;}
TSharedPtr<FJsonObject> AEidoverseScene::Interaction(const FString& Id) const{const auto* E=Items.Find(Id);return E?E->Interact:nullptr;}
FString AEidoverseScene::EntityId(AActor* A) const{for(auto& E:Items)if(E.Value.Actor.Get()==A)return E.Key;return {};}
void AEidoverseScene::HoldTransform(const FString& Id,bool Hold){if(Hold)HeldTransforms.Add(Id);else {HeldTransforms.Remove(Id);RemoteLeases.Remove(Id);}}
void AEidoverseScene::SetLocalTransform(const FString& Id,const FTransform& T){if(auto* E=Items.Find(Id)){E->Target=T;if(E->Actor.IsValid())E->Actor->SetActorTransform(T);}}
void AEidoverseScene::HoldRemote(const FString& Id,const FTransform& T,double Seconds){if(auto* E=Items.Find(Id)){E->Target=T;RemoteLeases.Add(Id,FPlatformTime::Seconds()+Seconds);}}
void AEidoverseScene::LeaseTransform(const FString& Id,const TSharedPtr<FJsonObject>& State,const FVector& Origin)
{
    auto* E=Items.Find(Id);FVector P;if(!E||HeldTransforms.Contains(Id)||!Vec(State,TEXT("p"),P))return;
    FQuat Q=AEidoverseBridge::FromWireRotation(FQuat(FVector::YAxisVector,Num(State,TEXT("yaw"))));
    const TArray<TSharedPtr<FJsonValue>>* A=nullptr;if(State->TryGetArrayField(TEXT("q"),A))
    {if(A->Num()!=4)return;FQuat W((*A)[0]->AsNumber(),(*A)[1]->AsNumber(),(*A)[2]->AsNumber(),(*A)[3]->AsNumber());if(W.ContainsNaN()||W.SizeSquared()<.001)return;Q=AEidoverseBridge::FromWireRotation(W.GetNormalized());}
    E->Target.SetLocation(Origin+AEidoverseBridge::FromWirePosition(P));E->Target.SetRotation(Q);RemoteLeases.Add(Id,FPlatformTime::Seconds()+.5);
}
bool AEidoverseBridge::SendInteractionVerb(const FString& Verb,const TSharedRef<FJsonObject>& Args)
{
    if(!bJoined||bSpectator||FPlatformTime::Seconds()-LastInteraction<.25){InteractionStatus=TEXT("Join as a participant, and wait briefly between actions.");return false;}
    LastInteraction=FPlatformTime::Seconds();auto J=MakeShared<FJsonObject>();J->SetStringField(TEXT("type"),TEXT("verb"));J->SetStringField(TEXT("verb"),Verb);J->SetObjectField(TEXT("args"),Args);Send(J);InteractionStatus=TEXT("Action requested; awaiting server.");return true;
}
bool AEidoverseBridge::UseEntity(const FString& Id,const FString& Action)
{
    if(Id.IsEmpty()||Id.Len()>128||Action.IsEmpty()||Action.Len()>64)return false;
    auto A=MakeShared<FJsonObject>();A->SetStringField(TEXT("id"),Id);A->SetStringField(TEXT("action"),Action);return SendInteractionVerb(TEXT("use"),A);
}
bool AEidoverseBridge::PuntEntity(const FString& Id,float Power)
{
    if(!DynamicScene||!DynamicScene->EntityActor(Id)||!FMath::IsFinite(Power))return false;
    auto* Pawn=UGameplayStatics::GetPlayerPawn(this,0);if(!Pawn)return false;
    FVector Center,Extent;DynamicScene->EntityActor(Id)->GetActorBounds(false,Center,Extent);
    FVector Direction=ToWirePosition(Center-Pawn->GetActorLocation());Direction.Y=0;Direction=Direction.GetSafeNormal();
    if(Direction.IsNearlyZero()){Direction=ToWirePosition(Pawn->GetActorForwardVector());Direction.Y=0;Direction=Direction.GetSafeNormal();}
    if(Direction.IsNearlyZero())Direction=FVector(0,0,1);
    auto A=MakeShared<FJsonObject>();A->SetStringField(TEXT("id"),Id);A->SetNumberField(TEXT("power"),FMath::Clamp(Power,.5f,10.f));
    A->SetArrayField(TEXT("dir"),Array({Direction.X,bDeterministicScenePhysics?.9:0.,Direction.Z}));return SendInteractionVerb(TEXT("punt"),A);
}
void AEidoverseBridge::SendLease(const FString& Op,const FString& Id,bool Pose)
{
    auto J=MakeShared<FJsonObject>();J->SetStringField(TEXT("type"),TEXT("lease"));J->SetStringField(TEXT("op"),Op);J->SetStringField(TEXT("id"),Id);
    if(Pose&&DynamicScene)if(auto* A=DynamicScene->EntityActor(Id))
    {const FVector P=ToWirePosition(A->GetActorLocation()-OriginCentimeters);const FQuat Q=ToWireRotation(A->GetActorQuat());const FVector F=Q.RotateVector(FVector::ZAxisVector);J->SetArrayField(TEXT("p"),Array({P.X,P.Y,P.Z}));J->SetArrayField(TEXT("q"),Array({Q.X,Q.Y,Q.Z,Q.W}));J->SetNumberField(TEXT("yaw"),FMath::Atan2(F.X,F.Z));}
    Send(J);
}
void AEidoverseBridge::ClearInteractions()
{
    for(auto& S:ObjectSims){if(bJoined)SendLease(TEXT("release"),S.Key,true);if(DynamicScene)DynamicScene->HoldTransform(S.Key,false);}
    ObjectSims.Empty();PendingPunts.Empty();PendingPuntExpiry.Empty();LocalReach.Reset();PendingEmote.Empty();ActivePhysicsObjects=0;
}
void AEidoverseBridge::ReceiveInteraction(const TSharedPtr<FJsonObject>& Entry)
{
    const FString Verb=Str(Entry,TEXT("verb"));auto Args=Obj(Entry,TEXT("args"));if(!Args)return;
    if(Verb==TEXT("epoch")){ClearInteractions();bDeterministicScenePhysics=!Str(Args,TEXT("sim")).IsEmpty();bSceneDirty=true;return;}
    const FString Id=Str(Args,TEXT("id"));
    if(Str(Entry,TEXT("actor"))==AssignedIdentity&&(Verb==TEXT("use")||Verb==TEXT("punt")))InteractionStatus=TEXT("Server confirmed ")+Verb+TEXT(" ")+Id;
    // Epoch worlds run the shared versioned simulation on the server. A second
    // local lease simulation would disagree with it and overwrite the landing.
    if(Verb!=TEXT("punt")||!DynamicScene||bDeterministicScenePhysics||!bSimulateObjectPhysics||bSpectator)return;
    auto Info=DynamicScene->Interaction(Id);auto* A=DynamicScene->EntityActor(Id);
    if(!A||!Info||Flag(Info,TEXT("locked"))||Flag(Info,TEXT("mounted"))||Flag(Info,TEXT("moving"))||ObjectSims.Num()+PendingPunts.Num()>=8)return;
    FVector Direction;
    if(!Vec(Args,TEXT("dir"),Direction))
    {AActor* From=nullptr;if(Str(Entry,TEXT("actor"))==AssignedIdentity)From=UGameplayStatics::GetPlayerPawn(this,0);else if(auto* P=Peers.Find(Str(Entry,TEXT("actor"))))From=P->Actor.Get();Direction=From?ToWirePosition(A->GetActorLocation()-From->GetActorLocation()):FVector(0,0,1);}
    Direction.Y=0;Direction=Direction.GetSafeNormal();const double Power=FMath::Clamp(Num(Args,TEXT("power"),5),.5,10.);
    FVector Launch=FromWirePosition(Direction*Power+FVector(0,Power*.45,0));
    if(auto* Sim=ObjectSims.Find(Id)){Sim->Velocity+=Launch;return;}
    if(PendingPunts.Contains(Id))return;PendingPunts.Add(Id,Launch);PendingPuntExpiry.Add(Id,FPlatformTime::Seconds()+4);
    auto J=MakeShared<FJsonObject>();J->SetStringField(TEXT("type"),TEXT("lease"));J->SetStringField(TEXT("op"),TEXT("claim"));J->SetStringField(TEXT("id"),Id);J->SetBoolField(TEXT("take"),Str(Entry,TEXT("actor"))==AssignedIdentity);Send(J);
}
void AEidoverseBridge::ReceiveLease(const TSharedPtr<FJsonObject>& J)
{
    if(!DynamicScene)return;const FString Op=Str(J,TEXT("op")),Id=Str(J,TEXT("id"));
    if(ReceiveHelmLease(Op,Id,J))return;
    if(Op!=TEXT("state")&&Op!=TEXT("claimed"))PendingPuntExpiry.Remove(Id);
    if(Op==TEXT("state")){DynamicScene->LeaseTransform(Id,J,OriginCentimeters);return;}
    if(Op==TEXT("granted"))
    {
        auto* Launch=PendingPunts.Find(Id);auto* A=DynamicScene->EntityActor(Id);
        if(!Launch||!A||bDeterministicScenePhysics||!bSimulateObjectPhysics){PendingPunts.Remove(Id);SendLease(TEXT("release"),Id);return;}
        FEidoverseObjectSim S;S.Velocity=*Launch;S.RestRotation=A->GetActorQuat();PendingPunts.Remove(Id);
        if(auto From=Obj(J,TEXT("from"))) {DynamicScene->LeaseTransform(Id,From,OriginCentimeters);FVector P;if(Vec(From,TEXT("p"),P))A->SetActorLocation(OriginCentimeters+FromWirePosition(P));}
        FVector Center;A->GetActorBounds(true,Center,S.Extent);S.CenterOffset=Center-A->GetActorLocation();
        if(S.Extent.GetMin()<1||S.Extent.GetMax()>500){SendLease(TEXT("release"),Id);return;}
        S.bBall=S.Extent.GetMax()<45&&S.Extent.GetMax()/S.Extent.GetMin()<1.25;
        ObjectSims.Add(Id,S);DynamicScene->HoldTransform(Id,true);InteractionStatus=TEXT("Simulating ")+Id;return;
    }
    if(Op==TEXT("lost")||Op==TEXT("denied")||Op==TEXT("released"))
    {PendingPunts.Remove(Id);ObjectSims.Remove(Id);DynamicScene->HoldTransform(Id,false);bSceneDirty=true;if(Op==TEXT("denied"))InteractionStatus=TEXT("Physics lease held elsewhere or refused.");}
}
void AEidoverseBridge::TickInteractions(float Dt,double Now)
{
    if(!DynamicScene)return;
    for(auto It=PendingPuntExpiry.CreateIterator();It;++It)if(Now>It.Value()){PendingPunts.Remove(It.Key());It.RemoveCurrent();}
    for(auto It=ObjectSims.CreateIterator();It;++It)
    {
        auto& S=It.Value();auto* A=DynamicScene->EntityActor(It.Key());
        if(!A||!bSimulateObjectPhysics){SendLease(TEXT("release"),It.Key(),!!A);DynamicScene->HoldTransform(It.Key(),false);It.RemoveCurrent();continue;}
        const int Steps=FMath::Clamp(FMath::CeilToInt(Dt*120),1,8);const float Step=FMath::Min(Dt,.066f)/Steps;bool Ground=false;
        for(int N=0;N<Steps;++N)
        {
            S.Velocity.Z-=980*Step;FVector Start=A->GetActorLocation()+S.CenterOffset,End=Start+S.Velocity*Step;
            FHitResult Hit;FCollisionQueryParams Q;Q.bTraceComplex=true;Q.AddIgnoredActor(A);if(auto* P=UGameplayStatics::GetPlayerPawn(this,0))Q.AddIgnoredActor(P);
            const auto Shape=S.bBall?FCollisionShape::MakeSphere(S.Extent.GetMin()):FCollisionShape::MakeBox(S.Extent*.97);
            if(GetWorld()->SweepSingleByChannel(Hit,Start,End,FQuat::Identity,ECC_WorldStatic,Shape,Q))
            {
                End=Hit.Location+Hit.Normal*.3;const double V=FVector::DotProduct(S.Velocity,Hit.Normal);
                if(V<0)S.Velocity-=Hit.Normal*(1+(S.bBall?.45:.08))*V;
                Ground=Hit.Normal.Z>.5;
                if(Ground){if(FMath::Abs(S.Velocity.Z)<60)S.Velocity.Z=0;const float Friction=FMath::Max(0.f,1-(S.bBall?.55f:3.5f)*Step);S.Velocity.X*=Friction;S.Velocity.Y*=Friction;}
            }
            FTransform T=A->GetActorTransform();T.SetLocation(End-S.CenterOffset);
            if(S.bBall&&S.Velocity.Size2D()>1){FVector Axis=FVector::CrossProduct(FVector::UpVector,S.Velocity.GetSafeNormal2D());T.SetRotation((FQuat(Axis,S.Velocity.Size2D()*Step/FMath::Max(5.,S.Extent.X))*T.GetRotation()).GetNormalized());}
            DynamicScene->SetLocalTransform(It.Key(),T);
        }
        S.GroundGrace=Ground?.1f:FMath::Max(0.f,S.GroundGrace-Dt);
        S.Age+=Dt;S.Settling=S.GroundGrace>0&&S.Velocity.Size()<25?S.Settling+Dt:0;
        if(Now-S.SentAt>=1./15){S.SentAt=Now;SendLease(TEXT("state"),It.Key(),true);}
        if(S.Settling>.45||S.Age>20||A->GetActorLocation().Z<OriginCentimeters.Z-100000)
        {A->SetActorRotation(S.RestRotation);DynamicScene->SetLocalTransform(It.Key(),A->GetActorTransform());SendLease(TEXT("release"),It.Key(),true);DynamicScene->HoldTransform(It.Key(),false);It.RemoveCurrent();bSceneDirty=true;}
    }
    ActivePhysicsObjects=ObjectSims.Num();
    auto* PC=UGameplayStatics::GetPlayerController(this,0);if(!PC||PC->IsMoveInputIgnored()||bSpectator)return;
    // Editing keys win while something is selected in build mode (web build.js priority).
    if(HUD&&HUD->IsFrameOpen(TEXT("build"))&&(!HUD->GetSelectedEntity().IsEmpty()||HUD->HasGhost()))return;
    if(PC->WasInputKeyJustPressed(EKeys::E)&&HandleUseKey())return;
    if(PC->WasInputKeyJustPressed(EKeys::E)||PC->WasInputKeyJustPressed(EKeys::F))
    {
        FVector Eye;FRotator R;PC->GetPlayerViewPoint(Eye,R);FHitResult Hit;FCollisionQueryParams Q;Q.bTraceComplex=true;if(PC->GetPawn())Q.AddIgnoredActor(PC->GetPawn());
        // Reach is 3 m from the head; in third person the camera sits behind it, so the
        // aim ray runs through the view but the distance is measured from the body.
        const FVector Head=PC->GetPawn()?PC->GetPawn()->GetActorLocation()+FVector(0,0,57):Eye;
        if(GetWorld()->LineTraceSingleByChannel(Hit,Eye,Eye+R.Vector()*(300+FVector::Dist(Eye,Head)),ECC_Visibility,Q)&&FVector::Dist(Hit.ImpactPoint,Head)<=300)
        {FString Id=DynamicScene->EntityId(Hit.GetActor());if(!Id.IsEmpty()){if(PC->WasInputKeyJustPressed(EKeys::F))PuntEntity(Id);else {FString Action=TEXT("use");const TArray<TSharedPtr<FJsonValue>>* Actions=nullptr;auto Info=DynamicScene->Interaction(Id);if(Info&&Info->TryGetArrayField(TEXT("actions"),Actions)&&Actions->Num())Action=(*Actions)[0]->AsString();UseEntity(Id,Action);}}}
    }
}
