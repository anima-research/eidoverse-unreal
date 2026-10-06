#include "EidoverseAvatar.h"
#include "EidoverseLoadTrace.h"
#include "Components/PointLightComponent.h"
#include "Tasks/Task.h"
#include "Async/Async.h"
#include "IImageWrapperModule.h"
#include "glTFRuntimeAsset.h"
#include "glTFRuntimeFunctionLibrary.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "PhysicsEngine/BodySetup.h"
#include "Components/StaticMeshComponent.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonReader.h"
#include "IImageWrapperModule.h"
#include "IImageWrapper.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"

namespace
{
    // Production library originals: median 9 MB, p90 32 MB, largest 90 MB (2026-09).
    constexpr int32 MaxStaticBytes=128*1024*1024;
    // Winged VRMs carry additional skinned geometry. Keep both transport and
    // validation bounded, without increasing the world-prop budget.
    // Production bodies reach ~56 MB (uploaded VRMs with their own textures).
    constexpr int32 MaxAvatarBytes=64*1024*1024;
    uint32 Word(const TArray<uint8>& B,int32 Offset)
    { uint32 V=0;FMemory::Memcpy(&V,B.GetData()+Offset,4);return V; }
    // HTTP writes on its worker; completion consumes only after the stream ends.
    class FAvatarStream : public FArchive
    {
    public:
        explicit FAvatarStream(int32 InLimit):Limit(InLimit){}
        const int32 Limit;
        TArray<uint8> Bytes;
        virtual void Serialize(void* Data,int64 N) override
        {if(N<0||N>Limit-Bytes.Num()){SetError();return;}Bytes.Append(static_cast<uint8*>(Data),int32(N));}
    };
    using FJsonArray=TArray<TSharedPtr<FJsonValue>>;
    const FJsonArray& Array(const TSharedPtr<FJsonObject>& J,const TCHAR* Key)
    {static const FJsonArray Empty;const FJsonArray* A=nullptr;return J&&J->TryGetArrayField(Key,A)?*A:Empty;}
    TSharedPtr<FJsonObject> Object(const TSharedPtr<FJsonObject>& J,const TCHAR* Key)
    {const TSharedPtr<FJsonObject>* O=nullptr;return J&&J->TryGetObjectField(Key,O)?*O:nullptr;}

    // glTF requires renderers to flip winding under a node transform with a
    // negative determinant; glTFRuntime bakes node transforms into vertices
    // without doing so. Count mesh nodes whose accumulated transform mirrors.
    double LinearDeterminant(const TSharedPtr<FJsonObject>& Node)
    {
        const FJsonArray& M=Array(Node,TEXT("matrix"));
        if(M.Num()==16)
        {
            auto E=[&](int32 Row,int32 Col){return M[Col*4+Row]->AsNumber();};  // column-major
            return E(0,0)*(E(1,1)*E(2,2)-E(1,2)*E(2,1))-E(0,1)*(E(1,0)*E(2,2)-E(1,2)*E(2,0))+E(0,2)*(E(1,0)*E(2,1)-E(1,1)*E(2,0));
        }
        const FJsonArray& S=Array(Node,TEXT("scale"));
        return S.Num()==3?S[0]->AsNumber()*S[1]->AsNumber()*S[2]->AsNumber():1.0;  // rotations have det +1
    }
    void CountMirrored(const TSharedPtr<FJsonObject>& Doc,int32 Index,double Det,int32 Depth,int32& Mirrored,int32& Normal)
    {
        const FJsonArray& Nodes=Array(Doc,TEXT("nodes"));
        if(Depth>64||!Nodes.IsValidIndex(Index)||!Nodes[Index]->AsObject())return;
        const auto Node=Nodes[Index]->AsObject();
        Det*=LinearDeterminant(Node);
        if(Node->HasField(TEXT("mesh")))(Det<0?Mirrored:Normal)++;
        for(const auto& C:Array(Node,TEXT("children")))CountMirrored(Doc,int32(C->AsNumber()),Det,Depth+1,Mirrored,Normal);
    }
    bool MostlyMirrored(const TSharedPtr<FJsonObject>& Doc)
    {
        const FJsonArray& Scenes=Array(Doc,TEXT("scenes"));
        double SceneIndex=0;Doc->TryGetNumberField(TEXT("scene"),SceneIndex);
        if(!Scenes.IsValidIndex(int32(SceneIndex))||!Scenes[int32(SceneIndex)]->AsObject())return false;
        int32 Mirrored=0,Normal=0;
        for(const auto& R:Array(Scenes[int32(SceneIndex)]->AsObject(),TEXT("nodes")))CountMirrored(Doc,int32(R->AsNumber()),1.0,0,Mirrored,Normal);
        if(Mirrored&&Normal)UE_LOG(LogTemp,Warning,TEXT("EIDOVERSE model mixes %d mirrored and %d normal mesh nodes; winding follows the majority"),Mirrored,Normal);
        return Mirrored>Normal;
    }
    bool FiniteJson(const TSharedPtr<FJsonValue>& V,int Depth)
    {
        if(!V||Depth>32)return false;
        if(V->Type==EJson::Number)return FMath::IsFinite(V->AsNumber());
        if(V->Type==EJson::Array){for(auto& X:V->AsArray())if(!FiniteJson(X,Depth+1))return false;}
        if(V->Type==EJson::Object){for(auto& P:V->AsObject()->Values)if(!FiniteJson(P.Value,Depth+1))return false;}
        return true;
    }
    const EglTFRuntimeMaterialType MaterialTypes[]={EglTFRuntimeMaterialType::Opaque,EglTFRuntimeMaterialType::TwoSided,
        EglTFRuntimeMaterialType::Masked,EglTFRuntimeMaterialType::TwoSidedMasked,
        EglTFRuntimeMaterialType::Translucent,EglTFRuntimeMaterialType::TwoSidedTranslucent};
}

