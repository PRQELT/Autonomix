// Copyright Autonomix. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AutonomixInterfaces.h"

/**
 * FAutonomixSequencerActions
 *
 * Provides tools for creating and editing Level Sequences (cinematics/timelines):
 *   - create_level_sequence: Create a LevelSequence asset and optionally spawn it
 *   - add_sequencer_track: Add Actor, Camera, Transform, or Audio tracks
 *   - add_sequencer_keyframe: Animate properties over time (transforms, floats, etc.)
 *
 * Level Sequences are used for cutscenes, camera animations, gameplay timelines,
 * and any property animation that needs precise timing control.
 */
class AUTONOMIXACTIONS_API FAutonomixSequencerActions : public IAutonomixActionExecutor
{
public:
	FAutonomixSequencerActions();
	virtual ~FAutonomixSequencerActions();

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
	/** Create a new LevelSequence asset. */
	FAutonomixActionResult ExecuteCreateLevelSequence(const TSharedRef<FJsonObject>& Params, FAutonomixActionResult& Result);

	/** Add a track to an existing LevelSequence (Actor binding, Camera cut, Audio, etc.). */
	FAutonomixActionResult ExecuteAddSequencerTrack(const TSharedRef<FJsonObject>& Params, FAutonomixActionResult& Result);

	/** Add a keyframe to a track property at a specific time. */
	FAutonomixActionResult ExecuteAddSequencerKeyframe(const TSharedRef<FJsonObject>& Params, FAutonomixActionResult& Result);
};
