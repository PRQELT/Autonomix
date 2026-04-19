// Copyright Autonomix. All Rights Reserved.

using UnrealBuildTool;

public class AutonomixLLM : ModuleRules
{
	public AutonomixLLM(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"HTTP",
			"Json",
			"JsonUtilities",
			"AutonomixCore"
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"ApplicationCore"
		});
	}
}
