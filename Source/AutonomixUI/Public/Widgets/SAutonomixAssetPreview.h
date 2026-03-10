// Copyright Autonomix. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/DeclarativeSyntaxSupport.h"

/**
 * Widget for previewing assets created by the AI (thumbnails, properties).
 */
class AUTONOMIXUI_API SAutonomixAssetPreview : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SAutonomixAssetPreview) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	/** Show preview for an asset at the given path */
	void ShowAssetPreview(const FString& AssetPath);

	/** Clear the preview */
	void ClearPreview();

private:
	TSharedPtr<class SImage> ThumbnailImage;
	TSharedPtr<class STextBlock> AssetInfoText;
};
