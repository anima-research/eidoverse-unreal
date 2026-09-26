using UnrealBuildTool;
public class UnderwaterOptics : ModuleRules
{
    public UnderwaterOptics(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PublicDependencyModuleNames.AddRange(new[] { "Core", "CoreUObject", "Engine", "InputCore", "ProceduralMeshComponent" });
        PrivateDependencyModuleNames.AddRange(new[] { "Projects", "RenderCore", "RHI", "WebSockets", "Json", "HTTP", "ImageWrapper", "glTFRuntime", "Slate", "SlateCore", "MeshDescription", "StaticMeshDescription" });
        if (Target.bBuildEditor) PrivateDependencyModuleNames.Add("UnrealEd");
    }
}
