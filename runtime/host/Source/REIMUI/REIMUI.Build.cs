using UnrealBuildTool;

public class REIMUI : ModuleRules
{
	public REIMUI(ReadOnlyTargetRules Target) : base(Target)
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
			"REIMInput"
		});
	}
}
