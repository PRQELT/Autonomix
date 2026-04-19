// Copyright Autonomix. All Rights Reserved.

using UnrealBuildTool;

public class AutonomixUI : ModuleRules
{
	public AutonomixUI(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"InputCore",
			"Slate",
			"SlateCore",
			"AutonomixCore",
			"AutonomixLLM"
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"UnrealEd",
			"EditorStyle",
			"ToolMenus",
			"Projects",
			"EditorSubsystem",
			"ApplicationCore",
			"Json",
			"JsonUtilities",
			"AutonomixEngine",
			"AutonomixActions",
			// For SAutonomixFileChangesPanel: open files in IDE
			"SourceCodeAccess",
		});
	}
}
