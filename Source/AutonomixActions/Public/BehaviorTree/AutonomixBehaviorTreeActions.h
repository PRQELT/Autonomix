// Copyright Autonomix. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AutonomixInterfaces.h"

/**
 * FAutonomixBehaviorTreeActions
 *
 * Gameplay AI tools for creating and configuring Behavior Trees:
 *   - create_blackboard: Create Blackboard assets with typed keys
 *   - create_behavior_tree: Create Behavior Tree assets and assign Blackboards
 *   - inject_bt_nodes: Add Selectors, Sequences, Decorators, Services programmatically
 *   - configure_navmesh: Spawn NavMeshBoundsVolumes and trigger NavMesh builds
 *
 * Behavior Trees are Unreal's primary AI decision-making system. Combined with
 * the Blackboard (shared memory), they let the AI create NPC behavior patterns
 * like patrol → investigate → attack → flee.
 */
class AUTONOMIXACTIONS_API FAutonomixBehaviorTreeActions : public IAutonomixActionExecutor
{
public:
	FAutonomixBehaviorTreeActions();
	virtual ~FAutonomixBehaviorTreeActions();

	virtual FName GetActionName() const override;
	virtual FText GetDisplayName() const override;
	virtual EAutonomixActionCategory GetCategory() const override;
	virtual EAutonomixRiskLevel GetDefaultRiskLevel() const override;
	virtual FAutonomixActionPlan PreviewAction(const TSharedRef<FJsonObject>& Params) override;
	virtual FAutonomixActionResult ExecuteAction(const TSharedRef<FJsonObject>& Params) override;
	virtual bool CanUndo() const override;
	virtual bool UndoAction() override;
	virtual TArray<FString> GetSupportedToolNames() const override;
	virtual bool ValidateParams(const TSharedRef<FJsonObject>& Params, TArray<FString>& OutErrors) const override;

private:
	/** Create a Blackboard asset with typed keys (Bool, Int, Float, String, Vector, Object, Enum, Class). */
	FAutonomixActionResult ExecuteCreateBlackboard(const TSharedRef<FJsonObject>& Params, FAutonomixActionResult& Result);

	/** Create a Behavior Tree asset and optionally assign a Blackboard. */
	FAutonomixActionResult ExecuteCreateBehaviorTree(const TSharedRef<FJsonObject>& Params, FAutonomixActionResult& Result);

	/** Inject nodes into an existing BT (Selectors, Sequences, Tasks, Decorators, Services). */
	FAutonomixActionResult ExecuteInjectBTNodes(const TSharedRef<FJsonObject>& Params, FAutonomixActionResult& Result);

	/** Spawn a NavMeshBoundsVolume, scale to level, and trigger NavMesh rebuild. */
	FAutonomixActionResult ExecuteConfigureNavMesh(const TSharedRef<FJsonObject>& Params, FAutonomixActionResult& Result);
};
