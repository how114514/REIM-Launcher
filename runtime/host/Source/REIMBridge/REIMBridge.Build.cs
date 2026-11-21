using UnrealBuildTool;

public class REIMBridge : ModuleRules
{
	public REIMBridge(ReadOnlyTargetRules Target) : base(Target)
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
			"Json",
			"RenderCore"
		});

		// Runtime/contract is shared by the DLL, Preview and UE Bridge.
		PublicIncludePaths.Add(ModuleDirectory + "/../../../contract");
	}
}
