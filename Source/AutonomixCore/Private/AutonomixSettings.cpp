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

	// --- OpenAI defaults (from Roo Code openAiNativeDefaultModelId) ---
	OpenAiModelId = TEXT("gpt-5.1-codex-max");
	OpenAiBaseUrl = TEXT("");  // empty = official https://api.openai.com/v1
	OpenAiReasoningEffort = EAutonomixReasoningEffort::Medium;

	// --- Azure OpenAI defaults (ported from Roo Code azureOpenAiDefaultApiVersion) ---
	// Azure auth/URL model is fundamentally different from official OpenAI:
	// - Header: 'api-key: {key}' NOT 'Authorization: Bearer {key}'
	// - URL: https://{resource}.openai.azure.com/openai/deployments/{deployment}/chat/completions
	// - Query: ?api-version=2024-02-01
	// - Uses Chat Completions only (no Responses API)
	AzureApiKey = TEXT("");
	AzureDeploymentName = TEXT("");  // User must set this to their deployment name
	AzureBaseUrl = TEXT("");         // User must set: https://{resource}.openai.azure.com
	AzureApiVersion = TEXT("2024-02-01");  // Stable GA version (matches Roo Code azureOpenAiDefaultApiVersion)

	// --- Google Gemini defaults (from Roo Code geminiDefaultModelId) ---
	GeminiModelId = TEXT("gemini-3.1-pro-preview");
	GeminiBaseUrl = TEXT("");  // empty = official generativelanguage.googleapis.com
	GeminiThinkingBudgetTokens = 0;  // 0 = disabled by default. User must opt-in for thinking models.
	GeminiReasoningEffort = EAutonomixReasoningEffort::Disabled;  // Disabled by default. Prevents sending unknown fields to non-thinking models.

	// --- DeepSeek defaults (from Roo Code deepSeekDefaultModelId) ---
	DeepSeekModelId = TEXT("deepseek-chat");
	DeepSeekBaseUrl = TEXT("https://api.deepseek.com/v1");

	// --- Mistral defaults (from Roo Code mistralDefaultModelId) ---
	MistralModelId = TEXT("codestral-latest");

	// --- xAI defaults (from Roo Code xaiDefaultModelId) ---
	xAIModelId = TEXT("grok-code-fast-1");

	// --- OpenRouter defaults (from Roo Code openRouterDefaultModelId) ---
	OpenRouterModelId = TEXT("anthropic/claude-sonnet-4.5");

	// --- Ollama defaults (from Roo Code ollamaDefaultModelId) ---
	OllamaBaseUrl = TEXT("http://localhost:11434");
	OllamaModelId = TEXT("devstral:24b");
	OllamaContextSize = 8192;

	// --- LM Studio defaults (from Roo Code lMStudioDefaultModelId) ---
	LMStudioBaseUrl = TEXT("http://localhost:1234");
	LMStudioModelId = TEXT("mistralai/devstral-small-2505");

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

	// New tool defaults (v1.1)
	bEnablePythonTools = false;        // Opt-in: requires Developer mode, powerful but risky
	bEnableViewportCapture = true;     // Safe: read-only viewport capture
	bEnableDataTableTools = true;      // Safe: asset creation
	bEnableBehaviorTreeTools = true;   // Safe: asset creation + level modification
	bEnableSequencerTools = true;      // Safe: asset creation + level modification
	bEnablePIETools = false;           // Opt-in: requires Developer mode, can crash editor
	bEnableGASTools = true;            // Safe: asset creation + C++ generation
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
		// Official OpenAI API only — for Azure, use the Azure provider.
		// If user accidentally put an Azure URL here, OpenAICompatClient will
		// auto-detect it via _isAzureUrl() and apply Azure wire format anyway.
		return OpenAiBaseUrl.IsEmpty() ? TEXT("https://api.openai.com/v1") : OpenAiBaseUrl;
	case EAutonomixProvider::Azure:
		// Azure base URL: https://{resource}.openai.azure.com
		// The OpenAICompatClient will append the deployment path + api-version.
		// If the user left BaseUrl empty, return empty so the client can validate
		// and give a clear error instead of sending a request to nowhere.
		return AzureBaseUrl;
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
	{
		// Roo Code uses Ollama's native SDK (/api/chat), but we use the OpenAI-compatible
		// endpoint (/v1/chat/completions). The OpenAICompatClient appends /chat/completions,
		// so the base URL MUST end with /v1.
		//
		// Users commonly set "http://localhost:11434" without /v1 → 404.
		// Always normalize: strip trailing slash, then ensure /v1 suffix.
		FString Base = OllamaBaseUrl.IsEmpty() ? TEXT("http://localhost:11434") : OllamaBaseUrl;
		while (Base.EndsWith(TEXT("/"))) Base.RemoveAt(Base.Len() - 1);
		if (!Base.EndsWith(TEXT("/v1")))
		{
			Base += TEXT("/v1");
		}
		return Base;
	}
	case EAutonomixProvider::LMStudio:
	{
		// Roo Code lm-studio.ts: baseURL = (baseUrl || "http://localhost:1234") + "/v1"
		// Same pattern: always normalize to include /v1.
		FString Base = LMStudioBaseUrl.IsEmpty() ? TEXT("http://localhost:1234") : LMStudioBaseUrl;
		while (Base.EndsWith(TEXT("/"))) Base.RemoveAt(Base.Len() - 1);
		if (!Base.EndsWith(TEXT("/v1")))
		{
			Base += TEXT("/v1");
		}
		return Base;
	}
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
	case EAutonomixProvider::Azure:      return AzureApiKey;   // 'api-key' header, not 'Authorization: Bearer'
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
		return OpenAiModelId.IsEmpty() ? TEXT("gpt-5.1-codex-max") : OpenAiModelId;
	case EAutonomixProvider::Azure:
		// For Azure, the "model" is the deployment name, NOT the base model ID.
		// Returning the deployment name is what the API expects in the URL and body.
		return AzureDeploymentName;
	case EAutonomixProvider::Google:
		return GeminiModelId.IsEmpty() ? TEXT("gemini-3.1-pro-preview") : GeminiModelId;
	case EAutonomixProvider::DeepSeek:
		return DeepSeekModelId.IsEmpty() ? TEXT("deepseek-chat") : DeepSeekModelId;
	case EAutonomixProvider::Mistral:
		return MistralModelId.IsEmpty() ? TEXT("codestral-latest") : MistralModelId;
	case EAutonomixProvider::xAI:
		return xAIModelId.IsEmpty() ? TEXT("grok-code-fast-1") : xAIModelId;
	case EAutonomixProvider::OpenRouter:
		return OpenRouterModelId.IsEmpty() ? TEXT("anthropic/claude-sonnet-4.5") : OpenRouterModelId;
	case EAutonomixProvider::Ollama:
		return OllamaModelId.IsEmpty() ? TEXT("devstral:24b") : OllamaModelId;
	case EAutonomixProvider::LMStudio:
		return LMStudioModelId.IsEmpty() ? TEXT("mistralai/devstral-small-2505") : LMStudioModelId;
	case EAutonomixProvider::Custom:
		return CustomEndpointModelId;
	case EAutonomixProvider::GitHubCopilot:
		return CopilotModelId.IsEmpty() ? TEXT("gpt-4") : CopilotModelId;
	default:
		return ModelEnumToApiString(EAutonomixClaudeModel::Sonnet_4_6);
	}
}

