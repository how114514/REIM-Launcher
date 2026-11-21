using UnrealBuildTool;

public class REIMRenderer : ModuleRules
{
	public REIMRenderer(ReadOnlyTargetRules Target) : base(Target)
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
			"InputCore",
			"RenderCore",
			"RHI"
		});
	}
}
