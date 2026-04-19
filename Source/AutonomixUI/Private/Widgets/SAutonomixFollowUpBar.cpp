// Copyright Autonomix. All Rights Reserved.

#include "Widgets/SAutonomixFollowUpBar.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Text/STextBlock.h"

void SAutonomixFollowUpBar::Construct(const FArguments& InArgs)
{
	OnFollowUpSelected = InArgs._OnFollowUpSelected;

	ChildSlot
	[
		SNew(SVerticalBox)
		.Visibility(EVisibility::Collapsed)

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(4, 2)
		[
			SNew(STextBlock)
			.Text(FText::FromString(TEXT("💡 Follow-up suggestions:")))
			.ColorAndOpacity(FLinearColor(0.5f, 0.5f, 0.6f))
			.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
		]

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(4, 0, 4, 4)
		[
			SNew(SWrapBox)
			.UseAllottedSize(true)
			.InnerSlotPadding(FVector2D(4, 4))

			// Suggestion buttons will be added dynamically
			+ SWrapBox::Slot()
			[
				SAssignNew(SuggestionContainer, SHorizontalBox)
			]
		]
	];
}

void SAutonomixFollowUpBar::ShowSuggestionsForResult(const FString& CompletionResult)
{
	if (!SuggestionContainer.IsValid()) return;
	SuggestionContainer->ClearChildren();

	TArray<FString> Suggestions = GenerateSuggestions(CompletionResult);

	for (const FString& Suggestion : Suggestions)
	{
		SuggestionContainer->AddSlot()
		.AutoWidth()
		.Padding(0, 0, 4, 0)
		[
			SNew(SButton)
			.Text(FText::FromString(Suggestion))
			.ToolTipText(FText::FromString(TEXT("Click to send this follow-up")))
			.ButtonColorAndOpacity(FLinearColor(0.15f, 0.15f, 0.2f))
			.OnClicked_Lambda([this, Suggestion]() -> FReply
			{
				OnFollowUpSelected.ExecuteIfBound(Suggestion);
				return FReply::Handled();
			})
		];
	}

	if (Suggestions.Num() > 0)
	{
		SetVisibility(EVisibility::Visible);
	}
}

void SAutonomixFollowUpBar::Hide()
{
	SetVisibility(EVisibility::Collapsed);
	if (SuggestionContainer.IsValid())
	{
		SuggestionContainer->ClearChildren();
	}
}

TArray<FString> SAutonomixFollowUpBar::GenerateSuggestions(const FString& ResultText)
{
	TArray<FString> Suggestions;

	const FString Lower = ResultText.ToLower();

	// C++ class created
	if (Lower.Contains(TEXT(".h")) || Lower.Contains(TEXT(".cpp")) ||
		Lower.Contains(TEXT("class")) || Lower.Contains(TEXT("actor")))
	{
		Suggestions.Add(TEXT("Add Blueprint exposure (UFUNCTION/UPROPERTY macros)"));
		Suggestions.Add(TEXT("Write unit tests for this implementation"));
		Suggestions.Add(TEXT("Add documentation comments (Doxygen style)"));
		Suggestions.Add(TEXT("Make this multiplayer-compatible with replication"));
		return Suggestions;
	}

	// Blueprint work
	if (Lower.Contains(TEXT("blueprint")) || Lower.Contains(TEXT("graph")))
	{
		Suggestions.Add(TEXT("Add error handling and input validation"));
		Suggestions.Add(TEXT("Create a C++ base class for better performance"));
		Suggestions.Add(TEXT("Add tooltips and description to all exposed variables"));
		return Suggestions;
	}

	// Material/asset
	if (Lower.Contains(TEXT("material")) || Lower.Contains(TEXT("texture")))
	{
		Suggestions.Add(TEXT("Create material instances for runtime variations"));
		Suggestions.Add(TEXT("Optimize the material complexity and shader instructions"));
		Suggestions.Add(TEXT("Add LOD variants for performance"));
		return Suggestions;
	}

	// Build/compile
	if (Lower.Contains(TEXT("build")) || Lower.Contains(TEXT("compil")))
	{
		Suggestions.Add(TEXT("Run the project and test the changes in-editor"));
		Suggestions.Add(TEXT("Profile memory usage of the new code"));
		return Suggestions;
	}

	// Generic suggestions
	Suggestions.Add(TEXT("Add error handling and edge case validation"));
	Suggestions.Add(TEXT("Write documentation comments for all public APIs"));
	Suggestions.Add(TEXT("Optimize for performance"));
	Suggestions.Add(TEXT("Add logging for debugging"));

	return Suggestions;
}
