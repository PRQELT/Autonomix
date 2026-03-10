// Copyright Autonomix. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/DeclarativeSyntaxSupport.h"

/**
 * Code block widget with syntax highlighting hints and copy-to-clipboard button.
 */
class AUTONOMIXUI_API SAutonomixCodeBlock : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SAutonomixCodeBlock) {}
		SLATE_ARGUMENT(FString, Code)
		SLATE_ARGUMENT(FString, Language)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

private:
	FReply OnCopyClicked();

	FString CodeContent;
	FString LanguageHint;
};
