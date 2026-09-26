#include "SpectralOcean.h"
#include "Components/StaticMeshComponent.h"
#include "Components/LightComponent.h"
#include "Engine/Texture2D.h"
#include "Engine/TextureRenderTargetCube.h"
#include "Components/SceneCaptureComponentCube.h"
#include "Engine/PostProcessVolume.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Camera/PlayerCameraManager.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialParameterCollection.h"
#include "Materials/MaterialParameterCollectionInstance.h"
#include "Rendering/Texture2DResource.h"
#include "HAL/PlatformTime.h"
#include "HAL/IConsoleManager.h"
#include "Misc/ScopeExit.h"
#include "RenderingThread.h"
#if WITH_EDITOR
#include "Editor.h"
#include "EditorViewportClient.h"
#endif

namespace Spectral
{
    constexpr int WakeN=128;
    constexpr float WakeLength=6400.f;
    FVector2f Mul(FVector2f A,FVector2f B) { return FVector2f(A.X*B.X-A.Y*B.Y,A.X*B.Y+A.Y*B.X); }
    float Smooth(float A,float B,float V) { const float T=FMath::Clamp((V-A)/(B-A),0.f,1.f);return T*T*(3-2*T); }
    float At(const TArray<float>& A,int X,int Y) { return (X>=0&&X<WakeN&&Y>=0&&Y<WakeN)?A[Y*WakeN+X]:0.f; }
}

ASpectralOcean::ASpectralOcean()
{
    PrimaryActorTick.bCanEverTick=true;
    PrimaryActorTick.TickGroup=TG_PrePhysics;
    SurfaceMesh=CreateDefaultSubobject<UStaticMeshComponent>(TEXT("SpectralSurface"));
    SetRootComponent(SurfaceMesh);
    UnderMesh=CreateDefaultSubobject<UStaticMeshComponent>(TEXT("SpectralUnderside"));
    UnderMesh->SetupAttachment(SurfaceMesh);
    UnderCapture=CreateDefaultSubobject<USceneCaptureComponentCube>(TEXT("UnderwaterReflection"));
    UnderCapture->SetupAttachment(SurfaceMesh);
    UnderCapture->bCaptureEveryFrame=false;
    UnderCapture->bCaptureOnMovement=false;
    // Store scene depth with HDR radiance so the material can attenuate the
    // reflected water path, including rays that leave the finite terrain mesh.
    UnderCapture->CaptureSource=SCS_SceneColorSceneDepth;
    // Low-frequency reflection fallback, not six additional full-quality views.
    // The material handles underwater path attenuation after capture.
    UnderCapture->ShowFlags.SetLightFunctions(false);
    UnderCapture->ShowFlags.SetDynamicShadows(false);
    UnderCapture->ShowFlags.SetVolumetricFog(false);
    UnderCapture->ShowFlags.SetLumenGlobalIllumination(false);
    UnderCapture->ShowFlags.SetLumenReflections(false);
    UnderCapture->MaxViewDistanceOverride=18000.f;
    UnderCapture->LODDistanceFactor=3.f;
    UnderCapture->PostProcessSettings.bOverride_AutoExposureMethod=true;
    UnderCapture->PostProcessSettings.AutoExposureMethod=AEM_Manual;
    UnderCapture->PostProcessSettings.bOverride_AutoExposureApplyPhysicalCameraExposure=true;
    UnderCapture->PostProcessSettings.AutoExposureApplyPhysicalCameraExposure=false;
    for(UStaticMeshComponent* Mesh:{SurfaceMesh.Get(),UnderMesh.Get()})
    {
        Mesh->SetMobility(EComponentMobility::Movable);
        Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Mesh->SetCastShadow(false);
        Mesh->SetRenderCustomDepth(true);
        Mesh->SetCustomDepthStencilValue(1);
    }
    #if WITH_EDITORONLY_DATA
    bIsSpatiallyLoaded=false;
    #endif
}

