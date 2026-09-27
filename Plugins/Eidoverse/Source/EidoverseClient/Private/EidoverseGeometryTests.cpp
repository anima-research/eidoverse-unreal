#include "EidoverseAvatar.h"
#include "EidoverseBridge.h"
#include "Misc/AutomationTest.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "glTFRuntimeAsset.h"
#include "glTFRuntimeFunctionLibrary.h"
#include "Engine/StaticMesh.h"
#include "StaticMeshResources.h"
#include "MeshCardRepresentation.h"
#include "MeshCardBuild.h"
#include "Misc/Base64.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEidoverseWinding,"Eidoverse.GeometryWinding",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEidoverseWinding::RunTest(const FString&)
{
    const float V[]={0,0,0, 1,0,0, 0,1,0, 0,0,1, 0,0,1, 0,0,1};
    const uint16 I[]={0,1,2};TArray<uint8> Bytes;
    Bytes.Append(reinterpret_cast<const uint8*>(V),sizeof(V));Bytes.Append(reinterpret_cast<const uint8*>(I),sizeof(I));
    const FString Json=FString::Printf(TEXT(R"({"asset":{"version":"2.0"},"buffers":[{"byteLength":78,"uri":"data:application/octet-stream;base64,%s"}],"bufferViews":[{"buffer":0,"byteOffset":0,"byteLength":36},{"buffer":0,"byteOffset":36,"byteLength":36},{"buffer":0,"byteOffset":72,"byteLength":6}],"accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3","min":[0,0,0],"max":[1,1,0]},{"bufferView":1,"componentType":5126,"count":3,"type":"VEC3"},{"bufferView":2,"componentType":5123,"count":3,"type":"SCALAR"}],"meshes":[{"primitives":[{"attributes":{"POSITION":0,"NORMAL":1},"indices":2}]}],"nodes":[{"mesh":0}],"scenes":[{"nodes":[0]}],"scene":0})"),*FBase64::Encode(Bytes));
    auto* Loader=UglTFRuntimeFunctionLibrary::glTFLoadAssetFromString(Json,UEidoverseAvatarAsset::ImportConfig());
    if(!TestNotNull(TEXT("fixture parsed"),Loader))return false;
    auto* Mesh=Loader->LoadStaticMeshRecursive(TEXT(""),{},UEidoverseAvatarAsset::StaticGeometryConfig());
    if(!TestNotNull(TEXT("fixture imported"),Mesh)||!Mesh->GetRenderData())return false;
    const auto& L=Mesh->GetRenderData()->LODResources[0];const auto Ix=L.IndexBuffer.GetArrayView();
    if(TestNotNull(TEXT("runtime mesh has Lumen surface-cache cards"),L.CardRepresentationData))
        TestEqual(TEXT("six-axis runtime surface cache"),L.CardRepresentationData->MeshCardsBuildData.CardBuildData.Num(),6);
    const auto& P=L.VertexBuffers.PositionVertexBuffer;const auto& N=L.VertexBuffers.StaticMeshVertexBuffer;
    TestEqual(TEXT("one triangle"),Ix.Num(),3);
    const FVector A(P.VertexPosition(Ix[0])),B(P.VertexPosition(Ix[1])),C(P.VertexPosition(Ix[2]));
    const FVector Normal(N.VertexTangentZ(Ix[0]));
    TestTrue(TEXT("normal maps wire +Z to UE +X"),Normal.Equals(FVector::ForwardVector,.001));
    TestTrue(TEXT("outward normal agrees with UE clockwise front face"),FVector::DotProduct(FVector::CrossProduct(C-A,B-A).GetSafeNormal(),Normal)>.99);
    TestTrue(TEXT("bounds and centimetre scale unchanged"),Mesh->GetBounds().BoxExtent.Equals(FVector(0,50,50),.001));
    TestTrue(TEXT("asymmetric import is on the correct side, not mirrored"),Mesh->GetBounds().Origin.Equals(FVector(0,-50,50),.001));
    {
        // The same triangle under a mirroring root node (determinant -1): a matrix
        // node as the right-handed water export uses, and a negative scale. The
        // model is imported un-mirrored and mirrored back by component scale, so
        // the mesh itself must equal the plain import.
        const TCHAR* Roots[]={TEXT(R"({"matrix":[-1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1],"children":[0]})"),TEXT(R"({"scale":[-1,1,1],"children":[0]})")};
        for(const TCHAR* Root:Roots)
        {
            const FString Mirrored=Json.Replace(TEXT(R"("nodes":[{"mesh":0}],"scenes":[{"nodes":[0]}])"),*FString::Printf(TEXT(R"("nodes":[{"mesh":0},%s],"scenes":[{"nodes":[1]}])"),Root));
            TSharedPtr<FJsonObject> Doc;FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Mirrored),Doc);
            TestTrue(TEXT("mirrored fixture detected"),UEidoverseAvatarAsset::UnmirrorDocument(Doc));
            FString Fixed;FJsonSerializer::Serialize(Doc.ToSharedRef(),TJsonWriterFactory<>::Create(&Fixed));
            auto* MirrorLoader=UglTFRuntimeFunctionLibrary::glTFLoadAssetFromString(Fixed,UEidoverseAvatarAsset::ImportConfig());
            auto* MirrorMesh=MirrorLoader?MirrorLoader->LoadStaticMeshRecursive(TEXT(""),{},UEidoverseAvatarAsset::StaticGeometryConfig()):nullptr;
            if(!TestNotNull(TEXT("mirrored fixture imported"),MirrorMesh)||!MirrorMesh->GetRenderData())continue;
            const auto& ML=MirrorMesh->GetRenderData()->LODResources[0];const auto MIx=ML.IndexBuffer.GetArrayView();
            const auto& MP=ML.VertexBuffers.PositionVertexBuffer;
            const FVector MA(MP.VertexPosition(MIx[0])),MB(MP.VertexPosition(MIx[1])),MC(MP.VertexPosition(MIx[2]));
            const FVector MN(ML.VertexBuffers.StaticMeshVertexBuffer.VertexTangentZ(MIx[0]));
            TestTrue(TEXT("un-mirrored normal is the authored +X"),MN.Equals(FVector::ForwardVector,.001));
            TestTrue(TEXT("un-mirrored winding follows the normal"),FVector::DotProduct(FVector::CrossProduct(MC-MA,MB-MA).GetSafeNormal(),MN)>.99);
            // Component scale (1,-1,1) restores the authored mirror image.
            TestTrue(TEXT("component mirror restores the mirrored placement"),(MirrorMesh->GetBounds().Origin*FVector(1,-1,1)).Equals(FVector(0,50,50),.001));
        }
        TSharedPtr<FJsonObject> Plain;FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json),Plain);
        TestFalse(TEXT("plain models are left alone"),UEidoverseAvatarAsset::UnmirrorDocument(Plain));
    }
    const auto Config=UEidoverseAvatarAsset::ImportConfig();
    TestTrue(TEXT("import basis changes handedness"),Config.BasisMatrix.Determinant()<0);
    for(FVector Axis:{FVector::XAxisVector,FVector::YAxisVector,FVector::ZAxisVector})
        TestTrue(TEXT("import and protocol use identical bases"),FVector(Config.BasisMatrix.TransformVector(Axis)).Equals(AEidoverseBridge::FromWirePosition(Axis)/100.,1e-6));
    return true;
}
#endif
