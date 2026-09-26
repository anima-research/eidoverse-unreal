using UnrealBuildTool;

public class EidoverseClient : ModuleRules
{
    public EidoverseClient(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PublicDependencyModuleNames.AddRange(new[] { "Core", "CoreUObject", "Engine", "InputCore", "ProceduralMeshComponent" });
        PrivateDependencyModuleNames.AddRange(new[] {
            "UnderwaterOptics",
            "Projects", "RenderCore", "RHI", "WebSockets", "Json", "HTTP", "ImageWrapper", "glTFRuntime",
            "Slate", "SlateCore", "UMG", "MeshDescription", "StaticMeshDescription"
        });
        if (Target.bBuildEditor) PrivateDependencyModuleNames.Add("UnrealEd");
    }
}
