// Pictures (client/lib/pictures.js): an image from the library hung on a named part.
// The part's material is copied (never the shared one): base colour = image, factor
// white; lit:'self' also makes it emissive. flip:true flips V. A newer src supersedes.
#include "EidoverseScene.h"
#include "EidoverseAvatar.h"
#include "Dom/JsonObject.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Modules/ModuleManager.h"
#include "Engine/Texture2D.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Components/StaticMeshComponent.h"

namespace
{
    FString PicStr(const TSharedPtr<FJsonObject>& O, const TCHAR* K) { FString S; if (O) O->TryGetStringField(K, S); return S; }
    UTexture2D* DecodePicture(const TArray<uint8>& Bytes, bool bFlip)
    {
        IImageWrapperModule& Module = FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
        const EImageFormat Format = Module.DetectImageFormat(Bytes.GetData(), Bytes.Num());
        if (Format == EImageFormat::Invalid) return nullptr;
        TSharedPtr<IImageWrapper> W = Module.CreateImageWrapper(Format);
        TArray<uint8> Raw;
        if (!W || !W->SetCompressed(Bytes.GetData(), Bytes.Num()) || !W->GetRaw(ERGBFormat::BGRA, 8, Raw)) return nullptr;
        const int32 Width = W->GetWidth(), Height = W->GetHeight();
        if (Width <= 0 || Height <= 0 || Width > 8192 || Height > 8192) return nullptr;
        if (bFlip)
            for (int32 Y = 0; Y < Height / 2; ++Y)
                for (int32 X = 0; X < Width * 4; ++X) Swap(Raw[Y * Width * 4 + X], Raw[(Height - 1 - Y) * Width * 4 + X]);
        UTexture2D* T = UTexture2D::CreateTransient(Width, Height, PF_B8G8R8A8);
        if (!T) return nullptr;
        T->SRGB = true;
        void* Data = T->GetPlatformData()->Mips[0].BulkData.Lock(LOCK_READ_WRITE);
        FMemory::Memcpy(Data, Raw.GetData(), Raw.Num());
        T->GetPlatformData()->Mips[0].BulkData.Unlock();
        T->UpdateResource();
        return T;
    }
}

UTexture2D* EidoDecodeImage(const TArray<uint8>& Bytes) { return DecodePicture(Bytes, false); }

void AEidoverseScene::TickPictures()
{
    for (auto& Pair : Items)
    {
        FEidoverseSceneItem& E = Pair.Value;
        const FString Src = PicStr(E.Picture, TEXT("src")), Part = PicStr(E.Picture, TEXT("part")), Lit = PicStr(E.Picture, TEXT("lit"));
        bool bFlip = false; if (E.Picture) E.Picture->TryGetBoolField(TEXT("flip"), bFlip);
        const FString Want = Src.IsEmpty() ? FString() : Src + TEXT("|") + Lit + (bFlip ? TEXT("|flip") : TEXT(""));
        if (Want == E.PictureApplied) continue;
        UStaticMeshComponent* C = E.PartComps.FindRef(Part).Get();
        if (Want.IsEmpty()) { if (C) C->EmptyOverrideMaterials(); E.PictureApplied.Empty(); continue; }
        if (!C) continue;                            // the part has not loaded yet
        bool bPending = false;
        UTexture2D* T = LibraryTexture(Src, bFlip, bPending);
        if (bPending) continue;
        if (!T) { E.PictureApplied = Want; UE_LOG(LogTemp, Warning, TEXT("EIDOVERSE picture failed to load: %s"), *Src); continue; }
        for (int32 I = 0; I < C->GetNumMaterials(); ++I)
        {
            UMaterialInstanceDynamic* MI = C->CreateDynamicMaterialInstance(I);
            if (!MI) continue;
            MI->SetTextureParameterValue(TEXT("baseColorTexture"), T);
            MI->SetVectorParameterValue(TEXT("baseColorFactor"), FLinearColor::White);
            if (Lit == TEXT("self"))
            {
                MI->SetTextureParameterValue(TEXT("emissiveTexture"), T);
                MI->SetVectorParameterValue(TEXT("emissiveFactor"), FLinearColor::White);
                MI->SetScalarParameterValue(TEXT("emissiveStrength"), 1);
            }
        }
        E.PictureApplied = Want;
    }
}

UTexture2D* AEidoverseScene::LibraryTexture(const FString& Path, bool bFlip, bool& bPending)
{
    bPending = false;
    const FString Url = LibraryBase + Path, Key = Url + (bFlip ? TEXT("#flip") : TEXT(""));
    FPictureLoad* Load = PictureLoads.Find(Key);
    if (!Load)
    {
        if (LibraryBase.IsEmpty() || PictureLoads.Num() >= 96) return nullptr;
        PictureLoads.Add(Key);
        bPending = true;
        TWeakObjectPtr<AEidoverseScene> Weak(this);
        auto Request = FHttpModule::Get().CreateRequest();
        Request->SetURL(Url); Request->SetVerb(TEXT("GET")); Request->SetTimeout(20);
        Request->OnProcessRequestComplete().BindLambda([Weak, Key, bFlip](FHttpRequestPtr, FHttpResponsePtr Response, bool bOk)
        {
            AEidoverseScene* Self = Weak.Get(); if (!Self) return;
            FPictureLoad& L = Self->PictureLoads.FindOrAdd(Key); L.bDone = true;
            if (!bOk || !Response.IsValid() || Response->GetResponseCode() != 200 || Response->GetContent().Num() > 32 * 1024 * 1024) return;
            if (UTexture2D* T = DecodePicture(Response->GetContent(), bFlip)) { Self->PictureTextures.Add(T); L.Texture = T; }
        });
        Request->ProcessRequest();
        return nullptr;
    }
    if (!Load->bDone) { bPending = true; return nullptr; }
    return Load->Texture.Get();
}
