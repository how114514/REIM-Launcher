using UnrealBuildTool;
using System.Collections.Generic;

public class REIMEditorTarget : TargetRules
{
	public REIMEditorTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Editor;
		DefaultBuildSettings = BuildSettingsVersion.V7;
		IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_8;
		ExtraModuleNames.AddRange(new string[] { "REIMGame", "REIMBridge", "REIMRenderer", "REIMUI", "REIMInput" });
	}
}
