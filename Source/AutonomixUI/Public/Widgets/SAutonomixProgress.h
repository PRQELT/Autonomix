// Copyright Autonomix. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/DeclarativeSyntaxSupport.h"

/**
 * Progress overlay for long-running operations (compilation, import, packaging).
 */
class AUTONOMIXUI_API SAutonomixProgress : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SAutonomixProgress) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	/** Show the progress indicator with a message */
	void ShowProgress(const FString& Message, float Progress = -1.0f);

	/** Update the progress value (0.0 - 1.0, or -1 for indeterminate) */
	void UpdateProgress(float Progress);

	/** Update the status message */
	void UpdateMessage(const FString& Message);

	/** Hide the progress indicator */
	void HideProgress();

private:
	TSharedPtr<class STextBlock> MessageText;
	TSharedPtr<class SProgressBar> ProgressBar;
	bool bVisible = false;
};