bool UEidoverseAvatarAsset::PrepareLitMaterials(const TSharedPtr<FJsonObject>& Document)
{
    check(IsInGameThread());LitParents.Empty();LitTemplates.Empty();AdaptedMaterialCount=0;
    const TCHAR* Names[]={TEXT("MI_AvatarLit_Opaque"),TEXT("MI_AvatarLit_TwoSided"),TEXT("MI_AvatarLit_Masked"),
        TEXT("MI_AvatarLit_TwoSidedMasked"),TEXT("MI_AvatarLit_Translucent"),TEXT("MI_AvatarLit_TwoSidedTranslucent"),
        TEXT("MI_AvatarPBR_Translucent"),TEXT("MI_AvatarPBR_TwoSidedTranslucent")};
    for(const TCHAR* Name:Names)
    {
        auto* Template=LoadObject<UMaterialInterface>(nullptr,*(FString(TEXT("/Game/Underwater/Avatars/Materials/"))+Name));
        if(!Template)return false;LitTemplates.Add(Template);
    }
    const auto& SourceMaterials=Array(Document,TEXT("materials"));
    const auto& Legacy=Array(Object(Object(Document,TEXT("extensions")),TEXT("VRM")),TEXT("materialProperties"));
    for(int32 I=0;I<SourceMaterials.Num();++I)
    {
        const auto Source=SourceMaterials[I]->AsObject();if(!Source)continue;
        const auto Extensions=Object(Source,TEXT("extensions"));
        FString LegacyShader;if(Legacy.IsValidIndex(I)&&Legacy[I]->AsObject())Legacy[I]->AsObject()->TryGetStringField(TEXT("shader"),LegacyShader);
        const bool Adapt=(Extensions&&(Extensions->HasField(TEXT("KHR_materials_unlit"))||Extensions->HasField(TEXT("VRMC_materials_mtoon"))))||LegacyShader.Contains(TEXT("MToon"))||LegacyShader.Contains(TEXT("Unlit"));
        if(!Adapt)continue; // Already-authored PBR stays PBR, including its extensions.
        FString Alpha;Source->TryGetStringField(TEXT("alphaMode"),Alpha);bool TwoSided=false;Source->TryGetBoolField(TEXT("doubleSided"),TwoSided);
        const int32 TemplateIndex=(Alpha==TEXT("BLEND")?4:Alpha==TEXT("MASK")?2:0)+(TwoSided?1:0);
        // Unlit exporters often omit PBR factors. glTF's metallic=1 default is
        // not a useful physical interpretation of a flat-colored avatar. These
        // defaults are inherited only when the incoming material omits a factor;
        // glTFRuntime injects authored factors/textures on the final child MID.
        // Cooked constant instances are valid runtime MID parents; MIDs cannot
        // parent other MIDs. Their neutral defaults are overridden by source data.
        LitParents.Add(I,LitTemplates[TemplateIndex]);++AdaptedMaterialCount;
    }
    return true;
}

FglTFRuntimeMaterialsConfig UEidoverseAvatarAsset::MaterialConfig() const
{
    FglTFRuntimeMaterialsConfig M;M.bGeneratesMipMaps=true;
    // Primitive merging drops source material names in this importer version.
    // Avatars need those names for authored behaviors such as lampglass breath.
    M.bMergeSectionsByMaterial=bStaticModel;
    M.ImagesConfig.MaxWidth=2048;M.ImagesConfig.MaxHeight=2048;
    M.bMaterialsOverrideMapInjectParams=true;
    for(const auto& P:LitParents)M.MaterialsOverrideMap.Add(P.Key,P.Value);
    for(int32 I=0;I<6&&I<LitTemplates.Num();++I)
    {
        M.UnlitOverrideMap.Add(MaterialTypes[I],LitTemplates[I]);
        // Surface-forward transparency must not depend on the disabled global
        // translucency lighting volume. Ordinary lit PBR keeps its other variants.
        if(I>=4&&LitTemplates.IsValidIndex(I+2))M.MetallicRoughnessOverrideMap.Add(MaterialTypes[I],LitTemplates[I+2]);
    }
    return M;
}

bool UEidoverseAvatarAsset::ResolveUrl(const FString& Base,const FString& Reference,FString& Out)
{
    Out.Empty();
    const bool Local=Base.StartsWith(TEXT("http://127.0.0.1:"))||Base.StartsWith(TEXT("http://localhost:"));
    if((!Base.StartsWith(TEXT("https://"))&&!Local)||!Base.EndsWith(TEXT("/library/"))||Base.Contains(TEXT("@"))||Base.Contains(TEXT("?"))||Base.Contains(TEXT("#")))return false;
    if(Reference.IsEmpty()||Reference.Len()>512)return false;
    FString Path,Version;int32 Query;
    if(Reference.FindChar('?',Query))
    {
        Path=Reference.Left(Query);Version=Reference.Mid(Query+1);
        if(!Version.StartsWith(TEXT("v="))||Version.Len()<3||Version.Len()>34)return false;
        for(TCHAR C:Version.Mid(2))if(C<'0'||C>'9')return false;
    }
    else Path=Reference;
    if(Path.StartsWith(TEXT("/"))||Path.Contains(TEXT("//"))||Path.Contains(TEXT("..")))return false;
    if(!Path.EndsWith(TEXT(".vrm"),ESearchCase::IgnoreCase)&&!Path.EndsWith(TEXT(".glb"),ESearchCase::IgnoreCase))return false;
    for(TCHAR C:Path)if(!((C>='a'&&C<='z')||(C>='A'&&C<='Z')||(C>='0'&&C<='9')||C=='_'||C=='-'||C=='.'||C=='/'))return false;
    Out=Base+Reference;return true;
}

