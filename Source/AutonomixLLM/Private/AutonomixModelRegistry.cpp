// Copyright Autonomix. All Rights Reserved.
// Model info database — adapted from Roo Code's packages/types/src/providers/*.ts

#include "AutonomixModelRegistry.h"
#include "AutonomixCoreModule.h"

// ---------------------------------------------------------------------------
// Anthropic models
// ---------------------------------------------------------------------------
TArray<FAutonomixModelInfo> FAutonomixModelRegistry::GetAnthropicModels()
{
	TArray<FAutonomixModelInfo> Models;
	auto Make = [](const FString& Id, const FString& Name, int32 Ctx, int32 MaxOut,
		bool bThink, bool b1M, float InPrice, float OutPrice,
		float CacheW = 0.f, float CacheR = 0.f) -> FAutonomixModelInfo
	{
		FAutonomixModelInfo M;
		M.Provider = EAutonomixProvider::Anthropic;
		M.ModelId = Id;
		M.DisplayName = Name;
		M.ContextWindow = Ctx;
		M.MaxOutputTokens = MaxOut;
		M.bSupportsReasoningBudget = bThink;
		M.MaxThinkingTokens = bThink ? 64000 : 0;
		M.bSupports1MContext = b1M;
		M.bSupportsImages = true;
		M.bSupportsPromptCache = true;
		M.InputPricePerMillion = InPrice;
		M.OutputPricePerMillion = OutPrice;
		M.CacheWritesPricePerMillion = CacheW;
		M.CacheReadsPricePerMillion = CacheR;
		return M;
	};

	// Claude Sonnet 4.x — flagship, 200K default, 1M beta
	Models.Add(Make(TEXT("claude-sonnet-4-6"),         TEXT("Claude Sonnet 4.6"),         200000, 64000, true,  true,  3.0f,  15.0f, 3.75f, 0.3f));
	Models.Add(Make(TEXT("claude-sonnet-4-5"),         TEXT("Claude Sonnet 4.5"),         200000, 64000, true,  true,  3.0f,  15.0f, 3.75f, 0.3f));
	Models.Add(Make(TEXT("claude-sonnet-4-20250514"),  TEXT("Claude Sonnet 4 (20250514)"),200000, 64000, true,  true,  3.0f,  15.0f, 3.75f, 0.3f));
	// Claude Opus 4.x — most capable
	Models.Add(Make(TEXT("claude-opus-4-6"),           TEXT("Claude Opus 4.6"),           200000,128000, true,  true,  5.0f,  25.0f, 6.25f, 0.5f));
	Models.Add(Make(TEXT("claude-opus-4-5-20251101"),  TEXT("Claude Opus 4.5"),           200000, 32000, true,  false, 5.0f,  25.0f, 6.25f, 0.5f));
	Models.Add(Make(TEXT("claude-opus-4-20250514"),    TEXT("Claude Opus 4"),             200000, 32000, true,  false,15.0f,  75.0f,18.75f, 1.5f));
	// Claude Haiku 4.x — fastest, cheapest
	Models.Add(Make(TEXT("claude-haiku-4-5-20251001"), TEXT("Claude Haiku 4.5"),          200000, 64000, true,  false, 1.0f,   5.0f, 1.25f, 0.1f));
	// Claude 3.7 Sonnet (extended thinking variant)
	Models.Add(Make(TEXT("claude-3-7-sonnet-20250219:thinking"), TEXT("Claude 3.7 Sonnet (Thinking)"), 200000, 128000, true, false, 3.0f, 15.0f, 3.75f, 0.3f));
	Models.Add(Make(TEXT("claude-3-7-sonnet-20250219"), TEXT("Claude 3.7 Sonnet"),       200000,  8192, false, false, 3.0f,  15.0f, 3.75f, 0.3f));
	// Claude 3.5 series
	Models.Add(Make(TEXT("claude-3-5-sonnet-20241022"), TEXT("Claude 3.5 Sonnet"),       200000,  8192, false, false, 3.0f,  15.0f, 3.75f, 0.3f));
	Models.Add(Make(TEXT("claude-3-5-haiku-20241022"),  TEXT("Claude 3.5 Haiku"),        200000,  8192, false, false, 1.0f,   5.0f, 1.25f, 0.1f));
	// Claude 3 series
	Models.Add(Make(TEXT("claude-3-opus-20240229"),     TEXT("Claude 3 Opus"),           200000,  4096, false, false,15.0f,  75.0f,18.75f, 1.5f));
	Models.Add(Make(TEXT("claude-3-haiku-20240307"),    TEXT("Claude 3 Haiku"),          200000,  4096, false, false, 0.25f,  1.25f, 0.3f, 0.03f));

	return Models;
}