UTexture2D* ASpectralOcean::MakeField(int32 N)
{
    UTexture2D* T=UTexture2D::CreateTransient(N,N,PF_A32B32G32R32F);
    T->SRGB=false;T->NeverStream=true;T->Filter=TF_Trilinear;
    T->AddressX=TA_Wrap;T->AddressY=TA_Wrap;
    FTexturePlatformData* Data=T->GetPlatformData();
    // CreateTransient allocates, but does not clear, mip zero. In editor worlds
    // UpdateWake never runs, so sampling that uninitialized float field created
    // black squares / invalid normals across the wake region. Start ALL fields
    // at neutral displacement and foam before publishing the GPU resource.
    auto& BaseMip=Data->Mips[0];
    void* BaseMemory=BaseMip.BulkData.Lock(LOCK_READ_WRITE);
    FMemory::Memzero(BaseMemory,N*N*sizeof(FVector4f));BaseMip.BulkData.Unlock();
    for(int Size=N/2;Size>0;Size/=2)
    {
        FTexture2DMipMap* Mip=new FTexture2DMipMap();
        Mip->SizeX=Size;Mip->SizeY=Size;Mip->SizeZ=1;
        Mip->BulkData.Lock(LOCK_READ_WRITE);
        void* Memory=Mip->BulkData.Realloc(Size*Size*sizeof(FVector4f));
        FMemory::Memzero(Memory,Size*Size*sizeof(FVector4f));Mip->BulkData.Unlock();
        Data->Mips.Add(Mip);
    }
    T->UpdateResource();return T;
}

void ASpectralOcean::Upload(UTexture2D* T,const TArray<FVector4f>& Pixels,int32 N)
{
    TArray<FVector4f> Level=Pixels;
    for(int Mip=0,Size=N;Size>0;Mip++,Size/=2)
    {
        auto* Region=new FUpdateTextureRegion2D(0,0,0,0,Size,Size);
        uint8* Copy=new uint8[Size*Size*sizeof(FVector4f)];
        FMemory::Memcpy(Copy,Level.GetData(),Size*Size*sizeof(FVector4f));
        T->UpdateTextureRegions(Mip,1,Region,Size*sizeof(FVector4f),sizeof(FVector4f),Copy,
            [](uint8* P,const FUpdateTextureRegion2D* R){delete[] P;delete R;});
        if(Size>1)
        {
            TArray<FVector4f> Next;Next.SetNumUninitialized(Size*Size/4);
            for(int Y=0;Y<Size/2;Y++)for(int X=0;X<Size/2;X++)
                Next[Y*(Size/2)+X]=(Level[(2*Y)*Size+2*X]+Level[(2*Y)*Size+2*X+1]+Level[(2*Y+1)*Size+2*X]+Level[(2*Y+1)*Size+2*X+1])*.25f;
            Level=MoveTemp(Next);
        }
    }
}

void ASpectralOcean::FFT2(TArray<FVector2f>& A,int32 N)
{
    auto Transform=[&](int Start,int Stride)
    {
        for(int I=1,J=0;I<N;I++)
        {
            int Bit=N>>1;for(;J&Bit;Bit>>=1)J^=Bit;J^=Bit;
            if(I<J)Swap(A[Start+I*Stride],A[Start+J*Stride]);
        }
        for(int Len=2;Len<=N;Len<<=1)
        {
            const float Angle=2.f*PI/Len;FVector2f WLen(FMath::Cos(Angle),FMath::Sin(Angle));
            for(int I=0;I<N;I+=Len)
            {
                FVector2f W(1,0);
                for(int J=0;J<Len/2;J++)
                {
                    FVector2f U=A[Start+(I+J)*Stride],V=Spectral::Mul(A[Start+(I+J+Len/2)*Stride],W);
                    A[Start+(I+J)*Stride]=U+V;A[Start+(I+J+Len/2)*Stride]=U-V;
                    W=Spectral::Mul(W,WLen);
                }
            }
        }
    };
    for(int Y=0;Y<N;Y++)Transform(Y*N,1);
    for(int X=0;X<N;X++)Transform(X,N);
    const float Scale=1.f/(N*N);for(FVector2f& V:A)V*=Scale;
}