bool UEidoverseAvatarAsset::ValidateGlb(const TArray<uint8>& B,TSharedPtr<FJsonObject>& J,FString& Error,bool bRequireSkin)
{
    auto Bad=[&Error](const TCHAR* Why){Error=Why;return false;};
    if(B.Num()<28||B.Num()>(bRequireSkin?MaxAvatarBytes:MaxStaticBytes)||Word(B,0)!=0x46546c67||Word(B,4)!=2||Word(B,8)!=uint32(B.Num()))return Bad(TEXT("invalid or oversized GLB"));
    const uint32 JsonBytes=Word(B,12);
    if(JsonBytes>2*1024*1024||JsonBytes>uint32(B.Num()-28)||Word(B,16)!=0x4e4f534a)return Bad(TEXT("invalid JSON chunk"));
    FUTF8ToTCHAR Text(reinterpret_cast<const ANSICHAR*>(B.GetData()+20),JsonBytes);
    if(!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(FString(Text.Length(),Text.Get())),J)||!J||!FiniteJson(MakeShared<FJsonValueObject>(J),0))return Bad(TEXT("invalid JSON"));
    const int32 BinStart=20+JsonBytes;
    if(Word(B,BinStart+4)!=0x004e4942||Word(B,BinStart)>uint32(B.Num()-BinStart-8))return Bad(TEXT("missing embedded buffer"));
    const auto& Buffers=Array(J,TEXT("buffers"));
    if(Buffers.Num()!=1||!Buffers[0]->AsObject()||Buffers[0]->AsObject()->HasField(TEXT("uri")))return Bad(TEXT("only embedded GLB buffers are accepted"));
    const int64 BinSize=Word(B,BinStart);
    double BufferSize=0;if(!Buffers[0]->AsObject()->TryGetNumberField(TEXT("byteLength"),BufferSize)||BufferSize<0||BufferSize>BinSize)return Bad(TEXT("invalid buffer length"));
    const auto& Nodes=Array(J,TEXT("nodes"));const auto& Meshes=Array(J,TEXT("meshes"));const auto& Skins=Array(J,TEXT("skins"));
    for(auto& Extension:Array(J,TEXT("extensionsRequired")))
        if(Extension->AsString()==TEXT("KHR_draco_mesh_compression")||Extension->AsString()==TEXT("EXT_texture_webp")||Extension->AsString()==TEXT("KHR_texture_basisu"))
            return Bad(TEXT("compressed model needs native=1 provenance serving or an additional decoder"));
    if(Nodes.IsEmpty()||Nodes.Num()>512||Meshes.IsEmpty()||Meshes.Num()>32||(bRequireSkin&&Skins.IsEmpty())||Skins.Num()>8||Array(J,TEXT("materials")).Num()>64)return Bad(TEXT("model complexity limit"));
    // Reject cycles, duplicate parents, and invalid node references before recursion.
    TArray<int32> Parent;Parent.Init(-1,Nodes.Num());
    for(int32 I=0;I<Nodes.Num();++I)
    {
        auto N=Nodes[I]->AsObject();if(!N)return Bad(TEXT("invalid node"));
        for(auto& C:Array(N,TEXT("children")))
        {double V;if(!C->TryGetNumber(V)||V<0||V>=Nodes.Num()||V!=int32(V)||Parent[int32(V)]!=-1)return Bad(TEXT("invalid node tree"));Parent[int32(V)]=I;}
    }
    for(int32 I=0;I<Parent.Num();++I){int32 At=I,Depth=0;while(At!=-1){if(++Depth>128)return Bad(TEXT("cyclic or deep node tree"));At=Parent[At];}}
    int64 Elements=0;
    for(auto& A:Array(J,TEXT("accessors")))
    {auto O=A->AsObject();double N=0;if(!O||!O->TryGetNumberField(TEXT("count"),N)||N<0||N>1000000)return Bad(TEXT("invalid accessor count"));Elements+=int64(N);if(Elements>4000000)return Bad(TEXT("geometry budget exceeded"));}
    int32 Primitives=0;
    for(auto& M:Meshes){if(!M->AsObject())return Bad(TEXT("invalid mesh"));Primitives+=Array(M->AsObject(),TEXT("primitives")).Num();}
    if(Primitives>128)return Bad(TEXT("too many mesh sections"));
    const auto& Views=Array(J,TEXT("bufferViews"));
    for(auto& V:Views)
    {
        auto O=V->AsObject();double Offset=0,Length=0,Buffer=0;
        if(!O)return Bad(TEXT("invalid buffer view"));O->TryGetNumberField(TEXT("byteOffset"),Offset);O->TryGetNumberField(TEXT("buffer"),Buffer);
        if(!O->TryGetNumberField(TEXT("byteLength"),Length)||Buffer!=0||Offset<0||Length<0||Offset+Length>BinSize)return Bad(TEXT("buffer view out of range"));
    }
    // Multipart world props often have many small maps. Keep avatars tighter,
    // while avatars may carry three authored 4K maps. Runtime textures are
    // still capped to 2048 by MaterialConfig; this is a source-decode budget.
    // Source-texture limits bound decode memory only: GPU copies are capped at 2048 px
    // (MaterialConfig). Sized for production bodies and models (2026-09 survey).
    const auto& Images=Array(J,TEXT("images"));if(Images.Num()>(bRequireSkin?64:128))return Bad(TEXT("too many textures"));
    auto& ImageModule=FModuleManager::GetModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
    int64 Pixels=0;
    for(auto& I:Images)
    {
        auto O=I->AsObject();double Index;
        if(!O||O->HasField(TEXT("uri"))||!O->TryGetNumberField(TEXT("bufferView"),Index)||Index<0||Index>=Views.Num()||Index!=int32(Index))return Bad(TEXT("external or invalid texture"));
        auto V=Views[int32(Index)]->AsObject();double Offset=0;V->TryGetNumberField(TEXT("byteOffset"),Offset);const int64 Length=int64(V->GetNumberField(TEXT("byteLength")));
        const uint8* Data=B.GetData()+BinStart+8+int64(Offset);
        auto Format=ImageModule.DetectImageFormat(Data,Length);
        if(Format!=EImageFormat::PNG&&Format!=EImageFormat::JPEG)return Bad(TEXT("unsupported texture encoding"));
        auto Image=ImageModule.CreateImageWrapper(Format);
        if(!Image||!Image->SetCompressed(Data,Length)||Image->GetWidth()<=0||Image->GetHeight()<=0||Image->GetWidth()>8192||Image->GetHeight()>8192)return Bad(TEXT("texture dimensions exceeded"));
        Pixels+=int64(Image->GetWidth())*Image->GetHeight();if(Pixels>128LL*1024*1024)return Bad(TEXT("texture memory budget exceeded"));
    }
    return true;
}

