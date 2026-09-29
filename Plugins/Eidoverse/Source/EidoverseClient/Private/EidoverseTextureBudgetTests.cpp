#include "EidoverseAvatar.h"
#include "Misc/AutomationTest.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "IImageWrapperModule.h"
#include "IImageWrapper.h"
#include "Modules/ModuleManager.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEidoverseTextureBudget,"Eidoverse.TextureBudget",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEidoverseTextureBudget::RunTest(const FString&)
{
    auto MakeGlb=[](int32 Count,int32 Size)
    {
        auto& Module=FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
        auto PNG=Module.CreateImageWrapper(EImageFormat::PNG);
        TArray<uint8> Raw;Raw.Init(255,Size*Size*4);PNG->SetRaw(Raw.GetData(),Raw.Num(),Size,Size,ERGBFormat::RGBA,8);
        const auto Image=PNG->GetCompressed();
        FString Entries;for(int32 I=0;I<Count;++I){if(I)Entries+=TEXT(",");Entries+=TEXT("{\"bufferView\":0,\"mimeType\":\"image/png\"}");}
        FString Json=FString::Printf(TEXT("{\"asset\":{\"version\":\"2.0\"},\"nodes\":[{}],\"meshes\":[{}],\"skins\":[{}],\"buffers\":[{\"byteLength\":%d}],\"bufferViews\":[{\"buffer\":0,\"byteLength\":%d}],\"images\":[%s]}"),int32(Image.Num()),int32(Image.Num()),*Entries);
        FTCHARToUTF8 Utf8(*Json);TArray<uint8> J;J.Append(reinterpret_cast<const uint8*>(Utf8.Get()),Utf8.Length());while(J.Num()%4)J.Add(' ');
        const uint32 BinSize=Align(uint32(Image.Num()),4u);TArray<uint8> B;
        auto Word=[&B](uint32 N){for(int I=0;I<4;++I)B.Add(uint8(N>>(I*8)));};
        Word(0x46546c67);Word(2);Word(28+J.Num()+BinSize);Word(J.Num());Word(0x4e4f534a);B.Append(J);Word(BinSize);Word(0x004e4942);B.Append(Image.GetData(),Image.Num());while(B.Num()%4)B.Add(0);return B;
    };
    TSharedPtr<FJsonObject> J;FString Error;
    const auto ManySmall=MakeGlb(72,16);
    TestTrue(TEXT("multipart world textures accepted"),UEidoverseAvatarAsset::ValidateGlb(ManySmall,J,Error,false));
    TestFalse(TEXT("avatar texture cap unchanged"),UEidoverseAvatarAsset::ValidateGlb(ManySmall,J,Error,true));
    TestFalse(TEXT("static texture count remains bounded"),UEidoverseAvatarAsset::ValidateGlb(MakeGlb(129,1),J,Error,false));
    TestFalse(TEXT("aggregate pixel budget remains bounded"),UEidoverseAvatarAsset::ValidateGlb(MakeGlb(33,1024),J,Error,false));
    TestEqual(TEXT("rejection is pixel budget, not count"),Error,FString(TEXT("texture memory budget exceeded")));
    const auto Three4K=MakeGlb(3,4096);
    TestTrue(TEXT("winged avatar source textures accepted"),UEidoverseAvatarAsset::ValidateGlb(Three4K,J,Error,true));
    TestFalse(TEXT("world source texture budget unchanged"),UEidoverseAvatarAsset::ValidateGlb(Three4K,J,Error,false));
    TestTrue(TEXT("production-scale avatar textures accepted (5 x 4K)"),UEidoverseAvatarAsset::ValidateGlb(MakeGlb(5,4096),J,Error,true));
    TestFalse(TEXT("avatar source textures remain bounded"),UEidoverseAvatarAsset::ValidateGlb(MakeGlb(7,4096),J,Error,true));
    auto Large=MakeGlb(1,1);
    auto Pad=[&Large](int32 Size)
    {
        const int32 OldSize=Large.Num();Large.AddZeroed(Size-OldSize);
        auto SetWord=[&Large](int32 At,uint32 Value){for(int I=0;I<4;++I)Large[At+I]=uint8(Value>>(I*8));};
        uint32 JsonSize=0;FMemory::Memcpy(&JsonSize,Large.GetData()+12,4);
        SetWord(8,Size);SetWord(20+JsonSize,Size-28-JsonSize);
    };
    Pad(64*1024*1024);
    TestTrue(TEXT("avatar download boundary accepted"),UEidoverseAvatarAsset::ValidateGlb(Large,J,Error,true));
    TestFalse(TEXT("world download budget unchanged"),UEidoverseAvatarAsset::ValidateGlb(Large,J,Error,false));
    Pad(64*1024*1024+4);
    TestFalse(TEXT("avatar download remains bounded"),UEidoverseAvatarAsset::ValidateGlb(Large,J,Error,true));
    return true;
}
#endif
