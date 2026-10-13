using UnrealBuildTool;

public class OcclusionWave : ModuleRules
{
	public OcclusionWave(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "RenderCore", "RHI", "Renderer", "Projects" });
	}
}
