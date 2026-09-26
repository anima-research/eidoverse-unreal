#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MarineEcosystem.generated.h"

class UInstancedStaticMeshComponent;
class UStaticMesh;

// Small, independently steering schools. All visual fish are instanced, not actors.
UCLASS(BlueprintType)
class UNDERWATEROPTICS_API AMarineEcosystem : public AActor
{
    GENERATED_BODY()
public:
    AMarineEcosystem();
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaSeconds) override;
    UPROPERTY(EditAnywhere, Category="Marine life") TArray<TObjectPtr<UStaticMesh>> SpeciesMeshes;
    UPROPERTY(EditAnywhere, Category="Marine life") int32 Seed = 9127;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Marine life") int32 FishCount = 0;
private:
    struct FSwimmer { FVector P, V, Avoid; float Size, Phase; int32 School, Species, Instance; };
    struct FSchool { FVector Center; float Radius, Speed, Clearance; int32 Species; };
    TArray<FSwimmer> Fish;
    TArray<FSchool> Schools;
    UPROPERTY(Transient) TArray<TObjectPtr<UInstancedStaticMeshComponent>> Groups;
    TArray<float> Bed;
    float Accumulator = 0;
    int32 ProbeCursor = 0;
    float Ground(float X, float Y) const;
};