// ---------------------------------------------------------------------------
// OpenAI models
// ---------------------------------------------------------------------------
TArray<FAutonomixModelInfo> FAutonomixModelRegistry::GetOpenAIModels()
{
	TArray<FAutonomixModelInfo> Models;
	auto Make = [](const FString& Id, const FString& Name, int32 Ctx, int32 MaxOut,
		bool bReasoningEffort, float InPrice, float OutPrice, float CacheR = 0.f) -> FAutonomixModelInfo
	{
		FAutonomixModelInfo M;
		M.Provider = EAutonomixProvider::OpenAI;
		M.ModelId = Id;
		M.DisplayName = Name;
		M.ContextWindow = Ctx;
		M.MaxOutputTokens = MaxOut;
		M.bSupportsReasoningEffort = bReasoningEffort;
		M.bSupportsImages = true;
		M.bSupportsPromptCache = true;
		M.InputPricePerMillion = InPrice;
		M.OutputPricePerMillion = OutPrice;
		M.CacheReadsPricePerMillion = CacheR;
		return M;
	};

	// GPT-5.x series
	Models.Add(Make(TEXT("gpt-5.2"),            TEXT("GPT-5.2"),              400000, 128000, true,  1.75f, 14.0f, 0.175f));
	Models.Add(Make(TEXT("gpt-5.1-codex-max"),  TEXT("GPT-5.1 Codex Max"),   400000, 128000, true,  1.25f, 10.0f, 0.125f));
	Models.Add(Make(TEXT("gpt-5.2-codex"),      TEXT("GPT-5.2 Codex"),       400000, 128000, true,  1.75f, 14.0f, 0.175f));
	// GPT-4.x series
	Models.Add(Make(TEXT("gpt-4.1"),            TEXT("GPT-4.1"),              128000,  32768, false, 2.0f,   8.0f, 0.5f));
	Models.Add(Make(TEXT("gpt-4.1-mini"),       TEXT("GPT-4.1 Mini"),        128000,  16384, false, 0.4f,   1.6f, 0.1f));
	Models.Add(Make(TEXT("gpt-4o"),             TEXT("GPT-4o"),              128000,  16384, false, 2.5f,  10.0f, 1.25f));
	Models.Add(Make(TEXT("gpt-4o-mini"),        TEXT("GPT-4o Mini"),         128000,  16384, false, 0.15f,  0.6f, 0.075f));
	// o-series (reasoning models)
	Models.Add(Make(TEXT("o3"),                 TEXT("o3"),                  200000,100000, true,  10.0f, 40.0f, 2.5f));
	Models.Add(Make(TEXT("o4-mini"),            TEXT("o4-mini"),             200000, 65536, true,   1.1f,  4.4f, 0.275f));
	Models.Add(Make(TEXT("o3-mini"),            TEXT("o3-mini"),             200000, 65536, true,   1.1f,  4.4f, 0.275f));
	Models.Add(Make(TEXT("o1"),                 TEXT("o1"),                  200000, 32768, true,  15.0f, 60.0f, 3.75f));
	Models.Add(Make(TEXT("o1-mini"),            TEXT("o1-mini"),             128000, 65536, true,   3.0f, 12.0f, 0.75f));

	return Models;
}

