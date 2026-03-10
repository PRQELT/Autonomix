// Copyright Autonomix. All Rights Reserved.

#include "Widgets/SAutonomixPlanPreview.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"

void SAutonomixPlanPreview::Construct(const FArguments& InArgs)
{
	OnPlanApproved = InArgs._OnPlanApproved;
	OnPlanRejected = InArgs._OnPlanRejected;

	// Start hidden — only shown when there's a pending plan to review
	SetVisibility(EVisibility::Collapsed);

	ChildSlot
	[
		SNew(SBorder)
		.BorderBackgroundColor(FLinearColor(0.15f, 0.15f, 0.2f))
		.Padding(8.0f)
		[
			SNew(SVerticalBox)

			// Header
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0, 0, 0, 4)
			[
				SNew(STextBlock)
				.Text(FText::FromString(TEXT("🔧 Pending Tool Actions — Review Before Execution")))
				.Font(FCoreStyle::GetDefaultFontStyle("Bold", 12))
				.ColorAndOpacity(FSlateColor(FLinearColor(1.0f, 0.9f, 0.3f)))
			]

			// Action list
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SAssignNew(ActionList, SVerticalBox)
			]

			// Approve / Reject buttons
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0, 6, 0, 0)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(0, 0, 8, 0)
				[
					SNew(SButton)
					.Text(FText::FromString(TEXT("✅ Approve & Execute")))
					.OnClicked(FOnClicked::CreateSP(this, &SAutonomixPlanPreview::OnApproveClicked))
					.ButtonColorAndOpacity(FLinearColor(0.2f, 0.6f, 0.2f))
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(0, 0, 8, 0)
				[
					SNew(SButton)
					.Text(FText::FromString(TEXT("❌ Reject")))
					.OnClicked(FOnClicked::CreateSP(this, &SAutonomixPlanPreview::OnRejectClicked))
					.ButtonColorAndOpacity(FLinearColor(0.6f, 0.2f, 0.2f))
				]
			]
		]
	];
}

void SAutonomixPlanPreview::ShowPlan(const FAutonomixActionPlan& Plan)
{
	CurrentPlan = Plan;
	bPlanVisible = true;
	SetVisibility(EVisibility::Visible);

	// Clear previous actions
	if (ActionList.IsValid())
	{
		ActionList->ClearChildren();
	}

	// Populate the action list with tool call descriptions
	for (const FAutonomixAction& Action : Plan.Actions)
	{
		FString RiskBadge;
		FLinearColor RiskColor;
		switch (Action.RiskLevel)
		{
		case EAutonomixRiskLevel::Low:
			RiskBadge = TEXT("🟢 Low");
			RiskColor = FLinearColor(0.3f, 0.8f, 0.3f);
			break;
		case EAutonomixRiskLevel::Medium:
			RiskBadge = TEXT("🟡 Medium");
			RiskColor = FLinearColor(0.8f, 0.8f, 0.2f);
			break;
		case EAutonomixRiskLevel::High:
			RiskBadge = TEXT("🟠 High");
			RiskColor = FLinearColor(0.9f, 0.5f, 0.1f);
			break;
		case EAutonomixRiskLevel::Critical:
			RiskBadge = TEXT("🔴 Critical");
			RiskColor = FLinearColor(0.9f, 0.2f, 0.2f);
			break;
		}

		ActionList->AddSlot()
			.AutoHeight()
			.Padding(4, 2)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(0, 0, 8, 0)
				[
					SNew(STextBlock)
					.Text(FText::FromString(RiskBadge))
					.ColorAndOpacity(FSlateColor(RiskColor))
				]
				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				[
					SNew(STextBlock)
					.Text(FText::FromString(Action.Description))
					.AutoWrapText(true)
				]
			];
	}
}

void SAutonomixPlanPreview::ShowToolCalls(const TArray<FAutonomixToolCall>& ToolCalls)
{
	// Build a quick action plan from tool calls for display
	FAutonomixActionPlan Plan;
	Plan.Summary = FString::Printf(TEXT("%d tool(s) pending execution"), ToolCalls.Num());

	for (const FAutonomixToolCall& TC : ToolCalls)
	{
		FAutonomixAction Action;
		Action.Description = FString::Printf(TEXT("🔧 %s"), *TC.ToolName);
		Action.Category = TC.Category;

		// Assign risk based on category
		switch (TC.Category)
		{
		case EAutonomixActionCategory::Cpp:
		case EAutonomixActionCategory::Build:
		case EAutonomixActionCategory::Settings:
			Action.RiskLevel = EAutonomixRiskLevel::High;
			break;
		case EAutonomixActionCategory::SourceControl:
		case EAutonomixActionCategory::FileSystem:
			Action.RiskLevel = EAutonomixRiskLevel::Medium;
			break;
		default:
			Action.RiskLevel = EAutonomixRiskLevel::Low;
			break;
		}

		Plan.Actions.Add(Action);
	}

	ShowPlan(Plan);
}

void SAutonomixPlanPreview::HidePlan()
{
	bPlanVisible = false;
	SetVisibility(EVisibility::Collapsed);
	if (ActionList.IsValid()) ActionList->ClearChildren();
}

FReply SAutonomixPlanPreview::OnApproveClicked()
{
	OnPlanApproved.ExecuteIfBound(CurrentPlan);
	HidePlan();
	return FReply::Handled();
}

FReply SAutonomixPlanPreview::OnRejectClicked()
{
	OnPlanRejected.ExecuteIfBound(CurrentPlan);
	HidePlan();
	return FReply::Handled();
}