void UEidoverseAvatarAsset::Fail(const FString& Why)
{State=EState::Failed;Failure=Why;Loader=nullptr;UE_LOG(LogTemp,Warning,TEXT("EIDOVERSE avatar fallback: %s (%s)"),*Why,*Url);}
void UEidoverseAvatarAsset::Cancel()
{bCancelled=true;if(Request){Request->OnProcessRequestComplete().Unbind();Request->CancelRequest();Request.Reset();}if(State!=EState::Ready)State=EState::Failed;}
bool UEidoverseAvatarAsset::RetryAfterFailure()
{
    if(State!=EState::Failed||bCancelled||Retries>=3)return false;
    ++Retries;State=EState::Queued;Failure.Empty();return true;
}
void UEidoverseAvatarAsset::Start()
{
    if(State!=EState::Queued||bCancelled)return;State=EState::Loading;
    StartedAt=FPlatformTime::Seconds();
    Request=FHttpModule::Get().CreateRequest();Request->SetURL(Url);Request->SetVerb(TEXT("GET"));Request->SetTimeout(30);
    Request->SetOption(HttpRequestOptions::EnableCookies,TEXT("false"));
    auto Stream=MakeShared<FAvatarStream,ESPMode::ThreadSafe>(bStaticModel?MaxStaticBytes:MaxAvatarBytes);
    if(!Request->SetResponseBodyReceiveStream(Stream)){Fail(TEXT("HTTP streaming unavailable"));return;}
    TWeakObjectPtr<UEidoverseAvatarAsset> Weak(this);
    Request->OnProcessRequestComplete().BindLambda([Weak,Stream](FHttpRequestPtr Req,FHttpResponsePtr Response,bool Ok)
    {
        auto* Self=Weak.Get();if(!Self||Self->bCancelled)return;
        Self->Request.Reset();
        if(!Ok||!Response||Response->GetResponseCode()!=200||Stream->IsError()||Response->GetURL()!=Self->Url)
        {
            const FString Why=!Response?FString::Printf(TEXT("no response after %.0fs"),FPlatformTime::Seconds()-Self->StartedAt)
                :Stream->IsError()?FString::Printf(TEXT("oversized (limit %d MB)"),Stream->Limit>>20)
                :Response->GetResponseCode()!=200?FString::Printf(TEXT("HTTP %d"),Response->GetResponseCode())
                :Response->GetURL()!=Self->Url?TEXT("redirected"):TEXT("transfer failed");
            Self->Fail(TEXT("download failed: ")+Why);return;
        }
        EidoLoadTrace::Note(TEXT("download(wall)"),Self->Url,(FPlatformTime::Seconds()-Self->StartedAt)*1000);
        Self->Decode(Stream->Bytes);
    });
    if(!Request->ProcessRequest())Fail(TEXT("could not start download"));
}
FglTFRuntimeConfig UEidoverseAvatarAsset::ImportConfig()
{
    FglTFRuntimeConfig Config;Config.bAllowExternalFiles=false;Config.bNoArchive=true;
    Config.TransformBaseType=EglTFRuntimeTransformBaseType::Matrix;
    Config.BasisMatrix=FMatrix(FPlane(0,-1,0,0),FPlane(0,0,1,0),FPlane(1,0,0,0),FPlane(0,0,0,1));
    return Config;
}
FglTFRuntimeStaticMeshConfig UEidoverseAvatarAsset::StaticGeometryConfig()
{
    FglTFRuntimeStaticMeshConfig Config;
    // The reflected import basis already converts CCW to UE clockwise faces.
    // Only a positive-determinant basis would need a separate winding reversal.
    // Mirrored node transforms are handled in StaticGeometryConfigFor.
    Config.bReverseWinding=ImportConfig().BasisMatrix.Determinant()>0;
    Config.bAllowCPUAccess=true;
    Config.bBuildLumenCards=true;
    return Config;
}
bool UEidoverseAvatarAsset::UnmirrorDocument(const TSharedPtr<FJsonObject>& Doc)
{
    // glTF requires flipping winding under mirrored node transforms; glTFRuntime
    // does not, and also inverts normals there. Import such a model un-mirrored
    // (one extra mirror root) and let the component's negative scale mirror it
    // back: Unreal culls, shades and collides negatively scaled meshes correctly.
    if(!Doc||!MostlyMirrored(Doc))return false;
    TArray<TSharedPtr<FJsonValue>> Nodes=Array(Doc,TEXT("nodes"));
    TArray<TSharedPtr<FJsonValue>> Scenes=Array(Doc,TEXT("scenes"));
    double SceneIndex=0;Doc->TryGetNumberField(TEXT("scene"),SceneIndex);
    if(!Scenes.IsValidIndex(int32(SceneIndex))||!Scenes[int32(SceneIndex)]->AsObject())return false;
    auto Scene=Scenes[int32(SceneIndex)]->AsObject();
    auto Root=MakeShared<FJsonObject>();
    Root->SetStringField(TEXT("name"),TEXT("eidoverse-unmirror"));
    Root->SetArrayField(TEXT("scale"),{MakeShared<FJsonValueNumber>(-1),MakeShared<FJsonValueNumber>(1),MakeShared<FJsonValueNumber>(1)});
    Root->SetArrayField(TEXT("children"),Array(Scene,TEXT("nodes")));
    Nodes.Add(MakeShared<FJsonValueObject>(Root));
    Doc->SetArrayField(TEXT("nodes"),Nodes);
    Scene->SetArrayField(TEXT("nodes"),{MakeShared<FJsonValueNumber>(Nodes.Num()-1)});
    return true;
}
bool UEidoverseAvatarAsset::ReplaceGlbJson(TArray<uint8>& Bytes,const TSharedPtr<FJsonObject>& Doc)
{
    if(Bytes.Num()<20)return false;
    const uint32 JsonLength=Word(Bytes,12);
    if(20+int64(JsonLength)>Bytes.Num())return false;
    FString Text;
    if(!FJsonSerializer::Serialize(Doc.ToSharedRef(),TJsonWriterFactory<TCHAR,TCondensedJsonPrintPolicy<TCHAR>>::Create(&Text)))return false;
    FTCHARToUTF8 Utf8(*Text);
    TArray<uint8> Json((const uint8*)Utf8.Get(),Utf8.Length());
    while(Json.Num()%4)Json.Add(' ');
    TArray<uint8> Out;
    Out.Append(Bytes.GetData(),12);
    const uint32 NewLength=Json.Num(),Type=0x4E4F534A;
    Out.Append((const uint8*)&NewLength,4);Out.Append((const uint8*)&Type,4);Out.Append(Json);
    Out.Append(Bytes.GetData()+20+JsonLength,Bytes.Num()-20-JsonLength);
    const uint32 Total=Out.Num();FMemory::Memcpy(Out.GetData()+8,&Total,4);
    Bytes=MoveTemp(Out);
    return true;
}
void UEidoverseAvatarAsset::Decode(const TArray<uint8>& Bytes)
{
    // Validation (JSON + texture headers), un-mirroring and the glTF parse touch no
    // UObjects: run them on a worker. A large body parsed on the game thread cost
    // 100-150 ms of frame. Only adopting the parser comes back to the game thread.
    FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper")); // never first-load off the game thread
    AddToRoot();bRootedForDecode=true;
    TWeakObjectPtr<UEidoverseAvatarAsset> Weak(this);
    const bool bStatic=bStaticModel;const FglTFRuntimeConfig Config=ImportConfig();const double T0=FPlatformTime::Seconds();
    UE::Tasks::Launch(UE_SOURCE_LOCATION,[Weak,Bytes,bStatic,Config,T0]()
    {
        TSharedPtr<FJsonObject> J;FString Error;bool bMirror=false;TSharedPtr<FglTFRuntimeParser> Parser;
        if(ValidateGlb(Bytes,J,Error,!bStatic))
        {
            TArray<uint8> Source=Bytes;
            bMirror=bStatic&&UnmirrorDocument(J)&&ReplaceGlbJson(Source,J);
            Parser=FglTFRuntimeParser::FromData(bMirror?Source:Bytes,Config);
            if(!Parser)Error=TEXT("GLB parser rejected avatar");
        }
        const double Ms=(FPlatformTime::Seconds()-T0)*1000;
        AsyncTask(ENamedThreads::GameThread,[Weak,J,Error,bMirror,Parser,Ms]()
        {
            UEidoverseAvatarAsset* Self=Weak.Get();if(!Self)return;
            if(Self->bRootedForDecode){Self->RemoveFromRoot();Self->bRootedForDecode=false;}
            if(Self->bCancelled)return;
            EidoLoadTrace::Note(TEXT("decode(worker wall)"),Self->Url,Ms);
            if(!Parser){Self->Fail(Error);return;}
            Self->bMirrored=bMirror;
            Self->FinishDecode(J,Parser.ToSharedRef());
        });
    });
}
void UEidoverseAvatarAsset::FinishDecode(const TSharedPtr<FJsonObject>& J,TSharedRef<FglTFRuntimeParser> Parser)
{
    EidoLoadTrace::FScope Scope(bStaticModel?TEXT("model-decode(game)"):TEXT("avatar-decode(game)"),Url);
    Loader=NewObject<UglTFRuntimeAsset>();
    if(!Loader->SetParser(Parser)){Loader=nullptr;Fail(TEXT("GLB parser rejected avatar"));return;}
    DecodedAt=FPlatformTime::Seconds();
    if(!PrepareLitMaterials(J)){Fail(TEXT("lit avatar material templates missing; cook /Game/Underwater/Avatars"));return;}
    if(bStaticModel)
    {
        auto MeshConfig=StaticGeometryConfig();MeshConfig.MaterialsConfig=MaterialConfig();
        // Workers must not touch a mesh owned by a world being torn down.
        // Adopt completed meshes and cook collision on the game thread below.
        MeshConfig.bAllowCPUAccess=true;
        MeshConfig.bBuildComplexCollision=false;MeshConfig.CollisionComplexity=CTF_UseDefault;
        FglTFRuntimeStaticMeshAsync Done;Done.BindDynamic(this,&UEidoverseAvatarAsset::StaticMeshReady);
        AddToRoot();bRootedForBuild=true;
        Loader->LoadStaticMeshRecursiveAsync(TEXT(""),{},Done,MeshConfig);return;
    }
    auto Extensions=Object(J,TEXT("extensions"));auto VRM1=Object(Extensions,TEXT("VRMC_vrm"));auto VRM0=Object(Extensions,TEXT("VRM"));bVRM0=!VRM1&&VRM0.IsValid();
    auto AddBone=[this](const FString& Human,int32 Index){FglTFRuntimeNode N;if(Loader->GetNode(Index,N))Humanoid.Add(FName(Human),FName(N.Name));};
    if(auto Human=Object(Object(VRM1,TEXT("humanoid")),TEXT("humanBones")))
        for(auto& B:Human->Values){auto O=B.Value->AsObject();double I;if(O&&O->TryGetNumberField(TEXT("node"),I)&&I>=0&&I<512)AddBone(FString(B.Key),int32(I));}
    for(auto& B:Array(Object(VRM0,TEXT("humanoid")),TEXT("humanBones")))
    {
        auto O=B->AsObject();double I;FString Name;
        if(!O||!O->TryGetStringField(TEXT("bone"),Name)||!O->TryGetNumberField(TEXT("node"),I)||I<0||I>=512)continue;
        // VRM 0.x thumbs are one joint "higher" than VRM 1.0's (three-vrm maps them so).
        if(Name.EndsWith(TEXT("ThumbProximal")))Name=Name.Replace(TEXT("ThumbProximal"),TEXT("ThumbMetacarpal"));
        else if(Name.EndsWith(TEXT("ThumbIntermediate")))Name=Name.Replace(TEXT("ThumbIntermediate"),TEXT("ThumbProximal"));
        AddBone(Name,int32(I));
    }
    Springs=FEidoSpringData::Parse(J,[this](int32 Index){FglTFRuntimeNode N;return Loader->GetNode(Index,N)?N.Name:FString();});
    ParseExpressions(J);
    TArray<FString> Exclude;for(auto& N:Loader->GetNodes())if(N.MeshIndex>=0&&N.SkinIndex<0)Exclude.Add(N.Name);
    FglTFRuntimeSkeletalMeshConfig MeshConfig;MeshConfig.MaterialsConfig=MaterialConfig();MeshConfig.bDisableMorphTargets=false; // VRM expressions (blink, mouth) drive blend shapes
    MeshConfig.bAllowCPUAccess=true; // Infer emitter bounds and skin attachment after decoding.
    FglTFRuntimeSkeletalMeshAsync Done;Done.BindDynamic(this,&UEidoverseAvatarAsset::MeshReady);
    // Keep the loader and callback alive even if a peer leaves during worker decoding.
    AddToRoot();bRootedForBuild=true;
    Loader->LoadSkeletalMeshRecursiveAsync(TEXT(""),Exclude,Done,MeshConfig);
}
void UEidoverseAvatarAsset::StaticMeshReady(UStaticMesh* Result)
{
    if(bRootedForBuild){RemoveFromRoot();bRootedForBuild=false;}
    Loader=nullptr;if(bCancelled)return;
    if(!Result){Fail(TEXT("static model construction failed"));return;}
    UObject* Owner=StaticMeshOuter.Get();
    if(!IsValid(Owner)){Fail(TEXT("static model scene closed"));return;}
    Result->Rename(nullptr,Owner,REN_DontCreateRedirectors|REN_DoNotDirty|REN_NonTransactional);
    EidoLoadTrace::Note(TEXT("model-build(wall)"),Url,(FPlatformTime::Seconds()-DecodedAt)*1000);
    auto* Body=Result->GetBodySetup();
    if(!Body){StaticMesh=Result;State=EState::Ready;return;}
    // Cook the exact collision on a worker: synchronous cooking of a large model held
    // the game thread for up to half a second. The model counts as loaded (and is
    // shown) only once its collision exists, so nobody walks through it meanwhile.
    Body->bNeverNeedsCookedCollisionData=false;
    Body->CollisionTraceFlag=CTF_UseComplexAsSimple;
    Body->InvalidatePhysicsData();
    PendingStaticMesh=Result;CookStartedAt=FPlatformTime::Seconds();
    AddToRoot();bRootedForBuild=true;
    TWeakObjectPtr<UEidoverseAvatarAsset> Weak(this);
    Body->CreatePhysicsMeshesAsync(FOnAsyncPhysicsCookFinished::CreateLambda([Weak](bool bOk)
    {
        UEidoverseAvatarAsset* Self=Weak.Get();if(!Self)return;
        if(Self->bRootedForBuild){Self->RemoveFromRoot();Self->bRootedForBuild=false;}
        UStaticMesh* Mesh=Self->PendingStaticMesh;Self->PendingStaticMesh=nullptr;
        if(Self->bCancelled||!Mesh)return;
        EidoLoadTrace::Note(TEXT("model-collision(async wall)"),Self->Url,(FPlatformTime::Seconds()-Self->CookStartedAt)*1000);
        if(!bOk)UE_LOG(LogTemp,Warning,TEXT("EIDOVERSE model collision cook failed; shown without collision: %s"),*Self->Url);
        Self->StaticMesh=Mesh;Self->State=EState::Ready;
    }));
}
void UEidoverseAvatarAsset::MeshReady(USkeletalMesh* Result)
{
    if(bRootedForBuild){RemoveFromRoot();bRootedForBuild=false;}
    if(bCancelled){Loader=nullptr;return;}
    if(!Result){Fail(TEXT("skeletal mesh construction failed"));return;}
    EidoLoadTrace::Note(TEXT("avatar-build(wall)"),Url,(FPlatformTime::Seconds()-DecodedAt)*1000);
    EidoLoadTrace::FScope Scope(TEXT("avatar-finish"),Url);
    Mesh=Result;
    // VRMs may have rigid eyes, eyebrows and accessories parented to bones.
    // Keep their node transforms and bind them to the nearest skeletal ancestor.
    const auto& Ref=Mesh->GetRefSkeleton();TArray<FTransform> BoneCS=Ref.GetRefBonePose();
    for(int32 I=0;I<BoneCS.Num();++I)if(Ref.GetParentIndex(I)>=0)BoneCS[I]*=BoneCS[Ref.GetParentIndex(I)];
    auto StaticConfig=StaticGeometryConfig();StaticConfig.MaterialsConfig=MaterialConfig();StaticConfig.bBuildSimpleCollision=false;
    for(auto N:Loader->GetNodes())if(N.MeshIndex>=0&&N.SkinIndex<0)
    {
        FTransform Transform=N.Transform;auto At=N;int32 Bone=Ref.FindBoneIndex(FName(N.Name));
        while(At.ParentIndex>=0)
        {
            if(!Loader->GetNode(At.ParentIndex,At))break;
            Transform*=At.Transform;if(Bone<0)Bone=Ref.FindBoneIndex(FName(At.Name));
        }
        if(auto* Part=Loader->LoadStaticMesh(N.MeshIndex,StaticConfig))
        {FEidoverseAvatarAttachment A;A.Mesh=Part;A.Bone=Bone>=0?Ref.GetBoneName(Bone):NAME_None;A.Transform=Bone>=0?Transform.GetRelativeTransform(BoneCS[Bone]):Transform;Attachments.Add(A);}
    }
    BuildEmissiveLamps();
    State=EState::Ready;Loader=nullptr;
    UE_LOG(LogTemp,Display,TEXT("EIDOVERSE avatar ready: %d bones, %d humanoid mappings, %d rigid parts, %d spring joints, %d colliders"),Ref.GetNum(),Humanoid.Num(),Attachments.Num(),Springs.Joints.Num(),Springs.Colliders.Num());
    UE_LOG(LogTemp,Display,TEXT("EIDOVERSE avatar materials: %d adapted to lit shading"),AdaptedMaterialCount);
}

