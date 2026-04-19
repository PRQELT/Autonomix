// Copyright Autonomix. All Rights Reserved.

#include "Level/AutonomixLevelActions.h"
#include "AutonomixCoreModule.h"
#include "Editor.h"
#include "EditorLevelUtils.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameFramework/WorldSettings.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/DefaultPawn.h"
#include "Components/StaticMeshComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/PointLight.h"
#include "Engine/DirectionalLight.h"
#include "Engine/Blueprint.h"
#include "FileHelpers.h"
#include "LevelEditor.h"
#include "EditorActorFolders.h"
#include "ScopedTransaction.h"

FAutonomixLevelActions::FAutonomixLevelActions() {}
FAutonomixLevelActions::~FAutonomixLevelActions() {}
FName FAutonomixLevelActions::GetActionName() const { return FName(TEXT("Level")); }
FText FAutonomixLevelActions::GetDisplayName() const { return FText::FromString(TEXT("Level Actions")); }
EAutonomixActionCategory FAutonomixLevelActions::GetCategory() const { return EAutonomixActionCategory::Level; }
EAutonomixRiskLevel FAutonomixLevelActions::GetDefaultRiskLevel() const { return EAutonomixRiskLevel::Medium; }
bool FAutonomixLevelActions::CanUndo() const { return true; }
bool FAutonomixLevelActions::UndoAction() { return false; }

TArray<FString> FAutonomixLevelActions::GetSupportedToolNames() const
{
	return {
		TEXT("spawn_actor"),
		TEXT("place_light"),
		TEXT("modify_world_settings")
	};
}

bool FAutonomixLevelActions::ValidateParams(const TSharedRef<FJsonObject>& Params, TArray<FString>& OutErrors) const { return true; }

FAutonomixActionPlan FAutonomixLevelActions::PreviewAction(const TSharedRef<FJsonObject>& Params)
{
	FAutonomixActionPlan Plan;
	Plan.Summary = TEXT("Level/World operation");
	FAutonomixAction Action;
	Action.Description = Plan.Summary;
	Action.Category = EAutonomixActionCategory::Level;
	Action.RiskLevel = EAutonomixRiskLevel::Medium;
	Plan.Actions.Add(Action);
	return Plan;
}

FAutonomixActionResult FAutonomixLevelActions::ExecuteAction(const TSharedRef<FJsonObject>& Params)
{
	FScopedTransaction Transaction(FText::FromString(TEXT("Autonomix Level Action")));

	FAutonomixActionResult Result;
	Result.bSuccess = false;

	if (!GEditor)
	{
		Result.Errors.Add(TEXT("Editor not available."));
		return Result;
	}

	UWorld* World = GEditor->GetEditorWorldContext().World();
	if (!World)
	{
		Result.Errors.Add(TEXT("No world loaded."));
		return Result;
	}

	World->Modify();

	// Dispatch by tool name injected by ActionRouter
	FString ToolName;
	Params->TryGetStringField(TEXT("_tool_name"), ToolName);

	if (ToolName == TEXT("spawn_actor"))
	{
		return ExecuteSpawnActor(Params, World);
	}
	else if (ToolName == TEXT("place_light"))
	{
		return ExecutePlaceLight(Params, World);
	}
	else if (ToolName == TEXT("modify_world_settings"))
	{
		return ExecuteModifyWorldSettings(Params, World);
	}

	// Legacy fallback: dispatch by "action" field
	FString Action;
	if (Params->TryGetStringField(TEXT("action"), Action))
	{
		if (Action == TEXT("spawn_actor")) return ExecuteSpawnActor(Params, World);
		else if (Action == TEXT("place_light")) return ExecutePlaceLight(Params, World);
		else if (Action == TEXT("modify_world_settings")) return ExecuteModifyWorldSettings(Params, World);
	}

	Result.Errors.Add(FString::Printf(TEXT("Unknown level action: %s"), *ToolName));
	return Result;
}

