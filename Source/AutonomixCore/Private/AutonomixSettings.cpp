// Copyright Autonomix. All Rights Reserved.

#include "AutonomixSettings.h"
#include "AutonomixCoreModule.h"
#include "Misc/MessageDialog.h"

static const FString DefaultApiEndpoint = TEXT("https://api.anthropic.com/v1/messages");

UAutonomixDeveloperSettings::UAutonomixDeveloperSettings()
{
	// Provider selection — defaults to Anthropic (backward compat)
	ActiveProvider = EAutonomixProvider::Anthropic;

	// --- Anthropic defaults ---
	ApiEndpoint = DefaultApiEndpoint;
	ClaudeModel = EAutonomixClaudeModel::Sonnet_4_6;
	ContextWindow = EAutonomixContextWindow::Standard_200K;
	MaxResponseTokens = 8192;
	RequestTimeoutSeconds = 120;
	bEnableExtendedThinking = false;
	ThinkingBudgetTokens = 3000;

	// --- OpenAI defaults ---
	OpenAiModelId = TEXT("gpt-4o");
	OpenAiBaseUrl = TEXT("");  // empty = official https://api.openai.com/v1
	OpenAiReasoningEffort = EAutonomixReasoningEffort::Medium;

	// --- Google Gemini defaults ---
	GeminiModelId = TEXT("gemini-2.5-pro");
	GeminiBaseUrl = TEXT("");  // empty = official generativelanguage.googleapis.com
	GeminiThinkingBudgetTokens = 8192;
	GeminiReasoningEffort = EAutonomixReasoningEffort::Low;

	// --- DeepSeek defaults ---
	DeepSeekModelId = TEXT("deepseek-chat");
	DeepSeekBaseUrl = TEXT("https://api.deepseek.com/v1");

	// --- Mistral defaults ---
	MistralModelId = TEXT("mistral-large-latest");

	// --- xAI defaults ---
	xAIModelId = TEXT("grok-3");

	// --- OpenRouter defaults ---
	OpenRouterModelId = TEXT("anthropic/claude-sonnet-4-6");

	// --- Ollama defaults ---
	OllamaBaseUrl = TEXT("http://localhost:11434");
	OllamaModelId = TEXT("llama3.1");
	OllamaContextSize = 8192;

	// --- LM Studio defaults ---
	LMStudioBaseUrl = TEXT("http://localhost:1234");
	LMStudioModelId = TEXT("");

	// --- Custom endpoint defaults ---
	CustomBaseUrl = TEXT("");
	CustomApiKey = TEXT("");
	CustomEndpointModelId = TEXT("");

	// Safety defaults -- Marketplace-safe: Sandbox by default, opt-in for power features
	SecurityMode = EAutonomixSecurityMode::Sandbox;
	MaxAutoRetries = 3;
	bAutoApproveLowRisk = true;
	bAutoApproveAllTools = false;  // Must be explicitly enabled by user
	bAutoApproveReadOnlyTools = true; // Safe: read-only tools never mutate project
	MaxConsecutiveAutoApprovedRequests = 25; // Prevent runaway loops
	MaxAutoApprovedCostDollars = 5.0f; // $5 default cost cap
	bRequireTypedConfirmation = true;
	bEnableAutoBackup = true;
	MaxBackupCount = 50;
	bAutoCheckout = true;
	bAllowExternalProcessExecution = false; // Must be explicitly enabled by user

	// Privacy
	bHasAcceptedPrivacyDisclosure = false;

	// Cost defaults
	DailyTokenLimit = 0; // Unlimited
	bShowCostEstimates = true;
	bShowPerRequestCost = true; // Show per-request cost in header

	// Context defaults
	ContextTokenBudget = 30000;
	bIncludeSourceTree = true;
	bIncludeAssetSummary = true;
	bIncludeSettingsSnapshot = false;
	bIncludeClassHierarchy = true;

	// Context Management defaults (auto-condense)
	bAutoCondenseContext = true;
	AutoCondenseThresholdPercent = 80; // Trigger at 80% context usage

	// UI defaults
	ChatFontSize = 12;
	bShowTimestamps = true;
	bEnableStreamingDisplay = true;

	// Tool defaults -- all enabled
	bEnableBlueprintTools = true;
	bEnableCppTools = true;
	bEnableMaterialTools = true;
	bEnableImportTools = true;
	bEnableLevelTools = true;
	bEnableSettingsTools = true;
	bEnableBuildTools = true;
	bEnablePerformanceTools = true;
}

