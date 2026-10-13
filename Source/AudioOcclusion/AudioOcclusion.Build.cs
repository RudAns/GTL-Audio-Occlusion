using UnrealBuildTool;

public class AudioOcclusion : ModuleRules
{
	public AudioOcclusion(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PublicDependencyModuleNames.AddRange(new string[] {
			"Core", "CoreUObject", "Engine", "InputCore", "ApplicationCore", "EnhancedInput",
			"UMG", "Slate", "SlateCore", "AudioMixer", "AudioExtensions", "AudioGameplayVolume",
			"MetasoundEngine", "Synthesis", "NavigationSystem", "AIModule", "Niagara",
			"RenderCore", "RHI", "OcclusionWave", "SteamAudio"
		});
	}
}
