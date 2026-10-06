// The native chunker against the web's own output (Tools/eidoverse/tts_vectors.json,
// made by tts_vectors.ts from client/lib/tts-chunk.js).
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "EidoverseTTS.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEidoverseTtsChunksTest, "Eidoverse.TtsChunks", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEidoverseTtsChunksTest::RunTest(const FString&)
{
    FString Json;
    const FString Path = FPaths::Combine(FPaths::ProjectDir(), TEXT("Tools/eidoverse/tts_vectors.json"));
    if (!FFileHelper::LoadFileToString(Json, *Path)) { AddError(TEXT("no vectors at ") + Path); return false; }
    TArray<TSharedPtr<FJsonValue>> Cases;
    if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Cases)) { AddError(TEXT("bad vectors")); return false; }
    int32 Pass = 0;
    for (const auto& C : Cases)
    {
        const TSharedPtr<FJsonObject> O = C->AsObject();
        const FString Text = O->GetStringField(TEXT("text"));
        const FString Spoken = EidoTts::SpokenForm(Text);
        TestEqual(TEXT("spoken: ") + Text.Left(40), Spoken, O->GetStringField(TEXT("spoken")));
        TArray<FString> Want; for (const auto& V : O->GetArrayField(TEXT("chunks"))) Want.Add(V->AsString());
        const TArray<FString> Got = EidoTts::Chunks(Text);
        const bool bSame = Got == Want;
        if (!bSame) AddError(FString::Printf(TEXT("chunks differ for \"%s\":\n  got  [%s]\n  want [%s]"), *Text.Left(60), *FString::Join(Got, TEXT(" | ")), *FString::Join(Want, TEXT(" | "))));
        Pass += bSame && Spoken == O->GetStringField(TEXT("spoken"));
    }
    AddInfo(FString::Printf(TEXT("%d/%d match"), Pass, Cases.Num()));
    return true;
}
