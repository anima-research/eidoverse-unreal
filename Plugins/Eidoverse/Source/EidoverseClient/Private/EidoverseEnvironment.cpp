#include "Materials/MaterialParameterCollection.h"
#include "Materials/MaterialParameterCollectionInstance.h"
#include "EidoverseScene.h"
#include "EidoverseBridge.h"
#include "EidoverseGroundCover.h"
#include "Dom/JsonObject.h"
#include "ProceduralMeshComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"

namespace EidoEnv
{
using Array=TArray<TSharedPtr<FJsonValue>>;
const Array& Arr(const TSharedPtr<FJsonObject>& O,const TCHAR* K){static Array Empty;const Array* A=nullptr;return O&&O->TryGetArrayField(K,A)?*A:Empty;}
TSharedPtr<FJsonObject> Obj(const TSharedPtr<FJsonObject>& O,const TCHAR* K){const TSharedPtr<FJsonObject>* V=nullptr;return O&&O->TryGetObjectField(K,V)?*V:nullptr;}
double Num(const TSharedPtr<FJsonObject>& O,const TCHAR* K,double D=0){double V;return O&&O->TryGetNumberField(K,V)&&FMath::IsFinite(V)?V:D;}
FString Str(const TSharedPtr<FJsonObject>& O,const TCHAR* K){FString V;if(O)O->TryGetStringField(K,V);return V;}
struct Geometry
{
    TArray<FVector> P,N;TArray<int32> I;TArray<FVector2D> UV;TArray<FLinearColor> C;
    float RootZ=0,Height=100,Wind=1;
    void Tri(FVector A,FVector B,FVector D,FLinearColor Color,float Tip=1)
    {
        const int K=P.Num();P.Append({A,D,B});I.Append({K,K+1,K+2});const FVector Normal=FVector::CrossProduct(B-A,D-A).GetSafeNormal();N.Append({Normal,Normal,Normal});
        for(FVector V:{A,D,B})UV.Add(FVector2D(Wind,FMath::Clamp((V.Z-RootZ)/Height,0.,1.)));C.Append({Color*.65f,Color,Color*.65f});
    }
};
}
using namespace EidoEnv;

