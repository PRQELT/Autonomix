// Copyright Autonomix. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AutonomixTypes.h"

/**
 * Static model info database for all supported providers.
 * Modeled after Roo Code's packages/types/src/providers/*.ts model records.
 *
 * FAutonomixModelRegistry::GetModelInfo() returns capability data (context window,
 * max tokens, thinking support, pricing) for any known model ID.
 * Unknown models get safe defaults.
 */
class AUTONOMIXLLM_API FAutonomixModelRegistry
{
public:
	/**
	 * Look up model info for a given provider + model ID.
	 * Returns reasonable defaults if the model is not found in the registry.
	 */
	static FAutonomixModelInfo GetModelInfo(EAutonomixProvider Provider, const FString& ModelId);

	/**
	 * Get all known model IDs for a given provider.
	 */
	static TArray<FString> GetKnownModelIds(EAutonomixProvider Provider);

	/**
	 * Get all known models for a provider as FAutonomixModelInfo structs.
	 */
	static TArray<FAutonomixModelInfo> GetKnownModels(EAutonomixProvider Provider);

	/**
	 * Returns true if the model is known to support extended thinking (Anthropic budget_tokens
	 * or OpenAI reasoning effort or Gemini thinkingBudget).
	 */
	static bool ModelSupportsThinking(EAutonomixProvider Provider, const FString& ModelId);

	/**
	 * Returns true if the model is known to support Anthropic's 1M context beta.
	 */
	static bool ModelSupports1MContext(EAutonomixProvider Provider, const FString& ModelId);

private:
	// ----------------------------------------------------------------
	// Anthropic (Claude) models
	// ----------------------------------------------------------------
	static TArray<FAutonomixModelInfo> GetAnthropicModels();

	// ----------------------------------------------------------------
	// OpenAI models
	// ----------------------------------------------------------------
	static TArray<FAutonomixModelInfo> GetOpenAIModels();

	// ----------------------------------------------------------------
	// Google Gemini models
	// ----------------------------------------------------------------
	static TArray<FAutonomixModelInfo> GetGeminiModels();

	// ----------------------------------------------------------------
	// DeepSeek models
	// ----------------------------------------------------------------
	static TArray<FAutonomixModelInfo> GetDeepSeekModels();

	// ----------------------------------------------------------------
	// Mistral models
	// ----------------------------------------------------------------
	static TArray<FAutonomixModelInfo> GetMistralModels();

	// ----------------------------------------------------------------
	// xAI (Grok) models
	// ----------------------------------------------------------------
	static TArray<FAutonomixModelInfo> GetxAIModels();
};