void ASpectralOcean::InitializeOcean()
{
    if(Bands.Num())return;
    Fields.Reset(); // PIE duplication must not retain the editor instance's GPU fields.
    const float Lengths[3]={204800,12800,800};
    const float Rms[3]={22,18,2.8f};
    FRandomStream Random(91731);
    for(int B=0;B<3;B++)
    {
        FSpectralBand Band;Band.N=B==2?64:128;Band.Length=Lengths[B];
        const int N=Band.N,Count=N*N;
        Band.Initial.SetNumZeroed(Count);Band.Height.SetNumZeroed(Count);Band.X.SetNumZeroed(Count);Band.Y.SetNumZeroed(Count);
        Band.Pixels.SetNumZeroed(Count);Band.Foam.SetNumZeroed(Count);
        double Variance=0;
        for(int Y=0;Y<N;Y++)for(int X=0;X<N;X++)
        {
            const int KX=X<=N/2?X:X-N,KY=Y<=N/2?Y:Y-N;
            const float K=2.f*PI*FMath::Sqrt(float(KX*KX+KY*KY))/(Band.Length*.01f);
            if(K<1e-6f || X==N/2 || Y==N/2)continue;
            const float Wavelength=2*PI/K;
            const float MinWave=B==0?35.f:(B==1?2.2f:.28f);
            const float MaxWave=B==0?650.f:(B==1?45.f:2.8f);
            const float Window=Spectral::Smooth(MinWave,MinWave*1.7f,Wavelength)*(1-Spectral::Smooth(MaxWave*.65f,MaxWave,Wavelength));
            const float Dot=(KX*.9063f+KY*.4226f)/FMath::Sqrt(float(KX*KX+KY*KY));
            const float WindLength=B==0?90.f:12.f;
            const float P=FMath::Exp(-1.f/FMath::Square(K*WindLength))*Window*(.12f+.88f*Dot*Dot)/(K*K*K*K);
            const float Radius=FMath::Sqrt(-2*FMath::Loge(FMath::Max(Random.FRand(),1e-6f)));
            const float Angle=Random.FRand()*2*PI;
            const float Amp=FMath::Sqrt(P*.5f);
            Band.Initial[Y*N+X]=FVector2f(Radius*FMath::Cos(Angle),Radius*FMath::Sin(Angle))*Amp;
            Variance+=2.0*Band.Initial[Y*N+X].SizeSquared();
        }
        Band.Gain=Rms[B]*Count/FMath::Sqrt(FMath::Max(Variance,1e-10));
        Bands.Add(MoveTemp(Band));Fields.Add(MakeField(N));
    }
    WakeField=MakeField(Spectral::WakeN);
    WakeHeight.SetNumZeroed(Spectral::WakeN*Spectral::WakeN);WakeVelocity=WakeHeight;WakeFoam=WakeHeight;
    UnderReflection=NewObject<UTextureRenderTargetCube>(this);
    UnderReflection->bHDR=true;UnderReflection->bAutoGenerateMips=true;
    UnderReflection->Init(128,PF_FloatRGBA);UnderReflection->UpdateResourceImmediate();
    UnderCapture->TextureTarget=UnderReflection;
    UnderCapture->HiddenActors.AddUnique(this);
    if(SurfaceMaterial){SurfaceMID=UMaterialInstanceDynamic::Create(SurfaceMaterial,this);SurfaceMesh->SetMaterial(0,SurfaceMID);}
    if(UnderMaterial){UnderMID=UMaterialInstanceDynamic::Create(UnderMaterial,this);UnderMesh->SetMaterial(0,UnderMID);}
    if(OpticsMaterial)OpticsMID=UMaterialInstanceDynamic::Create(OpticsMaterial,this);
    Evaluate(GetWorld()->GetTimeSeconds(),.016f);
}