FAutonomixActionResult FAutonomixLevelActions::ExecuteSpawnActor(const TSharedRef<FJsonObject>& Params, UWorld* World)
{
	FAutonomixActionResult Result;
	Result.bSuccess = false;

	FString ClassName;
	if (!Params->TryGetStringField(TEXT("class"), ClassName))
	{
		Result.Errors.Add(TEXT("Missing 'class' field for spawn_actor."));
		return Result;
	}

	FVector Location = FVector::ZeroVector;
	FRotator Rotation = FRotator::ZeroRotator;
	const TSharedPtr<FJsonObject>* LocObj = nullptr;
	if (Params->TryGetObjectField(TEXT("location"), LocObj))
	{
		(*LocObj)->TryGetNumberField(TEXT("x"), Location.X);
		(*LocObj)->TryGetNumberField(TEXT("y"), Location.Y);
		(*LocObj)->TryGetNumberField(TEXT("z"), Location.Z);
	}
	const TSharedPtr<FJsonObject>* RotObj = nullptr;
	if (Params->TryGetObjectField(TEXT("rotation"), RotObj))
	{
		(*RotObj)->TryGetNumberField(TEXT("pitch"), Rotation.Pitch);
		(*RotObj)->TryGetNumberField(TEXT("yaw"), Rotation.Yaw);
		(*RotObj)->TryGetNumberField(TEXT("roll"), Rotation.Roll);
	}

	// Resolve class: built-in names or Blueprint content paths
	UClass* ActorClass = nullptr;

	// Check if it's a Blueprint content path (starts with /Game/)
	if (ClassName.StartsWith(TEXT("/Game/")) || ClassName.StartsWith(TEXT("/Script/")))
	{
		UBlueprint* BP = LoadObject<UBlueprint>(nullptr, *ClassName);
		if (BP && BP->GeneratedClass)
		{
			ActorClass = BP->GeneratedClass;
		}
		else
		{
			// Try loading as UClass directly
			ActorClass = LoadObject<UClass>(nullptr, *ClassName);
		}
	}

	if (!ActorClass)
	{
		// Try built-in class names
		ActorClass = FindFirstObject<UClass>(*ClassName, EFindFirstObjectOptions::None);
	}

	if (!ActorClass)
	{
		if (ClassName == TEXT("StaticMeshActor")) ActorClass = AStaticMeshActor::StaticClass();
		else if (ClassName == TEXT("PointLight")) ActorClass = APointLight::StaticClass();
		else if (ClassName == TEXT("DirectionalLight")) ActorClass = ADirectionalLight::StaticClass();
		else if (ClassName == TEXT("PlayerStart"))
		{
			ActorClass = FindFirstObject<UClass>(TEXT("PlayerStart"), EFindFirstObjectOptions::None);
			if (!ActorClass) ActorClass = AActor::StaticClass();
		}
		else ActorClass = AActor::StaticClass();
	}

	FActorSpawnParameters SpawnParams;
	FString ActorLabel;
	if (Params->TryGetStringField(TEXT("label"), ActorLabel))
	{
		SpawnParams.Name = FName(*ActorLabel);
	}

	AActor* NewActor = World->SpawnActor<AActor>(ActorClass, Location, Rotation, SpawnParams);
	if (NewActor)
	{
		if (!ActorLabel.IsEmpty())
		{
			NewActor->SetActorLabel(ActorLabel);
		}

		Result.bSuccess = true;
		Result.ResultMessage = FString::Printf(TEXT("Spawned %s at (%s)"), *ClassName, *Location.ToString());
	}
	else
	{
		Result.Errors.Add(FString::Printf(TEXT("Failed to spawn actor of class %s"), *ClassName));
	}

	return Result;
}

