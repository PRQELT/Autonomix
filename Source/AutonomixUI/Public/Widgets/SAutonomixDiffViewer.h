// Copyright Autonomix. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/DeclarativeSyntaxSupport.h"

/**
 * Side-by-side diff viewer for file modifications proposed by the AI.
 */
class AUTONOMIXUI_API SAutonomixDiffViewer : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SAutonomixDiffViewer) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	/** Show a diff between original and modified content */
	void ShowDiff(const FString& FilePath, const FString& OriginalContent, const FString& ModifiedContent);

	/** Clear the diff view */
	void ClearDiff();

private:
	TSharedPtr<class STextBlock> OriginalText;
	TSharedPtr<class STextBlock> ModifiedText;
	TSharedPtr<class STextBlock> FilePathText;
};
