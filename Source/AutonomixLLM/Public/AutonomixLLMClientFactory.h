// Copyright Autonomix. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AutonomixInterfaces.h"
#include "AutonomixTypes.h"

/**
 * Factory for creating the correct IAutonomixLLMClient implementation
 * based on the currently selected provider in UAutonomixDeveloperSettings.
 *
 * Usage:
 *   TSharedPtr<IAutonomixLLMClient> Client = FAutonomixLLMClientFactory::CreateClient();
 *   // Wires up all settings from UAutonomixDeveloperSettings automatically.
 *
 * To force a specific provider (e.g. for testing):
 *   TSharedPtr<IAutonomixLLMClient> Client = FAutonomixLLMClientFactory::CreateClientForProvider(
 *       EAutonomixProvider::OpenAI, Settings);
 */
class AUTONOMIXLLM_API FAutonomixLLMClientFactory
{
public:
	/**
	 * Create the client for the currently configured active provider.
	 * Reads all configuration from UAutonomixDeveloperSettings::Get().
	 * Returns nullptr if the provider is not configured (missing API key, etc.).
	 */
	static TSharedPtr<IAutonomixLLMClient> CreateClient();

	/**
	 * Create a client for a specific provider using the given settings object.
	 */
	static TSharedPtr<IAutonomixLLMClient> CreateClientForProvider(
		EAutonomixProvider Provider,
		const class UAutonomixDeveloperSettings* Settings
	);

	/**
	 * Get a human-readable display name for the current provider + model combination.
	 * Used for display in the chat panel header.
	 */
	static FString GetActiveProviderDisplayName();
};
