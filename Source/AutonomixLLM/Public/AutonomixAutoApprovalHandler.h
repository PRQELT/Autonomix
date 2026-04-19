// Copyright Autonomix. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AutonomixTypes.h"

/**
 * Tracks consecutive auto-approved requests and cumulative cost.
 * When limits are exceeded, signals that user approval is required.
 *
 * Ported from Roo Code's AutoApprovalHandler.ts.
 *
 * Two independent limits, both checked before each tool execution batch:
 *  1. MaxConsecutiveRequests — how many tool batches before requiring user confirmation
 *  2. MaxCumulativeCost — how much USD spent before requiring user confirmation
 *
 * Usage:
 *   Before each ProcessToolCallQueue():
 *     FAutonomixAutoApprovalCheck Check = Handler.CheckLimits(Settings, RequestCost);
 *     if (Check.bRequiresApproval) { show pause dialog }
 *     else { proceed with tool execution }
 */
struct FAutonomixAutoApprovalCheck
{
	/** True if we can proceed without approval */
	bool bCanProceed = true;

	/** True if user approval is required */
	bool bRequiresApproval = false;

	/** Reason for requiring approval */
	FString ApprovalReason;

	/** Current request count since last reset */
	int32 CurrentRequestCount = 0;

	/** Current cost since last reset (USD) */
	float CurrentCost = 0.0f;
};

class AUTONOMIXLLM_API FAutonomixAutoApprovalHandler
{
public:
	FAutonomixAutoApprovalHandler();

	/**
	 * Check if auto-approval limits have been reached.
	 * Call this before each tool execution batch.
	 *
	 * @param MaxRequests       Max requests before requiring approval (0 = unlimited)
	 * @param MaxCost           Max cost (USD) before requiring approval (0.0 = unlimited)
	 * @param CurrentRequestCost  Cost of the most recent API request
	 * @return                  Approval check result
	 */
	FAutonomixAutoApprovalCheck CheckLimits(
		int32 MaxRequests,
		float MaxCost,
		float CurrentRequestCost = 0.0f);

	/**
	 * Record a tool execution batch (increment counters).
	 * Call after successful tool execution.
	 */
	void RecordBatch(float BatchCost = 0.0f);

	/**
	 * Reset tracking after user approves continuation past a limit.
	 */
	void ResetBaseline();

	/**
	 * Full reset (new conversation started).
	 */
	void Reset();

	int32 GetRequestCountSinceReset() const { return RequestCountSinceReset; }
	float GetCostSinceReset() const { return CostSinceReset; }

private:
	int32 RequestCountSinceReset = 0;
	float CostSinceReset = 0.0f;
};