void ASpectralOcean::BeginPlay()
{
    Super::BeginPlay();InitializeOcean();
    PerformanceOverrideTag=FName(*GetPathName());
    IConsoleVariable::FSetContext Context;
    Context.Flags=ECVF_SetByTemp;Context.Tag=PerformanceOverrideTag;
    if(bSkipUnusedTranslucencyVolume)
        if(auto* CVar=IConsoleManager::Get().FindConsoleVariable(TEXT("r.TranslucencyLightingVolume")))
        {
            PreviousTranslucencyVolume=CVar->GetInt();
            CVar->Set(0,Context);
        }
    if(ShadowPageBudget>0)
        if(auto* CVar=IConsoleManager::Get().FindConsoleVariable(TEXT("r.Shadow.Virtual.MaxPhysicalPages")))
        {
            PreviousShadowPages=CVar->GetInt();
            CVar->Set(FMath::Max(ShadowPageBudget,PreviousShadowPages),Context);
        }
    if(OpticsMID)for(TActorIterator<APostProcessVolume> It(GetWorld());It;++It)
        if(It->ActorHasTag(TEXT("WaterOpticsPostProcess")))
        {
            // Prototype map owns this replacement; original gameplay map is untouched.
            It->Settings.WeightedBlendables.Array.Reset();
            It->Settings.AddBlendable(OpticsMID,1.f);
        }
}

void ASpectralOcean::EndPlay(const EEndPlayReason::Type Reason)
{
    // Tagged temporary layers restore the prior setting/priority, not just its
    // value. Never persist prototype rendering choices in project/global config.
    auto Restore=[&](const TCHAR* Name,int32 Previous)
    {
        if(auto* CVar=IConsoleManager::Get().FindConsoleVariable(Name))
        {
            CVar->Unset(ECVF_SetByTemp,PerformanceOverrideTag);
        }
    };
    if(bSkipUnusedTranslucencyVolume)Restore(TEXT("r.TranslucencyLightingVolume"),PreviousTranslucencyVolume);
    if(ShadowPageBudget>0)Restore(TEXT("r.Shadow.Virtual.MaxPhysicalPages"),PreviousShadowPages);
    Super::EndPlay(Reason);
}

void ASpectralOcean::Evaluate(float Time,float Dt)
{
    for(int B=0;B<Bands.Num();B++)
    {
        FSpectralBand& Band=Bands[B];const int N=Band.N;
        for(int Y=0;Y<N;Y++)for(int X=0;X<N;X++)
        {
            const int I=Y*N+X,J=((N-Y)%N)*N+(N-X)%N;
            const float KX=2*PI*(X<=N/2?X:X-N)/Band.Length,KY=2*PI*(Y<=N/2?Y:Y-N)/Band.Length;
            const float K=FMath::Sqrt(KX*KX+KY*KY),Omega=FMath::Sqrt(980.f*K);
            FVector2f E(FMath::Cos(Omega*Time),FMath::Sin(Omega*Time));
            FVector2f A=Spectral::Mul(Band.Initial[I],E);
            FVector2f C=Spectral::Mul(FVector2f(Band.Initial[J].X,-Band.Initial[J].Y),FVector2f(E.X,-E.Y));
            const FVector2f H=(A+C)*Band.Gain*WaveScale;
            Band.Height[I]=H;
            const FVector2f MinusI(H.Y,-H.X);
            Band.X[I]=K>1e-8f?MinusI*(KX/K)*1.15f:FVector2f::ZeroVector;
            Band.Y[I]=K>1e-8f?MinusI*(KY/K)*1.15f:FVector2f::ZeroVector;
        }
        FFT2(Band.Height,N);FFT2(Band.X,N);FFT2(Band.Y,N);
        const float Step=Band.Length/N;
        for(int Y=0;Y<N;Y++)for(int X=0;X<N;X++)
        {
            const int I=Y*N+X,L=Y*N+(X+N-1)%N,R=Y*N+(X+1)%N,D=((Y+N-1)%N)*N+X,U=((Y+1)%N)*N+X;
            const float XX=(Band.X[R].X-Band.X[L].X)/(2*Step),XY=(Band.X[U].X-Band.X[D].X)/(2*Step);
            const float YX=(Band.Y[R].X-Band.Y[L].X)/(2*Step),YY=(Band.Y[U].X-Band.Y[D].X)/(2*Step);
            const float J=(1+XX)*(1+YY)-XY*YX;
            const float Breaking=FMath::Clamp((.65f-J)*2.f,0.f,1.f);
            Band.Foam[I]=FMath::Max(Band.Foam[I]*FMath::Exp(-Dt*.65f),Breaking);
            Band.Pixels[I]=FVector4f(Band.X[I].X,Band.Y[I].X,Band.Height[I].X,Band.Foam[I]);
        }
        Upload(Fields[B],Band.Pixels,N);
    }
}

