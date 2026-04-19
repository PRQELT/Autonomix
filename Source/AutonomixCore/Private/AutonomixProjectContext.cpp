// Copyright Autonomix. All Rights Reserved.

#include "AutonomixProjectContext.h"

FString FAutonomixProjectContext::ToContextString() const
{
	FString Result;
	Result += FString::Printf(TEXT("Project: %s\n"), *ProjectName);
	Result += FString::Printf(TEXT("Engine: %s\n"), *EngineVersion);
	Result += FString::Printf(TEXT("Current Level: %s\n"), *CurrentLevelPath);
	Result += FString::Printf(TEXT("Modules: %d\n"), ProjectModules.Num());
	Result += FString::Printf(TEXT("Enabled Plugins: %d\n"), EnabledPlugins.Num());
	Result += FString::Printf(TEXT("Assets: %d total\n"), Assets.Num());

	for (const auto& Pair : AssetCountsByClass)
	{
		Result += FString::Printf(TEXT("  %s: %d\n"), *Pair.Key, Pair.Value);
	}

	Result += FString::Printf(TEXT("Content Files: %d\n"), ContentTree.Num());
	Result += FString::Printf(TEXT("Source Files: %d\n"), SourceTree.Num());

	return Result;
}

int32 FAutonomixProjectContext::EstimateTokenCount() const
{
	return ToContextString().Len() / 4;
}
