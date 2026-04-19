// Copyright Autonomix. All Rights Reserved.

using UnrealBuildTool;

public class AutonomixActions : ModuleRules
{
	public AutonomixActions(ReadOnlyTargetRules Target) : base(Target)
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
			"AutonomixEngine"
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"UnrealEd",
			"AssetTools",
			"AssetRegistry",
			"Kismet",
			"KismetCompiler",
			"BlueprintGraph",
			"GraphEditor",
			"MaterialEditor",
			"Landscape",
			"FoliageEdit",
			"SourceControl",
			"DesktopPlatform",
			"Settings",
			"EditorScriptingUtilities",
			"MeshDescription",
			"StaticMeshDescription",
			"RenderCore",
			"RHI",
			"NavigationSystem",
			"AIModule",
			"LevelEditor",
			"MainFrame",
			"LiveCoding",
			"AutonomixLLM",
			// UMG / Widget Blueprint authoring
			"UMG",
			"UMGEditor",
			// Animation Blueprint authoring
			"AnimationBlueprintEditor",
			"AnimGraph",
			"AnimGraphRuntime",
			// Enhanced Input asset authoring
			"EnhancedInput",
			"InputBlueprintNodes",
			"InputCore",
			// Viewport capture (multimodal vision)
			"ImageWrapper",
			"Slate",
			"SlateCore",
			// Behavior Tree / AI
			"GameplayTasks",
			// Sequencer / Cinematics
			"LevelSequence",
			"MovieScene",
			"MovieSceneTracks",
			"MovieSceneTools",
			// Python scripting (conditional)
			"PythonScriptPlugin",
			// Data Validation (UEditorValidatorSubsystem)
			"DataValidation",
			// Gameplay Ability System
			"GameplayAbilities",
			"GameplayTags",
			"GameplayTagsEditor",
			"GameplayAbilitiesEditor"
		});

		// Conditionally add Python support
		if (Target.bBuildWithEditorOnlyData)
		{
			PrivateDefinitions.Add("WITH_PYTHON=1");
		}
	}
}
