// Copyright Autonomix. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "AutonomixCheckpointManager.h"

DECLARE_DELEGATE_OneParam(FOnAutonomixRestoreCheckpoint, const FString& /*CommitHash*/);
DECLARE_DELEGATE_OneParam(FOnAutonomixViewCheckpointDiff, const FString& /*CommitHash*/);

/**
 * Checkpoint timeline panel.
 * Ported from Roo Code's CheckpointRestoreDialog.tsx.
 *
 * Shows a scrollable list of git checkpoints for the current session.
 * Each entry: timestamp, description, modified file count.
 * Buttons: "Restore" and "View Diff".
 */
class AUTONOMIXUI_API SAutonomixCheckpointPanel : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SAutonomixCheckpointPanel) {}
		SLATE_EVENT(FOnAutonomixRestoreCheckpoint, OnRestoreCheckpoint)
		SLATE_EVENT(FOnAutonomixViewCheckpointDiff, OnViewDiff)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	/** Refresh the checkpoint list from the checkpoint manager */
	void RefreshCheckpoints(const TArray<FAutonomixCheckpoint>& Checkpoints);

private:
	TSharedPtr<class SScrollBox> CheckpointList;
	FOnAutonomixRestoreCheckpoint OnRestoreCheckpoint;
	FOnAutonomixViewCheckpointDiff OnViewDiff;

	TSharedRef<SWidget> BuildCheckpointRow(const FAutonomixCheckpoint& Checkpoint);
};
