#include "EidoverseGroundCover.h"
#include "Misc/AutomationTest.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "Engine/StaticMesh.h"
#include "StaticMeshResources.h"
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEidoverseGroundCoverTest,"Eidoverse.GroundCover",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEidoverseGroundCoverTest::RunTest(const FString&)
{
    auto* Mesh=EidoGroundCover::BuildTuft(GetTransientPackage(),nullptr);
    if(!TestNotNull(TEXT("runtime tuft built"),Mesh)||!Mesh->GetRenderData())return false;
    const auto* R=Mesh->GetRenderData();
    TestEqual(TEXT("three distance LODs"),R->LODResources.Num(),3);
    TestFalse(TEXT("grass excluded from ray-tracing geometry"),Mesh->bSupportRayTracing);
    const int32 Expected[]={108,36,6};
    for(int32 I=0;I<R->LODResources.Num();++I)
    {
        const auto& L=R->LODResources[I];
        TestEqual(TEXT("bounded triangle budget"),L.GetNumTriangles(),uint32(Expected[I]));
        TestEqual(TEXT("one section per LOD"),L.Sections.Num(),1);
        TestFalse(TEXT("no grass shadow workload"),L.Sections[0].bCastShadow);
        const auto& P=L.VertexBuffers.PositionVertexBuffer;
        for(uint32 V=0;V<P.GetNumVertices();++V)
        {
            const FVector Pos(P.VertexPosition(V));
            TestFalse(TEXT("finite vertices"),Pos.ContainsNaN());
            TestTrue(TEXT("tuft is rooted and bounded"),Pos.Z>=0&&Pos.Z<=50&&Pos.Size2D()<70);
        }
    }
    TestTrue(TEXT("progressively cheaper screen thresholds"),R->ScreenSize[0].Default>R->ScreenSize[1].Default&&R->ScreenSize[1].Default>R->ScreenSize[2].Default);
    return true;
}
#endif
