// Copyright Autonomix. All Rights Reserved.

using UnrealBuildTool;

public class AutonomixEngine : ModuleRules
{
	public AutonomixEngine(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"Json",
			"JsonUtilities",
			"AutonomixCore",
			"AutonomixLLM"
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"UnrealEd",
			"AssetTools",
			"AssetRegistry",
			"SourceControl",
			"Settings",
			"ContentBrowser",
			"EditorSubsystem",
			"Slate",
			"SlateCore",
			"InputCore",
			// Phase 1: AutonomixIgnoreController + AutonomixFileContextTracker use IDirectoryWatcher
			"DirectoryWatcher",
			// Phase 2: AutonomixEnvironmentDetails uses blueprint/level editor APIs
			"LevelEditor",
			"MessageLog",
		});
	}
}
