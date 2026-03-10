// Copyright Autonomix. All Rights Reserved.

#include "AutonomixCostTracker.h"
#include "AutonomixCoreModule.h"

FAutonomixCostTracker::FAutonomixCostTracker() {}

// ============================================================================
// Claude Model Pricing (USD per 1M tokens, as of 2025)
// Based on Roo Code's cost.ts + Anthropic pricing page
// ============================================================================

FAutonomixModelPricing FAutonomixCostTracker::GetModelPricing(EAutonomixClaudeModel Model)
{
	FAutonomixModelPricing Pricing;

	switch (Model)
	{
	case EAutonomixClaudeModel::Sonnet_4_6:
		// Claude Sonnet 4.6 / Claude Sonnet 4 pricing
		Pricing.InputPricePerMillion = 3.0f;
		Pricing.OutputPricePerMillion = 15.0f;
		Pricing.CacheWritePricePerMillion = 3.75f;   // 1.25x input
		Pricing.CacheReadPricePerMillion = 0.30f;    // 0.1x input
		break;

	case EAutonomixClaudeModel::Sonnet_4_5:
		// Claude Sonnet 4.5 pricing
		Pricing.InputPricePerMillion = 3.0f;
		Pricing.OutputPricePerMillion = 15.0f;
		Pricing.CacheWritePricePerMillion = 3.75f;
		Pricing.CacheReadPricePerMillion = 0.30f;
		break;

	case EAutonomixClaudeModel::Opus_4_6:
		// Claude Opus 4.6 / Claude Opus 4 pricing
		Pricing.InputPricePerMillion = 15.0f;
		Pricing.OutputPricePerMillion = 75.0f;
		Pricing.CacheWritePricePerMillion = 18.75f;  // 1.25x input
		Pricing.CacheReadPricePerMillion = 1.50f;    // 0.1x input
		break;

	case EAutonomixClaudeModel::Opus_4_5:
		// Claude Opus 4.5 pricing
		Pricing.InputPricePerMillion = 15.0f;
		Pricing.OutputPricePerMillion = 75.0f;
		Pricing.CacheWritePricePerMillion = 18.75f;
		Pricing.CacheReadPricePerMillion = 1.50f;
		break;

	case EAutonomixClaudeModel::Haiku_4:
		// Claude Haiku 4 pricing (fastest, cheapest)
		Pricing.InputPricePerMillion = 0.80f;
		Pricing.OutputPricePerMillion = 4.0f;
		Pricing.CacheWritePricePerMillion = 1.0f;
		Pricing.CacheReadPricePerMillion = 0.08f;
		break;

	case EAutonomixClaudeModel::Custom:
	default:
		// Use Sonnet 4.6 as fallback estimate
		Pricing.InputPricePerMillion = 3.0f;
		Pricing.OutputPricePerMillion = 15.0f;
		Pricing.CacheWritePricePerMillion = 3.75f;
		Pricing.CacheReadPricePerMillion = 0.30f;
		break;
	}

	return Pricing;
}

FAutonomixRequestCost FAutonomixCostTracker::CalculateRequestCost(
	EAutonomixClaudeModel Model,
	const FAutonomixTokenUsage& Usage)
{
	FAutonomixModelPricing Pricing = GetModelPricing(Model);
	FAutonomixRequestCost Cost;

	// Anthropic billing model: InputTokens does NOT include cached tokens
	// Total input = base input + cache creation + cache reads
	// (matches Roo Code's calculateApiCostAnthropic)
	int32 BaseInputTokens = Usage.InputTokens;
	int32 CacheCreationTokens = Usage.CacheCreationInputTokens;
	int32 CacheReadTokens = Usage.CacheReadInputTokens;

	// Cost = (tokens / 1,000,000) * price_per_million
	Cost.InputCost = (BaseInputTokens / 1000000.0f) * Pricing.InputPricePerMillion;
	Cost.OutputCost = (Usage.OutputTokens / 1000000.0f) * Pricing.OutputPricePerMillion;
	Cost.CacheWriteCost = (CacheCreationTokens / 1000000.0f) * Pricing.CacheWritePricePerMillion;
	Cost.CacheReadCost = (CacheReadTokens / 1000000.0f) * Pricing.CacheReadPricePerMillion;

	Cost.TotalCost = Cost.InputCost + Cost.OutputCost + Cost.CacheWriteCost + Cost.CacheReadCost;

	return Cost;
}

FString FAutonomixCostTracker::FormatCost(float Cost)
{
	if (Cost < 0.001f)
	{
		return TEXT("< $0.001");
	}
	else if (Cost < 0.01f)
	{
		return FString::Printf(TEXT("$%.4f"), Cost);
	}
	else if (Cost < 1.0f)
	{
		return FString::Printf(TEXT("$%.3f"), Cost);
	}
	else
	{
		return FString::Printf(TEXT("$%.2f"), Cost);
	}
}

void FAutonomixCostTracker::AddRequestCost(const FAutonomixRequestCost& RequestCost)
{
	SessionTotalCost += RequestCost.TotalCost;
	SessionRequestCount++;
	CostSinceLastReset += RequestCost.TotalCost;
	RequestCountSinceLastReset++;
}

void FAutonomixCostTracker::Reset()
{
	SessionTotalCost = 0.0f;
	SessionRequestCount = 0;
	CostSinceLastReset = 0.0f;
	RequestCountSinceLastReset = 0;
}

void FAutonomixCostTracker::ResetTrackingBaseline()
{
	CostSinceLastReset = 0.0f;
	RequestCountSinceLastReset = 0;
}
