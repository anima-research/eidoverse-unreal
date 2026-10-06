// Locomotion beyond walking and swimming: authored climb routes (traversal.js) and
// vessel helms (vehicles.js). E picks, in the browser's order: a climb route, a helm,
// then using the aimed object.
#include "EidoverseBridge.h"
#include "EidoverseUI.h"
#include "EidoverseScene.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"

namespace
{
    bool LVec(const TSharedPtr<FJsonValue>& V, FVector& Out)
    {
        const TArray<TSharedPtr<FJsonValue>>* A = nullptr;
        if (!V || !V->TryGetArray(A) || A->Num() != 3) return false;
        for (int32 I = 0; I < 3; ++I) { double N; if (!(*A)[I]->TryGetNumber(N) || !FMath::IsFinite(N)) return false; Out[I] = N; }
        return true;
    }
    bool LVecField(const TSharedPtr<FJsonObject>& O, const TCHAR* K, FVector& Out) { return O && LVec(O->TryGetField(K), Out); }
    double LNum(const TSharedPtr<FJsonObject>& O, const TCHAR* K, double D) { double N; return O && O->TryGetNumberField(K, N) && FMath::IsFinite(N) ? N : D; }
}

bool AEidoverseBridge::HandleUseKey()
{
    auto* Explorer = Cast<AEidoverseExplorer>(UGameplayStatics::GetPlayerPawn(this, 0));
    if (!Explorer || !DynamicScene || bSpectator) return false;
    // Climbing: E lets go (you drop under gravity).
    if (Climb.IsSet()) { Climb.Reset(); return true; }
    if (Helm.bActive) { EndHelm(true); return true; }
    if (Explorer->bSeated) return false;
    const FVector Feet = Explorer->GetActorLocation() - FVector(0, 0, Explorer->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
    // A climb route end within its radius (traversal.js nearbyTraversal).
    {
        const FEidoverseSceneItem* Best = nullptr; FString BestId; int32 BestRoute = -1; bool bBestRev = false; double BestD = 1e9;
        for (const auto& Pair : DynamicScene->GetItems())
        {
            const auto& E = Pair.Value; const AActor* A = E.Actor.Get();
            const TArray<TSharedPtr<FJsonValue>>* Routes = nullptr;
            if (!A || !E.Traversal || !E.Traversal->TryGetArrayField(TEXT("routes"), Routes)) continue;
            for (int32 R = 0; R < Routes->Num() && R < 32; ++R)
            {
                const TSharedPtr<FJsonObject> Route = (*Routes)[R]->AsObject(); const TArray<TSharedPtr<FJsonValue>>* Path = nullptr;
                if (!Route || !Route->TryGetArrayField(TEXT("path"), Path) || Path->Num() < 2 || Path->Num() > 16) continue;
                const double Radius = FMath::Clamp(LNum(Route, TEXT("radius"), 1.4), .3, 3.) * 100;
                for (const bool bRev : {false, true})
                {
                    FVector End; if (!LVec((*Path)[bRev ? Path->Num() - 1 : 0], End)) continue;
                    const double D = FVector::Dist(A->GetActorTransform().TransformPosition(FromWirePosition(End)), Feet);
                    if (D <= Radius && D < BestD) { BestD = D; Best = &E; BestId = Pair.Key; BestRoute = R; bBestRev = bRev; }
                }
            }
        }
        if (Best)
        {
            const TSharedPtr<FJsonObject> Route = Best->Traversal->GetArrayField(TEXT("routes"))[BestRoute]->AsObject();
            FClimb C; C.Id = BestId; C.bReverse = bBestRev; C.Speed = float(FMath::Clamp(LNum(Route, TEXT("speed"), 1.3), .3, 4.));
            for (const auto& P : Route->GetArrayField(TEXT("path"))) { FVector V; if (LVec(P, V)) C.PathLocal.Add(V); }
            if (bBestRev) Algo::Reverse(C.PathLocal);
            // Ease from the actual hand-off point; the rest stays in the carrier's frame.
            C.PathLocal.Insert(ToWirePosition(Best->Actor->GetActorTransform().InverseTransformPosition(Feet)) , 0);
            Climb = C;
            return true;
        }
    }
    // A helm within 2.5 m (vehicles.js): claim the lease; piloting starts when granted.
    for (const auto& Pair : DynamicScene->GetItems())
    {
        const auto& E = Pair.Value; FVector HelmL;
        if (!E.Actor.IsValid() || !LVecField(E.Vehicle, TEXT("helm"), HelmL)) continue;
        if (FVector::Dist(E.Actor->GetActorTransform().TransformPosition(FromWirePosition(HelmL)), Feet) > 250) continue;
        auto J = MakeShared<FJsonObject>(); J->SetStringField(TEXT("op"), TEXT("claim")); J->SetStringField(TEXT("id"), Pair.Key);
        if (SendRaw(TEXT("lease"), J)) { Helm = FHelm(); Helm.Id = Pair.Key; Helm.bPending = true; }
        return true;
    }
    return false;
}

bool AEidoverseBridge::ReceiveHelmLease(const FString& Op, const FString& Id, const TSharedPtr<FJsonObject>& J)
{
    if (Helm.Id.IsEmpty() || Id != Helm.Id) return false;
    if (Op == TEXT("granted") && Helm.bPending)
    {
        Helm.bPending = false; Helm.bActive = true; Helm.Vel = FVector::ZeroVector;
        if (AActor* A = DynamicScene ? DynamicScene->EntityActor(Id) : nullptr) Helm.YawWire = -FMath::DegreesToRadians(A->GetActorRotation().Yaw);
        if (DynamicScene) DynamicScene->HoldTransform(Id, true);
        if (HUD) HUD->Hint(TEXT("piloting — W/S thrust, A/D steer, Space rise, C dive, E exit"), 5);
        return true;
    }
    if (Op == TEXT("denied")) { FString Why; J->TryGetStringField(TEXT("why"), Why); if (HUD) HUD->Hint(Why.IsEmpty() ? TEXT("helm occupied") : Why, 3); Helm = FHelm(); return true; }
    if (Op == TEXT("lost") || Op == TEXT("released")) { EndHelm(false); return true; }
    return false;
}

void AEidoverseBridge::EndHelm(bool bRelease)
{
    auto* Explorer = Cast<AEidoverseExplorer>(UGameplayStatics::GetPlayerPawn(this, 0));
    AActor* A = DynamicScene && !Helm.Id.IsEmpty() ? DynamicScene->EntityActor(Helm.Id) : nullptr;
    if (bRelease && A)
    {
        // Back to yaw-only; step off at the exit point when the vessel names one.
        A->SetActorRotation(FRotator(0, A->GetActorRotation().Yaw, 0));
        const FVector P = ToWirePosition(A->GetActorLocation() - OriginCentimeters);
        auto J = MakeShared<FJsonObject>(); J->SetStringField(TEXT("op"), TEXT("release")); J->SetStringField(TEXT("id"), Helm.Id);
        J->SetArrayField(TEXT("p"), {MakeShared<FJsonValueNumber>(P.X), MakeShared<FJsonValueNumber>(P.Y), MakeShared<FJsonValueNumber>(P.Z)});
        J->SetNumberField(TEXT("yaw"), Helm.YawWire);
        SendRaw(TEXT("lease"), J);
        if (Explorer)
        {
            const FEidoverseSceneItem* E = DynamicScene->GetItems().Find(Helm.Id); FVector Exit;
            const double Half = Explorer->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
            if (E && LVecField(E->Vehicle, TEXT("exit"), Exit)) Explorer->SetActorLocation(A->GetActorTransform().TransformPosition(FromWirePosition(Exit)) + FVector(0, 0, Half + 2), false, nullptr, ETeleportType::TeleportPhysics);
        }
    }
    if (DynamicScene && !Helm.Id.IsEmpty()) { if (A) DynamicScene->SetLocalTransform(Helm.Id, A->GetActorTransform()); DynamicScene->HoldTransform(Helm.Id, false); }
    Helm = FHelm();
    if (Explorer && Explorer->bSeated && SeatTo.IsEmpty() && !bChairSeat) Explorer->SetSeated(false);
    LocoMode.Empty(); LocoClip.Empty();
}

void AEidoverseBridge::TickLocomotion(AEidoverseExplorer* Explorer, float Dt)
{
    LocoMode.Empty(); LocoClip.Empty(); LocoSpeed = 0;
    if (!DynamicScene) return;
    APlayerController* PC = Cast<APlayerController>(Explorer->GetController());
    const double Half = Explorer->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
    if (Climb.IsSet())
    {
        FClimb& C = Climb.GetValue();
        const FEidoverseSceneItem* E = DynamicScene->GetItems().Find(C.Id);
        if (!E || !E->Actor.IsValid() || !E->Traversal) { Climb.Reset(); Explorer->SetSeated(false); return; }
        const FTransform T = E->Actor->GetActorTransform();
        // stepTraversal: travel along the segments through the carrier's CURRENT transform.
        double Travel = FMath::Clamp(double(Dt), 0., .1) * C.Speed * 100;
        FVector Pos = T.TransformPosition(FromWirePosition(C.PathLocal[FMath::Min(C.Segment, C.PathLocal.Num() - 1)]));
        while (C.Segment < C.PathLocal.Num() - 1)
        {
            const FVector From = T.TransformPosition(FromWirePosition(C.PathLocal[C.Segment])), To = T.TransformPosition(FromWirePosition(C.PathLocal[C.Segment + 1]));
            const double Len = FVector::Dist(From, To), Remaining = FMath::Max(0., Len - C.Distance);
            if (Travel >= Remaining) { Travel -= Remaining; ++C.Segment; C.Distance = 0; Pos = To; continue; }
            C.Distance += Travel; Pos = FMath::Lerp(From, To, Len > 0 ? FMath::Min(1., C.Distance / Len) : 1.); break;
        }
        const bool bDone = C.Segment >= C.PathLocal.Num() - 1;
        FVector Facing = FromWirePosition(C.PathLocal.Last() - C.PathLocal[1]); if (C.bReverse) Facing = -Facing;   // face the rungs both ways
        Facing = T.TransformVectorNoScale(Facing).GetSafeNormal2D();
        Explorer->SetSeated(true);
        Explorer->SetActorLocationAndRotation(Pos + FVector(0, 0, Half), Facing.IsNearlyZero() ? Explorer->GetActorQuat() : Facing.Rotation().Quaternion(), false, nullptr, ETeleportType::TeleportPhysics);
        LocoMode = TEXT("climb"); LocoClip = TEXT("climb"); LocoSpeed = C.Speed;
        Explorer->Posture.Empty();
        if (bDone) { Climb.Reset(); Explorer->SetSeated(false); }
        return;
    }
    if (Helm.bActive)
    {
        AActor* A = DynamicScene->EntityActor(Helm.Id);
        const FEidoverseSceneItem* E = DynamicScene->GetItems().Find(Helm.Id);
        if (!A || !E || !E->Vehicle || !PC) { EndHelm(false); return; }
        const float Step = FMath::Min(Dt, .1f);
        auto Key = [PC](const FKey& K) { return PC->IsInputKeyDown(K) ? 1.f : 0.f; };
        const double Speed = FMath::Clamp(LNum(E->Vehicle, TEXT("speed"), 4), .1, 15.), Turn = FMath::Clamp(LNum(E->Vehicle, TEXT("turnRate"), .45), .1, 2.);
        Helm.YawWire += (Key(EKeys::A) - Key(EKeys::D)) * Turn * Step;
        FVector Desired(0, (Key(EKeys::SpaceBar) - FMath::Max(Key(EKeys::C), Key(EKeys::LeftControl))) * .5, Key(EKeys::W) - Key(EKeys::S));
        if (!Desired.IsNearlyZero()) Desired = Desired.GetSafeNormal() * Speed;
        Desired = FVector(Desired.X * FMath::Cos(Helm.YawWire) + Desired.Z * FMath::Sin(Helm.YawWire), Desired.Y, -Desired.X * FMath::Sin(Helm.YawWire) + Desired.Z * FMath::Cos(Helm.YawWire));
        Helm.Vel = FMath::Lerp(Helm.Vel, Desired, 1 - FMath::Exp(-1.5 * Step));
        const FVector OldWire = ToWirePosition(A->GetActorLocation() - OriginCentimeters);
        FVector NewWire = OldWire + Helm.Vel * Step;
        { double MinY, MaxY; if (E->Vehicle->TryGetNumberField(TEXT("minY"), MinY)) NewWire.Y = FMath::Max(NewWire.Y, MinY); if (E->Vehicle->TryGetNumberField(TEXT("maxY"), MaxY)) NewWire.Y = FMath::Min(NewWire.Y, MaxY); }
        // Probes: rays along the motion (distance + 0.3 m); a hit stops the vessel.
        TArray<FVector> Probes;
        const TArray<TSharedPtr<FJsonValue>>* ProbeArr = nullptr;
        if (E->Vehicle->TryGetArrayField(TEXT("probes"), ProbeArr)) for (const auto& V : *ProbeArr) { FVector P; if (LVec(V, P)) Probes.Add(P); }
        if (Probes.IsEmpty()) Probes.Add(FVector::ZeroVector);
        const FVector MoveUE = FromWirePosition(NewWire - OldWire);
        if (!MoveUE.IsNearlyZero())
        {
            FCollisionQueryParams Q(SCENE_QUERY_STAT(EidoverseHelm), true, A); Q.AddIgnoredActor(Explorer);
            const FQuat YawQ = FromWireRotation(FQuat(FVector::YAxisVector, Helm.YawWire));
            for (const FVector& P : Probes)
            {
                const FVector From = A->GetActorLocation() + YawQ.RotateVector(FromWirePosition(P));
                FHitResult Hit;
                if (GetWorld()->LineTraceSingleByChannel(Hit, From, From + MoveUE.GetSafeNormal() * (MoveUE.Size() + 30), ECC_Visibility, Q)) { NewWire = OldWire; Helm.Vel = FVector::ZeroVector; break; }
            }
        }
        const FQuat VesselQ = FromWireRotation(FQuat(FVector::YAxisVector, Helm.YawWire));
        A->SetActorLocationAndRotation(OriginCentimeters + FromWirePosition(NewWire), VesselQ);
        DynamicScene->SetLocalTransform(Helm.Id, A->GetActorTransform());
        // Me at the helm, facing with the vessel, sitting.
        FVector HelmL; LVecField(E->Vehicle, TEXT("helm"), HelmL);
        Explorer->SetSeated(true);
        Explorer->SetActorLocationAndRotation(A->GetActorTransform().TransformPosition(FromWirePosition(HelmL)) + FVector(0, 0, Half), FRotator(0, A->GetActorRotation().Yaw, 0), false, nullptr, ETeleportType::TeleportPhysics);
        LocoMode = TEXT("pilot"); LocoClip = TEXT("sitchair"); LocoSpeed = float(Helm.Vel.Size());
        const double Now = FPlatformTime::Seconds();
        if (Now - Helm.SentAt >= .066)
        {
            Helm.SentAt = Now;
            const FQuat W = ToWireRotation(VesselQ);
            auto J = MakeShared<FJsonObject>(); J->SetStringField(TEXT("op"), TEXT("state")); J->SetStringField(TEXT("id"), Helm.Id);
            J->SetArrayField(TEXT("p"), {MakeShared<FJsonValueNumber>(NewWire.X), MakeShared<FJsonValueNumber>(NewWire.Y), MakeShared<FJsonValueNumber>(NewWire.Z)});
            J->SetNumberField(TEXT("yaw"), Helm.YawWire);
            J->SetArrayField(TEXT("q"), {MakeShared<FJsonValueNumber>(W.X), MakeShared<FJsonValueNumber>(W.Y), MakeShared<FJsonValueNumber>(W.Z), MakeShared<FJsonValueNumber>(W.W)});
            SendRaw(TEXT("lease"), J);
        }
        return;
    }
    // The standing offer: a climb route end in reach says so once (E — climb up <name>).
    if (Explorer->bSeated) return;
    const FVector Feet = Explorer->GetActorLocation() - FVector(0, 0, Half);
    FString Key, Line;
    for (const auto& Pair : DynamicScene->GetItems())
    {
        const auto& E = Pair.Value; const TArray<TSharedPtr<FJsonValue>>* Routes = nullptr;
        if (!E.Actor.IsValid() || !E.Traversal || !E.Traversal->TryGetArrayField(TEXT("routes"), Routes)) continue;
        for (int32 R = 0; R < Routes->Num() && R < 32 && Key.IsEmpty(); ++R)
        {
            const TSharedPtr<FJsonObject> Route = (*Routes)[R]->AsObject(); const TArray<TSharedPtr<FJsonValue>>* Path = nullptr;
            if (!Route || !Route->TryGetArrayField(TEXT("path"), Path) || Path->Num() < 2) continue;
            for (const bool bRev : {false, true})
            {
                FVector End; if (!LVec((*Path)[bRev ? Path->Num() - 1 : 0], End)) continue;
                if (FVector::Dist(E.Actor->GetActorTransform().TransformPosition(FromWirePosition(End)), Feet) > FMath::Clamp(LNum(Route, TEXT("radius"), 1.4), .3, 3.) * 100) continue;
                FString Name = TEXT("ladder"); Route->TryGetStringField(TEXT("name"), Name);
                Key = FString::Printf(TEXT("%s/%d/%d"), *Pair.Key, R, bRev); Line = FString::Printf(TEXT("E — %s %s"), bRev ? TEXT("climb down") : TEXT("climb up"), *Name);
                break;
            }
        }
        if (!Key.IsEmpty()) break;
    }
    if (Key != ClimbHintKey && !Key.IsEmpty() && HUD) HUD->Hint(Line, 3);
    ClimbHintKey = Key;
}
