#include "EidoverseGroundCover.h"
#include "Engine/StaticMesh.h"
#include "MeshDescription.h"
#include "StaticMeshAttributes.h"
#include "StaticMeshResources.h"

UStaticMesh* EidoGroundCover::BuildTuft(UObject* Owner,UMaterialInterface* Material)
{
    FMeshDescription Descriptions[3];
    for(int32 LOD=0;LOD<3;++LOD)
    {
        auto& D=Descriptions[LOD];FStaticMeshAttributes A(D);A.Register();
        auto Positions=A.GetVertexPositions();auto Normals=A.GetVertexInstanceNormals();
        auto Tangents=A.GetVertexInstanceTangents();auto Signs=A.GetVertexInstanceBinormalSigns();
        auto Colors=A.GetVertexInstanceColors();auto UV=A.GetVertexInstanceUVs();UV.SetNumChannels(1);
        const auto Group=D.CreatePolygonGroup();A.GetPolygonGroupMaterialSlotNames()[Group]=TEXT("GroundCover");
        auto Tri=[&](FVector P,FVector Q,FVector R)
        {
            // Clockwise front-face winding, with deliberately upward-biased
            // normals: a thin leaf should not alternate between black and neon.
            const FVector N=(FVector::CrossProduct(Q-P,R-P).GetSafeNormal()+FVector(0,0,1.4)).GetSafeNormal();
            const FVector T=FVector::CrossProduct(FVector::YAxisVector,N).GetSafeNormal();
            TArray<FVertexInstanceID> Face;
            for(FVector V:{P,R,Q})
            {
                auto Vertex=D.CreateVertex();Positions[Vertex]=FVector3f(V);auto I=D.CreateVertexInstance(Vertex);Face.Add(I);
                Normals[I]=FVector3f(N);Tangents[I]=FVector3f(T);Signs[I]=1;
                const float Tip=FMath::Clamp(float(V.Z/50.),0.f,1.f);
                const float Shade=FMath::Lerp(.34f,1.f,FMath::Sqrt(Tip));
                Colors[I]=FVector4f(Shade,Shade,Shade,1);UV.Set(I,0,FVector2f(1,Tip));
            }
            D.CreateTriangle(Group,Face);
        };
        FRandomStream Random(8317);const int32 Count=LOD==0?36:LOD==1?12:6;
        for(int32 B=0;B<Count;++B)
        {
            const float Angle=B*2.399963f;
            const FVector Direction(FMath::Cos(Angle),FMath::Sin(Angle),0),Side(-Direction.Y,Direction.X,0);
            const float Radius=FMath::Sqrt(Random.FRand())*43;
            const FVector Foot=Direction*Radius;
            const float Height=Random.FRandRange(24,50)*(1-.18f*Radius/43);
            const float Width=LOD==0?Random.FRandRange(.8,1.5):LOD==1?2.2f:3.8f;
            const FVector Mid=Foot+Direction*Height*.13+FVector(0,0,Height*.56);
            const FVector Tip=Foot+Direction*Height*.42+FVector(0,0,Height);
            if(LOD==2)Tri(Foot-Side*Width,Foot+Side*Width,Tip);
            else
            {
                Tri(Foot-Side*Width,Foot+Side*Width,Mid+Side*Width*.65);
                Tri(Foot-Side*Width,Mid+Side*Width*.65,Mid-Side*Width*.65);
                Tri(Mid-Side*Width*.65,Mid+Side*Width*.65,Tip);
            }
        }
    }
    auto* Mesh=NewObject<UStaticMesh>(Owner,NAME_None,RF_Transient);
    Mesh->bSupportRayTracing=false; // Ground cover must not grow the ray-tracing scene.
    Mesh->GetStaticMaterials().Add(FStaticMaterial(Material,TEXT("GroundCover")));
    UStaticMesh::FBuildMeshDescriptionsParams Params;
    Params.bFastBuild=true;Params.bCommitMeshDescription=false;Params.bMarkPackageDirty=false;
    if(!Mesh->BuildFromMeshDescriptions({&Descriptions[0],&Descriptions[1],&Descriptions[2]},Params))return nullptr;
    auto* Render=Mesh->GetRenderData();
    Render->ScreenSize[0].Default=1;
    Render->ScreenSize[1].Default=.065f;
    Render->ScreenSize[2].Default=.022f;
    for(auto& LOD:Render->LODResources)for(auto& Section:LOD.Sections)Section.bCastShadow=false;
    return Mesh;
}
