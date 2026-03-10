// Copyright Autonomix. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "AutonomixTypes.h"

DECLARE_DELEGATE_OneParam(FOnAutonomixPlanApproved, const FAutonomixActionPlan&);
DECLARE_DELEGATE_OneParam(FOnAutonomixPlanRejected, const FAutonomixActionPlan&);

/**
 * Plan preview widget showing proposed actions with risk badges and approve/reject buttons.
 *
 * HIDDEN by default (Collapsed). Only becomes visible when:
 * - Tool calls are received and auto-approve is OFF
 * - The user needs to review and approve/reject the pending actions
 *
 * After approval or rejection, hides itself again.
 */
class AUTONOMIXUI_API SAutonomixPlanPreview : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SAutonomixPlanPreview) {}
		SLATE_EVENT(FOnAutonomixPlanApproved, OnPlanApproved)
		SLATE_EVENT(FOnAutonomixPlanRejected, OnPlanRejected)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	/** Display a new action plan for review */
	void ShowPlan(const FAutonomixActionPlan& Plan);

	/** Display pending tool calls for review (auto-generates an action plan) */
	void ShowToolCalls(const TArray<FAutonomixToolCall>& ToolCalls);

	/** Hide the plan preview */
	void HidePlan();

	/** Whether a plan is currently being shown */
	bool IsPlanVisible() const { return bPlanVisible; }

private:
	FReply OnApproveClicked();
	FReply OnRejectClicked();

	FAutonomixActionPlan CurrentPlan;
	bool bPlanVisible = false;
	FOnAutonomixPlanApproved OnPlanApproved;
	FOnAutonomixPlanRejected OnPlanRejected;
	TSharedPtr<SVerticalBox> ActionList;
};