FVector4f ASpectralOcean::Sample(const TArray<FVector4f>& A,int N,double X,double Y)
{
    if(N<=0||int64(N)*N!=A.Num()||!FMath::IsFinite(X)||!FMath::IsFinite(Y))return FVector4f(0,0,0,0);
    // Float subtraction could round a tiny negative coordinate to exactly N,
    // indexing one row beyond the grid. Wrap in double, then enforce [0,N).
    auto Wrap=[N](double V){V=FMath::Fmod(V,double(N));if(V<0)V+=N;return V>=N?0.:V;};
    X=Wrap(X);Y=Wrap(Y);
    const int I=FMath::FloorToInt(X),J=FMath::FloorToInt(Y);
    const float U=float(X-I),V=float(Y-J);
    return FMath::Lerp(FMath::Lerp(A[J*N+I],A[J*N+(I+1)%N],U),FMath::Lerp(A[((J+1)%N)*N+I],A[((J+1)%N)*N+(I+1)%N],U),V);
}

FVector ASpectralOcean::GetDisplacement(FVector P) const
{
    FVector D=FVector::ZeroVector;
    if(!FMath::IsFinite(P.X)||!FMath::IsFinite(P.Y)||!FMath::IsFinite(P.Z))return D;
    for(const FSpectralBand& B:Bands)
    {
        FVector4f V=Sample(B.Pixels,B.N,P.X/B.Length*B.N,P.Y/B.Length*B.N);D+=FVector(V.X,V.Y,V.Z);
    }
    if(WakeHeight.Num())
    {
        const float X=(P.X-WakeCenter.X)/Spectral::WakeLength*Spectral::WakeN+Spectral::WakeN*.5f;
        const float Y=(P.Y-WakeCenter.Y)/Spectral::WakeLength*Spectral::WakeN+Spectral::WakeN*.5f;
        int I=FMath::FloorToInt(X),J=FMath::FloorToInt(Y);
        if(I>=0&&J>=0&&I<Spectral::WakeN-1&&J<Spectral::WakeN-1)
            D.Z+=FMath::Lerp(FMath::Lerp(Spectral::At(WakeHeight,I,J),Spectral::At(WakeHeight,I+1,J),X-I),FMath::Lerp(Spectral::At(WakeHeight,I,J+1),Spectral::At(WakeHeight,I+1,J+1),X-I),Y-J);
    }
    D.Z+=GetAuthoredHeight(P);
    return D;
}

void ASpectralOcean::SetAuthoredWaves(const TArray<FVector4>& Waves, const TArray<double>& Phases)
{
    AuthoredWaves.Reset();AuthoredPhases.Reset();
    for(int32 I=0;I<FMath::Min(Waves.Num(),8);++I)
    {
        const FVector4& W=Waves[I];
        const double Phase=Phases.IsValidIndex(I)?Phases[I]:0;
        if(!FMath::IsFinite(W.X)||!FMath::IsFinite(W.Y)||!FMath::IsFinite(W.Z)||!FMath::IsFinite(W.W)||!FMath::IsFinite(Phase))continue;
        AuthoredWaves.Add(W);
        // Reduce here in double precision; the GPU only sees a small angle.
        AuthoredPhases.Add(FMath::Fmod(Phase,2*UE_DOUBLE_PI));
    }
    AuthoredWaveCount=AuthoredWaves.Num();
    WriteAuthoredWaves();
}

float ASpectralOcean::GetAuthoredHeight(FVector P) const
{
    double H=0;
    for(int32 I=0;I<AuthoredWaves.Num();++I)
    {
        const FVector4& W=AuthoredWaves[I];
        // float32 like the shader, so CPU physics and the rendered surface agree.
        H+=float(W.X)*FMath::Sin(float(W.Y)*(float(P.X)*float(W.Z)+float(P.Y)*float(W.W))+float(AuthoredPhases[I]));
    }
    return H;
}

