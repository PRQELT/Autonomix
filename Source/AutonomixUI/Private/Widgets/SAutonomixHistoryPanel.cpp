// Copyright Autonomix. All Rights Reserved.

#include "Widgets/SAutonomixHistoryPanel.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Text/STextBlock.h"

void SAutonomixHistoryPanel::Construct(const FArguments& InArgs)
{
	OnLoadTask = InArgs._OnLoadTask;
	OnDeleteTask = InArgs._OnDeleteTask;

	ChildSlot
	[
		SNew(SVerticalBox)

		// Header
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(4, 4, 4, 2)
		[
			SNew(STextBlock)
			.Text(FText::FromString(TEXT("📋 Task History")))
			.Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
			.ColorAndOpacity(FLinearColor(0.8f, 0.8f, 1.0f))
		]

		// Search box
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(4, 2)
		[
			SAssignNew(SearchBox, SEditableTextBox)
			.HintText(FText::FromString(TEXT("Search tasks...")))
			.OnTextChanged(this, &SAutonomixHistoryPanel::OnSearchTextChanged)
		]

		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(SSeparator).Thickness(1.0f)
		]

		// History list
		+ SVerticalBox::Slot()
		.FillHeight(1.0f)
		[
			SAssignNew(HistoryList, SScrollBox)
		]
	];
}

void SAutonomixHistoryPanel::RefreshHistory(const TArray<FAutonomixTaskHistoryItem>& Items)
{
	AllItems = Items;
	RebuildList();
}

void SAutonomixHistoryPanel::OnSearchTextChanged(const FText& NewText)
{
	CurrentSearchQuery = NewText.ToString();
	RebuildList();
}

void SAutonomixHistoryPanel::RebuildList()
{
	if (!HistoryList.IsValid()) return;
	HistoryList->ClearChildren();

	if (AllItems.Num() == 0)
	{
		HistoryList->AddSlot()
		[
			SNew(STextBlock)
			.Text(FText::FromString(TEXT("No history yet.\nCompleted tasks will appear here.")))
			.ColorAndOpacity(FLinearColor(0.4f, 0.4f, 0.4f))
			.AutoWrapText(true)
		];
		return;
	}

	for (const FAutonomixTaskHistoryItem& Item : AllItems)
	{
		// Filter by search query
		if (!CurrentSearchQuery.IsEmpty())
		{
			if (!Item.Title.Contains(CurrentSearchQuery, ESearchCase::IgnoreCase) &&
				!Item.FirstUserMessage.Contains(CurrentSearchQuery, ESearchCase::IgnoreCase))
			{
				continue;
			}
		}

		HistoryList->AddSlot()
		.Padding(0, 0, 0, 2)
		[
			BuildHistoryItem(Item)
		];
	}
}

TSharedRef<SWidget> SAutonomixHistoryPanel::BuildHistoryItem(const FAutonomixTaskHistoryItem& Item)
{
	const FString RelTime = FormatRelativeTime(Item.LastActiveAt);
	const FString TokenStr = FString::Printf(TEXT("%dk tokens"), (Item.TotalTokenUsage.InputTokens + Item.TotalTokenUsage.OutputTokens) / 1000);
	const FString CostStr = Item.TotalCostUSD > 0.001f
		? FString::Printf(TEXT("$%.3f"), Item.TotalCostUSD)
		: TEXT("");

	return SNew(SBorder)
		.BorderBackgroundColor(FLinearColor(0.1f, 0.1f, 0.12f))
		.Padding(6.0f)
		[
			SNew(SVerticalBox)

			// Title
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(SHorizontalBox)

				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				[
					SNew(STextBlock)
					.Text(FText::FromString(Item.Title))
					.Font(FCoreStyle::GetDefaultFontStyle("Bold", 8))
					.ColorAndOpacity(FLinearColor(0.9f, 0.9f, 0.9f))
				]

				+ SHorizontalBox::Slot()
				.AutoWidth()
				[
					SNew(STextBlock)
					.Text(FText::FromString(RelTime))
					.ColorAndOpacity(FLinearColor(0.4f, 0.4f, 0.4f))
					.Font(FCoreStyle::GetDefaultFontStyle("Regular", 7))
				]
			]

			// Preview
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0, 1, 0, 2)
			[
				SNew(STextBlock)
				.Text(FText::FromString(Item.FirstUserMessage.Left(80) + (Item.FirstUserMessage.Len() > 80 ? TEXT("...") : TEXT(""))))
				.ColorAndOpacity(FLinearColor(0.5f, 0.5f, 0.5f))
				.Font(FCoreStyle::GetDefaultFontStyle("Regular", 7))
				.AutoWrapText(false)
			]

			// Stats + actions
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(SHorizontalBox)

				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(0, 0, 8, 0)
				[
					SNew(STextBlock)
					.Text(FText::FromString(TokenStr))
					.ColorAndOpacity(FLinearColor(0.4f, 0.4f, 0.5f))
					.Font(FCoreStyle::GetDefaultFontStyle("Regular", 7))
				]

				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(0, 0, 8, 0)
				[
					SNew(STextBlock)
					.Text(FText::FromString(CostStr))
					.ColorAndOpacity(FLinearColor(0.3f, 0.7f, 0.3f))
					.Font(FCoreStyle::GetDefaultFontStyle("Regular", 7))
				]

				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				[
					SNew(SSpacer)
				]

				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(0, 0, 4, 0)
				[
					SNew(SButton)
					.Text(FText::FromString(TEXT("▶ Resume")))
					.ToolTipText(FText::FromString(TEXT("Load this task into a new tab")))
					.OnClicked_Lambda([this, TabId = Item.TabId]() -> FReply
					{
						OnLoadTask.ExecuteIfBound(TabId);
						return FReply::Handled();
					})
				]

				+ SHorizontalBox::Slot()
				.AutoWidth()
				[
					SNew(SButton)
					.Text(FText::FromString(TEXT("✕")))
					.ToolTipText(FText::FromString(TEXT("Delete from history")))
					.OnClicked_Lambda([this, TabId = Item.TabId]() -> FReply
					{
						OnDeleteTask.ExecuteIfBound(TabId);
						return FReply::Handled();
					})
				]
			]
		];
}

FString SAutonomixHistoryPanel::FormatRelativeTime(const FDateTime& Timestamp)
{
	const FTimespan Delta = FDateTime::UtcNow() - Timestamp;
	if (Delta.GetTotalMinutes() < 1) return TEXT("just now");
	if (Delta.GetTotalMinutes() < 60) return FString::Printf(TEXT("%.0fm ago"), Delta.GetTotalMinutes());
	if (Delta.GetTotalHours() < 24) return FString::Printf(TEXT("%.0fh ago"), Delta.GetTotalHours());
	if (Delta.GetTotalDays() < 7) return FString::Printf(TEXT("%.0fd ago"), Delta.GetTotalDays());
	return Timestamp.ToString(TEXT("%b %d"));
}
