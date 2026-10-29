// Slice D's hook (Plugins/Eidoverse/Bridge/SLICES.md): any other bridge/* message (held things and seats arrive in bridge/scene; Bridge/src/bodies.ts).
#include "EidoverseBridge.h"
#include "Dom/JsonObject.h"

void AEidoverseBridge::OnBodiesMsg(const FString& Type,const TSharedPtr<FJsonObject>& J)
{
    // slice D: not built yet (Bridge/SLICES.md)
}

// red-first stubs (the test's surface; built in the next commit)
void AEidoverseBridge::ReceiveSceneSeats(const TSharedPtr<FJsonObject>& Scene) {}
const AEidoverseBridge::FEidoSeat* AEidoverseBridge::FindSceneSeat(const FString& Host,const FString& Name) const { return nullptr; }
FString AEidoverseBridge::BridgeRide(const FString& Type,const TSharedRef<FJsonObject>& Fields,TFunction<void(const TSharedPtr<FJsonObject>&)> Done,bool bQuiet) { return FString(); }
bool AEidoverseBridge::BodyRequest(const FString& Verb,const TSharedRef<FJsonObject>& Args,TFunction<void(const TSharedPtr<FJsonObject>&)> Done) { return false; }
bool AEidoverseBridge::GrabEntity(const FString& Id) { return false; }
bool AEidoverseBridge::LetGoOfEntity(bool bThrow,FVector V) { return false; }
TSharedPtr<FJsonObject> AEidoverseBridge::SeatWirePose() const { return nullptr; }
FString AEidoverseBridge::GetSeatOccupant(const FString& Host) const { return FString(); }
void AEidoverseBridge::TickBodyTrack(double Now) {}
FVector AEidoverseBridge::MyFeetWire() const { return FVector::ZeroVector; }
bool AEidoverseBridge::IsBodyTracked(float Within) const { return false; }
void AEidoverseBridge::SendUnride() {}
void AEidoverseBridge::ReceiveHolds(const TSharedPtr<FJsonObject>& J) {}
bool AEidoverseBridge::RideSeat(const FString& Host,const FString& Slot) { return false; }
void AEidoverseBridge::StandUpBridged(AEidoverseExplorer* Explorer,bool bSendDismount,bool bHave,const FTransform& Seat) {}
bool AEidoverseBridge::UseBridgedHelm(const FVector& Feet) { return false; }
void AEidoverseBridge::SendDrive(const FVector& PosWire,const FQuat& QWire) {}