void ASpectralOcean::WriteAuthoredWaves()
{
    if(!Collection||!GetWorld())return;
    auto* MPC=GetWorld()->GetParameterCollectionInstance(Collection);
    if(!MPC)return;
    MPC->SetScalarParameterValue(TEXT("AuthoredWaveCount"),AuthoredWaves.Num());
    float Phase[8]={0,0,0,0,0,0,0,0};
    for(int32 I=0;I<8;++I)
    {
        const FVector4 W=AuthoredWaves.IsValidIndex(I)?AuthoredWaves[I]:FVector4(0,0,1,0);
        if(AuthoredPhases.IsValidIndex(I))Phase[I]=AuthoredPhases[I];
        MPC->SetVectorParameterValue(FName(*FString::Printf(TEXT("AuthoredWave%d"),I)),FLinearColor(W.X,W.Y,W.Z,W.W));
    }
    MPC->SetVectorParameterValue(TEXT("AuthoredWavePhase0"),FLinearColor(Phase[0],Phase[1],Phase[2],Phase[3]));
    MPC->SetVectorParameterValue(TEXT("AuthoredWavePhase1"),FLinearColor(Phase[4],Phase[5],Phase[6],Phase[7]));
}

float ASpectralOcean::GetSurfaceHeight(FVector P) const
{
    FVector Q=P;
    for(int I=0;I<3;I++){FVector D=GetDisplacement(Q);Q.X=P.X-D.X;Q.Y=P.Y-D.Y;}
    return SurfaceZ+GetDisplacement(Q).Z;
}

void ASpectralOcean::UpdateWake(float Dt)
{
    APawn* Sub=nullptr;for(TActorIterator<APawn> It(GetWorld());It;++It)if(It->ActorHasTag(TEXT("OceanSubmarine"))){Sub=*It;break;}
    if(!Sub)return;
    const FVector P=Sub->GetActorLocation();
    const FVector Velocity=bHadSub?(P-LastSubPosition)/FMath::Max(Dt,.001f):FVector::ZeroVector;
    LastSubPosition=P;bHadSub=true;
    const float Cell=Spectral::WakeLength/Spectral::WakeN;
    const FVector Center3=P-Sub->GetActorForwardVector()*950.f;
    FVector2D NewCenter(FMath::GridSnap(Center3.X,double(Cell)),FMath::GridSnap(Center3.Y,double(Cell)));
    const int ShiftX=FMath::RoundToInt((NewCenter.X-WakeCenter.X)/Cell),ShiftY=FMath::RoundToInt((NewCenter.Y-WakeCenter.Y)/Cell);
    if(ShiftX||ShiftY)
    {
        for(TArray<float>* A:{&WakeHeight,&WakeVelocity,&WakeFoam})
        {
            TArray<float> Old=*A;
            for(int Y=0;Y<Spectral::WakeN;Y++)for(int X=0;X<Spectral::WakeN;X++)(*A)[Y*Spectral::WakeN+X]=Spectral::At(Old,X+ShiftX,Y+ShiftY);
        }
        WakeCenter=NewCenter;
    }
    const float Surface=GetSurfaceHeight(P);
    const int Steps=FMath::Clamp(FMath::CeilToInt(Dt/.02f),1,8);const float Step=FMath::Min(Dt,.16f)/Steps;
    for(int S=0;S<Steps;S++)
    {
        TArray<float> Next=WakeHeight;
        for(int Y=1;Y<Spectral::WakeN-1;Y++)for(int X=1;X<Spectral::WakeN-1;X++)
        {
            const int I=Y*Spectral::WakeN+X;
            const float Lap=(WakeHeight[I-1]+WakeHeight[I+1]+WakeHeight[I-Spectral::WakeN]+WakeHeight[I+Spectral::WakeN]-4*WakeHeight[I])/(Cell*Cell);
            FVector W(WakeCenter.X+(X-Spectral::WakeN*.5f)*Cell,WakeCenter.Y+(Y-Spectral::WakeN*.5f)*Cell,Surface);
            const FVector Q=Sub->GetActorTransform().InverseTransformPosition(W);
            const float Cap=Q.X<-1750?FMath::Sqrt(FMath::Max(0.f,1-FMath::Square((Q.X+1750)/330))):(Q.X>-80?FMath::Sqrt(FMath::Max(0.f,1-FMath::Square((Q.X+80)/330))):1.f);
            const float Distance=(FMath::Sqrt(FMath::Square(Q.Y/285)+FMath::Square(Q.Z/270))-Cap)*270;
            const float Touch=(1-Spectral::Smooth(270,350,FMath::Abs(Q.Z)))*(Q.X>-2100&&Q.X<270?1:0);
            const float Edge=Touch*FMath::Exp(-FMath::Square(Distance/85.f));
            const float Speed=FMath::Min(Velocity.Size(),350.f);
            const float Force=Edge*(FMath::Clamp(float(Velocity.Z),-160.f,160.f)*.55f+Speed*.08f);
            WakeVelocity[I]=(WakeVelocity[I]+(90000.f*Lap+Force)*Step)*FMath::Exp(-Step*.65f);
            Next[I]=FMath::Clamp(WakeHeight[I]+WakeVelocity[I]*Step,-25.f,25.f);
            const float Boundary=FMath::Min(FMath::Min(X,Y),FMath::Min(Spectral::WakeN-1-X,Spectral::WakeN-1-Y));
            Next[I]*=Spectral::Smooth(0,10,Boundary);
            WakeFoam[I]=FMath::Max(WakeFoam[I]*FMath::Exp(-Step*.5f),Edge*FMath::Clamp((Speed-8.f)/100.f,0.f,.65f));
        }
        WakeHeight=MoveTemp(Next);
    }
    TArray<FVector4f> Pixels;Pixels.SetNumUninitialized(Spectral::WakeN*Spectral::WakeN);
    for(int I=0;I<Pixels.Num();I++)Pixels[I]=FVector4f(0,0,WakeHeight[I],WakeFoam[I]);
    Upload(WakeField,Pixels,Spectral::WakeN);
}