void AEidoverseScene::BuildFlora(const TSharedPtr<FJsonObject>& Data,const FVector& Origin)
{
    for(auto& C:Flora)if(C)C->DestroyComponent();Flora.Empty();FloraCount=0;
    if(GroundCover){GroundCover->DestroyComponent();GroundCover=nullptr;}
    auto* CoverMaterial=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Underwater/Avatars/Materials/M_EidoverseGroundCover"));
    if(CoverMaterial&&!GroundCoverMesh)GroundCoverMesh=EidoGroundCover::BuildTuft(this,CoverMaterial);
    TArray<FTransform> CoverTransforms;TArray<FLinearColor> CoverColors;TArray<float> CoverWind;
    TMap<FIntPoint,Geometry> Tiles;
    TArray<FBox> ClearingBounds;for(auto& E:Items)if(E.Value.Actor.IsValid())ClearingBounds.Add(E.Value.Actor->GetComponentsBoundingBox());
    for(auto& S:Arr(Data,TEXT("strokes")))if(auto St=S->AsObject())
    {
        const FString Species=Str(St,TEXT("species")),Arch=Str(St,TEXT("archetype"));
        const bool Blade=Arch==TEXT("blades"),Flower=Species==TEXT("sunflower"),Corn=Species==TEXT("corn"),Yucca=Species==TEXT("yucca");
        FLinearColor Color=Species==TEXT("galleta_dry")?FLinearColor(.31,.26,.09):Species==TEXT("sagebrush")?FLinearColor(.22,.31,.23):FLinearColor(.08,.22,.035);
        const auto& Tint=Arr(St,TEXT("color"));if(Tint.Num()==3)Color*=FLinearColor(FMath::Clamp(Tint[0]->AsNumber(),.2,2.5),FMath::Clamp(Tint[1]->AsNumber(),.2,2.5),FMath::Clamp(Tint[2]->AsNumber(),.2,2.5));
        const float H=FMath::Clamp(Num(St,TEXT("height"),.38),.03,5.)*100;
        for(auto& V:Arr(St,TEXT("placements")))if(auto P=V->AsObject())
        {
            if(FloraCount>=12000)break;
            FVector Base=AEidoverseBridge::FromWirePosition(FVector(Num(P,TEXT("x")),Num(P,TEXT("y")),Num(P,TEXT("z"))));
            bool Covered=false;const FVector WorldBase=Origin+Base;
            for(auto& B:ClearingBounds)if(B.IsInsideXY(WorldBase))
            {FHitResult Hit;FCollisionQueryParams Q;Q.bTraceComplex=true;Q.AddIgnoredActor(this);if(GetWorld()->LineTraceSingleByChannel(Hit,WorldBase+FVector(0,0,500),WorldBase-FVector(0,0,5),ECC_Visibility,Q)&&!EntityId(Hit.GetActor()).IsEmpty()){Covered=true;break;}}
            if(Covered)continue;++FloraCount;
            const float Scale=FMath::Clamp(Num(P,TEXT("scale"),1),.1,4.),Yaw=Num(P,TEXT("yaw")),Phase=Num(P,TEXT("phase"));
            if(Blade&&GroundCoverMesh)
            {
                // Preserve server-authored placement/height. Fuller shared tufts,
                // not additional world entities or per-frame placement traces.
                // Yaw, then the world tilt (vegetation.js: about wire X by tx, then wire Z by tz).
                const double Tx=Num(P,TEXT("tx")),Tz=Num(P,TEXT("tz"));
                const FQuat Wire=FQuat(FVector::ZAxisVector,Tz)*FQuat(FVector::XAxisVector,Tx)*FQuat(FVector::YAxisVector,Yaw);
                CoverTransforms.Add(FTransform(AEidoverseBridge::FromWireRotation(Wire),Base,FVector(Scale,Scale,Scale*H/38)));
                const float Variation=FMath::Clamp(Num(P,TEXT("colorVar"),.5),0.,1.);
                CoverColors.Add(Color*FMath::Lerp(FLinearColor(.74,.86,.66),FLinearColor(1.3,1.08,.80),Variation));
                CoverWind.Add(FMath::Clamp(Num(St,TEXT("wind"),1),0.,2.));
                continue;
            }
            auto& G=Tiles.FindOrAdd(FIntPoint(FMath::FloorToInt(Base.X/1600),FMath::FloorToInt(Base.Y/1600)));
            G.RootZ=Base.Z;G.Height=(Blade?H:Yucca?80:Flower?170:Corn?200:85)*Scale;G.Wind=FMath::Clamp(Num(St,TEXT("wind"),1),0.,2.);
            FRandomStream R(int32((Phase+1)*10000));Color.A=1;
            if(Blade||Yucca)
            {
                const int Count=Yucca?14:8;
                for(int B=0;B<Count;++B)
                {
                    const float Angle=Yaw+B*2.39996f;const FVector D(FMath::Cos(Angle),FMath::Sin(Angle),0),Side(-D.Y,D.X,0);
                    FVector Foot=Base+D*R.FRandRange(0,Blade?28:8)*Scale;
                    const float Height=(Yucca?80:H)*Scale*R.FRandRange(.65,1.3),Width=(Yucca?5:1.8)*Scale;
                    FVector Mid=Foot+D*Height*.15+FVector(0,0,Height*.55),Tip=Foot+D*Height*(Yucca?.65:.4)+FVector(0,0,Height);
                    G.Tri(Foot-Side*Width,Foot+Side*Width,Mid+Side*Width*.7,Color,.55);G.Tri(Foot-Side*Width,Mid+Side*Width*.7,Mid-Side*Width*.7,Color,.55);G.Tri(Mid-Side*Width*.7,Mid+Side*Width*.7,Tip,Color);
                }
            }
            else
            {
                const float Height=(Flower?170:Corn?200:85)*Scale;const FVector Top=Base+FVector(0,0,Height);
                for(int B=0;B<3;++B){float A=B*PI/3;FVector Side(FMath::Cos(A),FMath::Sin(A),0);G.Tri(Base-Side*1.3*Scale,Base+Side*1.3*Scale,Top,FLinearColor(.15,.10,.035));}
                for(int B=0;B<(Flower?8:Corn?12:32);++B)
                {
                    const float A=Yaw+B*2.39996f;const FVector D(FMath::Cos(A),FMath::Sin(A),0),Side(-D.Y,D.X,0);
                    FVector Stem=Base+FVector(0,0,Height*R.FRandRange(.25,.92));float Len=(Flower?26:Corn?48:35)*Scale;
                    FVector Tip=Stem+D*Len+FVector(0,0,Len*.35);G.Tri(Stem,Stem+D*Len*.45+Side*Len*.2,Tip,Color);G.Tri(Stem,Tip,Stem+D*Len*.45-Side*Len*.2,Color);
                }
                if(Flower)for(int B=0;B<12;++B)
                {float A=B*PI/6;FVector D(0,FMath::Cos(A),FMath::Sin(A)),Side(0,-D.Z,D.Y);G.Tri(Top,Top+D*18*Scale+Side*4*Scale,Top+D*18*Scale-Side*4*Scale,FLinearColor(.85,.5,.015));}
            }
        }
    }
    if(!CoverTransforms.IsEmpty())
    {
        GroundCover=NewObject<UHierarchicalInstancedStaticMeshComponent>(this);AddInstanceComponent(GroundCover);
        GroundCover->SetupAttachment(RootComponent);GroundCover->SetWorldLocation(Origin);GroundCover->SetStaticMesh(GroundCoverMesh);
        GroundCover->SetCollisionEnabled(ECollisionEnabled::NoCollision);GroundCover->SetCastShadow(false);GroundCover->SetVisibleInRayTracing(false);
        GroundCover->SetCullDistances(8800,9000);   // keep(d) thins 15-90 m in the material; nothing past 90 mGroundCover->NumCustomDataFloats=5;GroundCover->bAutoRebuildTreeOnInstanceChanges=false;
        GroundCover->RegisterComponent();GroundCover->AddInstances(CoverTransforms,false,false);
        GroundCover->SetNumCustomDataFloats(5);   // sizes the per-instance custom data for every instance
        for(int32 I=0;I<CoverTransforms.Num();++I)
        {
            const auto& C=CoverColors[I];const float Custom[]={C.R,C.G,C.B,CoverWind[I],float(50*CoverTransforms[I].GetScale3D().Z)};
            GroundCover->SetCustomData(I,MakeArrayView(Custom),false);
        }
        GroundCover->BuildTreeIfOutdated(false,true);GroundCover->MarkRenderStateDirty();
    }
    auto* Base=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Underwater/Avatars/Materials/M_EidoverseFlora"));
    for(auto& Pair:Tiles)
    {
        auto* C=NewObject<UProceduralMeshComponent>(this);AddInstanceComponent(C);C->SetupAttachment(RootComponent);C->SetWorldLocation(Origin);C->SetCollisionEnabled(ECollisionEnabled::NoCollision);C->SetCastShadow(false);C->SetCullDistance(8500);C->RegisterComponent();
        auto& G=Pair.Value;C->CreateMeshSection_LinearColor(0,G.P,G.I,G.N,G.UV,G.C,{},false);C->SetMaterial(0,Base?Base:Material(TEXT("terrain")));Flora.Add(C);
    }
}
void AEidoverseScene::SetPreviewHour(float Hour)
{
    if(!FMath::IsFinite(Hour))return;
    PreviewHour=Hour<0?-1:FMath::Fmod(Hour,24.f);
    if(LastAtmosphere)ApplyWeather(LastAtmosphere);
}
void AEidoverseScene::ApplyWeather(const TSharedPtr<FJsonObject>& Atmosphere)
{
    if(!Atmosphere)return;
    LastAtmosphere=Atmosphere;
    auto Clock=Obj(Atmosphere,TEXT("clock")),W=Obj(Atmosphere,TEXT("weather"));WeatherHour=PreviewHour>=0?PreviewHour:Num(Clock,TEXT("hour"),12);
    WeatherState=Str(W,TEXT("weather"));if(WeatherState.IsEmpty())WeatherState=TEXT("clear");
    const float K=FMath::Clamp(Num(W,TEXT("k"),1),0.,1.);
    // materials.js: WX_COVER / WET by weather, CLOUD_COVER by the authored cloud kind.
    auto RainOf=[](const FString& S){return S.Contains(TEXT("storm"))||S==TEXT("cyclone")?1.f:S==TEXT("rain")?.65f:S==TEXT("sunshower")?.25f:0.f;};
    auto CoverOf=[](const FString& S){static const TMap<FString,float> M={{TEXT("fair"),.18f},{TEXT("sunshower"),.35f},{TEXT("overcast"),.85f},{TEXT("rain"),.9f},{TEXT("storm"),.92f},{TEXT("cyclone"),.96f},{TEXT("darkstorm"),1.f}};const float* V=M.Find(S);return V?*V:0.f;};
    auto WetOf=[](const FString& S){static const TMap<FString,float> M={{TEXT("sunshower"),.55f},{TEXT("overcast"),.15f},{TEXT("rain"),.85f},{TEXT("storm"),1.f},{TEXT("cyclone"),1.f},{TEXT("darkstorm"),1.f}};const float* V=M.Find(S);return V?*V:0.f;};
    RainAmount=RainOf(WeatherState)*K;float CoverW=CoverOf(WeatherState)*K;WetTarget=WetOf(WeatherState)*K;
    if(Str(W,TEXT("source"))==TEXT("forecast"))if(auto Seg=Obj(W,TEXT("seg")))
    {
        const float Mix=FMath::Clamp((Num(Atmosphere,TEXT("time"))-Num(Seg,TEXT("startMs")))/FMath::Max(1.,Num(W,TEXT("seconds"))*1000),0.,1.);
        const FString Prev=Str(Seg,TEXT("prevState"));
        RainAmount=FMath::Lerp(RainOf(Prev)*K,RainAmount,Mix);CoverW=FMath::Lerp(CoverOf(Prev)*K,CoverW,Mix);WetTarget=FMath::Lerp(WetOf(Prev)*K,WetTarget,Mix);
    }
    static const TMap<FString,float> CloudKind={{TEXT("cumulus"),.38f},{TEXT("stratus"),.6f},{TEXT("cirrus"),.15f}};
    const float* KindCover=CloudKind.Find(Str(SkyArgs,TEXT("clouds")));
    CoverTarget=FMath::Max(KindCover?*KindCover:0.f,CoverW);
    if(!bWeatherEased){CloudAmount=CoverTarget;Wetness=WetTarget;bWeatherEased=true;}
    ApplyLightingNow();
    if(!Clouds)
    {
        if(auto* Base=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Underwater/Avatars/Materials/M_EidoverseClouds")))
        {CloudMaterial=UMaterialInstanceDynamic::Create(Base,this);Clouds=NewObject<UStaticMeshComponent>(this);AddInstanceComponent(Clouds);Clouds->SetupAttachment(RootComponent);Clouds->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Sphere.Sphere")));Clouds->SetRelativeScale3D(FVector(1000));Clouds->SetCollisionEnabled(ECollisionEnabled::NoCollision);Clouds->SetCastShadow(false);Clouds->SetTranslucentSortPriority(-100);Clouds->SetMaterial(0,CloudMaterial);Clouds->RegisterComponent();}
    }
    ApplyLightingNow();
    // Aquatic worlds: keep the spectral rig and atmosphere-only sun in step with the weather.
    if(!WaterRig.IsEmpty())UpdateWaterLighting(true);
}
void AEidoverseScene::ApplyLightingNow()
{
    // sky.js fallback law: day = max(0, sin((h-6)/12 pi)), warmth = (1-day)^1.5; sun and sky
    // fill scaled by sky.sun / sky.ambient; exposure (1-.22 warmth) x sky.exposure; nights are
    // not black (a cool moon floor stands in for the browser's never-off sun).
    const float SunK=FMath::Clamp(Num(SkyArgs,TEXT("sun"),1),0.,2.5),AmbK=FMath::Clamp(Num(SkyArgs,TEXT("ambient"),1),0.,2.5);
    const float FillK=FMath::Clamp(Num(SkyArgs,TEXT("fill"),1),0.,2.5),ExpK=FMath::Clamp(Num(SkyArgs,TEXT("exposure"),1),.3,1.8),FogK=FMath::Clamp(Num(SkyArgs,TEXT("fog"),1),0.,3.);
    const float Azimuth=Num(SkyArgs,TEXT("azimuth"),180);
    const float Elevation=FMath::Sin((WeatherHour-6)*PI/12),Day=FMath::Clamp(Elevation*3.f,0.f,1.f),Warmth=FMath::Pow(1-FMath::Max(0.f,Elevation),1.5f);
    SunElevationSin=FMath::Sin(FMath::DegreesToRadians(FMath::Max(0.f,Elevation)*75));
    if(Sun&&!Sun->LightFunctionMaterial)
        if(UMaterialInterface* LF=LoadObject<UMaterialInterface>(nullptr,TEXT("/Eidoverse/FX/M_EidoverseCloudShadow.M_EidoverseCloudShadow")))
            Sun->SetLightFunctionMaterial(LF);
    SunFactor=Day*(1-CloudAmount*.83);CloudCover=CloudAmount;SunLight=SunFactor;
    if(Sun){Sun->SetWorldRotation(FRotator(-Elevation*75,(WeatherHour-12)*15-35+(Azimuth-180),0));Sun->SetIntensity(3.2f*Day*(1-CloudAmount*.83)*SunK);Sun->SetLightColor(FMath::Lerp(FLinearColor(1,.67,.37),FLinearColor(1,.95,.88),Day));}
    if(SkyFill)SkyFill->SetIntensity(FMath::Lerp(.2f,.8f,Day)*(1-CloudAmount*.45)*AmbK);
    if(!Moon&&Sun)
    {
        Moon=NewObject<UDirectionalLightComponent>(this);AddInstanceComponent(Moon);Moon->SetupAttachment(RootComponent);
        Moon->SetCastShadows(false);Moon->SetAtmosphereSunLight(false);Moon->SetLightColor(FLinearColor(.55,.65,1));Moon->RegisterComponent();
    }
    // Counter-fill from the other side of the sky in the warm hours (sky.js fill = .95 warmth).
    if(Moon){Moon->SetWorldRotation(FRotator(-50,(WeatherHour-12)*15+145,0));Moon->SetIntensity((.45f*(1-Day)+.25f*Warmth*FillK*Day)*AmbK);}
    // One shadowed key light: the sun by day, the moon once the sun has dimmed below it
    // (otherwise nights cast no shadows at all). Flip only on change: it rebuilds shadow state.
    if(Sun&&Moon)
    {
        const bool bMoonKey=Sun->Intensity<Moon->Intensity;
        if(Sun->CastShadows==bMoonKey)Sun->SetCastShadows(!bMoonKey);
        if(Moon->CastShadows!=bMoonKey){Moon->SetCastShadows(bMoonKey);Moon->SetDynamicShadowCascades(3);}
    }
    if(APostProcessVolume* PP=ScenePost.Get()){PP->Settings.bOverride_AutoExposureBias=true;PP->Settings.AutoExposureBias=FMath::Log2((1-.22f*Warmth*(1-Day))*ExpK);}
    if(Clouds){Clouds->SetVisibility(CloudAmount>.01);CloudMaterial->SetScalarParameterValue(TEXT("Coverage"),CloudAmount);CloudMaterial->SetVectorParameterValue(TEXT("CloudColor"),FMath::Lerp(FLinearColor(.018,.025,.04),FLinearColor(.55,.59,.63),Day)*(1-RainAmount*.5));}
    // core.js FogExp2(0.018 x sky.fog): 50 % at 46 m. UE fog density is per 1000 cm.
    if(!WeatherFog){WeatherFog=NewObject<UExponentialHeightFogComponent>(this);AddInstanceComponent(WeatherFog);WeatherFog->SetupAttachment(RootComponent);WeatherFog->RegisterComponent();}
    // Under the world's water level the fog is the water's (EidoverseLevelWater.cpp).
    if(bUnderLevel){ApplyUnderLevelFog();for(auto& M:Materials)if(M.Value&&M.Key!=TEXT("glass"))M.Value->SetScalarParameterValue(TEXT("roughnessFactor"),FMath::Lerp(.86f,.2f,Wetness*.8f));return;}
    if(bUnderFogApplied){bUnderFogApplied=false;WeatherFog->SetVisibility(!(Waters&&Waters->Num()>0));}
    WeatherFog->SetFogHeightFalloff(.001f);WeatherFog->SetStartDistance(0);
    // The browser's sky dome is exempt from fog: stop fogging past the world (5 km).
    if(WeatherFog->FogCutoffDistance!=500000.f){WeatherFog->FogCutoffDistance=500000.f;WeatherFog->MarkRenderStateDirty();}
    WeatherFog->SetFogDensity(FogK*.15f+CloudAmount*.03f+RainAmount*.06f);
    WeatherFog->SetFogInscatteringColor(FMath::Lerp(FLinearColor(.12,.15,.2),FLinearColor(.62,.68,.76),Day).CopyWithNewOpacity(1)*(1-CloudAmount*.25f));
    for(auto& M:Materials)if(M.Value&&M.Key!=TEXT("glass"))M.Value->SetScalarParameterValue(TEXT("roughnessFactor"),FMath::Lerp(.86f,.2f,Wetness*.8f));
}
void AEidoverseScene::TickWeather(float Dt)
{
    auto* PC=UGameplayStatics::GetPlayerController(this,0);if(!PC)return;FVector Eye;FRotator View;PC->GetPlayerViewPoint(Eye,View);
    if(Clouds)Clouds->SetWorldLocation(Eye);
    const double Now=GetWorld()->GetTimeSeconds();if(Now-LastRainUpdate<.05)return;
    {
        // Ease cover (15 s) and wetness (wet 10 s / dry 40 s) between atmosphere updates.
        const float Step=float(FMath::Min(.5,Now-LastRainUpdate));
        CloudAmount+=(CoverTarget-CloudAmount)*FMath::Min(1.f,Step/15);
        Wetness+=(WetTarget-Wetness)*FMath::Min(1.f,Step/(WetTarget>Wetness?10.f:40.f));
        if(bWeatherEased&&FMath::Abs(CoverTarget-CloudAmount)+FMath::Abs(WetTarget-Wetness)>.001f)ApplyLightingNow();
        // Surfaces (materials.js): wetness and the scrolling cloud shade, one parameter set.
        if(!WeatherCollection)WeatherCollection=LoadObject<UMaterialParameterCollection>(nullptr,TEXT("/Eidoverse/FX/MPC_EidoverseWeather.MPC_EidoverseWeather"));
        if(UMaterialParameterCollectionInstance* MPC=WeatherCollection?GetWorld()->GetParameterCollectionInstance(WeatherCollection):nullptr)
        {
            const float Speed=1+2.5f*CloudAmount;
            CloudScroll1+=FVector2D(1.9,.55)*Speed*Step;CloudScroll2+=FVector2D(-.7,1.3)*Speed*Step;
            const float Strength=.5f*FMath::Clamp(4*SunElevationSin,0.f,1.f)*(1-FMath::Clamp((CloudAmount-.8f)/.17f,0.f,1.f))*FMath::Min(1.f,3*CloudAmount);
            MPC->SetScalarParameterValue(TEXT("Wet"),Wetness);MPC->SetScalarParameterValue(TEXT("Coverage"),CloudAmount);MPC->SetScalarParameterValue(TEXT("Strength"),Strength);
            MPC->SetScalarParameterValue(TEXT("Scroll1X"),CloudScroll1.X/420);MPC->SetScalarParameterValue(TEXT("Scroll1Y"),CloudScroll1.Y/420);
            MPC->SetScalarParameterValue(TEXT("Scroll2X"),CloudScroll2.X/97);MPC->SetScalarParameterValue(TEXT("Scroll2Y"),CloudScroll2.Y/97);
        }
    }
    LastRainUpdate=Now;
    if(!Rain&&RainAmount>0)
    {
        Rain=NewObject<UInstancedStaticMeshComponent>(this);AddInstanceComponent(Rain);Rain->SetupAttachment(RootComponent);Rain->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube")));Rain->SetMaterial(0,Material(TEXT("glass")));Rain->SetCollisionEnabled(ECollisionEnabled::NoCollision);Rain->SetCastShadow(false);Rain->RegisterComponent();
        for(int I=0;I<192;++I)Rain->AddInstance(FTransform());
    }
    if(!Rain)return;Rain->SetVisibility(RainAmount>.01);if(RainAmount<=.01)return;
    FRandomStream R(1709);const int Count=FMath::RoundToInt(RainAmount*192);
    for(int I=0;I<192;++I)
    {
        FVector P=Eye+FVector(R.FRandRange(-1100,1100),R.FRandRange(-1100,1100),FMath::Fmod(R.FRandRange(0,1800)+FMath::Fmod(-Now*850,1800.)+1800,1800.)-900);
        // Wrap an absolute-world distribution around the view; walking must
        // not drag raindrops horizontally along with the camera.
        P.X=Eye.X+FMath::Fmod(P.X-2*Eye.X+2200000000.,2200.)-1100;
        P.Y=Eye.Y+FMath::Fmod(P.Y-2*Eye.Y+2200000000.,2200.)-1100;
        FHitResult Hit;FCollisionQueryParams Q;Q.bTraceComplex=true;if(PC->GetPawn())Q.AddIgnoredActor(PC->GetPawn());
        const bool Covered=GetWorld()->LineTraceSingleByChannel(Hit,P,P+FVector(0,0,3000),ECC_Visibility,Q);
        Rain->UpdateInstanceTransform(I,FTransform(FRotator(0,0,8),P,(I<Count&&!Covered)?FVector(.007,.007,.22):FVector::ZeroVector),true,false,true);
    }
    Rain->MarkRenderStateDirty();
}

FString AEidoverseScene::DescribeLighting() const
{
    return FString::Printf(TEXT("hour=%.1f sun=%.2f sunPitch=%.0f fill=%.2f moon=%.2f fog=%.4f cloud=%.2f wet=%.2f bias=%.2f"),
        WeatherHour,Sun?Sun->Intensity:-1.f,Sun?Sun->GetComponentRotation().Pitch:0.f,SkyFill?SkyFill->Intensity:-1.f,Moon?Moon->Intensity:-1.f,
        WeatherFog?WeatherFog->FogDensity:-1.f,CloudAmount,Wetness,ScenePost.IsValid()?ScenePost->Settings.AutoExposureBias:-99.f);
}
