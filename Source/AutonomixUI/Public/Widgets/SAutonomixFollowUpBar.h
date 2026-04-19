// Copyright Autonomix. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/DeclarativeSyntaxSupport.h"

DECLARE_DELEGATE_OneParam(FOnAutonomixFollowUpSelected, const FString& /*PromptText*/);

/**
 * Follow-up suggestion bar shown after attempt_completion.
 * Ported from Roo Code's FollowUpSuggest.tsx.
 *
 * Shows 3-4 clickable suggestion buttons related to the completed task.
 * Clicking a suggestion auto-submits it as the next prompt.
 */
class AUTONOMIXUI_API SAutonomixFollowUpBar : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SAutonomixFollowUpBar) {}
		SLATE_EVENT(FOnAutonomixFollowUpSelected, OnFollowUpSelected)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	/**
	 * Show follow-up suggestions based on completed task result.
	 * Analyzes result text to generate context-aware suggestions.
	 */
	void ShowSuggestionsForResult(const FString& CompletionResult);

	/** Hide the follow-up bar */
	void Hide();

private:
	TSharedPtr<class SHorizontalBox> SuggestionContainer;
	FOnAutonomixFollowUpSelected OnFollowUpSelected;

	/** Generate UE-specific follow-up suggestions from result context */
	static TArray<FString> GenerateSuggestions(const FString& ResultText);
};
