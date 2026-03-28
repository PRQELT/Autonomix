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
	SLATE_BEGIN_ARGS(SAutonomixMessage) : _ShowRoleLabel(true) {}
		SLATE_ARGUMENT(FAutonomixMessage, Message)
		SLATE_ARGUMENT(bool, ShowRoleLabel)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	/** Append text to the message content (for streaming) */
	void AppendText(const FString& DeltaText);

	/** Get the message ID */
	FGuid GetMessageId() const { return MessageData.MessageId; }

	const FAutonomixMessage& GetMessageData() const { return MessageData; }

private:
	FText GetHeaderText() const;

	FAutonomixMessage MessageData;
	TSharedPtr<class SMultiLineEditableText> ContentTextBlock;
};
