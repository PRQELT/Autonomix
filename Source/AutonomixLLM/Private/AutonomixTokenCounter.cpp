// Copyright Autonomix. All Rights Reserved.

#include "AutonomixTokenCounter.h"
#include "AutonomixSettings.h"
#include "AutonomixModelRegistry.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

int32 FAutonomixTokenCounter::EstimateTokens(const FString& Text)
{
	if (Text.IsEmpty()) return 0;
	return FMath::Max(1, Text.Len() / CharsPerToken);
}

int32 FAutonomixTokenCounter::EstimateTokens(const TArray<FAutonomixMessage>& Messages)
{
	int32 Total = 0;
	for (const FAutonomixMessage& Msg : Messages)
	{
		Total += EstimateTokens(Msg.Content);

		// If content blocks JSON is present, count that too (may contain more data)
		if (!Msg.ContentBlocksJson.IsEmpty())
		{
			Total += EstimateTokens(Msg.ContentBlocksJson);
		}
		else
		{
			// Content block overhead per message
			Total += MessageOverheadTokens;
		}
	}
	return Total;
}

int32 FAutonomixTokenCounter::EstimateTokens(const TSharedPtr<FJsonObject>& Json)
{
	if (!Json.IsValid()) return 0;

	FString Serialized;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Serialized);
	FJsonSerializer::Serialize(Json.ToSharedRef(), Writer);

	return EstimateTokens(Serialized);
}

int32 FAutonomixTokenCounter::EstimateTokens(const TArray<TSharedPtr<FJsonValue>>& JsonArray)
{
	if (JsonArray.Num() == 0) return 0;

	FString Serialized;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Serialized);
	FJsonSerializer::Serialize(JsonArray, Writer);

	return EstimateTokens(Serialized);
}

int32 FAutonomixTokenCounter::EstimateTokens(const TArray<TSharedPtr<FJsonObject>>& JsonArray)
{
	int32 Total = 0;
	for (const TSharedPtr<FJsonObject>& Obj : JsonArray)
	{
		Total += EstimateTokens(Obj);
	}
	return Total;
}

int32 FAutonomixTokenCounter::GetContextWindowTokens(bool bExtended)
{
	// Provider-aware context window lookup.
	// The old hardcoded 200K/1M only worked for Anthropic.
	// Now we check the active provider + model from settings and use the model registry.
	const UAutonomixDeveloperSettings* Settings = UAutonomixDeveloperSettings::Get();
	if (Settings)
	{
		// For Anthropic, honor the explicit ContextWindow setting (Standard_200K / Extended_1M)
		if (Settings->ActiveProvider == EAutonomixProvider::Anthropic)
		{
			return bExtended ? 1000000 : 200000;
		}

		// For all other providers, look up the model's actual context window from the registry
		FString ModelId;
		switch (Settings->ActiveProvider)
		{
		case EAutonomixProvider::OpenAI:      ModelId = Settings->OpenAiModelId; break;
		case EAutonomixProvider::Google:       ModelId = Settings->GeminiModelId; break;
		case EAutonomixProvider::DeepSeek:     ModelId = Settings->DeepSeekModelId; break;
		case EAutonomixProvider::Mistral:      ModelId = Settings->MistralModelId; break;
		case EAutonomixProvider::xAI:          ModelId = Settings->xAIModelId; break;
		case EAutonomixProvider::OpenRouter:   ModelId = Settings->OpenRouterModelId; break;
		case EAutonomixProvider::Ollama:       ModelId = Settings->OllamaModelId; break;
		case EAutonomixProvider::LMStudio:     ModelId = Settings->LMStudioModelId; break;
		case EAutonomixProvider::Custom:       ModelId = Settings->CustomEndpointModelId; break;
		case EAutonomixProvider::Azure:         ModelId = Settings->AzureDeploymentName; break;
		case EAutonomixProvider::GitHubCopilot: ModelId = Settings->CopilotModelId; break;
		default: break;
		}

		if (!ModelId.IsEmpty())
		{
			FAutonomixModelInfo Info = FAutonomixModelRegistry::GetModelInfo(Settings->ActiveProvider, ModelId);
			if (Info.ContextWindow > 0)
			{
				return Info.ContextWindow;
			}
		}

		// For Ollama, use the configured context size if no registry entry
		if (Settings->ActiveProvider == EAutonomixProvider::Ollama && Settings->OllamaContextSize > 0)
		{
			return Settings->OllamaContextSize;
		}
	}

	// Fallback: Anthropic-style default
	return bExtended ? 1000000 : 200000;
}

float FAutonomixTokenCounter::GetContextUsagePercent(int32 UsedTokens, int32 TotalWindowTokens)
{
	if (TotalWindowTokens <= 0) return 0.0f;
	return FMath::Clamp(100.0f * static_cast<float>(UsedTokens) / static_cast<float>(TotalWindowTokens), 0.0f, 100.0f);
}
