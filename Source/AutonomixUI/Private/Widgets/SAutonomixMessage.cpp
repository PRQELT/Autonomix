// Copyright Autonomix. All Rights Reserved.

#include "Widgets/SAutonomixMessage.h"
#include "Widgets/Text/STextBlock.h"

void SAutonomixMessage::Construct(const FArguments& InArgs)
{
	MessageData = InArgs._Message;

	FLinearColor RoleColor = FLinearColor::White;
	FString RoleLabel;
	switch (MessageData.Role)
	{
	case EAutonomixMessageRole::User:      RoleLabel = TEXT("You"); RoleColor = FLinearColor(0.3f, 0.6f, 1.0f); break;
	case EAutonomixMessageRole::Assistant: RoleLabel = TEXT("Autonomix"); RoleColor = FLinearColor(0.2f, 0.9f, 0.4f); break;
	case EAutonomixMessageRole::System:    RoleLabel = TEXT("System"); RoleColor = FLinearColor(0.7f, 0.7f, 0.7f); break;
	case EAutonomixMessageRole::ToolResult:RoleLabel = TEXT("Tool"); RoleColor = FLinearColor(1.0f, 0.8f, 0.2f); break;
	case EAutonomixMessageRole::Error:     RoleLabel = TEXT("Error"); RoleColor = FLinearColor(1.0f, 0.2f, 0.2f); break;
	}

	ChildSlot
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 2)
		[
			SNew(STextBlock)
			.Text(FText::FromString(RoleLabel))
			.ColorAndOpacity(FSlateColor(RoleColor))
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(8, 0, 0, 4)
		[
			SAssignNew(ContentTextBlock, STextBlock)
			.Text(FText::FromString(MessageData.Content))
			.AutoWrapText(true)
		]
	];
}

void SAutonomixMessage::AppendText(const FString& DeltaText)
{
	MessageData.Content += DeltaText;
	if (ContentTextBlock.IsValid())
	{
		ContentTextBlock->SetText(FText::FromString(MessageData.Content));
	}
}