void UEidoverseAvatarMesh::OnComponentDestroyed(bool bDestroyingHierarchy)
{
    for(auto& Part:RigidParts)if(IsValid(Part))Part->DestroyComponent();RigidParts.Empty();
    for(auto& Light:EmissiveLights)if(IsValid(Light))Light->DestroyComponent();EmissiveLights.Empty();
    Super::OnComponentDestroyed(bDestroyingHierarchy);
}
void UEidoverseAvatarMesh::SetAvatar(UEidoverseAvatarAsset* Asset)
{
    EidoLoadTrace::FScope Scope(TEXT("avatar-apply"),Asset?Asset->Url:FString());
    for(auto& Part:RigidParts)if(Part)Part->DestroyComponent();RigidParts.Empty();Avatar=Asset;
    EmptyOverrideMaterials();
    SetSkinnedAssetAndUpdate(Asset->Mesh);SetCollisionEnabled(ECollisionEnabled::NoCollision);SetCastShadow(true);
    const auto& Ref=Asset->Mesh->GetRefSkeleton();Reference=Ref.GetRefBonePose();ReferenceComponentRotations.SetNum(Reference.Num());
    for(int32 I=0;I<Reference.Num();++I){const int32 P=Ref.GetParentIndex(I);ReferenceComponentRotations[I]=P>=0?ReferenceComponentRotations[P]*Reference[I].GetRotation():Reference[I].GetRotation();}
    for(auto& A:Asset->Attachments)
    {auto* Part=NewObject<UStaticMeshComponent>(GetOwner());GetOwner()->AddInstanceComponent(Part);Part->SetupAttachment(this,A.Bone);Part->SetRelativeTransform(A.Transform);Part->SetStaticMesh(A.Mesh);Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);Part->SetCastShadow(true);Part->RegisterComponent();RigidParts.Add(Part);}
    SetRelativeRotation(FRotator(0,Asset->bVRM0?180:0,0));
    CreateEmissiveLights();
    PrepareRetarget();
    PrepareSprings();
    PrepareWings();
}
void UEidoverseAvatarMesh::RotateBone(FName HumanBone,const FVector& Axis,float Degrees)
{
    auto* Name=Avatar->Humanoid.Find(HumanBone);if(!Name)return;const int32 I=GetBoneIndex(*Name);if(!BoneSpaceTransforms.IsValidIndex(I))return;
    const FQuat R=ReferenceComponentRotations[I];
    // Axes are body axes (forward, right, up). VRM 0.x rigs face -X in component
    // space (the component is yawed 180 to face forward), so their forward and
    // right axes are mirrored; without this every lowered arm is raised instead.
    const FVector BodyAxis=Avatar->bVRM0?FVector(-Axis.X,-Axis.Y,Axis.Z):Axis;
    // Accumulate axes rather than overwriting a previous shoulder/elbow adjustment.
    BoneSpaceTransforms[I].SetRotation((BoneSpaceTransforms[I].GetRotation()*R.Inverse()*FQuat(BodyAxis,FMath::DegreesToRadians(Degrees))*R).GetNormalized());
}