// ---------------------------------------------------------------------------
// Google Gemini models
// ---------------------------------------------------------------------------
TArray<FAutonomixModelInfo> FAutonomixModelRegistry::GetGeminiModels()
{
	TArray<FAutonomixModelInfo> Models;
	auto Make = [](const FString& Id, const FString& Name, int32 Ctx, int32 MaxOut,
		bool bBudget, bool bEffort, int32 MaxThink, float InPrice, float OutPrice, float CacheR = 0.f) -> FAutonomixModelInfo
	{
		FAutonomixModelInfo M;
		M.Provider = EAutonomixProvider::Google;
		M.ModelId = Id;
		M.DisplayName = Name;
		M.ContextWindow = Ctx;
		M.MaxOutputTokens = MaxOut;
		M.bSupportsReasoningBudget = bBudget;
		M.bSupportsReasoningEffort = bEffort;
		M.MaxThinkingTokens = MaxThink;
		M.bSupportsImages = true;
		M.bSupportsPromptCache = true;
		M.InputPricePerMillion = InPrice;
		M.OutputPricePerMillion = OutPrice;
		M.CacheReadsPricePerMillion = CacheR;
		return M;
	};

	// Gemini 3.x — reasoning effort models
	Models.Add(Make(TEXT("gemini-3.1-pro-preview"),  TEXT("Gemini 3.1 Pro"),   1048576, 65536, false, true,  0,     4.0f, 18.0f, 0.4f));
	Models.Add(Make(TEXT("gemini-3-pro-preview"),    TEXT("Gemini 3 Pro"),     1048576, 65536, false, true,  0,     4.0f, 18.0f, 0.4f));
	Models.Add(Make(TEXT("gemini-3-flash-preview"),  TEXT("Gemini 3 Flash"),   1048576, 65536, false, true,  0,     0.5f,  3.0f, 0.05f));
	// Gemini 2.5 Pro — thinking budget models
	Models.Add(Make(TEXT("gemini-2.5-pro"),              TEXT("Gemini 2.5 Pro"),       1048576, 64000, true, false, 32768, 2.5f,  15.0f, 0.625f));
	Models.Add(Make(TEXT("gemini-2.5-pro-preview-06-05"),TEXT("Gemini 2.5 Pro (0605)"),1048576, 65535, true, false, 32768, 2.5f,  15.0f, 0.625f));
	// Gemini 2.5 Flash — thinking budget models
	Models.Add(Make(TEXT("gemini-2.5-flash"),            TEXT("Gemini 2.5 Flash"),     1048576, 65536, true, false, 24576, 0.3f,   2.5f, 0.075f));
	Models.Add(Make(TEXT("gemini-flash-latest"),         TEXT("Gemini Flash Latest"),  1048576, 65536, true, false, 24576, 0.3f,   2.5f, 0.075f));
	// Gemini 2.5 Flash Lite
	Models.Add(Make(TEXT("gemini-flash-lite-latest"),    TEXT("Gemini Flash Lite"),    1048576, 65536, true, false, 24576, 0.1f,   0.4f, 0.025f));

	return Models;
}

// ---------------------------------------------------------------------------
// DeepSeek models
// ---------------------------------------------------------------------------
TArray<FAutonomixModelInfo> FAutonomixModelRegistry::GetDeepSeekModels()
{
	TArray<FAutonomixModelInfo> Models;

	{
		FAutonomixModelInfo M;
		M.Provider = EAutonomixProvider::DeepSeek;
		M.ModelId = TEXT("deepseek-chat");
		M.DisplayName = TEXT("DeepSeek Chat (V3)");
		M.ContextWindow = 64000;
		M.MaxOutputTokens = 8192;
		M.bSupportsReasoningBudget = false;
		M.bSupportsImages = false;
		M.InputPricePerMillion = 0.27f;
		M.OutputPricePerMillion = 1.1f;
		Models.Add(M);
	}
	{
		FAutonomixModelInfo M;
		M.Provider = EAutonomixProvider::DeepSeek;
		M.ModelId = TEXT("deepseek-reasoner");
		M.DisplayName = TEXT("DeepSeek Reasoner (R1)");
		M.ContextWindow = 64000;
		M.MaxOutputTokens = 8192;
		M.bSupportsReasoningBudget = true;
		M.MaxThinkingTokens = 32000;
		M.bSupportsImages = false;
		M.InputPricePerMillion = 0.55f;
		M.OutputPricePerMillion = 2.19f;
		Models.Add(M);
	}

	return Models;
}

// ---------------------------------------------------------------------------
// Mistral models
// ---------------------------------------------------------------------------
TArray<FAutonomixModelInfo> FAutonomixModelRegistry::GetMistralModels()
{
	TArray<FAutonomixModelInfo> Models;
	auto Make = [](const FString& Id, const FString& Name, int32 Ctx, float InP, float OutP) -> FAutonomixModelInfo
	{
		FAutonomixModelInfo M;
		M.Provider = EAutonomixProvider::Mistral;
		M.ModelId = Id;
		M.DisplayName = Name;
		M.ContextWindow = Ctx;
		M.MaxOutputTokens = 8192;
		M.InputPricePerMillion = InP;
		M.OutputPricePerMillion = OutP;
		return M;
	};
	Models.Add(Make(TEXT("mistral-large-latest"),  TEXT("Mistral Large"),  128000,  2.0f,  6.0f));
	Models.Add(Make(TEXT("mistral-medium-latest"), TEXT("Mistral Medium"), 128000,  0.4f,  2.0f));
	Models.Add(Make(TEXT("mistral-small-latest"),  TEXT("Mistral Small"),  128000,  0.1f,  0.3f));
	Models.Add(Make(TEXT("codestral-latest"),       TEXT("Codestral"),     256000,  0.3f,  0.9f));
	Models.Add(Make(TEXT("ministral-8b-latest"),   TEXT("Ministral 8B"),  128000,  0.1f,  0.1f));
	Models.Add(Make(TEXT("ministral-3b-latest"),   TEXT("Ministral 3B"),  128000, 0.04f, 0.04f));

	return Models;
}

