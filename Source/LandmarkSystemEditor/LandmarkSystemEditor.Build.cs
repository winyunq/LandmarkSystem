using UnrealBuildTool;

public class LandmarkSystemEditor : ModuleRules
{
	public LandmarkSystemEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;
		
		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
                "CoreUObject",
                "Engine",
                "Slate",
                "SlateCore",
                "UnrealEd",      // Required for Editor Modules
                "ComponentVisualizers", // For FComponentVisualizer
                "LandmarkSystem" // Dependency on Runtime module
			}
		);
			
		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
                "EditorStyle",
                "InputCore"
			}
		);
	}
}
