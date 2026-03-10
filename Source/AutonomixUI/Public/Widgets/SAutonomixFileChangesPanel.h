// Copyright Autonomix. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/DeclarativeSyntaxSupport.h"

/**
 * File changes panel showing all files modified during the session.
 * Ported from Roo Code's FileChangesPanel.tsx.
 *
 * Groups files as Created or Modified.
 * Clicking a file opens it in the UE source editor.
 */
class AUTONOMIXUI_API SAutonomixFileChangesPanel : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SAutonomixFileChangesPanel) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	/** Add a modified file to the panel */
	void AddModifiedFile(const FString& RelativePath, bool bWasCreated = false);

	/** Clear all tracked changes */
	void ClearChanges();

	/** Get count of tracked changed files */
	int32 GetChangeCount() const { return ChangedFiles.Num(); }

private:
	TSharedPtr<class SScrollBox> FileList;

	struct FFileChange
	{
		FString RelativePath;
		bool bWasCreated;
	};
	TArray<FFileChange> ChangedFiles;

	void RebuildList();
	TSharedRef<SWidget> BuildFileRow(const FFileChange& Change);
};
