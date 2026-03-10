// Copyright Autonomix. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "AutonomixTypes.h"

/**
 * Individual message widget displaying a single chat message with role icon and content.
 */
class AUTONOMIXUI_API SAutonomixMessage : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SAutonomixMessage) {}
		SLATE_ARGUMENT(FAutonomixMessage, Message)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	/** Append text to the message content (for streaming) */
	void AppendText(const FString& DeltaText);

	/** Get the message ID */
	FGuid GetMessageId() const { return MessageData.MessageId; }

private:
	FAutonomixMessage MessageData;
	TSharedPtr<class STextBlock> ContentTextBlock;
};
