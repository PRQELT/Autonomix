// Copyright Autonomix. All Rights Reserved.

#include "Widgets/SAutonomixMessage.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Text/SMultiLineEditableText.h"
#include "Widgets/Layout/SExpandableArea.h"

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

	// Keep system messages collapsible even if ChatView remapped them to Assistant for display
	bool bIsCollapsible = MessageData.bIsCollapsible;

	int32 NewlineIndex;
	FString BodyText;
	if (bIsCollapsible)
	{
		if (MessageData.Content.FindChar('\n', NewlineIndex))
		{
			BodyText = MessageData.Content.Mid(NewlineIndex + 1);
		}
		else
		{
			BodyText = TEXT("");
		}
	}
	else
	{
		BodyText = MessageData.Content;
	}

	TSharedRef<SMultiLineEditableText> MainTextWidget = SAssignNew(ContentTextBlock, SMultiLineEditableText)
		.Text(FText::FromString(BodyText))
		.AutoWrapText(true)
		.IsReadOnly(true);

	TSharedPtr<SWidget> BodyWidget;

	if (bIsCollapsible)
	{
		BodyWidget = SNew(SExpandableArea)
			.InitiallyCollapsed(true)
			.HeaderContent()
			[
				SNew(STextBlock)
				.Text(this, &SAutonomixMessage::GetHeaderText)
				.ColorAndOpacity(FSlateColor(FLinearColor(0.6f, 0.6f, 0.6f)))
			]
			.BodyContent()
			[
				MainTextWidget
			];
	}
	else
	{
		BodyWidget = MainTextWidget;
	}

	TSharedPtr<SVerticalBox> MainBox = SNew(SVerticalBox);

	if (InArgs._ShowRoleLabel)
	{
		MainBox->AddSlot()
			.AutoHeight()
			.Padding(0, 10, 0, 2)
			[
				SNew(STextBlock)
				.Text(FText::FromString(RoleLabel))
				.ColorAndOpacity(FSlateColor(RoleColor))
			];
	}

	MainBox->AddSlot()
		.AutoHeight()
		.Padding(8, 0, 0, 4)
		[
			BodyWidget.ToSharedRef()
		];

	ChildSlot
	[
		MainBox.ToSharedRef()
	];
}

void SAutonomixMessage::AppendText(const FString& DeltaText)
{
	MessageData.Content += DeltaText;

	bool bIsCollapsible = MessageData.bIsCollapsible;

	int32 NewlineIndex;
	FString BodyText;
	if (bIsCollapsible)
	{
		if (MessageData.Content.FindChar('\n', NewlineIndex))
		{
			BodyText = MessageData.Content.Mid(NewlineIndex + 1);
		}
		else
		{
			BodyText = TEXT("");
		}
	}
	else
	{
		BodyText = MessageData.Content;
	}

	if (ContentTextBlock.IsValid())
	{
		ContentTextBlock->SetText(FText::FromString(BodyText));
	}
}

FText SAutonomixMessage::GetHeaderText() const
{
	FString FirstLine;
	int32 NewlineIndex;
	if (MessageData.Content.FindChar('\n', NewlineIndex))
	{
		FirstLine = MessageData.Content.Left(NewlineIndex);
	}
	else
	{
		FirstLine = MessageData.Content;
	}
	
	if (FirstLine.Len() > 180)
	{
		FirstLine = FirstLine.Left(180) + TEXT("...");
	}
	
	if (FirstLine.TrimStartAndEnd().IsEmpty())
	{
		FirstLine = TEXT("Details...");
	}
	
	return FText::FromString(FirstLine);
}
