// Copyright Autonomix. All Rights Reserved.

#include "AutonomixLLMClientFactory.h"
#include "AutonomixCoreModule.h"
#include "AutonomixSettings.h"
#include "AutonomixClaudeClient.h"
#include "AutonomixOpenAICompatClient.h"
#include "AutonomixGeminiClient.h"
#include "AutonomixModelRegistry.h"

TSharedPtr<IAutonomixLLMClient> FAutonomixLLMClientFactory::CreateClient()
{
	const UAutonomixDeveloperSettings* Settings = UAutonomixDeveloperSettings::Get();
	if (!Settings)
	{
		UE_LOG(LogAutonomix, Error, TEXT("LLMClientFactory: Could not load UAutonomixDeveloperSettings."));
		return nullptr;
	}
	return CreateClientForProvider(Settings->ActiveProvider, Settings);
}

TSharedPtr<IAutonomixLLMClient> FAutonomixLLMClientFactory::CreateClientForProvider(
	EAutonomixProvider Provider,
	const UAutonomixDeveloperSettings* Settings)
{
	if (!Settings)
	{
		UE_LOG(LogAutonomix, Error, TEXT("LLMClientFactory: Null settings passed to CreateClientForProvider."));
		return nullptr;
	}

	FString ModelId = Settings->GetEffectiveModel();
	FString Endpoint = Settings->GetEffectiveEndpoint();
	FString ApiKey = Settings->GetActiveApiKey();

	UE_LOG(LogAutonomix, Log, TEXT("LLMClientFactory: Creating client for provider=%d model=%s"),
		(int32)Provider, *ModelId);

	// -------------------------------------------------------------------------
	// Anthropic (Claude) — uses AutonomixClaudeClient (Anthropic Messages API)
	// -------------------------------------------------------------------------
	if (Provider == EAutonomixProvider::Anthropic)
	{
		TSharedPtr<FAutonomixClaudeClient> Client = MakeShared<FAutonomixClaudeClient>();
		Client->SetEndpoint(Endpoint);
		Client->SetApiKey(ApiKey);
		Client->SetModel(ModelId);
		Client->SetMaxTokens(Settings->MaxResponseTokens);
		return Client;
	}

	// -------------------------------------------------------------------------
	// Google Gemini — uses AutonomixGeminiClient (Google Generative Language API)
	// -------------------------------------------------------------------------
	if (Provider == EAutonomixProvider::Google)
	{
		TSharedPtr<FAutonomixGeminiClient> Client = MakeShared<FAutonomixGeminiClient>();
		Client->SetApiKey(ApiKey);
		Client->SetModel(ModelId);
		Client->SetBaseUrl(Settings->GeminiBaseUrl);
		Client->SetMaxTokens(Settings->MaxResponseTokens);
		Client->SetThinkingBudget(Settings->GeminiThinkingBudgetTokens);
		Client->SetReasoningEffort(Settings->GeminiReasoningEffort);
		return Client;
	}

	// -------------------------------------------------------------------------
	// All other providers — use AutonomixOpenAICompatClient
	// -------------------------------------------------------------------------
	TSharedPtr<FAutonomixOpenAICompatClient> Client = MakeShared<FAutonomixOpenAICompatClient>();
	Client->SetProvider(Provider);
	Client->SetEndpoint(Endpoint);
	Client->SetApiKey(ApiKey);
	Client->SetModel(ModelId);
	Client->SetMaxTokens(Settings->MaxResponseTokens);

	switch (Provider)
	{
	case EAutonomixProvider::OpenAI:
		Client->SetReasoningEffort(Settings->OpenAiReasoningEffort);
		break;
	case EAutonomixProvider::DeepSeek:
	{
		// deepseek-reasoner supports reasoning effort mapping
		bool bIsReasoner = ModelId.Contains(TEXT("reasoner"));
		Client->SetReasoningEffort(bIsReasoner ? EAutonomixReasoningEffort::Medium : EAutonomixReasoningEffort::Disabled);
		break;
	}
	case EAutonomixProvider::OpenRouter:
	case EAutonomixProvider::Ollama:
	case EAutonomixProvider::LMStudio:
	case EAutonomixProvider::Mistral:
	case EAutonomixProvider::xAI:
	case EAutonomixProvider::Custom:
	default:
		Client->SetReasoningEffort(EAutonomixReasoningEffort::Disabled);
		break;
	}

	return Client;
}

FString FAutonomixLLMClientFactory::GetActiveProviderDisplayName()
{
	const UAutonomixDeveloperSettings* Settings = UAutonomixDeveloperSettings::Get();
	if (!Settings) return TEXT("Unknown");

	FString ModelId = Settings->GetEffectiveModel();

	switch (Settings->ActiveProvider)
	{
	case EAutonomixProvider::Anthropic:
		return FString::Printf(TEXT("Claude | %s"), *ModelId);
	case EAutonomixProvider::OpenAI:
		return FString::Printf(TEXT("OpenAI | %s"), *ModelId);
	case EAutonomixProvider::Google:
		return FString::Printf(TEXT("Gemini | %s"), *ModelId);
	case EAutonomixProvider::DeepSeek:
		return FString::Printf(TEXT("DeepSeek | %s"), *ModelId);
	case EAutonomixProvider::Mistral:
		return FString::Printf(TEXT("Mistral | %s"), *ModelId);
	case EAutonomixProvider::xAI:
		return FString::Printf(TEXT("xAI | %s"), *ModelId);
	case EAutonomixProvider::OpenRouter:
		return FString::Printf(TEXT("OpenRouter | %s"), *ModelId);
	case EAutonomixProvider::Ollama:
		return FString::Printf(TEXT("Ollama (local) | %s"), *ModelId);
	case EAutonomixProvider::LMStudio:
		return FString::Printf(TEXT("LM Studio (local) | %s"), *ModelId);
	case EAutonomixProvider::Custom:
		return FString::Printf(TEXT("Custom | %s"), *ModelId);
	default:
		return ModelId;
	}
}
