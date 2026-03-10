// Copyright Autonomix. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "AutonomixTaskHistory.h"

DECLARE_DELEGATE_OneParam(FOnAutonomixLoadHistoryTask, const FString& /*TabId*/);
DECLARE_DELEGATE_OneParam(FOnAutonomixDeleteHistoryTask, const FString& /*TabId*/);

/**
 * Task history browser panel.
 * Ported from Roo Code's HistoryView.tsx.
 *
 * Shows all past sessions with search, date grouping,
 * resume and delete actions.
 */
class AUTONOMIXUI_API SAutonomixHistoryPanel : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SAutonomixHistoryPanel) {}
		SLATE_EVENT(FOnAutonomixLoadHistoryTask, OnLoadTask)
		SLATE_EVENT(FOnAutonomixDeleteHistoryTask, OnDeleteTask)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	/** Refresh history list */
	void RefreshHistory(const TArray<FAutonomixTaskHistoryItem>& Items);

private:
	TSharedPtr<class SScrollBox> HistoryList;
	TSharedPtr<class SEditableTextBox> SearchBox;
	TArray<FAutonomixTaskHistoryItem> AllItems;
	FString CurrentSearchQuery;

	FOnAutonomixLoadHistoryTask OnLoadTask;
	FOnAutonomixDeleteHistoryTask OnDeleteTask;

	void OnSearchTextChanged(const FText& NewText);
	void RebuildList();
	TSharedRef<SWidget> BuildHistoryItem(const FAutonomixTaskHistoryItem& Item);
	static FString FormatRelativeTime(const FDateTime& Timestamp);
};
