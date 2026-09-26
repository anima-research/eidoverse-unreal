#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "SurfaceBoardingPlatform.generated.h"
class APawn;
UCLASS(BlueprintType)
class UNDERWATEROPTICS_API ASurfaceBoardingPlatform : public AActor
{
    GENERATED_BODY()
public:
    ASurfaceBoardingPlatform();
    virtual void Tick(float DeltaSeconds) override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Boarding") float DeckHeight=240;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Boarding") int32 BoardingState=0;
    UFUNCTION(BlueprintCallable, Category="Boarding") bool TryUseLadder(APawn* Pawn);
private:
    TWeakObjectPtr<APawn> Boarder;
    FVector ClimbStart;
    float Progress=0, FallSpeed=0, OldSpeed=0;
    void Release();
};
