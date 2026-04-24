// Copyright Autonomix. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AutonomixTypes.h"

/**
 * Simple approximate token counter for Autonomix conversations.
 *
 * Uses the standard ~4 characters per token approximation.
 * This is intentionally simple — for pre-flight context budget checks,
 * not billing-accurate counting.
 */
class FAutonomixTokenCounter
{
public:
	/** Approximate tokens for a plain text string (~4 chars per token) */
	static int32 EstimateTokens(const FString& Text);

	/** Approximate tokens for an array of messages (sums all content) */
	static int32 EstimateTokens(const TArray<FAutonomixMessage>& Messages);

	/** Approximate tokens for a JSON object (serializes then counts) */
	static int32 EstimateTokens(const TSharedPtr<FJsonObject>& Json);

	/** Approximate tokens for a JSON value array (serializes then counts) */
	static int32 EstimateTokens(const TArray<TSharedPtr<FJsonValue>>& JsonArray);

	/** Approximate tokens for a JSON object array (serializes then counts) */
	static int32 EstimateTokens(const TArray<TSharedPtr<FJsonObject>>& JsonArray);

	/** Get the context window size in tokens for the given setting */
	static int32 GetContextWindowTokens(bool bExtended = false);

	/** Compute context usage as a percentage (0–100) */
	static float GetContextUsagePercent(int32 UsedTokens, int32 TotalWindowTokens);

private:
	static constexpr int32 CharsPerToken = 4;
	static constexpr int32 MessageOverheadTokens = 10; // Per-message overhead
};
