// Copyright Autonomix. All Rights Reserved.

#include "Widgets/SAutonomixContextBar.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Notifications/SProgressBar.h"

void SAutonomixContextBar::Construct(const FArguments& InArgs)
{
	ContextUsageFraction = InArgs._ContextUsageFraction;
	InputTokens = InArgs._InputTokens;
	OutputTokens = InArgs._OutputTokens;
	MaxTokens = InArgs._MaxTokens;
	SessionCostUSD = InArgs._SessionCostUSD;
	ModeDisplayName = InArgs._ModeDisplayName;
	OnCondenseClicked = InArgs._OnCondenseClicked;
	OnModeClicked = InArgs._OnModeClicked;

	ChildSlot
	[
		SNew(SBorder)
		.BorderBackgroundColor(FLinearColor(0.05f, 0.05f, 0.07f))
		.Padding(FMargin(6, 2))
		[
			SNew(SHorizontalBox)

			// Mode indicator (clickable)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(0, 0, 8, 0)
			[
				SNew(SButton)
				.ButtonStyle(FCoreStyle::Get(), "NoBorder")
				.ToolTipText(FText::FromString(TEXT("Click to switch mode")))
				.OnClicked_Lambda([this]() -> FReply { OnModeClicked.ExecuteIfBound(); return FReply::Handled(); })
				[
					SAssignNew(ModeText, STextBlock)
					.Text_Lambda([this]() { return FText::FromString(ModeDisplayName.Get(TEXT("General"))); })
					.ColorAndOpacity(FLinearColor(0.6f, 0.8f, 1.0f))
					.Font(FCoreStyle::GetDefaultFontStyle("Bold", 8))
				]
			]

			// Context progress bar
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.VAlign(VAlign_Center)
			.Padding(0, 0, 8, 0)
			[
				SNew(SBox)
				.HeightOverride(6.0f)
				.ToolTipText(this, &SAutonomixContextBar::GetTokenInfoText)
				[
					SAssignNew(ContextProgressBar, SProgressBar)
							.Percent_Lambda([this]() -> TOptional<float>
							{
								return TOptional<float>(ContextUsageFraction.Get(0.0f));
							})
							.FillColorAndOpacity(this, &SAutonomixContextBar::GetProgressBarColor)
				]
			]

			// Token info text
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(0, 0, 8, 0)
			[
				SAssignNew(TokenInfoText, STextBlock)
				.Text(this, &SAutonomixContextBar::GetTokenInfoText)
				.ColorAndOpacity(FLinearColor(0.5f, 0.5f, 0.5f))
				.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
			]

			// Cost display
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(0, 0, 8, 0)
			[
				SAssignNew(CostText, STextBlock)
				.Text(this, &SAutonomixContextBar::GetCostText)
				.ColorAndOpacity(FLinearColor(0.4f, 0.9f, 0.4f))
				.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
			]

			// Condense button (shown when > 70%)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SAssignNew(CondenseButton, SButton)
				.Text(FText::FromString(TEXT("📦 Condense")))
				.ToolTipText(FText::FromString(TEXT("Condense conversation to free up context window")))
				.Visibility(this, &SAutonomixContextBar::GetCondenseButtonVisibility)
				.OnClicked_Lambda([this]() -> FReply { OnCondenseClicked.ExecuteIfBound(); return FReply::Handled(); })
				.ButtonColorAndOpacity(FLinearColor(0.6f, 0.4f, 0.1f))
			]
		]
	];
}

void SAutonomixContextBar::UpdateStats(float UsageFraction, int32 InTokens, int32 OutTokens, int32 MaxTok, float CostUSD)
{
	ContextUsageFraction = UsageFraction;
	InputTokens = InTokens;
	OutputTokens = OutTokens;
	MaxTokens = MaxTok;
	SessionCostUSD = CostUSD;
}

void SAutonomixContextBar::SetMode(const FString& ModeName)
{
	ModeDisplayName = ModeName;
}

FSlateColor SAutonomixContextBar::GetProgressBarColor() const
{
	const float Usage = ContextUsageFraction.Get(0.0f);
	if (Usage >= 0.90f) return FSlateColor(FLinearColor(1.0f, 0.2f, 0.2f));   // Red
	if (Usage >= 0.80f) return FSlateColor(FLinearColor(1.0f, 0.5f, 0.1f));   // Orange
	if (Usage >= 0.60f) return FSlateColor(FLinearColor(1.0f, 0.9f, 0.1f));   // Yellow
	return FSlateColor(FLinearColor(0.2f, 0.8f, 0.3f));                        // Green
}

FText SAutonomixContextBar::GetTokenInfoText() const
{
	const int32 Total = InputTokens.Get(0) + OutputTokens.Get(0);
	const int32 Max = MaxTokens.Get(200000);
	const float Pct = (Max > 0) ? (float)Total / (float)Max * 100.0f : 0.0f;

	return FText::FromString(FString::Printf(
		TEXT("%dk / %dk (%.0f%%)"),
		Total / 1000, Max / 1000, Pct
	));
}

FText SAutonomixContextBar::GetCostText() const
{
	const float Cost = SessionCostUSD.Get(0.0f);
	if (Cost < 0.001f) return FText::GetEmpty();
	return FText::FromString(FString::Printf(TEXT("$%.3f"), Cost));
}

EVisibility SAutonomixContextBar::GetCondenseButtonVisibility() const
{
	return ContextUsageFraction.Get(0.0f) >= 0.70f
		? EVisibility::Visible
		: EVisibility::Collapsed;
}
