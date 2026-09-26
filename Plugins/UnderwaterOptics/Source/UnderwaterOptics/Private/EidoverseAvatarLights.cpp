#include "EidoverseAvatar.h"
#include "Engine/SkeletalMesh.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "Rendering/SkeletalMeshLODRenderData.h"
#include "Components/PointLightComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "RenderUtils.h"

float FEidoverseAvatarLamp::InferredIntensity(const FLinearColor& Emission,float Strength)
{
    if(!FMath::IsFinite(Strength)||Strength<=0||!FMath::IsFinite(Emission.R)||!FMath::IsFinite(Emission.G)||!FMath::IsFinite(Emission.B))return 0;
    const float Glow=FMath::Max3(Emission.R,Emission.G,Emission.B)*Strength;
    return Glow>.5f?FMath::Min(40.f,6.f+2.6f*FMath::Max(Glow,Strength)):0;
}
float FEidoverseAvatarLamp::Breath(double Time)
{
    // Same authored lampglass rhythm as client/lib/avatar.js, not an identity rule.
    return FMath::IsFinite(Time)?.03f+.42f*FMath::Pow(FMath::Abs(FMath::Sin(Time*PI/3.4)),1.6):.03f;
}
void UEidoverseAvatarAsset::BuildEmissiveLamps()
{
    Lamps.Empty();if(!Mesh||!Mesh->GetResourceForRendering()||Mesh->GetResourceForRendering()->LODRenderData.IsEmpty())return;
    auto& LOD=Mesh->GetResourceForRendering()->LODRenderData[0];
    const auto& Positions=LOD.StaticVertexBuffers.PositionVertexBuffer;
    const auto& Weights=LOD.SkinWeightVertexBuffer;
    const auto& Skeleton=Mesh->GetRefSkeleton();TArray<FTransform> Rest=Skeleton.GetRefBonePose();
    for(int32 I=0;I<Rest.Num();++I)if(Skeleton.GetParentIndex(I)>=0)Rest[I]*=Rest[Skeleton.GetParentIndex(I)];
    for(auto& Section:LOD.RenderSections)
    {
        if(!Mesh->GetMaterials().IsValidIndex(Section.MaterialIndex))continue;
        const auto& Slot=Mesh->GetMaterials()[Section.MaterialIndex];auto* Material=Slot.MaterialInterface.Get();if(!Material)continue;
        float Transmission=0;
        Material->GetScalarParameterValue(FMaterialParameterInfo(TEXT("transmissionFactor")),Transmission);
        // The imported transmission shader can still enter the opaque shadow
        // path. A nearly-clear window must not seal a lamp inside its body.
        // This is clear transmission, not simulated coloured-glass caustics.
        if(Transmission>=.9f)Section.bCastShadow=false;
        FLinearColor Emission=FLinearColor::Black;float Strength=1;
        Material->GetVectorParameterValue(FMaterialParameterInfo(TEXT("emissiveFactor")),Emission);
        Material->GetScalarParameterValue(FMaterialParameterInfo(TEXT("emissiveStrength")),Strength);
        const float Intensity=FEidoverseAvatarLamp::InferredIntensity(Emission,Strength);if(Intensity<=0)continue;
        // A bulb must not occlude its own companion light. Only this emissive
        // section is excluded; the rest of the body still casts real shadows.
        Section.bCastShadow=false;
        if(Lamps.Num()>=2)continue;
        FBox Bounds(ForceInit);TMap<int32,double> BoneWeights;
        const uint32 End=FMath::Min(Section.BaseVertexIndex+Section.NumVertices,Positions.GetNumVertices());
        for(uint32 V=Section.BaseVertexIndex;V<End;++V)
        {
            Bounds+=FVector(Positions.VertexPosition(V));if(V>=Weights.GetNumVertices())continue;
            for(uint32 K=0;K<Weights.GetMaxBoneInfluences();++K)
            {
                const uint32 LocalBone=Weights.GetBoneIndex(V,K);
                if(Section.BoneMap.IsValidIndex(LocalBone))BoneWeights.FindOrAdd(Section.BoneMap[LocalBone])+=Weights.GetBoneWeight(V,K);
            }
        }
        if(!Bounds.IsValid)continue;
        int32 Bone=INDEX_NONE;double Best=0;
        for(const auto& W:BoneWeights)if(W.Value>Best){Best=W.Value;Bone=W.Key;}
        FEidoverseAvatarLamp Lamp;Lamp.MaterialIndex=Section.MaterialIndex;Lamp.Intensity=Intensity;
        Lamp.Radius=FMath::Clamp(float(Bounds.GetExtent().GetMax()),1.f,8.f);
        Lamp.Color=Emission/FMath::Max(.001f,FMath::Max3(Emission.R,Emission.G,Emission.B));Lamp.Color.A=1;
        Lamp.bBreath=Slot.MaterialSlotName.ToString().Contains(TEXT("lampglass"),ESearchCase::IgnoreCase);
        Lamp.EmissiveStrength=Strength;
        Lamp.Bone=Rest.IsValidIndex(Bone)?Skeleton.GetBoneName(Bone):NAME_None;
        Lamp.Offset=Rest.IsValidIndex(Bone)?Rest[Bone].InverseTransformPosition(Bounds.GetCenter()):Bounds.GetCenter();
        Lamps.Add(Lamp);
    }
    UE_LOG(LogTemp,Display,TEXT("EIDOVERSE avatar emissive lamps: %d (bounded to 2 per body)"),Lamps.Num());
}
void UEidoverseAvatarMesh::CreateEmissiveLights()
{
    for(auto& Light:EmissiveLights)if(Light)Light->DestroyComponent();EmissiveLights.Empty();EmissiveMaterials.Empty();
    if(!Avatar)return;
    for(const auto& Lamp:Avatar->Lamps)
    {
        auto* Light=NewObject<UPointLightComponent>(GetOwner());GetOwner()->AddInstanceComponent(Light);
        Light->SetupAttachment(this,Lamp.Bone);Light->SetRelativeLocation(Lamp.Offset);Light->SetMobility(EComponentMobility::Movable);
        Light->SetIntensityUnits(ELightUnits::Candelas);Light->SetLightColor(Lamp.Color);Light->SetIntensity(Lamp.Intensity);
        Light->SetAttenuationRadius(1200);Light->SetSourceRadius(Lamp.Radius);Light->SetCastShadows(true);
        Light->SetCastRaytracedShadows(IsRayTracingEnabled()?ECastRayTracedShadow::Enabled:ECastRayTracedShadow::UseProjectSetting);
        Light->SetVisibility(false);Light->RegisterComponent();EmissiveLights.Add(Light);
        UMaterialInstanceDynamic* Instance=nullptr;
        if(Lamp.bBreath)if(auto* Base=GetMaterial(Lamp.MaterialIndex))
        {
            auto* Existing=Cast<UMaterialInstanceDynamic>(Base);
            // Each body owns its pulse; never change shared cached materials or
            // parent a MID to another MID (which loses the material permutation).
            Instance=UMaterialInstanceDynamic::Create(Existing?Existing->Parent.Get():Base,this);
            Instance->CopyMaterialUniformParameters(Base);SetMaterial(Lamp.MaterialIndex,Instance);
        }
        EmissiveMaterials.Add(Instance);
    }
}
void UEidoverseAvatarMesh::UpdateEmissiveLights(double Time,float Dayness,bool bEnabled)
{
    if(!Avatar)return;const float Night=FMath::Square(1-FMath::Clamp(Dayness,0.f,1.f));
    for(int32 I=0;I<EmissiveLights.Num()&&Avatar->Lamps.IsValidIndex(I);++I)
    {
        const auto& Lamp=Avatar->Lamps[I];const float Pulse=Lamp.bBreath?FEidoverseAvatarLamp::Breath(Time):1;
        auto* Light=EmissiveLights[I].Get();Light->SetVisibility(bEnabled);Light->SetIntensity(Lamp.Intensity*Pulse*Night);
        if(EmissiveMaterials.IsValidIndex(I)&&EmissiveMaterials[I])
            EmissiveMaterials[I]->SetScalarParameterValue(TEXT("emissiveStrength"),Lamp.EmissiveStrength*Pulse*(.18f+.82f*Night));
    }
}
