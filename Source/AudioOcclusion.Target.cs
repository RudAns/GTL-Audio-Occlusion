using UnrealBuildTool;

public class AudioOcclusionTarget : TargetRules
{
	public AudioOcclusionTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Game;
		DefaultBuildSettings = BuildSettingsVersion.Latest;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		ExtraModuleNames.AddRange(new string[] { "AudioOcclusion", "OcclusionWave" });
	}
}