const UAutonomixDeveloperSettings* UAutonomixDeveloperSettings::Get()
{
	return GetDefault<UAutonomixDeveloperSettings>();
}

bool UAutonomixDeveloperSettings::IsApiKeySet() const
{
	return !ApiKey.IsEmpty() && ApiKey.Len() > 10;
}

FString UAutonomixDeveloperSettings::GetEffectiveEndpoint() const
{
	switch (ActiveProvider)
	{
	case EAutonomixProvider::Anthropic:
		return ApiEndpoint.IsEmpty() ? DefaultApiEndpoint : ApiEndpoint;
	case EAutonomixProvider::OpenAI:
		return OpenAiBaseUrl.IsEmpty() ? TEXT("https://api.openai.com/v1") : OpenAiBaseUrl;
	case EAutonomixProvider::Google:
		return GeminiBaseUrl.IsEmpty() ? TEXT("https://generativelanguage.googleapis.com") : GeminiBaseUrl;
	case EAutonomixProvider::DeepSeek:
		return DeepSeekBaseUrl.IsEmpty() ? TEXT("https://api.deepseek.com/v1") : DeepSeekBaseUrl;
	case EAutonomixProvider::Mistral:
		return TEXT("https://api.mistral.ai/v1");
	case EAutonomixProvider::xAI:
		return TEXT("https://api.x.ai/v1");
	case EAutonomixProvider::OpenRouter:
		return TEXT("https://openrouter.ai/api/v1");
	case EAutonomixProvider::Ollama:
		return OllamaBaseUrl.IsEmpty() ? TEXT("http://localhost:11434") : OllamaBaseUrl;
	case EAutonomixProvider::LMStudio:
		return LMStudioBaseUrl.IsEmpty() ? TEXT("http://localhost:1234") : LMStudioBaseUrl;
	case EAutonomixProvider::Custom:
		return CustomBaseUrl;
	default:
		return DefaultApiEndpoint;
	}
}

FString UAutonomixDeveloperSettings::GetActiveApiKey() const
{
	switch (ActiveProvider)
	{
	case EAutonomixProvider::Anthropic:  return ApiKey;
	case EAutonomixProvider::OpenAI:     return OpenAiApiKey;
	case EAutonomixProvider::Google:     return GeminiApiKey;
	case EAutonomixProvider::DeepSeek:   return DeepSeekApiKey;
	case EAutonomixProvider::Mistral:    return MistralApiKey;
	case EAutonomixProvider::xAI:        return xAIApiKey;
	case EAutonomixProvider::OpenRouter: return OpenRouterApiKey;
	case EAutonomixProvider::Ollama:     return TEXT("");   // no auth for local
	case EAutonomixProvider::LMStudio:   return TEXT("");   // no auth for local
	case EAutonomixProvider::Custom:     return CustomApiKey;
	default: return ApiKey;
	}
}

FString UAutonomixDeveloperSettings::GetEffectiveModel() const
{
	switch (ActiveProvider)
	{
	case EAutonomixProvider::Anthropic:
	{
		if (ClaudeModel == EAutonomixClaudeModel::Custom)
			return CustomModelId.IsEmpty() ? ModelEnumToApiString(EAutonomixClaudeModel::Sonnet_4_6) : CustomModelId;
		return ModelEnumToApiString(ClaudeModel);
	}
	case EAutonomixProvider::OpenAI:
		return OpenAiModelId.IsEmpty() ? TEXT("gpt-4o") : OpenAiModelId;
	case EAutonomixProvider::Google:
		return GeminiModelId.IsEmpty() ? TEXT("gemini-2.5-pro") : GeminiModelId;
	case EAutonomixProvider::DeepSeek:
		return DeepSeekModelId.IsEmpty() ? TEXT("deepseek-chat") : DeepSeekModelId;
	case EAutonomixProvider::Mistral:
		return MistralModelId.IsEmpty() ? TEXT("mistral-large-latest") : MistralModelId;
	case EAutonomixProvider::xAI:
		return xAIModelId.IsEmpty() ? TEXT("grok-3") : xAIModelId;
	case EAutonomixProvider::OpenRouter:
		return OpenRouterModelId.IsEmpty() ? TEXT("anthropic/claude-sonnet-4-6") : OpenRouterModelId;
	case EAutonomixProvider::Ollama:
		return OllamaModelId.IsEmpty() ? TEXT("llama3.1") : OllamaModelId;
	case EAutonomixProvider::LMStudio:
		return LMStudioModelId.IsEmpty() ? TEXT("local-model") : LMStudioModelId;
	case EAutonomixProvider::Custom:
		return CustomEndpointModelId;
	default:
		return ModelEnumToApiString(EAutonomixClaudeModel::Sonnet_4_6);
	}
}

