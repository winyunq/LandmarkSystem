using UnrealBuildTool;

public class LandmarkSystem : ModuleRules
{
	public LandmarkSystem(ReadOnlyTargetRules Target) : base(Target)
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
                "UMG",
                "Json",
                "JsonUtilities",
                "MassEntity",
                "MassCommon",
                "MassRepresentation",
                "MassLOD",
                "MassAPI",
                "MassBattle",
                "RTSCommandSystem",
                "DeveloperSettings"
			}
		);
			
		
		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				
			}
		);
	}
}
