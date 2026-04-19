// Copyright Autonomix. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AutonomixTypes.h"
#include "AutonomixProjectContext.generated.h"

/**
 * Serializable snapshot of the current project state.
 * Sent to the AI as context with every request.
 */
USTRUCT(BlueprintType)
struct AUTONOMIXCORE_API FAutonomixProjectContext
{
	GENERATED_BODY()

	/** Name of the Unreal project */
	UPROPERTY(BlueprintReadOnly, Category = "Autonomix")
	FString ProjectName;

	/** Engine version string (e.g. "5.5.0") */
	UPROPERTY(BlueprintReadOnly, Category = "Autonomix")
	FString EngineVersion;

	/** Absolute path to the project root */
	UPROPERTY(BlueprintReadOnly, Category = "Autonomix")
	FString ProjectRootPath;

	/** Content directory tree entries */
	UPROPERTY(BlueprintReadOnly, Category = "Autonomix")
	TArray<FAutonomixFileEntry> ContentTree;

	/** Source directory tree entries */
	UPROPERTY(BlueprintReadOnly, Category = "Autonomix")
	TArray<FAutonomixFileEntry> SourceTree;

	/** Config directory tree entries */
	UPROPERTY(BlueprintReadOnly, Category = "Autonomix")
	TArray<FAutonomixFileEntry> ConfigTree;

	/** All registered assets in the project */
	UPROPERTY(BlueprintReadOnly, Category = "Autonomix")
	TArray<FAutonomixAssetEntry> Assets;

	/** Summary counts by asset type */
	UPROPERTY(BlueprintReadOnly, Category = "Autonomix")
	TMap<FString, int32> AssetCountsByClass;

	/** Currently loaded/open level path */
	UPROPERTY(BlueprintReadOnly, Category = "Autonomix")
	FString CurrentLevelPath;

	/** List of modules in the project (from .uproject) */
	UPROPERTY(BlueprintReadOnly, Category = "Autonomix")
	TArray<FString> ProjectModules;

	/** List of enabled plugins */
	UPROPERTY(BlueprintReadOnly, Category = "Autonomix")
	TArray<FString> EnabledPlugins;

	/** Target platform(s) configured */
	UPROPERTY(BlueprintReadOnly, Category = "Autonomix")
	TArray<FString> TargetPlatforms;

	/** Timestamp of when this context was captured */
	UPROPERTY(BlueprintReadOnly, Category = "Autonomix")
	FDateTime CaptureTimestamp;

	FAutonomixProjectContext()
		: CaptureTimestamp(FDateTime::UtcNow())
	{
	}

	/** Serialize the entire context to a compressed string for the AI system prompt */
	FString ToContextString() const;

	/** Estimate the approximate token count of the serialized context */
	int32 EstimateTokenCount() const;
};
