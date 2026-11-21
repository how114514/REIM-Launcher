using UnrealBuildTool;

public class REIMGame : ModuleRules
{
	public REIMGame(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine"
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"REIMBridge",
			"REIMRenderer",
			"REIMUI",
			"REIMInput"
		});
	}
}
