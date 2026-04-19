// Copyright Autonomix. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "AutonomixTaskHistory.h"

DECLARE_DELEGATE_OneParam(FOnAutonomixLoadHistoryTask, const FString& /*TabId*/);
DECLARE_DELEGATE_OneParam(FOnAutonomixDeleteHistoryTask, const FString& /*TabId*/);
DECLARE_DELEGATE_TwoParams(FOnAutonomixRenameHistoryTask, const FString& /*TabId*/, const FString& /*NewTitle*/);

/** Sort modes for history list */
enum class EAutonomixHistorySortMode : uint8
{
	Newest,        // by LastActiveAt descending (default)
	Oldest,        // by LastActiveAt ascending
	MostExpensive, // by TotalCostUSD descending
	MostTokens     // by total tokens descending
};

/** Date group categories for history items */
enum class EAutonomixDateGroup : uint8
{
	Today,
	Yesterday,
	Previous7Days,
	Previous30Days,
	OlderMonth  // grouped by month name
};

/**
 * Enhanced task history browser panel.
 *
 * v5.0: Major rewrite with date grouping, rich per-entry display,
 * sort options, inline rename, delete confirmation, and search.
 *
 * Ported from Roo Code's HistoryView.tsx.
 */
class AUTONOMIXUI_API SAutonomixHistoryPanel : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SAutonomixHistoryPanel) {}
		SLATE_EVENT(FOnAutonomixLoadHistoryTask, OnLoadTask)
		SLATE_EVENT(FOnAutonomixDeleteHistoryTask, OnDeleteTask)
		SLATE_EVENT(FOnAutonomixRenameHistoryTask, OnRenameTask)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	/** Refresh history list from external data */
	void RefreshHistory(const TArray<FAutonomixTaskHistoryItem>& Items);

	/** Auto-generate a title from the first user message (public for MainPanel access) */
	static FString GenerateAutoTitle(const FString& FirstUserMessage);

private:
	// ---- Data ----
	TSharedPtr<class SScrollBox> HistoryList;
	TSharedPtr<class SEditableTextBox> SearchBox;
	TArray<FAutonomixTaskHistoryItem> AllItems;
	FString CurrentSearchQuery;
	EAutonomixHistorySortMode CurrentSortMode = EAutonomixHistorySortMode::Newest;

	// ---- Delegates ----
	FOnAutonomixLoadHistoryTask OnLoadTask;
	FOnAutonomixDeleteHistoryTask OnDeleteTask;
	FOnAutonomixRenameHistoryTask OnRenameTask;

	// ---- Inline rename state ----
	FString RenamingTabId;  // empty = not renaming
	TSharedPtr<class SEditableTextBox> RenameTextBox;

	// ---- Event handlers ----
	void OnSearchTextChanged(const FText& NewText);
	void OnSortModeChanged(EAutonomixHistorySortMode NewMode);

	// ---- Core rebuild ----
	void RebuildList();

	/** Apply current search filter to items */
	TArray<FAutonomixTaskHistoryItem> GetFilteredItems() const;

	/** Sort items by current sort mode */
	void SortItems(TArray<FAutonomixTaskHistoryItem>& Items) const;

	/** Determine the date group for a timestamp */
	static EAutonomixDateGroup GetDateGroup(const FDateTime& Timestamp);

	/** Get display string for a date group */
	static FString GetDateGroupLabel(EAutonomixDateGroup Group, const FDateTime& SampleTimestamp);

	// ---- Widget builders ----
	TSharedRef<SWidget> BuildDateGroupHeader(const FString& GroupLabel);
	TSharedRef<SWidget> BuildHistoryEntry(const FAutonomixTaskHistoryItem& Item);
	TSharedRef<SWidget> BuildSortDropdown();

	// ---- Formatting helpers ----
	static FString FormatRelativeTime(const FDateTime& Timestamp);
	static FString FormatTokenCount(int32 TotalTokens);
	static FString FormatCost(float CostUSD);
	static FString GetStatusIcon(EAutonomixTaskStatus Status);
	static FLinearColor GetStatusColor(EAutonomixTaskStatus Status);
	static FString TruncateTitle(const FString& Title, int32 MaxChars = 60);

	// ---- Actions ----
	void OnOpenClicked(const FString& TabId);
	void OnDeleteClicked(const FString& TabId);
	void OnRenameStarted(const FString& TabId, const FString& CurrentTitle);
	void OnRenameCommitted(const FText& NewText, ETextCommit::Type CommitType);
};