FEidoverseSwimMotion FEidoverseSwimMotion::Evaluate(const FString& Mode,float Speed,const FVector& LocalVelocity)
{
    FEidoverseSwimMotion M;
    M.bAquatic=Mode==TEXT("swim")||Mode==TEXT("surface")||Mode==TEXT("dive")||Mode==TEXT("ascend");
    if(!M.bAquatic)return M;
    FVector V=LocalVelocity;if(V.ContainsNaN())V=FVector::ZeroVector;
    const float MotionSpeed=FMath::Clamp(FMath::Max(FMath::IsFinite(Speed)?Speed:0.f,float(V.Size())),0.f,4.f);
    const float T=FMath::Clamp((MotionSpeed-.08f)/.85f,0.f,1.f);
    M.Effort=T*T*(3-2*T);M.CycleHz=FMath::Lerp(.28f,.72f,M.Effort);
    M.Surface=Mode==TEXT("surface")?1.f:0.f;
    const float Horizontal=FMath::Max(float(V.Size2D()),MotionSpeed*.3f);
    const float TravelLean=FMath::Clamp(90.f-FMath::RadiansToDegrees(FMath::Atan2(float(V.Z),Horizontal)),25.f,145.f);
    M.Lean=FMath::Lerp(8.f,TravelLean-8.f,M.Effort);
    if(V.X<-.15f)M.Lean=FMath::Min(M.Lean,30.f); // Backpedal without a head-first reversal.
    M.Bank=FMath::Clamp(float(V.Y)*-12.f,-12.f,12.f)*M.Effort;
    M.Dive=FMath::Clamp(FMath::Abs(float(V.Z))/.65f,0.f,1.f)*M.Effort;
    M.State=M.Effort<.08f?TEXT("Hovering"):TEXT("Swimming");
    if(M.Dive>.2f)M.State=V.Z<0?TEXT("Diving"):TEXT("Ascending");
    if(M.Surface>0){M.Lean=FMath::Min(M.Lean,35.f);M.Dive=0;M.State=TEXT("Surface swim");}
    return M;
}

