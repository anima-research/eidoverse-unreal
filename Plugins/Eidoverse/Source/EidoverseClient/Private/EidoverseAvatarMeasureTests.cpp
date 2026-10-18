// Eidoverse.AvatarMeasure: how tall a library body comes out of the native import, against the glTF's own numbers.
// A diagnostic for "this body is a different size here than in the browser": three.js skins through each joint's
// inverse bind matrix from the NODE rest pose; glTFRuntime builds its reference skeleton FROM the inverse bind
// matrices. Where a file's nodes and bind matrices disagree, the two clients start from different rest poses.
//
//   EIDO_AVATAR_MEASURE="eidoverse/assets/vrms/linn.vrm?v=…,eidoverse/assets/vrms/claude.vrm?v=…"
//   (library paths, as GET /avatars lists them; fetched read-only from the public library)
#include "EidoverseAvatar.h"
#include "Engine/SkeletalMesh.h"
#include "Misc/AutomationTest.h"
#include "ReferenceSkeleton.h"
#include "Tests/AutomationCommon.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEidoverseAvatarMeasure, "Eidoverse.AvatarMeasure", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FEidoverseAvatarMeasure::RunTest(const FString&)
{
    const FString List = FPlatformMisc::GetEnvironmentVariable(TEXT("EIDO_AVATAR_MEASURE"));
    if (List.IsEmpty()) { AddInfo(TEXT("skipped: set EIDO_AVATAR_MEASURE to library paths")); return true; }
    TArray<FString> Paths; List.ParseIntoArray(Paths, TEXT(","));
    struct FItem { FString Path; TStrongObjectPtr<UEidoverseAvatarAsset> Asset; bool bDone = false; };
    TSharedRef<TArray<FItem>> Items = MakeShared<TArray<FItem>>();
    for (const FString& P : Paths)
    {
        FString Url;
        if (!UEidoverseAvatarAsset::ResolveUrl(TEXT("https://eidoverse.animalabs.ai/library/"), P.TrimStartAndEnd(), Url)) { AddError(TEXT("not a library path: ") + P); continue; }
        FItem& I = Items->AddDefaulted_GetRef(); I.Path = P;
        I.Asset.Reset(NewObject<UEidoverseAvatarAsset>()); I.Asset->Url = Url; I.Asset->Start();
    }
    const double T0 = FPlatformTime::Seconds();
    ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this, Items, T0]() -> bool {
        bool bAll = true;
        for (FItem& I : *Items)
        {
            if (I.bDone) continue;
            UEidoverseAvatarAsset* A = I.Asset.Get();
            if (A->State == UEidoverseAvatarAsset::EState::Failed) { AddError(I.Path + TEXT(": ") + A->Failure); I.bDone = true; continue; }
            if (A->State != UEidoverseAvatarAsset::EState::Ready || !A->Mesh) { bAll = false; continue; }
            I.bDone = true;
            const FReferenceSkeleton& Ref = A->Mesh->GetRefSkeleton();
            const TArray<FTransform>& Local = Ref.GetRefBonePose();
            TArray<FTransform> Comp; Comp.SetNum(Local.Num());
            for (int32 B = 0; B < Local.Num(); ++B) { const int32 Pa = Ref.GetParentIndex(B); Comp[B] = Pa == INDEX_NONE ? Local[B] : Local[B] * Comp[Pa]; }
            auto Z = [&](const TCHAR* Human) -> FString
            {
                const FName* N = A->Humanoid.Find(Human); const int32 B = N ? Ref.FindBoneIndex(*N) : INDEX_NONE;
                return B == INDEX_NONE ? FString(TEXT("?")) : FString::Printf(TEXT("%.3f"), Comp[B].GetLocation().Z / 100);
            };
            const FBox Box = A->Mesh->GetImportedBounds().GetBox();
            double MaxScale = 0; for (const FTransform& T : Comp) MaxScale = FMath::Max(MaxScale, T.GetScale3D().GetAbsMax());
            AddInfo(FString::Printf(TEXT("%s: mesh bounds z %.3f..%.3f m (x %.3f, y %.3f wide); ref pose hips %s, head %s, leftHand %s m; largest bone scale %.3f; vrm0 %d"),
                *I.Path, Box.Min.Z / 100, Box.Max.Z / 100, Box.GetSize().X / 100, Box.GetSize().Y / 100, *Z(TEXT("hips")), *Z(TEXT("head")), *Z(TEXT("leftHand")), MaxScale, A->bVRM0 ? 1 : 0));
        }
        if (!bAll && FPlatformTime::Seconds() - T0 > 90) { AddError(TEXT("timed out loading")); return true; }
        return bAll;
    }));
    return true;
}
#endif
