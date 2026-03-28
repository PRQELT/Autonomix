// Copyright Autonomix. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "AutonomixTypes.h"

class SScrollBox;

/**
 * Scrollable chat history view displaying messages between user and AI.
 */
class AUTONOMIXUI_API SAutonomixChatView : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SAutonomixChatView) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	/** Add a message to the chat view */
	void AddMessage(const FAutonomixMessage& Message);

	/** Update a streaming message with new delta text */
	void UpdateStreamingMessage(const FGuid& MessageId, const FString& DeltaText, EAutonomixMessageRole Role = EAutonomixMessageRole::None);

	/** Clear all messages */
	void ClearMessages();

	/** Scroll to the bottom of the chat */
	void ScrollToBottom();

private:
	TSharedPtr<SScrollBox> ScrollBox;
	TSharedPtr<SVerticalBox> MessageContainer;
	bool bAutoScroll = true;

	TOptional<EAutonomixMessageRole> LastMessageRole;
};
