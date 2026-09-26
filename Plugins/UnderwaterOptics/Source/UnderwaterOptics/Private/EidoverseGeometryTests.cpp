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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEidoverseWinding,"Underwater.Eidoverse.GeometryWinding",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
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
    const auto Config=UEidoverseAvatarAsset::ImportConfig();
    TestTrue(TEXT("import basis changes handedness"),Config.BasisMatrix.Determinant()<0);
    for(FVector Axis:{FVector::XAxisVector,FVector::YAxisVector,FVector::ZAxisVector})
        TestTrue(TEXT("import and protocol use identical bases"),FVector(Config.BasisMatrix.TransformVector(Axis)).Equals(AEidoverseBridge::FromWirePosition(Axis)/100.,1e-6));
    return true;
}
#endif