// ---------------------------------------------------------------------------
// xAI (Grok) models
// ---------------------------------------------------------------------------
TArray<FAutonomixModelInfo> FAutonomixModelRegistry::GetxAIModels()
{
	TArray<FAutonomixModelInfo> Models;
	auto Make = [](const FString& Id, const FString& Name, int32 Ctx, bool bThink, float InP, float OutP) -> FAutonomixModelInfo
	{
		FAutonomixModelInfo M;
		M.Provider = EAutonomixProvider::xAI;
		M.ModelId = Id;
		M.DisplayName = Name;
		M.ContextWindow = Ctx;
		M.MaxOutputTokens = 16384;
		M.bSupportsReasoningBudget = bThink;
		M.MaxThinkingTokens = bThink ? 16384 : 0;
		M.bSupportsImages = true;
		M.InputPricePerMillion = InP;
		M.OutputPricePerMillion = OutP;
		return M;
	};
	Models.Add(Make(TEXT("grok-3"),           TEXT("Grok 3"),              131072, false, 3.0f,  15.0f));
	Models.Add(Make(TEXT("grok-3-mini"),      TEXT("Grok 3 Mini"),         131072, true,  0.3f,   0.5f));
	Models.Add(Make(TEXT("grok-2"),           TEXT("Grok 2"),              131072, false, 2.0f,  10.0f));
	Models.Add(Make(TEXT("grok-2-mini"),      TEXT("Grok 2 Mini"),         131072, false, 0.2f,   0.5f));

	return Models;
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

TArray<FAutonomixModelInfo> FAutonomixModelRegistry::GetKnownModels(EAutonomixProvider Provider)
{
	switch (Provider)
	{
	case EAutonomixProvider::Anthropic:  return GetAnthropicModels();
	case EAutonomixProvider::OpenAI:     return GetOpenAIModels();
	case EAutonomixProvider::Google:     return GetGeminiModels();
	case EAutonomixProvider::DeepSeek:   return GetDeepSeekModels();
	case EAutonomixProvider::Mistral:    return GetMistralModels();
	case EAutonomixProvider::xAI:        return GetxAIModels();
	default:
		return TArray<FAutonomixModelInfo>();
	}
}

TArray<FString> FAutonomixModelRegistry::GetKnownModelIds(EAutonomixProvider Provider)
{
	TArray<FString> Ids;
	for (const FAutonomixModelInfo& M : GetKnownModels(Provider))
	{
		Ids.Add(M.ModelId);
	}
	return Ids;
}

FAutonomixModelInfo FAutonomixModelRegistry::GetModelInfo(EAutonomixProvider Provider, const FString& ModelId)
{
	for (const FAutonomixModelInfo& M : GetKnownModels(Provider))
	{
		if (M.ModelId.Equals(ModelId, ESearchCase::IgnoreCase))
		{
			return M;
		}
	}

	// Unknown model — return safe defaults based on provider
	FAutonomixModelInfo Default;
	Default.Provider = Provider;
	Default.ModelId = ModelId;
	Default.DisplayName = ModelId;

	switch (Provider)
	{
	case EAutonomixProvider::Anthropic:
		Default.ContextWindow = 200000;
		Default.MaxOutputTokens = 8192;
		Default.bSupportsPromptCache = true;
		Default.bSupportsImages = true;
		break;
	case EAutonomixProvider::OpenAI:
		Default.ContextWindow = 128000;
		Default.MaxOutputTokens = 16384;
		break;
	case EAutonomixProvider::Google:
		Default.ContextWindow = 1048576;
		Default.MaxOutputTokens = 65536;
		break;
	case EAutonomixProvider::DeepSeek:
		Default.ContextWindow = 64000;
		Default.MaxOutputTokens = 8192;
		break;
	case EAutonomixProvider::Ollama:
	case EAutonomixProvider::LMStudio:
	case EAutonomixProvider::Custom:
		Default.ContextWindow = 8192;
		Default.MaxOutputTokens = 4096;
		break;
	default:
		Default.ContextWindow = 128000;
		Default.MaxOutputTokens = 8192;
		break;
	}

	UE_LOG(LogAutonomix, Verbose, TEXT("ModelRegistry: Unknown model '%s' for provider %d — using defaults."),
		*ModelId, (int32)Provider);

	return Default;
}

bool FAutonomixModelRegistry::ModelSupportsThinking(EAutonomixProvider Provider, const FString& ModelId)
{
	FAutonomixModelInfo Info = GetModelInfo(Provider, ModelId);
	return Info.bSupportsReasoningBudget || Info.bSupportsReasoningEffort;
}

bool FAutonomixModelRegistry::ModelSupports1MContext(EAutonomixProvider Provider, const FString& ModelId)
{
	FAutonomixModelInfo Info = GetModelInfo(Provider, ModelId);
	return Info.bSupports1MContext;
}
