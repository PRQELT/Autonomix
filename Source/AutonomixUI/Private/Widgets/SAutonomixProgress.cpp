// Copyright Autonomix. All Rights Reserved.

#include "Widgets/SAutonomixProgress.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Notifications/SProgressBar.h"

void SAutonomixProgress::Construct(const FArguments& InArgs)
{
	ChildSlot
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(4)
		[
			SAssignNew(MessageText, STextBlock)
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(4)
		[
			SAssignNew(ProgressBar, SProgressBar)
		]
	];
	SetVisibility(EVisibility::Collapsed);
}

void SAutonomixProgress::ShowProgress(const FString& Message, float Progress)
{
	SetVisibility(EVisibility::Visible);
	bVisible = true;
	UpdateMessage(Message);
	UpdateProgress(Progress);
}

void SAutonomixProgress::UpdateProgress(float Progress)
{
	if (ProgressBar.IsValid())
	{
		ProgressBar->SetPercent(Progress >= 0.0f ? Progress : TOptional<float>());
	}
}

void SAutonomixProgress::UpdateMessage(const FString& Message)
{
	if (MessageText.IsValid())
	{
		MessageText->SetText(FText::FromString(Message));
	}
}

void SAutonomixProgress::HideProgress()
{
	SetVisibility(EVisibility::Collapsed);
	bVisible = false;
}