FAutonomixActionResult FAutonomixLevelActions::ExecutePlaceLight(const TSharedRef<FJsonObject>& Params, UWorld* World)
{
	FAutonomixActionResult Result;
	Result.bSuccess = false;

	FString LightType = TEXT("PointLight");
	Params->TryGetStringField(TEXT("light_type"), LightType);

	FVector Location = FVector::ZeroVector;
	const TSharedPtr<FJsonObject>* LocObj = nullptr;
	if (Params->TryGetObjectField(TEXT("location"), LocObj))
	{
		(*LocObj)->TryGetNumberField(TEXT("x"), Location.X);
		(*LocObj)->TryGetNumberField(TEXT("y"), Location.Y);
		(*LocObj)->TryGetNumberField(TEXT("z"), Location.Z);
	}

	UClass* LightClass = APointLight::StaticClass();
	if (LightType == TEXT("DirectionalLight")) LightClass = ADirectionalLight::StaticClass();

	AActor* Light = World->SpawnActor<AActor>(LightClass, Location, FRotator::ZeroRotator);
	if (Light)
	{
		Result.bSuccess = true;
		Result.ResultMessage = FString::Printf(TEXT("Placed %s at %s"), *LightType, *Location.ToString());
	}
	else
	{
		Result.Errors.Add(TEXT("Failed to place light."));
	}

	return Result;
}

FAutonomixActionResult FAutonomixLevelActions::ExecuteModifyWorldSettings(const TSharedRef<FJsonObject>& Params, UWorld* World)
{
	FAutonomixActionResult Result;
	Result.bSuccess = false;

	AWorldSettings* WorldSettings = World->GetWorldSettings();
	if (!WorldSettings)
	{
		Result.Errors.Add(TEXT("Could not access World Settings."));
		return Result;
	}

	WorldSettings->Modify();

	TArray<FString> Changes;

	// Game Mode Override
	FString GameModeOverride;
	if (Params->TryGetStringField(TEXT("game_mode_override"), GameModeOverride))
	{
		UClass* GameModeClass = nullptr;

		// Try loading as Blueprint content path
		if (GameModeOverride.StartsWith(TEXT("/Game/")) || GameModeOverride.StartsWith(TEXT("/Script/")))
		{
			UBlueprint* BP = LoadObject<UBlueprint>(nullptr, *GameModeOverride);
			if (BP && BP->GeneratedClass && BP->GeneratedClass->IsChildOf(AGameModeBase::StaticClass()))
			{
				GameModeClass = BP->GeneratedClass;
			}
			else
			{
				// Try as class path directly
				GameModeClass = LoadObject<UClass>(nullptr, *GameModeOverride);
			}
		}

		if (!GameModeClass)
		{
			// Try by name
			GameModeClass = FindFirstObject<UClass>(*GameModeOverride, EFindFirstObjectOptions::None);
		}

		if (GameModeClass && GameModeClass->IsChildOf(AGameModeBase::StaticClass()))
		{
			WorldSettings->DefaultGameMode = TSubclassOf<AGameModeBase>(GameModeClass);
			Changes.Add(FString::Printf(TEXT("GameMode override set to %s"), *GameModeClass->GetName()));
		}
		else
		{
			Result.Warnings.Add(FString::Printf(TEXT("Could not find GameMode class: %s"), *GameModeOverride));
		}
	}

	// Default Pawn Class (on the GameMode CDO if accessible)
	FString DefaultPawnClass;
	if (Params->TryGetStringField(TEXT("default_pawn_class"), DefaultPawnClass))
	{
		Result.Warnings.Add(TEXT("default_pawn_class should be set on the GameMode Blueprint's defaults using set_blueprint_defaults tool instead."));
	}

	// Kill Z
	double KillZ;
	if (Params->TryGetNumberField(TEXT("kill_z"), KillZ))
	{
		WorldSettings->KillZ = KillZ;
		Changes.Add(FString::Printf(TEXT("KillZ set to %.1f"), KillZ));
	}

	if (Changes.Num() > 0)
	{
		Result.bSuccess = true;
		Result.ResultMessage = TEXT("World Settings updated: ") + FString::Join(Changes, TEXT(", "));

		// Mark the package as dirty so it saves
		WorldSettings->MarkPackageDirty();
	}
	else
	{
		Result.Errors.Add(TEXT("No valid world settings properties were provided to modify."));
	}

	return Result;
}
