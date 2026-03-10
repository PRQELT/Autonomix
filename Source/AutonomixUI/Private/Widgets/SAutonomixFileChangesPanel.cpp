// Copyright Autonomix. All Rights Reserved.

#include "Widgets/SAutonomixFileChangesPanel.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Text/STextBlock.h"
#include "ISourceCodeAccessModule.h"
#include "ISourceCodeAccessor.h"
#include "Misc/Paths.h"

void SAutonomixFileChangesPanel::Construct(const FArguments& InArgs)
{
	ChildSlot
	[
		SNew(SVerticalBox)

		// Header with change count
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(4, 4, 4, 2)
		[
			SNew(SHorizontalBox)

			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			[
				SNew(STextBlock)
				.Text(FText::FromString(TEXT("📁 File Changes")))
				.Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
				.ColorAndOpacity(FLinearColor(0.8f, 0.8f, 1.0f))
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			[
				SNew(SButton)
				.Text(FText::FromString(TEXT("Clear")))
				.OnClicked_Lambda([this]() -> FReply { ClearChanges(); return FReply::Handled(); })
			]
		]

		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(SSeparator).Thickness(1.0f)
		]

		// File list
		+ SVerticalBox::Slot()
		.FillHeight(1.0f)
		[
			SAssignNew(FileList, SScrollBox)
		]
	];
}

void SAutonomixFileChangesPanel::AddModifiedFile(const FString& RelativePath, bool bWasCreated)
{
	// Avoid duplicates
	for (const FFileChange& Existing : ChangedFiles)
	{
		if (Existing.RelativePath == RelativePath)
		{
			return;
		}
	}

	ChangedFiles.Add({ RelativePath, bWasCreated });
	RebuildList();
}

void SAutonomixFileChangesPanel::ClearChanges()
{
	ChangedFiles.Empty();
	RebuildList();
}

void SAutonomixFileChangesPanel::RebuildList()
{
	if (!FileList.IsValid()) return;
	FileList->ClearChildren();

	if (ChangedFiles.Num() == 0)
	{
		FileList->AddSlot()
		[
			SNew(STextBlock)
			.Text(FText::FromString(TEXT("No files modified yet.")))
			.ColorAndOpacity(FLinearColor(0.4f, 0.4f, 0.4f))
		];
		return;
	}

	// Group created vs modified
	TArray<FFileChange> Created, Modified;
	for (const FFileChange& Change : ChangedFiles)
	{
		if (Change.bWasCreated) Created.Add(Change);
		else Modified.Add(Change);
	}

	auto AddSection = [this](const FString& SectionTitle, const TArray<FFileChange>& Files)
	{
		if (Files.Num() == 0) return;

		FileList->AddSlot()
		.Padding(4, 2)
		[
			SNew(STextBlock)
			.Text(FText::FromString(SectionTitle))
			.Font(FCoreStyle::GetDefaultFontStyle("Bold", 8))
			.ColorAndOpacity(FLinearColor(0.6f, 0.6f, 0.6f))
		];

		for (const FFileChange& Change : Files)
		{
			FileList->AddSlot()
			.Padding(0, 0, 0, 1)
			[
				BuildFileRow(Change)
			];
		}
	};

	AddSection(TEXT("✨ Created"), Created);
	AddSection(TEXT("✏️ Modified"), Modified);
}

TSharedRef<SWidget> SAutonomixFileChangesPanel::BuildFileRow(const FFileChange& Change)
{
	return SNew(SBorder)
		.BorderBackgroundColor(FLinearColor(0.1f, 0.1f, 0.12f))
		.Padding(FMargin(6, 3))
		[
			SNew(SHorizontalBox)

			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			[
				SNew(SButton)
				.ButtonStyle(FCoreStyle::Get(), "NoBorder")
				.ToolTipText(FText::FromString(TEXT("Click to open file")))
				.OnClicked_Lambda([this, Path = Change.RelativePath]() -> FReply
				{
					// Open in source editor
					FString AbsPath = FPaths::Combine(FPaths::ProjectDir(), Path);
					ISourceCodeAccessModule& SCModule = FModuleManager::LoadModuleChecked<ISourceCodeAccessModule>("SourceCodeAccess");
					SCModule.GetAccessor().OpenFileAtLine(AbsPath, 1, 0);
					return FReply::Handled();
				})
				[
					SNew(STextBlock)
					.Text(FText::FromString(FPaths::GetCleanFilename(Change.RelativePath)))
					.ToolTipText(FText::FromString(Change.RelativePath))
					.ColorAndOpacity(FLinearColor(0.5f, 0.8f, 1.0f))
					.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
				]
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			[
				SNew(STextBlock)
				.Text(FText::FromString(Change.bWasCreated ? TEXT("NEW") : TEXT("MOD")))
				.ColorAndOpacity(Change.bWasCreated
					? FLinearColor(0.2f, 0.8f, 0.3f)   // Green for new
					: FLinearColor(0.8f, 0.6f, 0.2f))  // Orange for modified
				.Font(FCoreStyle::GetDefaultFontStyle("Bold", 7))
			]
		];
}