void ASpectralOcean::Bind(UMaterialInstanceDynamic* M)
{
    if(!M||Fields.Num()!=3)return;
    for(int B=0;B<3;B++)M->SetTextureParameterValue(FName(*FString::Printf(TEXT("Ocean%d"),B)),Fields[B]);
    M->SetTextureParameterValue(TEXT("OceanWake"),WakeField);
    M->SetTextureParameterValue(TEXT("UnderReflection"),UnderReflection);
    M->SetVectorParameterValue(TEXT("WakeOrigin"),FLinearColor(WakeCenter.X,WakeCenter.Y,Spectral::WakeLength,1));
    M->SetScalarParameterValue(TEXT("CameraDepth"),CameraDepth);
    M->SetVectorParameterValue(TEXT("OceanGridCenter"),FLinearColor(GetActorLocation().X,GetActorLocation().Y,0,0));
}

void ASpectralOcean::WithCaptureView(const FVector& Camera,TFunctionRef<void()> Capture)
{
    check(IsInGameThread());
    const FVector SavedLocation=GetActorLocation();
    const float SavedDepth=CameraDepth;
    const bool SurfaceVisible=SurfaceMesh->IsVisible(),UnderVisible=UnderMesh->IsVisible();
    const FVector SavedCubeLocation=UnderCapture->GetComponentLocation();
    UTextureRenderTargetCube* SavedReflection=UnderReflection;
    UTextureRenderTargetCube* SavedTarget=UnderCapture->TextureTarget;
    ON_SCOPE_EXIT
    {
        UnderReflection=SavedReflection;UnderCapture->TextureTarget=SavedTarget;
        CameraDepth=SavedDepth;SetActorLocation(SavedLocation);
        UnderCapture->SetWorldLocation(SavedCubeLocation);
        SurfaceMesh->SetVisibility(SurfaceVisible);UnderMesh->SetVisibility(UnderVisible);
        Bind(SurfaceMID);Bind(UnderMID);Bind(OpticsMID);
        FlushRenderingCommands();
    };
    CameraDepth=GetSurfaceHeight(Camera)-Camera.Z;
    SetActorLocation(FVector(FMath::GridSnap(Camera.X,100.0),FMath::GridSnap(Camera.Y,100.0),SurfaceZ));
    SurfaceMesh->SetVisibility(CameraDepth<=0);UnderMesh->SetVisibility(CameraDepth>0);
    if(CameraDepth>0)
    {
        if(!RemoteReflection)
        {
            RemoteReflection=NewObject<UTextureRenderTargetCube>(this);
            RemoteReflection->bHDR=true;RemoteReflection->bAutoGenerateMips=true;
            RemoteReflection->Init(128,PF_FloatRGBA);RemoteReflection->UpdateResourceImmediate();
        }
        UnderReflection=RemoteReflection;UnderCapture->TextureTarget=RemoteReflection;
        UnderCapture->SetWorldLocation(FVector(Camera.X,Camera.Y,FMath::Min(Camera.Z,double(GetSurfaceHeight(Camera)-60))));
        UnderCapture->CaptureScene();
    }
    Bind(SurfaceMID);Bind(UnderMID);Bind(OpticsMID);
    Capture();
    // Captures enqueue rendering. Complete it before restoring shared material
    // uniforms, even if the caller failed to perform its own blocking readback.
    FlushRenderingCommands();
}