bool UAutonomixDeveloperSettings::IsActiveProviderApiKeySet() const
{
	FString Key = GetActiveApiKey();
	// Local providers don't need an API key
	if (ActiveProvider == EAutonomixProvider::Ollama || ActiveProvider == EAutonomixProvider::LMStudio)
		return true;
	return !Key.IsEmpty() && Key.Len() > 10;
}

FString UAutonomixDeveloperSettings::GetModelDisplayName() const
{
	// Return a provider-qualified display name for all providers, not just Anthropic.
	const FString ModelId = GetEffectiveModel();
	switch (ActiveProvider)
	{
	case EAutonomixProvider::Anthropic:
	{
		switch (ClaudeModel)
		{
		case EAutonomixClaudeModel::Sonnet_4_6:	return TEXT("Claude Sonnet 4.6");
		case EAutonomixClaudeModel::Sonnet_4_5:	return TEXT("Claude Sonnet 4.5");
		case EAutonomixClaudeModel::Opus_4_6:	return TEXT("Claude Opus 4.6");
		case EAutonomixClaudeModel::Opus_4_5:	return TEXT("Claude Opus 4.5");
		case EAutonomixClaudeModel::Haiku_4:	return TEXT("Claude Haiku 4");
		case EAutonomixClaudeModel::Custom:		return FString::Printf(TEXT("Custom (%s)"), *CustomModelId);
		default: return ModelId;
		}
	}
	case EAutonomixProvider::OpenAI:
		return FString::Printf(TEXT("OpenAI: %s"), *ModelId);
	case EAutonomixProvider::Google:
		return FString::Printf(TEXT("Gemini: %s"), *ModelId);
	case EAutonomixProvider::DeepSeek:
		return FString::Printf(TEXT("DeepSeek: %s"), *ModelId);
	case EAutonomixProvider::Mistral:
		return FString::Printf(TEXT("Mistral: %s"), *ModelId);
	case EAutonomixProvider::xAI:
		return FString::Printf(TEXT("xAI: %s"), *ModelId);
	case EAutonomixProvider::OpenRouter:
		return FString::Printf(TEXT("OpenRouter: %s"), *ModelId);
	case EAutonomixProvider::Ollama:
		return FString::Printf(TEXT("Ollama (local): %s"), *ModelId);
	case EAutonomixProvider::LMStudio:
		return FString::Printf(TEXT("LM Studio (local): %s"), *ModelId);
	case EAutonomixProvider::Custom:
		return FString::Printf(TEXT("Custom: %s"), *ModelId);
	default:
		return ModelId;
	}
}

FString UAutonomixDeveloperSettings::ModelEnumToApiString(EAutonomixClaudeModel Model)
{
	switch (Model)
	{
	case EAutonomixClaudeModel::Sonnet_4_6:	return TEXT("claude-sonnet-4-6");
	case EAutonomixClaudeModel::Sonnet_4_5:	return TEXT("claude-sonnet-4-5-20250929");
	case EAutonomixClaudeModel::Opus_4_6:	return TEXT("claude-opus-4-6");
	case EAutonomixClaudeModel::Opus_4_5:	return TEXT("claude-opus-4-5");
	case EAutonomixClaudeModel::Haiku_4:	return TEXT("claude-haiku-4");
	default: return TEXT("claude-sonnet-4-6");
	}
}

bool UAutonomixDeveloperSettings::IsToolCategoryAllowed(EAutonomixActionCategory Category) const
{
	switch (SecurityMode)
	{
	case EAutonomixSecurityMode::Sandbox:
		switch (Category)
		{
		case EAutonomixActionCategory::Cpp:
		case EAutonomixActionCategory::Build:
		case EAutonomixActionCategory::Settings:
		case EAutonomixActionCategory::SourceControl:
		case EAutonomixActionCategory::FileSystem:
			return false;
		default:
			return true;
		}
	case EAutonomixSecurityMode::Advanced:
		switch (Category)
		{
		case EAutonomixActionCategory::Build:
			return false;
		default:
			return true;
		}
	case EAutonomixSecurityMode::Developer:
		return true;
	default:
		return false;
	}
}

