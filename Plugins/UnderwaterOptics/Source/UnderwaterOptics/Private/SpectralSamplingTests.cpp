#include "SpectralOcean.h"
#include "Misc/AutomationTest.h"
#include "Engine/Texture2D.h"
#include "TextureResource.h"
#include <limits>
#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSpectralSamplingTest,"Underwater.Ocean.SamplingBoundaries",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FSpectralSamplingTest::RunTest(const FString&)
{
    constexpr int N=128;TArray<FVector4f> A;A.SetNum(N*N);
    for(int J=0;J<N;++J)for(int I=0;I<N;++I)A[J*N+I]=FVector4f(I,J,0,1);
    const double Values[]={-1.e20,-256,-128.000001,-128,-127.999999,-1,-1.e-6,-1.e-20,0,1.e-20,1,127.999999,128,128.000001,256,1.e20};
    for(double X:Values)for(double Y:Values)
    {
        const FVector4f S=ASpectralOcean::Sample(A,N,X,Y);
        TestTrue(TEXT("wrapped sample remains finite and within field"),FMath::IsFinite(S.X)&&FMath::IsFinite(S.Y)&&S.X>=0&&S.X<=127&&S.Y>=0&&S.Y<=127&&S.W==1);
    }
    TestTrue(TEXT("negative near-zero row reproducer is continuous"),ASpectralOcean::Sample(A,N,4,-1.e-6).Equals(FVector4f(4,0,0,1),.001f));
    TestTrue(TEXT("bilinear interpolation"),ASpectralOcean::Sample(A,N,4.25,5.75).Equals(FVector4f(4.25,5.75,0,1),.0001f));
    TestTrue(TEXT("periodicity"),ASpectralOcean::Sample(A,N,-123.75,261.75).Equals(ASpectralOcean::Sample(A,N,4.25,5.75),.0001f));
    TestTrue(TEXT("invalid field rejected"),ASpectralOcean::Sample({},N,0,0).Equals(FVector4f(0,0,0,0)));
    TestTrue(TEXT("invalid coordinate rejected"),ASpectralOcean::Sample(A,N,std::numeric_limits<double>::quiet_NaN(),0).Equals(FVector4f(0,0,0,0)));
    auto* Field=ASpectralOcean::MakeField(8);
    TestNotNull(TEXT("field allocated"),Field);
    for(auto& Mip:Field->GetPlatformData()->Mips)
    {
        const auto* Pixels=static_cast<const FVector4f*>(Mip.BulkData.LockReadOnly());
        bool Zero=true;for(int32 I=0;I<Mip.SizeX*Mip.SizeY;++I)Zero&=Pixels[I].Equals(FVector4f(0,0,0,0),0);
        Mip.BulkData.Unlock();
        TestTrue(TEXT("every mip is neutral before first simulation upload"),Zero);
    }
    return true;
}
#endif