bool UAutonomixDeveloperSettings::IsActiveProviderApiKeySet() const
{
	FString Key = GetActiveApiKey();
	// Local providers and Copilot don't need an API key configured here
	if (ActiveProvider == EAutonomixProvider::Ollama || 
		ActiveProvider == EAutonomixProvider::LMStudio || 
		ActiveProvider == EAutonomixProvider::GitHubCopilot)
	{
		return true;
	}
	// Azure: also requires AzureBaseUrl and AzureDeploymentName to be usable
	if (ActiveProvider == EAutonomixProvider::Azure)
		return !Key.IsEmpty() && Key.Len() > 10 && !AzureBaseUrl.IsEmpty() && !AzureDeploymentName.IsEmpty();
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
	case EAutonomixProvider::Azure:
		// Show deployment name with Azure label so users know they're in Azure mode
		return ModelId.IsEmpty()
			? TEXT("Azure OpenAI (no deployment set)")
			: FString::Printf(TEXT("Azure: %s"), *ModelId);
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

// ============================================================================
// Model dropdown options (UFUNCTION used by UPROPERTY GetOptions meta)
// Model lists sourced from Roo Code packages/types/src/providers/*.ts
// ============================================================================

// Complete model lists sourced from Roo Code packages/types/src/providers/*.ts
// Every model ID matches exactly what Roo Code defines in its type files.

TArray<FString> UAutonomixDeveloperSettings::GetOpenAIModelOptions() const
{
	// Source: Roo-Code-main/packages/types/src/providers/openai.ts (openAiNativeModels)
	return {
		// GPT-5.x flagship series
		TEXT("gpt-5.4"),
		TEXT("gpt-5.3-codex"),
		TEXT("gpt-5.3-chat-latest"),
		TEXT("gpt-5.2"),
		TEXT("gpt-5.2-codex"),
		TEXT("gpt-5.2-chat-latest"),
		TEXT("gpt-5.1-codex-max"),
		TEXT("gpt-5.1"),
		TEXT("gpt-5.1-codex"),
		TEXT("gpt-5.1-codex-mini"),
		TEXT("gpt-5"),
		TEXT("gpt-5-mini"),
		TEXT("gpt-5-codex"),
		TEXT("gpt-5-nano"),
		TEXT("gpt-5-chat-latest"),
		// GPT-4.x series
		TEXT("gpt-4.1"),
		TEXT("gpt-4.1-mini"),
		TEXT("gpt-4.1-nano"),
		TEXT("gpt-4o"),
		TEXT("gpt-4o-mini"),
		// o-series (reasoning models)
		TEXT("o3"),
		TEXT("o3-high"),
		TEXT("o3-low"),
		TEXT("o4-mini"),
		TEXT("o4-mini-high"),
		TEXT("o4-mini-low"),
		TEXT("o3-mini"),
		TEXT("o3-mini-high"),
		TEXT("o3-mini-low"),
		TEXT("o1"),
		TEXT("o1-preview"),
		TEXT("o1-mini"),
		// Codex agent
		TEXT("codex-mini-latest"),
		// Dated snapshots (backward compatibility)
		TEXT("gpt-5-2025-08-07"),
		TEXT("gpt-5-mini-2025-08-07"),
		TEXT("gpt-5-nano-2025-08-07"),
	};
}

TArray<FString> UAutonomixDeveloperSettings::GetGeminiModelOptions() const
{
	// Source: Roo-Code-main/packages/types/src/providers/gemini.ts (geminiModels)
	return {
		// Gemini 3.x (reasoning effort models)
		TEXT("gemini-3.1-pro-preview"),
		TEXT("gemini-3.1-pro-preview-customtools"),
		TEXT("gemini-3-pro-preview"),
		TEXT("gemini-3-flash-preview"),
		// Gemini 2.5 Pro (thinking budget models)
		TEXT("gemini-2.5-pro"),
		TEXT("gemini-2.5-pro-preview-06-05"),
		TEXT("gemini-2.5-pro-preview-05-06"),
		TEXT("gemini-2.5-pro-preview-03-25"),
		// Gemini 2.5 Flash
		TEXT("gemini-flash-latest"),
		TEXT("gemini-2.5-flash-preview-09-2025"),
		TEXT("gemini-2.5-flash"),
		// Gemini 2.5 Flash Lite
		TEXT("gemini-flash-lite-latest"),
		TEXT("gemini-2.5-flash-lite-preview-09-2025"),
	};
}

TArray<FString> UAutonomixDeveloperSettings::GetDeepSeekModelOptions() const
{
	// Source: Roo-Code-main/packages/types/src/providers/deepseek.ts (deepSeekModels)
	return {
		TEXT("deepseek-chat"),
		TEXT("deepseek-reasoner"),
	};
}

TArray<FString> UAutonomixDeveloperSettings::GetMistralModelOptions() const
{
	// Source: Roo-Code-main/packages/types/src/providers/mistral.ts (mistralModels)
	return {
		TEXT("magistral-medium-latest"),
		TEXT("devstral-medium-latest"),
		TEXT("mistral-medium-latest"),
		TEXT("codestral-latest"),
		TEXT("mistral-large-latest"),
		TEXT("ministral-8b-latest"),
		TEXT("ministral-3b-latest"),
		TEXT("mistral-small-latest"),
		TEXT("pixtral-large-latest"),
	};
}

TArray<FString> UAutonomixDeveloperSettings::GetxAIModelOptions() const
{
	// Source: Roo-Code-main/packages/types/src/providers/xai.ts (xaiModels)
	return {
		TEXT("grok-code-fast-1"),
		TEXT("grok-4-1-fast-reasoning"),
		TEXT("grok-4-1-fast-non-reasoning"),
		TEXT("grok-4-fast-reasoning"),
		TEXT("grok-4-fast-non-reasoning"),
		TEXT("grok-4-0709"),
		TEXT("grok-3-mini"),
		TEXT("grok-3"),
	};
}

TArray<FString> UAutonomixDeveloperSettings::GetCopilotModelOptions() const
{
	// Return dynamically cached models if available
	if (CopilotAvailableModels.Num() > 0)
	{
		return CopilotAvailableModels;
	}

	// Fallback to these mapped standard proxy IDs via GitHub's API if not yet cached.
	return {
		TEXT("gpt-4o"),
		TEXT("gpt-4"),
		TEXT("claude-3.5-sonnet"),
		TEXT("claude-3.7-sonnet"),
		TEXT("gemini-2.1-pro"),
		TEXT("o1"),
		TEXT("o1-preview"),
		TEXT("o1-mini"),
		TEXT("o3-mini")
	};
}
