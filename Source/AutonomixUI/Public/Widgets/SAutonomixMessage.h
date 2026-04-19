// Copyright Autonomix. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "AutonomixTypes.h"

class SExpandableArea;
class SBorder;

/**
 * Individual message widget displaying a single chat message with role icon and content.
 *
 * Supports:
 * - Color-coded left border per message role (user=blue, error=red, tool success=green, system=gray)
 * - Collapsible tool call rendering via SExpandableArea (header: icon+name+status, body: args+result)
 * - Error message styling with red background tint and bold prefix
 * - Streaming text append
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

	/** Mark a tool call message as completed (collapse the expandable area, update status icon).
	 *  @param bSuccess  true = green check, false = red X */
	void MarkToolCompleted(bool bSuccess);

	/** Returns true if this message represents a tool call (🔧 prefix or has ToolName set) */
	bool IsToolCallMessage() const;

private:
	/** Build the header text for collapsible (non-tool) messages */
	FText GetHeaderText() const;

	/** Get the left border color based on message role */
	FLinearColor GetBorderColor() const;

	/** Determine if this is a tool call message from the content */
	static bool DetectToolCallFromContent(const FString& Content);

	/** Extract tool name from "🔧 Executing: tool_name" format */
	static FString ExtractToolNameFromContent(const FString& Content);

	FAutonomixMessage MessageData;
	TSharedPtr<class SMultiLineEditableText> ContentTextBlock;
	TSharedPtr<SExpandableArea> ToolExpandableArea;
	TSharedPtr<class STextBlock> ToolStatusIcon;
	TSharedPtr<SBorder> LeftBorderWidget;

	/** Whether the tool call has been marked complete */
	bool bToolCompleted = false;

	/** Whether the tool call succeeded (only valid when bToolCompleted is true) */
	bool bToolSuccess = false;
};
