// Copyright Autonomix. All Rights Reserved.

#include "Widgets/SAutonomixCheckpointPanel.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Text/STextBlock.h"

void SAutonomixCheckpointPanel::Construct(const FArguments& InArgs)
{
	OnRestoreCheckpoint = InArgs._OnRestoreCheckpoint;
	OnViewDiff = InArgs._OnViewDiff;

	ChildSlot
	[
		SNew(SVerticalBox)

		// Header
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(4, 4, 4, 2)
		[
			SNew(STextBlock)
			.Text(FText::FromString(TEXT("🕐 Checkpoints")))
			.Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
			.ColorAndOpacity(FLinearColor(0.8f, 0.8f, 1.0f))
		]

		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(SSeparator)
			.Thickness(1.0f)
		]

		// Checkpoint list
		+ SVerticalBox::Slot()
		.FillHeight(1.0f)
		[
			SAssignNew(CheckpointList, SScrollBox)
		]
	];
}

void SAutonomixCheckpointPanel::RefreshCheckpoints(const TArray<FAutonomixCheckpoint>& Checkpoints)
{
	if (!CheckpointList.IsValid()) return;
	CheckpointList->ClearChildren();

	if (Checkpoints.Num() == 0)
	{
		CheckpointList->AddSlot()
		[
			SNew(STextBlock)
			.Text(FText::FromString(TEXT("No checkpoints yet.\nCheckpoints are created before each tool batch.")))
			.ColorAndOpacity(FLinearColor(0.4f, 0.4f, 0.4f))
			.AutoWrapText(true)
		];
		return;
	}

	// Show most recent first
	TArray<FAutonomixCheckpoint> Sorted = Checkpoints;
	Sorted.Sort([](const FAutonomixCheckpoint& A, const FAutonomixCheckpoint& B) {
		return A.Timestamp > B.Timestamp;
	});

	for (const FAutonomixCheckpoint& CP : Sorted)
	{
		CheckpointList->AddSlot()
		.Padding(0, 0, 0, 2)
		[
			BuildCheckpointRow(CP)
		];
	}
}

TSharedRef<SWidget> SAutonomixCheckpointPanel::BuildCheckpointRow(const FAutonomixCheckpoint& Checkpoint)
{
	const FString TimeStr = Checkpoint.Timestamp.ToString(TEXT("%H:%M:%S"));
	const FString ShortHash = Checkpoint.CommitHash.Left(7);

	return SNew(SBorder)
		.BorderBackgroundColor(FLinearColor(0.12f, 0.12f, 0.15f))
		.Padding(6.0f)
		[
			SNew(SVerticalBox)

			// Title row
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(SHorizontalBox)

				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				[
					SNew(STextBlock)
					.Text(FText::FromString(FString::Printf(TEXT("%s  [%s]"), *Checkpoint.Description, *ShortHash)))
					.Font(FCoreStyle::GetDefaultFontStyle("Bold", 8))
					.ColorAndOpacity(FLinearColor(0.9f, 0.9f, 0.9f))
				]

				+ SHorizontalBox::Slot()
				.AutoWidth()
				[
					SNew(STextBlock)
					.Text(FText::FromString(TimeStr))
					.ColorAndOpacity(FLinearColor(0.5f, 0.5f, 0.5f))
					.Font(FCoreStyle::GetDefaultFontStyle("Regular", 7))
				]
			]

			// Info row
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0, 1, 0, 3)
			[
				SNew(STextBlock)
				.Text(FText::FromString(FString::Printf(
					TEXT("%d files  •  loop %d"),
					Checkpoint.ModifiedFiles.Num(),
					Checkpoint.LoopIteration
				)))
				.ColorAndOpacity(FLinearColor(0.4f, 0.4f, 0.4f))
				.Font(FCoreStyle::GetDefaultFontStyle("Regular", 7))
			]

			// Action buttons
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(SHorizontalBox)

				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(0, 0, 4, 0)
				[
					SNew(SButton)
					.Text(FText::FromString(TEXT("↩ Restore")))
					.ToolTipText(FText::FromString(TEXT("Restore project files to this checkpoint")))
					.ButtonColorAndOpacity(FLinearColor(0.6f, 0.2f, 0.2f))
					.OnClicked_Lambda([this, CommitHash = Checkpoint.CommitHash]() -> FReply
					{
						OnRestoreCheckpoint.ExecuteIfBound(CommitHash);
						return FReply::Handled();
					})
				]

				+ SHorizontalBox::Slot()
				.AutoWidth()
				[
					SNew(SButton)
					.Text(FText::FromString(TEXT("⟨/⟩ Diff")))
					.ToolTipText(FText::FromString(TEXT("Show what changed up to this checkpoint")))
					.OnClicked_Lambda([this, CommitHash = Checkpoint.CommitHash]() -> FReply
					{
						OnViewDiff.ExecuteIfBound(CommitHash);
						return FReply::Handled();
					})
				]
			]
		];
}