void UEidoverseAvatarMesh::Animate(const FString& Mode,float Speed,const FVector& LocalVelocity,double Time)
{
    if(!Avatar||Reference.IsEmpty())return;
    bLastFrameFromClips=false;LastClipTime=-1;   // the next clip frame blends from this pose
    const float Dt=LastAnimationTime<0?1.f/60:FMath::Clamp(float(Time-LastAnimationTime),0.f,.1f);
    LastAnimationTime=Time;
    const auto Motion=FEidoverseSwimMotion::Evaluate(Mode,Speed,LocalVelocity);
    AnimationState=Motion.bAquatic?Motion.State:Mode;
    const float Blend=1-FMath::Exp(-Dt/0.3f);
    AquaticBlend=FMath::Lerp(AquaticBlend,Motion.bAquatic?1.f:0.f,Blend);
    BodyLeanDegrees=FMath::Lerp(BodyLeanDegrees,Motion.Lean,Blend);
    SmoothedEffort=FMath::Lerp(SmoothedEffort,Motion.Effort,Blend);
    SmoothedBank=FMath::Lerp(SmoothedBank,Motion.Bank,Blend);
    SmoothedDive=FMath::Lerp(SmoothedDive,Motion.Dive,Blend);
    SmoothedSurface=FMath::Lerp(SmoothedSurface,Motion.Surface,Blend);
    // Integrating frequency preserves stroke phase when speed/mode changes.
    CyclePhase=FMath::Fmod(CyclePhase+Dt*2*PI*Motion.CycleHz,2*PI);
    const float Stroke=FMath::Sin(CyclePhase),Pull=.5f-.5f*FMath::Cos(CyclePhase);
    const float Kick=FMath::Sin(CyclePhase*2),KickLag=FMath::Sin(CyclePhase*2-.65f);
    const TArray<FTransform> Previous=BoneSpaceTransforms;
    BoneSpaceTransforms=Reference;
    const float Water=AquaticBlend,Effort=SmoothedEffort;

    // Reference pose is upright. Rotate about the hips (not the feet), so changing
    // trim does not shift the network anchor. Neck/head keep the gaze ahead.
    RotateBone(TEXT("hips"),FVector::RightVector,BodyLeanDegrees);
    RotateBone(TEXT("hips"),FVector::ForwardVector,SmoothedBank+Water*Effort*Stroke*2);
    RotateBone(TEXT("spine"),FVector::RightVector,Water*(-4+Effort*Stroke*2));
    RotateBone(TEXT("neck"),FVector::RightVector,-BodyLeanDegrees*.32f);
    RotateBone(TEXT("head"),FVector::RightVector,-BodyLeanDegrees*.38f);

    // Hover: hands scull gently in front of the torso, with bent knees.
    // Cruise: symmetrical reach/pull/recovery, with alternating flutter kicks.
    // Vertical travel: arms streamline overhead, leaving the legs to propel.
    const float HoverArm=-48+Stroke*8;
    const float CruiseArm=FMath::Lerp(75-100*Pull,-20+Stroke*25,SmoothedSurface);
    const float Arm=FMath::Lerp(FMath::Lerp(HoverArm,CruiseArm,Effort),78.f,SmoothedDive);
    const float Elbow=FMath::Lerp(65+Stroke*12,18+60*Pull,Effort)*(1-SmoothedDive*.8f);
    const float LandSwing=FMath::Sin(Time*5)*FMath::Clamp(Speed,0.f,1.5f);
    for(int32 Side:{-1,1})
    {
        const bool Left=Side==1;
        const FName UpperArm=Left?TEXT("leftUpperArm"):TEXT("rightUpperArm");
        const FName LowerArm=Left?TEXT("leftLowerArm"):TEXT("rightLowerArm");
        // The angles read "negative lowers, positive raises" (land -65 down from the
        // T-pose, hover -48 sculling low, dive +78 streamlined overhead); a positive
        // rotation about the body's forward axis raises the left arm, hence the sign.
        RotateBone(UpperArm,FVector::ForwardVector,-Side*FMath::Lerp(-65.f,Arm,Water));
        RotateBone(LowerArm,FVector::UpVector,-Side*Water*Elbow);
        RotateBone(Left?TEXT("leftHand"):TEXT("rightHand"),FVector::RightVector,Water*(Stroke*12-8));

        const float LegSwing=Side*Kick*FMath::Lerp(5.f,18.f,Effort);
        const float Thigh=FMath::Lerp(-15.f,0.f,Effort)+LegSwing;
        const float Knee=FMath::Lerp(28.f,8.f,Effort)+FMath::Max(0.f,-Side*KickLag)*FMath::Lerp(8.f,28.f,Effort);
        RotateBone(Left?TEXT("leftUpperLeg"):TEXT("rightUpperLeg"),FVector::RightVector,FMath::Lerp(Side*LandSwing*22,Thigh,Water));
        RotateBone(Left?TEXT("leftLowerLeg"):TEXT("rightLowerLeg"),FVector::RightVector,FMath::Lerp(FMath::Max(0.f,-Side*LandSwing)*25,Knee,Water));
        RotateBone(Left?TEXT("leftFoot"):TEXT("rightFoot"),FVector::RightVector,Water*(-18+Side*KickLag*9));
        if(Mode==TEXT("climb"))RotateBone(UpperArm,FVector::RightVector,(120+Side*LandSwing*20)*(1-Water));
    }
    // Blend all joint rotations as well as trim, including land/water transitions.
    // This also prevents a hot-swapped asset from retaining another rig's pose.
    if(Previous.Num()==BoneSpaceTransforms.Num())
    {
        const float JointBlend=1-FMath::Exp(-Dt/.085f);
        for(int32 I=0;I<BoneSpaceTransforms.Num();++I)
            BoneSpaceTransforms[I].SetRotation(FQuat::Slerp(Previous[I].GetRotation(),BoneSpaceTransforms[I].GetRotation(),JointBlend).GetNormalized());
    }
    UpdateSprings(Dt);
    ApplyExpressions();
    MarkRefreshTransformDirty();
}