void ASpectralOcean::Tick(float Dt)
{
    Super::Tick(Dt);if(!GetWorld())return;InitializeOcean();
    const double Start=FPlatformTime::Seconds();
    const float Time=GetWorld()->GetTimeSeconds();
    if(Time!=LastTime){Evaluate(Time,FMath::Min(Dt,.1f));if(GetWorld()->IsGameWorld())UpdateWake(Dt);LastTime=Time;UpdateCount++;}
    FVector Camera=GetActorLocation()+FVector(0,0,1000);
    if(APlayerController* PC=GetWorld()->GetFirstPlayerController())if(PC->PlayerCameraManager)Camera=PC->PlayerCameraManager->GetCameraLocation();
    #if WITH_EDITOR
    if(!GetWorld()->IsGameWorld()&&GEditor)for(FEditorViewportClient* V:GEditor->GetAllViewportClients())if(V&&V->IsPerspective()){Camera=V->GetViewLocation();break;}
    #endif
    CameraDepth=GetSurfaceHeight(Camera)-Camera.Z;
    SetActorLocation(FVector(FMath::GridSnap(Camera.X,100.0),FMath::GridSnap(Camera.Y,100.0),SurfaceZ));
    SurfaceMesh->SetVisibility(CameraDepth<=0);UnderMesh->SetVisibility(CameraDepth>0);
    Bind(SurfaceMID);Bind(UnderMID);Bind(OpticsMID);
    WriteAuthoredWaves();
    CaptureCountdown-=Dt;
    if(CameraDepth>0&&CaptureCountdown<=0&&GetWorld()->IsGameWorld())
    {
        UnderCapture->SetWorldLocation(FVector(Camera.X,Camera.Y,FMath::Min(Camera.Z,double(GetSurfaceHeight(Camera)-60))));
        UnderCapture->CaptureScene();CaptureCountdown=.5f;
    }
    LightCacheCountdown-=Dt;
    if(LightCacheCountdown<=0)
    {
        LightMaterials.Reset();
        for(TActorIterator<AActor> It(GetWorld());It;++It)
        {
            TArray<ULightComponent*> Lights;It->GetComponents(Lights);
            for(ULightComponent* Light:Lights)
                if(auto* MID=Cast<UMaterialInstanceDynamic>(Light->LightFunctionMaterial))LightMaterials.AddUnique(MID);
        }
        LightCacheCountdown=1.f;
    }
    for(const auto& Material:LightMaterials)if(Material.IsValid())Bind(Material.Get());
    SimulationMilliseconds=(FPlatformTime::Seconds()-Start)*1000;
}
