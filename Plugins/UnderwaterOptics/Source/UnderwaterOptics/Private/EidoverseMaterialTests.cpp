#include "EidoverseAvatar.h"
#include "Misc/AutomationTest.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "glTFRuntimeAsset.h"
#include "glTFRuntimeFunctionLibrary.h"
#include "Materials/Material.h"
#include "Engine/Texture2D.h"
#include "Serialization/JsonSerializer.h"
#include "IImageWrapperModule.h"
#include "IImageWrapper.h"
#include "Misc/Base64.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEidoverseLitMaterials,"Underwater.Eidoverse.LitMaterials",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEidoverseLitMaterials::RunTest(const FString&)
{
    auto J=MakeShared<FJsonObject>();auto Version=MakeShared<FJsonObject>();Version->SetStringField(TEXT("version"),TEXT("2.0"));J->SetObjectField(TEXT("asset"),Version);
    auto Numbers=[](std::initializer_list<double> V){TArray<TSharedPtr<FJsonValue>> A;for(double X:V)A.Add(MakeShared<FJsonValueNumber>(X));return A;};
    // A genuinely decoded embedded texture, not a pre-existing material asset.
    auto& Images=FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));auto PNG=Images.CreateImageWrapper(EImageFormat::PNG);
    const uint8 Pixel[]={32,96,160,255};if(!PNG->SetRaw(Pixel,4,1,1,ERGBFormat::RGBA,8))return false;
    const auto Compressed=PNG->GetCompressed();auto Image=MakeShared<FJsonObject>();
    Image->SetStringField(TEXT("uri"),TEXT("data:image/png;base64,")+FBase64::Encode(Compressed.GetData(),Compressed.Num()));
    J->SetArrayField(TEXT("images"),{MakeShared<FJsonValueObject>(Image)});
    auto Texture=MakeShared<FJsonObject>();Texture->SetNumberField(TEXT("source"),0);J->SetArrayField(TEXT("textures"),{MakeShared<FJsonValueObject>(Texture)});
    TArray<TSharedPtr<FJsonValue>> Materials;
    for(int32 I=0;I<10;++I)
    {
        auto M=MakeShared<FJsonObject>(),PBR=MakeShared<FJsonObject>(),Extensions=MakeShared<FJsonObject>();
        M->SetStringField(TEXT("alphaMode"),I<2?TEXT("OPAQUE"):I<4?TEXT("MASK"):I<6||I>=8?TEXT("BLEND"):TEXT("OPAQUE"));
        M->SetBoolField(TEXT("doubleSided"),I%2==1);M->SetNumberField(TEXT("alphaCutoff"),.37);
        PBR->SetArrayField(TEXT("baseColorFactor"),Numbers({.2,.4,.6,.7}));
        if(I==1||I==6){PBR->SetNumberField(TEXT("metallicFactor"),.4);PBR->SetNumberField(TEXT("roughnessFactor"),.35);}
        auto Tex=MakeShared<FJsonObject>();Tex->SetNumberField(TEXT("index"),0);PBR->SetObjectField(TEXT("baseColorTexture"),Tex);
        M->SetObjectField(TEXT("pbrMetallicRoughness"),PBR);
        M->SetArrayField(TEXT("emissiveFactor"),Numbers({.01,.02,.03}));
        if(I<6){Extensions->SetObjectField(TEXT("KHR_materials_unlit"),MakeShared<FJsonObject>());M->SetObjectField(TEXT("extensions"),Extensions);}
        Materials.Add(MakeShared<FJsonValueObject>(M));
    }
    J->SetArrayField(TEXT("materials"),Materials);
    FString Before;FJsonSerializer::Serialize(J,TJsonWriterFactory<>::Create(&Before));
    auto* Adapt=NewObject<UEidoverseAvatarAsset>();if(!TestTrue(TEXT("lit templates available"),Adapt->PrepareLitMaterials(J)))return false;
    TestEqual(TEXT("only unlit inputs adapted"),Adapt->AdaptedMaterialCount,6);
    FString After;FJsonSerializer::Serialize(J,TJsonWriterFactory<>::Create(&After));TestEqual(TEXT("source document remains unchanged"),Before,After);
    FglTFRuntimeConfig LoaderConfig;LoaderConfig.bAllowExternalFiles=false;
    auto* Loader=UglTFRuntimeFunctionLibrary::glTFLoadAssetFromString(Before,LoaderConfig);if(!TestNotNull(TEXT("test asset parser"),Loader))return false;
    const auto Config=Adapt->MaterialConfig();TestFalse(TEXT("authored PBR is not replaced"),Config.MaterialsOverrideMap.Contains(6));
    for(int32 I=0;I<10;++I)
    {
        auto* M=Loader->LoadMaterial(I,Config,false);if(!TestNotNull(TEXT("runtime material"),M))return false;
        TestTrue(TEXT("receives scene lighting"),M->GetShadingModels().HasShadingModel(MSM_DefaultLit));
        TestFalse(TEXT("not unlit"),M->GetShadingModels().HasShadingModel(MSM_Unlit));
        TestEqual(TEXT("alpha mode preserved"),M->GetBlendMode(),I<2||I==6||I==7?BLEND_Opaque:I<4?BLEND_Masked:BLEND_Translucent);
        TestEqual(TEXT("two-sided state preserved"),M->IsTwoSided(),I%2==1);
        if((I>=4&&I<6)||I>=8)TestEqual(TEXT("transparency receives forward surface lights"),M->GetMaterial()->TranslucencyLightingMode.GetValue(),TLM_SurfacePerPixelLighting);
        FLinearColor Color;M->GetVectorParameterValue(FMaterialParameterInfo(TEXT("baseColorFactor")),Color);
        TestTrue(TEXT("color and opacity preserved"),Color.Equals(FLinearColor(.2,.4,.6,.7),.0001));
        float Metal=-1,Rough=-1;M->GetScalarParameterValue(FMaterialParameterInfo(TEXT("metallicFactor")),Metal);M->GetScalarParameterValue(FMaterialParameterInfo(TEXT("roughnessFactor")),Rough);
        TestTrue(TEXT("metallic authored or neutral fallback"),FMath::IsNearlyEqual(Metal,I==1||I==6?.4f:I>=7?1.f:0.f));
        TestTrue(TEXT("roughness authored or matte fallback"),FMath::IsNearlyEqual(Rough,I==1||I==6?.35f:I>=7?1.f:.8f));
        UTexture* Loaded=nullptr;M->GetTextureParameterValue(FMaterialParameterInfo(TEXT("baseColorTexture")),Loaded);
        TestTrue(TEXT("embedded texture retained"),Loaded&&Loaded->GetOutermost()==GetTransientPackage());
        M->GetVectorParameterValue(FMaterialParameterInfo(TEXT("emissiveFactor")),Color);
        TestTrue(TEXT("authored emission retained"),FMath::IsNearlyEqual(Color.R,.01f)&&FMath::IsNearlyEqual(Color.G,.02f)&&FMath::IsNearlyEqual(Color.B,.03f));
    }
    return true;
}
#endif
