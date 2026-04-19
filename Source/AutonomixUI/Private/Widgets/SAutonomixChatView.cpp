// Copyright Autonomix. All Rights Reserved.

#include "Widgets/SAutonomixChatView.h"
#include "Widgets/SAutonomixMessage.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Text/STextBlock.h"

// ============================================================================
// Construct
// ============================================================================

void SAutonomixChatView::Construct(const FArguments& InArgs)
{
	OnContinueTask = InArgs._OnContinueTask;
	OnEndTask = InArgs._OnEndTask;

	ChildSlot
	[
		SNew(SVerticalBox)

		// Chat messages (fills available space)
		+ SVerticalBox::Slot()
		.FillHeight(1.0f)
		[
			SAssignNew(ScrollBox, SScrollBox)
			.OnUserScrolled_Lambda([this](float NewScrollOffset)
			{
				// Detect if user scrolled away from bottom.
				// If user scrolls up, disable auto-scroll; if they scroll back to bottom, re-enable.
				LastScrollOffset = NewScrollOffset;
				bAutoScroll = IsScrolledToBottom();
			})
			+ SScrollBox::Slot()
			[
				SAssignNew(MessageContainer, SVerticalBox)
			]
		]

		// Task resumption bar (hidden by default -- shown when task is interrupted)
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SAssignNew(ResumptionBarContainer, SVerticalBox)
			.Visibility(EVisibility::Collapsed)

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(8.0f, 6.0f)
			[
				SNew(SVerticalBox)

				// Info text
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 0.0f, 0.0f, 4.0f)
				[
					SAssignNew(ResumptionTimeText, STextBlock)
					.Text(FText::FromString(TEXT("\u23F8 This task was interrupted.")))
					.ColorAndOpacity(FSlateColor(FLinearColor(1.0f, 0.8f, 0.2f)))
					.Font(FCoreStyle::GetDefaultFontStyle("Bold", 11))
				]

				// Buttons
				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(SHorizontalBox)

					// Continue Task button (green)
					+ SHorizontalBox::Slot()
					.FillWidth(1.0f)
					.Padding(0.0f, 0.0f, 4.0f, 0.0f)
					[
						SNew(SButton)
						.Text(FText::FromString(TEXT("\u25B6 Continue Task")))
						.ToolTipText(FText::FromString(TEXT("Resume this interrupted task. The AI will review its progress and continue.")))
						.HAlign(HAlign_Center)
						.ButtonColorAndOpacity(FLinearColor(0.15f, 0.55f, 0.15f))
						.OnClicked_Lambda([this]()
						{
							OnContinueTask.ExecuteIfBound();
							return FReply::Handled();
						})
					]

					// End Task button (neutral)
					+ SHorizontalBox::Slot()
					.FillWidth(1.0f)
					.Padding(4.0f, 0.0f, 0.0f, 0.0f)
					[
						SNew(SButton)
						.Text(FText::FromString(TEXT("\u23F9 End Task")))
						.ToolTipText(FText::FromString(TEXT("Mark this task as completed and return to idle.")))
						.HAlign(HAlign_Center)
						.ButtonColorAndOpacity(FLinearColor(0.35f, 0.35f, 0.38f))
						.OnClicked_Lambda([this]()
						{
							OnEndTask.ExecuteIfBound();
							return FReply::Handled();
						})
					]
				]
			]
		]
	];
}

// ============================================================================
// AddMessage
// ============================================================================

void SAutonomixChatView::AddMessage(const FAutonomixMessage& Message)
{
	if (Message.Content.IsEmpty())
	{
		return;
	}

	// Flush any pending streaming buffer before adding a new message
	FlushStreamingBuffer();

	bool bShowRoleLabel = true;
	if (LastMessageRole.IsSet() && LastMessageRole.GetValue() == Message.Role)
	{
		bShowRoleLabel = false;
	}

	MessageContainer->AddSlot()
		.AutoHeight()
		.Padding(4.0f, 2.0f)
		[
			SNew(SAutonomixMessage)
			.Message(Message)
			.ShowRoleLabel(bShowRoleLabel)
		];

	LastMessageRole = Message.Role;

	if (bAutoScroll)
	{
		ScrollToBottom();
	}
}

// ============================================================================
// UpdateStreamingMessage -- batched streaming text
// ============================================================================

void SAutonomixChatView::UpdateStreamingMessage(const FGuid& MessageId, const FString& DeltaText, EAutonomixMessageRole Role)
{
	// Accumulate text in the buffer
	StreamingBuffer += DeltaText;
	StreamingTargetId = MessageId;
	StreamingTargetRole = Role;

	// Register a one-shot active timer to flush after the interval if not already registered.
	// Capturing 'this' is safe: RegisterActiveTimer ties the timer lifetime to this widget;
	// when the widget is destroyed, the timer is automatically unregistered.
	if (!bFlushTimerActive)
	{
		bFlushTimerActive = true;

		RegisterActiveTimer(FlushIntervalSeconds,
			FWidgetActiveTimerDelegate::CreateLambda(
				[this](double, float) -> EActiveTimerReturnType
				{
					FlushStreamingBuffer();
					return EActiveTimerReturnType::Stop;
				}
			));
	}
}

// ============================================================================
// FlushStreamingBuffer -- applies buffered text to the widget
// ============================================================================

