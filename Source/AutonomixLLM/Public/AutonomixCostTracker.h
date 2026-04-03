// Copyright Autonomix. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AutonomixTypes.h"
#include "AutonomixSettings.h"

/**
 * Model pricing data (prices per 1M tokens in USD).
 * Ported from Roo Code's cost.ts calculateApiCostAnthropic pattern.
 */
struct FAutonomixModelPricing
{
	float InputPricePerMillion = 0.0f;     // $/1M input tokens
	float OutputPricePerMillion = 0.0f;    // $/1M output tokens
	float CacheWritePricePerMillion = 0.0f; // $/1M cache write tokens (1.25x input for Anthropic)
	float CacheReadPricePerMillion = 0.0f;  // $/1M cache read tokens (0.1x input for Anthropic)
};

/**
 * Cost calculation result for a single API request.
 */
struct FAutonomixRequestCost
{
	float InputCost = 0.0f;       // Cost for input tokens
	float OutputCost = 0.0f;      // Cost for output tokens
	float CacheWriteCost = 0.0f;  // Cost for cache writes
	float CacheReadCost = 0.0f;   // Cost for cache reads
	float TotalCost = 0.0f;       // Total request cost
};

/**
 * Tracks API costs per request and for the session.
 *
 * Implements model-specific pricing modeled after Roo Code's cost.ts.
 * Supports the Anthropic billing model (input tokens do NOT include cache tokens).
 */
class AUTONOMIXLLM_API FAutonomixCostTracker
{
public:
	FAutonomixCostTracker();

	/**
	 * Calculate the cost for a single API request using the given model and token usage.
	 *
	 * @param Model    The Claude model enum value
	 * @param Usage    The token usage reported by the API response
	 * @return         The calculated cost breakdown
	 */
	static FAutonomixRequestCost CalculateCost(
		EAutonomixProvider Provider,
		const FString& ModelSlug,
		const FAutonomixTokenUsage& Usage);

	static FAutonomixRequestCost CalculateRequestCost(
		EAutonomixClaudeModel Model,
		const FAutonomixTokenUsage& Usage);

	/**
	 * Get the pricing info for a specific Claude model.
	 */
	static FAutonomixModelPricing GetModelPricing(EAutonomixClaudeModel Model);

	/**
	 * Format a cost as a readable string (e.g., "$0.0012" or "< $0.001")
	 */
	static FString FormatCost(float Cost);

	/**
	 * Accumulate a request cost into the session total.
	 */
	void AddRequestCost(const FAutonomixRequestCost& RequestCost);
	void AddRequestCost(EAutonomixProvider Provider, const FString& ModelSlug, const FAutonomixTokenUsage& Usage);

	/** Reset session totals */
	void Reset();

	/** Get total session cost */
	float GetSessionTotalCost() const { return SessionTotalCost; }

	/** Get total request count this session */
	int32 GetSessionRequestCount() const { return SessionRequestCount; }

	/** Get cost since last reset point */
	float GetCostSinceReset() const { return CostSinceLastReset; }

	/** Get request count since last reset point */
	int32 GetRequestCountSinceReset() const { return RequestCountSinceLastReset; }

	/** Reset the tracking baseline (used after user approves continuing past a cost limit) */
	void ResetTrackingBaseline();

private:
	float SessionTotalCost = 0.0f;
	int32 SessionRequestCount = 0;
	float CostSinceLastReset = 0.0f;
	int32 RequestCountSinceLastReset = 0;
};
