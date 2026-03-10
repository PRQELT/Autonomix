// Copyright Autonomix. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/Notifications/SProgressBar.h"

/**
 * Context window progress bar widget.
 *
 * Ported from Roo Code's ContextWindowProgress.tsx.
 * Shows a colored progress bar indicating how full the context window is:
 *   0-60%  → Green
 *   60-80% → Yellow
 *   80-90% → Orange
 *   90%+   → Red
 *
 * Hovering shows token counts.
 * A "Condense" button appears when usage > 70%.
 */
class AUTONOMIXUI_API SAutonomixContextBar : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SAutonomixContextBar) {}
		/** Context usage 0.0 to 1.0 */
		SLATE_ATTRIBUTE(float, ContextUsageFraction)
		/** Input token count */
		SLATE_ATTRIBUTE(int32, InputTokens)
		/** Output token count */
		SLATE_ATTRIBUTE(int32, OutputTokens)
		/** Max context window tokens */
		SLATE_ATTRIBUTE(int32, MaxTokens)
		/** Current estimated session cost in USD */
		SLATE_ATTRIBUTE(float, SessionCostUSD)
		/** Mode display name */
		SLATE_ATTRIBUTE(FString, ModeDisplayName)
		/** Called when user clicks Condense button */
		SLATE_EVENT(FSimpleDelegate, OnCondenseClicked)
		/** Called when user changes mode */
		SLATE_EVENT(FSimpleDelegate, OnModeClicked)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	/** Update the context window stats */
	void UpdateStats(float UsageFraction, int32 InTokens, int32 OutTokens, int32 MaxTok, float CostUSD);

	/** Update mode display */
	void SetMode(const FString& ModeName);

private:
	TSharedPtr<class SProgressBar> ContextProgressBar;
	TSharedPtr<class STextBlock> TokenInfoText;
	TSharedPtr<class STextBlock> CostText;
	TSharedPtr<class STextBlock> ModeText;
	TSharedPtr<class SButton> CondenseButton;

	TAttribute<float> ContextUsageFraction;
	TAttribute<int32> InputTokens;
	TAttribute<int32> OutputTokens;
	TAttribute<int32> MaxTokens;
	TAttribute<float> SessionCostUSD;
	TAttribute<FString> ModeDisplayName;

	FSimpleDelegate OnCondenseClicked;
	FSimpleDelegate OnModeClicked;

	FSlateColor GetProgressBarColor() const;
	FText GetTokenInfoText() const;
	FText GetCostText() const;
	EVisibility GetCondenseButtonVisibility() const;
};
