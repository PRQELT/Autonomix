// Copyright Autonomix. All Rights Reserved.

#include "Widgets/SAutonomixChatView.h"
#include "Widgets/SAutonomixMessage.h"
#include "Widgets/Layout/SScrollBox.h"

void SAutonomixChatView::Construct(const FArguments& InArgs)
{
	ChildSlot
	[
		SAssignNew(ScrollBox, SScrollBox)
		+ SScrollBox::Slot()
		[
			SAssignNew(MessageContainer, SVerticalBox)
		]
	];
}

void SAutonomixChatView::AddMessage(const FAutonomixMessage& Message)
{
	MessageContainer->AddSlot()
		.AutoHeight()
		.Padding(4.0f, 2.0f)
		[
			SNew(SAutonomixMessage).Message(Message)
		];

	if (bAutoScroll)
	{
		ScrollToBottom();
	}
}

void SAutonomixChatView::UpdateStreamingMessage(const FGuid& MessageId, const FString& DeltaText)
{
	// Find the last SAutonomixMessage child widget and append text to it
	// The streaming message is always the last one added
	FChildren* Children = MessageContainer->GetChildren();
	if (Children && Children->Num() > 0)
	{
		TSharedRef<SWidget> LastChild = Children->GetChildAt(Children->Num() - 1);
		TSharedPtr<SAutonomixMessage> MsgWidget = StaticCastSharedRef<SAutonomixMessage>(LastChild);
		if (MsgWidget.IsValid())
		{
			MsgWidget->AppendText(DeltaText);
		}
	}

	if (bAutoScroll)
	{
		ScrollToBottom();
	}
}

void SAutonomixChatView::ClearMessages()
{
	MessageContainer->ClearChildren();
}

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
