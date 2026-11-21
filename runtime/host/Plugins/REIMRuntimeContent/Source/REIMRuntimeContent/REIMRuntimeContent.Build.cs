using UnrealBuildTool;

public class REIMRuntimeContent : ModuleRules
{
	public REIMRuntimeContent(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine"
		});

		// Host-side template only. The version-specific mc_26.3.dll and generated
		// data are supplied by NativeVersion / the user's instance.json (M2-A4/F2).
		RuntimeDependencies.Add("$(PluginDir)/Config/instance.json.template");
		RuntimeDependencies.Add("$(PluginDir)/Config/README.txt");
	}
}
