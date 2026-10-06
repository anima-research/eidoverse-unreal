using UnrealBuildTool;

public class EidoverseClient : ModuleRules
{
    public EidoverseClient(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        // Several sources define file-local wire helpers with `using namespace`; unity
        // blobs would merge them ambiguously. Each file must compile on its own.
        bUseUnity = false;
        PublicDependencyModuleNames.AddRange(new[] { "Core", "CoreUObject", "Engine", "InputCore", "ProceduralMeshComponent" });
        PrivateDependencyModuleNames.AddRange(new[] {
            "UnderwaterOptics",
            "Projects", "RenderCore", "RHI", "WebSockets", "Json", "HTTP", "ImageWrapper", "glTFRuntime",
            "Slate", "SlateCore", "UMG", "MeshDescription", "StaticMeshDescription"
        });
        if (Target.bBuildEditor) PrivateDependencyModuleNames.Add("UnrealEd");
        // Keychain for the saved production sign-in (EidoverseSecureStore).
        if (Target.Platform == UnrealTargetPlatform.Mac) PublicFrameworks.Add("Security");
        // Placed sounds (sounds.js): AudioToolbox decodes mp3/m4a/wav/aiff on the Mac,
        // the engine's Vorbis decodes ogg.
        if (Target.Platform == UnrealTargetPlatform.Mac) PublicFrameworks.Add("AudioToolbox");
        AddEngineThirdPartyPrivateStaticDependencies(Target, "UEOgg", "Vorbis", "VorbisFile");
        // Voice (EidoverseVoice*): UE's bundled libwebrtc (M109) for the SFU leg; AVFoundation for the
        // mic (AVAudioEngine voice processing + the TCC permission prompt). Other platforms build inert.
        if (Target.Platform == UnrealTargetPlatform.Mac)
        {
            AddEngineThirdPartyPrivateStaticDependencies(Target, "WebRTC", "libOpus");   // libwebrtc.a leaves Opus to UE's libOpus
            PublicFrameworks.AddRange(new[] { "AVFoundation", "CoreAudio" });
            PrivateDefinitions.Add("EIDOVERSE_VOICE_WEBRTC=1");
        }
        else PrivateDefinitions.Add("EIDOVERSE_VOICE_WEBRTC=0");
        PrivateDependencyModuleNames.Add("AudioExtensions");   // USoundWaveProcedural subclass (speaker waves)
    }
}
