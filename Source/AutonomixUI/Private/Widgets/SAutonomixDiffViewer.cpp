// Copyright Autonomix. All Rights Reserved.

#include "Widgets/SAutonomixDiffViewer.h"
#include "Widgets/Text/STextBlock.h"

void SAutonomixDiffViewer::Construct(const FArguments& InArgs)
{
	ChildSlot
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(4)
		[
			SAssignNew(FilePathText, STextBlock)
		]
		+ SVerticalBox::Slot().FillHeight(1.0f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(0.5f).Padding(2)
			[
				SAssignNew(OriginalText, STextBlock).AutoWrapText(true)
			]
			+ SHorizontalBox::Slot().FillWidth(0.5f).Padding(2)
			[
				SAssignNew(ModifiedText, STextBlock).AutoWrapText(true)
			]
		]
	];
}

void SAutonomixDiffViewer::ShowDiff(const FString& FilePath, const FString& OriginalContent, const FString& ModifiedContent)
{
	if (FilePathText.IsValid()) FilePathText->SetText(FText::FromString(FilePath));
	if (OriginalText.IsValid()) OriginalText->SetText(FText::FromString(OriginalContent));
	if (ModifiedText.IsValid()) ModifiedText->SetText(FText::FromString(ModifiedContent));
}

void SAutonomixDiffViewer::ClearDiff()
{
	if (FilePathText.IsValid()) FilePathText->SetText(FText::GetEmpty());
	if (OriginalText.IsValid()) OriginalText->SetText(FText::GetEmpty());
	if (ModifiedText.IsValid()) ModifiedText->SetText(FText::GetEmpty());
}