// VRM expressions -> imported blend shape names (glTFRuntime names a morph by the mesh's
// extras.targetNames, else MorphTarget_<index>). VRM 1.0: expressions.preset/custom
// morphTargetBinds {node, index, weight 0..1}; VRM 0.x: blendShapeMaster groups by
// presetName with binds {mesh, index, weight 0..100}. 0.x presets are renamed to 1.0's.
void UEidoverseAvatarAsset::ParseExpressions(const TSharedPtr<FJsonObject>& J)
{
    Expressions.Empty();
    const auto& MeshList=Array(J,TEXT("meshes"));const auto& Nodes=Array(J,TEXT("nodes"));
    auto MorphName=[&MeshList](int32 MeshIndex,int32 Index)->FString
    {
        if(!MeshList.IsValidIndex(MeshIndex)||Index<0||Index>1024)return FString();
        const auto& Names=Array(Object(MeshList[MeshIndex]->AsObject(),TEXT("extras")),TEXT("targetNames"));
        if(Names.IsValidIndex(Index))return Names[Index]->AsString();
        return FString::Printf(TEXT("MorphTarget_%d"),Index);
    };
    auto Add=[this](const FString& Expression,const FString& Morph,float Weight)
    {if(!Morph.IsEmpty()&&Weight>0)Expressions.FindOrAdd(FName(Expression)).Add({FName(Morph),FMath::Min(Weight,1.f)});};
    const auto Ext=Object(J,TEXT("extensions"));
    if(const auto Vrm1=Object(Ext,TEXT("VRMC_vrm")))
    {
        const auto Exprs=Object(Vrm1,TEXT("expressions"));
        for(const TCHAR* Group:{TEXT("preset"),TEXT("custom")})
            if(const auto G=Object(Exprs,Group))
                for(const auto& E:G->Values)
                    for(const auto& B:Array(E.Value->AsObject(),TEXT("morphTargetBinds")))
                    {
                        const auto O=B->AsObject();double Node=-1,Index=-1,W=0;
                        if(!O||!O->TryGetNumberField(TEXT("node"),Node)||!O->TryGetNumberField(TEXT("index"),Index))continue;
                        O->TryGetNumberField(TEXT("weight"),W);
                        double MeshIndex=-1;if(Nodes.IsValidIndex(int32(Node)))Nodes[int32(Node)]->AsObject()->TryGetNumberField(TEXT("mesh"),MeshIndex);
                        Add(FString(E.Key),MorphName(int32(MeshIndex),int32(Index)),float(W));
                    }
    }
    else if(const auto Vrm0=Object(Ext,TEXT("VRM")))
    {
        static const TMap<FString,FString> Rename={{TEXT("a"),TEXT("aa")},{TEXT("i"),TEXT("ih")},{TEXT("u"),TEXT("ou")},{TEXT("e"),TEXT("ee")},{TEXT("o"),TEXT("oh")},
            {TEXT("blink_l"),TEXT("blinkLeft")},{TEXT("blink_r"),TEXT("blinkRight")},{TEXT("joy"),TEXT("happy")},{TEXT("sorrow"),TEXT("sad")},{TEXT("fun"),TEXT("relaxed")},
            {TEXT("lookup"),TEXT("lookUp")},{TEXT("lookdown"),TEXT("lookDown")},{TEXT("lookleft"),TEXT("lookLeft")},{TEXT("lookright"),TEXT("lookRight")}};
        for(const auto& G:Array(Object(Vrm0,TEXT("blendShapeMaster")),TEXT("blendShapeGroups")))
        {
            const auto O=G->AsObject();FString Preset;if(!O)continue;
            if(!O->TryGetStringField(TEXT("presetName"),Preset)||Preset.IsEmpty()||Preset==TEXT("unknown"))O->TryGetStringField(TEXT("name"),Preset);
            Preset=Preset.ToLower();if(const FString* R=Rename.Find(Preset))Preset=*R;
            for(const auto& B:Array(O,TEXT("binds")))
            {
                const auto BO=B->AsObject();double MeshIndex=-1,Index=-1,W=0;
                if(!BO||!BO->TryGetNumberField(TEXT("mesh"),MeshIndex)||!BO->TryGetNumberField(TEXT("index"),Index))continue;
                BO->TryGetNumberField(TEXT("weight"),W);
                Add(Preset,MorphName(int32(MeshIndex),int32(Index)),float(W/100.0));
            }
        }
    }

    // Look-at (three-vrm VRMLookAt): bone or expression type, with range maps
    // {input degrees, output degrees|weight}.
    auto Range=[](const TSharedPtr<FJsonObject>& M,const TCHAR* In,const TCHAR* Out,FVector2f Default)
    {double I=Default.X,O=Default.Y;if(M){M->TryGetNumberField(In,I);M->TryGetNumberField(Out,O);}return FVector2f(float(FMath::Max(I,1e-3)),float(O));};
    if(const auto Vrm1=Object(Ext,TEXT("VRMC_vrm")))
    {
        const auto L=Object(Vrm1,TEXT("lookAt"));FString Type;
        if(L)L->TryGetStringField(TEXT("type"),Type);
        bLookAtExpressions=Type==TEXT("expression");
        const FVector2f D(90,bLookAtExpressions?1.f:10.f);
        LookHorizontalInner=Range(Object(L,TEXT("rangeMapHorizontalInner")),TEXT("inputMaxValue"),TEXT("outputScale"),D);
        LookHorizontalOuter=Range(Object(L,TEXT("rangeMapHorizontalOuter")),TEXT("inputMaxValue"),TEXT("outputScale"),D);
        LookVerticalDown=Range(Object(L,TEXT("rangeMapVerticalDown")),TEXT("inputMaxValue"),TEXT("outputScale"),D);
        LookVerticalUp=Range(Object(L,TEXT("rangeMapVerticalUp")),TEXT("inputMaxValue"),TEXT("outputScale"),D);
    }
    else if(const auto Vrm0=Object(Ext,TEXT("VRM")))
    {
        const auto F=Object(Vrm0,TEXT("firstPerson"));FString Type;
        if(F)F->TryGetStringField(TEXT("lookAtTypeName"),Type);
        bLookAtExpressions=Type==TEXT("BlendShape");
        const FVector2f D(90,bLookAtExpressions?1.f:10.f);
        LookHorizontalInner=Range(Object(F,TEXT("lookAtHorizontalInner")),TEXT("xRange"),TEXT("yRange"),D);
        LookHorizontalOuter=Range(Object(F,TEXT("lookAtHorizontalOuter")),TEXT("xRange"),TEXT("yRange"),D);
        LookVerticalDown=Range(Object(F,TEXT("lookAtVerticalDown")),TEXT("xRange"),TEXT("yRange"),D);
        LookVerticalUp=Range(Object(F,TEXT("lookAtVerticalUp")),TEXT("xRange"),TEXT("yRange"),D);
    }
    if(bLookAtExpressions&&!Expressions.Contains(FName(TEXT("lookLeft")))&&!Expressions.Contains(FName(TEXT("lookUp"))))bLookAtExpressions=false;
}
