#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "EidoverseWarmup.generated.h"

// Front-door shader warm-up. Every glTF material in a world is an instance of a
// small, known set of base materials (glTFRuntime's PBR bases, our lit avatar
// templates, ground cover), drawn with the static or GPU-skinned vertex factory.
// Rendering each combination once, tiny and behind the door panel, compiles the
// shaders and pipeline states (base pass, shadows, ray tracing hit groups) that a
// busy world would otherwise compile mid-frame on first sight. Removed on join.
UCLASS()
class EIDOVERSECLIENT_API AEidoverseShaderWarmup : public AActor
{
    GENERATED_BODY()
public:
    AEidoverseShaderWarmup();
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaSeconds) override;
    // Materials whose shaders are still compiling (0 once warm).
    UPROPERTY(Transient, BlueprintReadOnly, Category="Eidoverse") int32 PendingMaterials = 0;
    UPROPERTY(Transient, BlueprintReadOnly, Category="Eidoverse") int32 WarmMaterials = 0;
private:
    UPROPERTY(Transient) TArray<TObjectPtr<UMaterialInterface>> Materials;
    UPROPERTY(Transient) TObjectPtr<class USceneComponent> Anchor;
    double StartedAt = 0;
    bool bReported = false;
};
