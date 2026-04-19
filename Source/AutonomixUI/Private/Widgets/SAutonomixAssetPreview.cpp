// Copyright Autonomix. All Rights Reserved.

#include "Widgets/SAutonomixAssetPreview.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Images/SImage.h"

void SAutonomixAssetPreview::Construct(const FArguments& InArgs)
{
	ChildSlot
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()
		[
			SAssignNew(ThumbnailImage, SImage)
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(4)
		[
			SAssignNew(AssetInfoText, STextBlock).AutoWrapText(true)
		]
	];
}

void SAutonomixAssetPreview::ShowAssetPreview(const FString& AssetPath)
{
	if (AssetInfoText.IsValid())
	{
		AssetInfoText->SetText(FText::FromString(AssetPath));
	}
	// Stub: load thumbnail from asset registry
}

void SAutonomixAssetPreview::ClearPreview()
{
	if (AssetInfoText.IsValid()) AssetInfoText->SetText(FText::GetEmpty());
}
