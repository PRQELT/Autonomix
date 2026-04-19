// Copyright Autonomix. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "AutonomixTypes.h"

class SScrollBox;
class STextBlock;
class SAutonomixMessage;

/**
 * Scrollable chat history view displaying messages between user and AI.
 *
 * Supports:
 * - Streaming text batching (accumulates tokens, flushes every ~50ms via active timer)
 * - Auto-scroll management (only scrolls to bottom if user hasn't scrolled up)
 * - Tool call completion marking (collapse tool expandable areas)
 */
class AUTONOMIXUI_API SAutonomixChatView : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SAutonomixChatView) {}
		SLATE_EVENT(FSimpleDelegate, OnContinueTask)
		SLATE_EVENT(FSimpleDelegate, OnEndTask)
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

	/** Mark the last tool call message as completed.
	 *  @param bSuccess  true=✅, false=❌ */
	void MarkLastToolCompleted(bool bSuccess);

	/** Show the task resumption bar (for interrupted tasks).
	 *  @param TimeAgoText  Human-readable time since interruption (e.g. "3 hours") */
	void ShowResumptionBar(const FString& TimeAgoText);

	/** Hide the task resumption bar */
	void HideResumptionBar();

private:
	/** Flush buffered streaming text to the actual widget */
	void FlushStreamingBuffer();

	/** Check if the user has scrolled away from the bottom */
	bool IsScrolledToBottom() const;

	TSharedPtr<SScrollBox> ScrollBox;
	TSharedPtr<SVerticalBox> MessageContainer;

	/** Auto-scroll: only auto-scroll if user hasn't scrolled up */
	bool bAutoScroll = true;

	/** Last known scroll offset — used to detect user scrolling up */
	float LastScrollOffset = 0.0f;

	TOptional<EAutonomixMessageRole> LastMessageRole;

	// ---- Streaming Text Batching ----
	/** Accumulated delta text waiting to be flushed */
	FString StreamingBuffer;

	/** Message ID for the current streaming target */
	FGuid StreamingTargetId;

	/** Role for the current streaming target (for role-change detection) */
	EAutonomixMessageRole StreamingTargetRole = EAutonomixMessageRole::None;

	/** Whether the flush timer is currently registered */
	bool bFlushTimerActive = false;

	/** Minimum interval between flushes (seconds) */
	static constexpr float FlushIntervalSeconds = 0.05f; // 50ms

	// ---- Task Resumption Bar ----
	TSharedPtr<SVerticalBox> ResumptionBarContainer;
	TSharedPtr<STextBlock> ResumptionTimeText;
	FSimpleDelegate OnContinueTask;
	FSimpleDelegate OnEndTask;
};