bool UAutonomixDeveloperSettings::IsIniSectionAllowed(const FString& Section) const
{
	if (SecurityMode == EAutonomixSecurityMode::Advanced)
	{
		return Section.StartsWith(TEXT("/Script/Autonomix"));
	}
	return true;
}

FName UAutonomixDeveloperSettings::GetContainerName() const
{
	return TEXT("Project");
}

FName UAutonomixDeveloperSettings::GetCategoryName() const
{
	return TEXT("Plugins");
}

FName UAutonomixDeveloperSettings::GetSectionName() const
{
	return TEXT("Autonomix");
}

#if WITH_EDITOR
FText UAutonomixDeveloperSettings::GetSectionText() const
{
	return FText::FromString(TEXT("Autonomix AI Assistant"));
}

FText UAutonomixDeveloperSettings::GetSectionDescription() const
{
	return FText::FromString(TEXT("Configure the Autonomix AI assistant plugin -- API keys, model selection, safety settings, context, and tool availability."));
}

void UAutonomixDeveloperSettings::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	if (!PropertyChangedEvent.Property) return;

	FName PropName = PropertyChangedEvent.Property->GetFName();

	// ====================================================================
	// CRITICAL (Gemini): Extended Thinking token constraint validation
	// Anthropic API requires: budget_tokens >= 1024, max_tokens > budget_tokens
	// ====================================================================
	if (PropName == GET_MEMBER_NAME_CHECKED(UAutonomixDeveloperSettings, ThinkingBudgetTokens))
	{
		ThinkingBudgetTokens = FMath::Max(1024, ThinkingBudgetTokens);

		if (bEnableExtendedThinking && MaxResponseTokens <= ThinkingBudgetTokens)
		{
			MaxResponseTokens = ThinkingBudgetTokens + 1024;
			UE_LOG(LogAutonomix, Warning,
				TEXT("Autonomix: MaxResponseTokens auto-adjusted to %d (must be > ThinkingBudgetTokens %d)"),
				MaxResponseTokens, ThinkingBudgetTokens);
		}
	}
	else if (PropName == GET_MEMBER_NAME_CHECKED(UAutonomixDeveloperSettings, MaxResponseTokens))
	{
		if (bEnableExtendedThinking && MaxResponseTokens <= ThinkingBudgetTokens)
		{
			MaxResponseTokens = ThinkingBudgetTokens + 1024;
			UE_LOG(LogAutonomix, Warning,
				TEXT("Autonomix: MaxResponseTokens clamped to %d (must be > ThinkingBudgetTokens %d)"),
				MaxResponseTokens, ThinkingBudgetTokens);
		}
	}
	else if (PropName == GET_MEMBER_NAME_CHECKED(UAutonomixDeveloperSettings, bEnableExtendedThinking))
	{
		if (bEnableExtendedThinking)
		{
			ThinkingBudgetTokens = FMath::Max(1024, ThinkingBudgetTokens);
			if (MaxResponseTokens <= ThinkingBudgetTokens)
			{
				MaxResponseTokens = ThinkingBudgetTokens + 1024;
			}
		}
	}

	// ====================================================================
	// CRITICAL (ChatGPT): Developer mode escalation confirmation dialog
	// Switching to Developer mode requires explicit user confirmation.
	// ====================================================================
	if (PropName == GET_MEMBER_NAME_CHECKED(UAutonomixDeveloperSettings, SecurityMode))
	{
		if (SecurityMode == EAutonomixSecurityMode::Developer)
		{
			FText Title = FText::FromString(TEXT("Enable Developer Mode?"));
			FText Message = FText::FromString(
				TEXT("WARNING: Developer Mode allows:\n\n")
				TEXT("- C++ file writes and compilation\n")
				TEXT("- Full INI/config modification\n")
				TEXT("- External process execution (UAT builds)\n")
				TEXT("- Source control operations\n")
				TEXT("- Project-wide mutation\n\n")
				TEXT("This gives the AI full power over your project.\n")
				TEXT("Only enable if you understand the risks.\n\n")
				TEXT("Continue?")
			);

			EAppReturnType::Type Result = FMessageDialog::Open(EAppMsgType::YesNo, Message, Title);

			if (Result != EAppReturnType::Yes)
			{
				// User declined -- revert to Advanced
				SecurityMode = EAutonomixSecurityMode::Advanced;
				UE_LOG(LogAutonomix, Log, TEXT("Autonomix: Developer mode switch declined. Reverting to Advanced."));
			}
		}
	}
}
#endif
