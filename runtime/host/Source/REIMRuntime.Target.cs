using UnrealBuildTool;
using System.Collections.Generic;

public class REIMRuntimeTarget : TargetRules
{
	public REIMRuntimeTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Game;
		DefaultBuildSettings = BuildSettingsVersion.V7;
		IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_8;
		/* Shipping keeps UE's default (no engine log). The Host writes its own
		 * REIM-host.log next to the instance, including the DLL log callback,
		 * so startup/exit/fatal causes stay locatable in Shipping (M2-F3/H2). */
		ExtraModuleNames.AddRange(new string[] { "REIMGame", "REIMBridge", "REIMRenderer", "REIMUI", "REIMInput" });
	}
}
