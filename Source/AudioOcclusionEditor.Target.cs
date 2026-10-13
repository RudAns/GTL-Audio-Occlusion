using UnrealBuildTool;

public class AudioOcclusionEditorTarget : TargetRules
{
	public AudioOcclusionEditorTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Editor;
		DefaultBuildSettings = BuildSettingsVersion.Latest;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		ExtraModuleNames.AddRange(new string[] { "AudioOcclusion", "OcclusionWave" });
	}
}