void SAutonomixChatView::FlushStreamingBuffer()
{
	bFlushTimerActive = false;

	if (StreamingBuffer.IsEmpty())
	{
		return;
	}

	FString TextToFlush = MoveTemp(StreamingBuffer);
	StreamingBuffer.Empty();

	// Find the last SAutonomixMessage child widget and append text to it
	FChildren* Children = MessageContainer->GetChildren();
	if (Children && Children->Num() > 0)
	{
		TSharedRef<SWidget> LastChild = Children->GetChildAt(Children->Num() - 1);
		TSharedPtr<SAutonomixMessage> MsgWidget = StaticCastSharedRef<SAutonomixMessage>(LastChild);
		if (MsgWidget.IsValid())
		{
			if (StreamingTargetRole == EAutonomixMessageRole::None || StreamingTargetRole == MsgWidget->GetMessageData().Role)
			{
				// Append to existing message
				MsgWidget->AppendText(TextToFlush);

				// Auto-detect tool completion from result prefix.
				// ChatSession prepends results with a checkmark or X mark.
				// When we see these prefixes on a tool call message, mark it completed
				// so the expandable area collapses and the status icon updates.
				if (MsgWidget->IsToolCallMessage())
				{
					if (TextToFlush.StartsWith(TEXT("\u2705")))
					{
						MsgWidget->MarkToolCompleted(true);
					}
					else if (TextToFlush.StartsWith(TEXT("\u274C")))
					{
						MsgWidget->MarkToolCompleted(false);
					}
				}
			}
			else
			{
				// Role mismatch -- create a new message for the new role
				FAutonomixMessage NewMsg(StreamingTargetRole, TextToFlush);
				AddMessage(NewMsg);
			}
		}
	}

	if (bAutoScroll)
	{
		ScrollToBottom();
	}
}

// ============================================================================
// MarkLastToolCompleted
// ============================================================================

void SAutonomixChatView::MarkLastToolCompleted(bool bSuccess)
{
	// Walk backwards to find the last tool call message widget
	FChildren* Children = MessageContainer->GetChildren();
	if (!Children) return;

	for (int32 i = Children->Num() - 1; i >= 0; --i)
	{
		TSharedRef<SWidget> Child = Children->GetChildAt(i);
		TSharedPtr<SAutonomixMessage> MsgWidget = StaticCastSharedRef<SAutonomixMessage>(Child);
		if (MsgWidget.IsValid() && MsgWidget->IsToolCallMessage())
		{
			MsgWidget->MarkToolCompleted(bSuccess);
			break;
		}
	}
}

// ============================================================================
// ClearMessages
// ============================================================================

void SAutonomixChatView::ClearMessages()
{
	// Flush any pending streaming text first
	FlushStreamingBuffer();
	StreamingTargetId.Invalidate();
	StreamingTargetRole = EAutonomixMessageRole::None;

	MessageContainer->ClearChildren();
	LastMessageRole.Reset();
	bAutoScroll = true;
}

// ============================================================================
// ScrollToBottom -- deferred to next tick for geometry calculation
// ============================================================================

void SAutonomixChatView::ScrollToBottom()
{
	if (!ScrollBox.IsValid()) return;

	// CRITICAL FIX: Defer ScrollToEnd to the next tick.
	// In Slate, calling ScrollToEnd immediately after adding a widget often fails
	// because the new widget's geometry hasn't been computed yet (especially with text wrapping).
	// RegisterActiveTimer on this widget ensures the layout pass completes first.
	TWeakPtr<SScrollBox> WeakScrollBox = ScrollBox;
	RegisterActiveTimer(0.0f,
		FWidgetActiveTimerDelegate::CreateLambda(
			[WeakScrollBox](double, float) -> EActiveTimerReturnType
			{
				if (TSharedPtr<SScrollBox> PinnedBox = WeakScrollBox.Pin())
				{
					PinnedBox->ScrollToEnd();
				}
				return EActiveTimerReturnType::Stop;
			}
		));
}

// ============================================================================
// IsScrolledToBottom -- check if user is at the bottom of scroll
// ============================================================================

bool SAutonomixChatView::IsScrolledToBottom() const
{
	if (!ScrollBox.IsValid()) return true;

	// SScrollBox doesn't expose a direct "is at bottom" API.
	// We use GetScrollOffsetOfEnd() vs GetScrollOffset() with some tolerance.
	float CurrentOffset = ScrollBox->GetScrollOffset();
	float MaxOffset = ScrollBox->GetScrollOffsetOfEnd();

	// Allow a small tolerance (20 pixels) to account for layout rounding
	return (MaxOffset - CurrentOffset) < 20.0f;
}

// ============================================================================
// ShowResumptionBar / HideResumptionBar
// ============================================================================

void SAutonomixChatView::ShowResumptionBar(const FString& TimeAgoText)
{
	if (!ResumptionBarContainer.IsValid()) return;

	FString InfoText;
	if (TimeAgoText.IsEmpty())
	{
		InfoText = TEXT("\u23F8 This task was interrupted. Would you like to continue?");
	}
	else
	{
		InfoText = FString::Printf(TEXT("\u23F8 This task was interrupted %s ago. Would you like to continue?"), *TimeAgoText);
	}

	if (ResumptionTimeText.IsValid())
	{
		ResumptionTimeText->SetText(FText::FromString(InfoText));
	}

	ResumptionBarContainer->SetVisibility(EVisibility::Visible);
	ScrollToBottom();
}

void SAutonomixChatView::HideResumptionBar()
{
	if (ResumptionBarContainer.IsValid())
	{
		ResumptionBarContainer->SetVisibility(EVisibility::Collapsed);
	}
}
