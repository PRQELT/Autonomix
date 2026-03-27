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

	// =========================================================================
	// FIX: Auto-disable streaming for local providers (Ollama, LM Studio, Custom)
	//
	// Local model servers (Ollama, LiteLLM, LM Studio) do NOT reliably support
	// OpenAI-style SSE streaming for tool calls. Symptoms:
	//   - Requests hang indefinitely
	//   - Partial/malformed JSON tool call arguments
	//   - Raw JSON returned instead of proper SSE events
	//   - "Payload is incomplete" warnings from UE HTTP module
	//
	// The fix: disable streaming for local providers and use synchronous
	// /v1/chat/completions responses instead. The non-streaming code path in
	// HandleRequestComplete parses the full JSON response body directly.
	//
	// Users can override by explicitly enabling streaming in the UI setting.
	// =========================================================================
	const bool bIsLocalProvider = (Provider == EAutonomixProvider::Ollama ||
	                               Provider == EAutonomixProvider::LMStudio);
	if (bIsLocalProvider)
	{
		Client->SetStreamingEnabled(false);
		UE_LOG(LogAutonomix, Log, TEXT("LLMClientFactory: Streaming auto-disabled for local provider %s (non-SSE compatible)."),
			*ModelId);

		// Pass Ollama context size (num_ctx) — defaults to 8192 in settings.
		// Ollama's internal default is only 2048, which is far too small for Autonomix's
		// system prompt + 93 tool schemas + project context.
		// Roo Code native-ollama.ts: passes options.num_ctx when ollamaNumCtx is set.
		if (Settings->OllamaContextSize > 0)
		{
			Client->SetOllamaContextSize(Settings->OllamaContextSize);
			UE_LOG(LogAutonomix, Log, TEXT("LLMClientFactory: Ollama num_ctx = %d"), Settings->OllamaContextSize);
		}
	}

	switch (Provider)
	{
	case EAutonomixProvider::OpenAI:
		Client->SetReasoningEffort(Settings->OpenAiReasoningEffort);
		break;

	case EAutonomixProvider::Azure:
		// Azure OpenAI Service wire format (ported from Roo Code openai.ts AzureOpenAI client):
		//   - Auth:  'api-key' header (NOT 'Authorization: Bearer')
		//   - URL:   https://{resource}.openai.azure.com/openai/deployments/{deployment}/chat/completions?api-version=...
		//   - API:   Chat Completions only (Responses API NOT supported by Azure)
		// The client auto-activates Azure wire format when AzureApiVersion is set OR
		// when the BaseUrl contains ".azure.com" (see IsAzureUrl() in the client).
		Client->SetAzureApiVersion(Settings->AzureApiVersion.IsEmpty() ? TEXT("2024-02-01") : Settings->AzureApiVersion);
		Client->SetReasoningEffort(EAutonomixReasoningEffort::Disabled);  // Azure does not support reasoning_effort
		UE_LOG(LogAutonomix, Log, TEXT("LLMClientFactory: Azure OpenAI — resource=%s deployment=%s api-version=%s"),
			*Settings->AzureBaseUrl, *Settings->AzureDeploymentName,
			*(Settings->AzureApiVersion.IsEmpty() ? TEXT("2024-02-01") : Settings->AzureApiVersion));
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
	case EAutonomixProvider::Azure:
		// Show deployment name — it's the "model" from Azure's perspective
		return ModelId.IsEmpty()
			? TEXT("Azure OpenAI | (no deployment)")
			: FString::Printf(TEXT("Azure OpenAI | %s"), *ModelId);
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
