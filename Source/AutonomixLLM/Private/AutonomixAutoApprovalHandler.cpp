// Copyright Autonomix. All Rights Reserved.

#include "AutonomixAutoApprovalHandler.h"
#include "AutonomixCoreModule.h"

FAutonomixAutoApprovalHandler::FAutonomixAutoApprovalHandler() {}

FAutonomixAutoApprovalCheck FAutonomixAutoApprovalHandler::CheckLimits(
	int32 MaxRequests,
	float MaxCost,
	float CurrentRequestCost)
{
	FAutonomixAutoApprovalCheck Check;
	Check.CurrentRequestCount = RequestCountSinceReset;
	Check.CurrentCost = CostSinceReset;

	// Check request count limit
	if (MaxRequests > 0 && RequestCountSinceReset >= MaxRequests)
	{
		Check.bCanProceed = false;
		Check.bRequiresApproval = true;
		Check.ApprovalReason = FString::Printf(
			TEXT("Auto-approval limit reached: %d/%d consecutive requests.\n"
				 "Click 'Continue' to keep going or 'Stop' to cancel."),
			RequestCountSinceReset,
			MaxRequests);
		UE_LOG(LogAutonomix, Log, TEXT("AutoApprovalHandler: Request limit reached (%d/%d)"),
			RequestCountSinceReset, MaxRequests);
		return Check;
	}

	// Check cost limit (with epsilon for float comparison)
	const float Epsilon = 0.0001f;
	if (MaxCost > 0.0f && CostSinceReset + CurrentRequestCost > MaxCost + Epsilon)
	{
		Check.bCanProceed = false;
		Check.bRequiresApproval = true;
		Check.ApprovalReason = FString::Printf(
			TEXT("Auto-approval cost limit reached: $%.4f/$%.2f spent.\n"
				 "Click 'Continue' to keep going or 'Stop' to cancel."),
			CostSinceReset + CurrentRequestCost,
			MaxCost);
		UE_LOG(LogAutonomix, Log, TEXT("AutoApprovalHandler: Cost limit reached ($%.4f/$%.2f)"),
			CostSinceReset + CurrentRequestCost, MaxCost);
		return Check;
	}

	return Check;
}

void FAutonomixAutoApprovalHandler::RecordBatch(float BatchCost)
{
	RequestCountSinceReset++;
	CostSinceReset += BatchCost;
}

void FAutonomixAutoApprovalHandler::ResetBaseline()
{
	RequestCountSinceReset = 0;
	CostSinceReset = 0.0f;
	UE_LOG(LogAutonomix, Log, TEXT("AutoApprovalHandler: Baseline reset after user approval."));
}

void FAutonomixAutoApprovalHandler::Reset()
{
	RequestCountSinceReset = 0;
	CostSinceReset = 0.0f;
}
