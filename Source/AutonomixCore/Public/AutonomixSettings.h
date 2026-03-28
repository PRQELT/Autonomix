// Copyright Autonomix. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "AutonomixTypes.h"
#include "AutonomixSettings.generated.h"

/**
 * Autonomix plugin settings.
 *
 * API keys are stored locally per-user via config=EditorPerProjectUserSettings
 * which writes to Saved/Config/ (excluded from source control).
 *
 * Access at runtime via: GetDefault<UAutonomixDeveloperSettings>()
 */
UCLASS(config = EditorPerProjectUserSettings, meta = (DisplayName = "Autonomix"))
class AUTONOMIXCORE_API UAutonomixDeveloperSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UAutonomixDeveloperSettings();

	// ============================================================================
	// Provider Selection
	// ============================================================================

	/** Active AI provider -- determines which API endpoint, wire format, and model to use.
	 *  Autonomix supports Anthropic (Claude), OpenAI (GPT/o-series), Google (Gemini),
	 *  DeepSeek, Mistral, xAI (Grok), OpenRouter, local Ollama/LM Studio, and any
	 *  OpenAI-compatible endpoint. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "API|Provider",
		meta = (DisplayName = "Active Provider",
		ToolTip = "Select which AI provider to use. Configure the provider-specific API key and model below."))
	EAutonomixProvider ActiveProvider;

	// ============================================================================
	// Anthropic (Claude) — legacy / primary provider fields
	// ============================================================================

	/** Anthropic API key. Stored locally -- never committed to source control. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "API|Anthropic",
		meta = (DisplayName = "Anthropic API Key", PasswordField = true,
		ToolTip = "Your Claude API key from console.anthropic.com."))
	FString ApiKey;

	/** Organization ID (optional, for team billing) */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "API|Anthropic",
		meta = (DisplayName = "Organization ID"))
	FString OrganizationId;

	/** Anthropic API endpoint URL. Override for proxies or custom deployments. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "API|Anthropic",
		meta = (DisplayName = "Anthropic API Endpoint"))
	FString ApiEndpoint;

	/** Select the Claude model to use */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "API|Anthropic",
		meta = (DisplayName = "Claude Model"))
	EAutonomixClaudeModel ClaudeModel;

	/** Custom Claude model identifier -- only used when ClaudeModel is set to Custom */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "API|Anthropic",
		meta = (DisplayName = "Custom Claude Model ID",
		EditCondition = "ClaudeModel == EAutonomixClaudeModel::Custom",
		EditConditionHides))
	FString CustomModelId;

	/** Context window size -- 200K standard or 1M extended (beta flag, higher cost).
	 *  1M context supported by: claude-sonnet-4-6, claude-sonnet-4-5, claude-opus-4-6. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "API|Anthropic",
		meta = (DisplayName = "Claude Context Window"))
	EAutonomixContextWindow ContextWindow;

	/** Enable Claude extended thinking -- step-by-step reasoning before responding.
	 *  Improves quality for complex tasks. Increases latency and cost.
	 *  Requires a model with supportsReasoningBudget (Sonnet/Opus 4.x). */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "API|Anthropic",
		meta = (DisplayName = "Enable Extended Thinking"))
	bool bEnableExtendedThinking;

	/** Maximum tokens Claude can use for thinking (budget_tokens).
	 *  Must be >= 1024. max_tokens must be > budget_tokens. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "API|Anthropic",
		meta = (DisplayName = "Thinking Budget (tokens)", ClampMin = "1024", ClampMax = "128000",
		EditCondition = "bEnableExtendedThinking", EditConditionHides))
	int32 ThinkingBudgetTokens;

	// ============================================================================
	// OpenAI (GPT-4o, gpt-4.1, o3, o4-mini, gpt-5.x, etc.)
	// ============================================================================

	/** OpenAI API key. Required when ActiveProvider = OpenAI. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "API|OpenAI",
		meta = (DisplayName = "OpenAI API Key", PasswordField = true))
	FString OpenAiApiKey;

	/** OpenAI model ID — select from the dropdown or type a custom model ID */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "API|OpenAI",
		meta = (DisplayName = "OpenAI Model",
		GetOptions = "GetOpenAIModelOptions",
		ToolTip = "Select a model from the dropdown or type a custom model ID."))
	FString OpenAiModelId;

	/** OpenAI base URL. Leave empty for official OpenAI API (https://api.openai.com/v1).
	 *  Do NOT use this for Azure — use the 'Azure OpenAI' provider instead. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "API|OpenAI",
		meta = (DisplayName = "OpenAI Base URL (empty = official)",
		ToolTip = "Leave empty for official OpenAI. For Azure endpoints, switch to the 'Azure OpenAI' provider instead."))
	FString OpenAiBaseUrl;

	/** Reasoning effort for OpenAI reasoning models (o3, o4-mini, o1, GPT-5.x).
	 *  Auto-ignored for non-reasoning models (gpt-4o, gpt-4.1, gpt-4.1-mini/nano). */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "API|OpenAI",
		meta = (DisplayName = "Reasoning Effort",
		ToolTip = "For reasoning models (o3, o4-mini, o1, GPT-5.x). Auto-ignored for non-reasoning models (gpt-4o, gpt-4.1)."))
	EAutonomixReasoningEffort OpenAiReasoningEffort;

	// ============================================================================
	// Azure OpenAI Service
	//
	// Azure uses a different auth model from the official OpenAI API:
	//   - Auth header: 'api-key: {key}' (NOT 'Authorization: Bearer {key}')
	//   - URL format:  https://{resource}.openai.azure.com/openai/deployments/{deployment}
	//   - Query param: ?api-version=2024-02-01
	//   - Uses Chat Completions API (/chat/completions), NOT the Responses API (/responses)
	//   - Model ID = your Azure deployment name, not the base OpenAI model name
	//
	// Ported from Roo Code openai.ts — AzureOpenAI client detection via _isAzureOpenAI()
	// ============================================================================

	/** Azure OpenAI API key. Get this from Azure portal → your resource → Keys and Endpoint.
	 *  Note: Azure uses 'api-key' header, NOT 'Authorization: Bearer'. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "API|Azure OpenAI",
		meta = (DisplayName = "Azure API Key", PasswordField = true,
		ToolTip = "Your Azure OpenAI resource key from Azure portal.\nGet it from: Azure portal → OpenAI resource → Resource Management → Keys and Endpoint"))
	FString AzureApiKey;

	/** Azure OpenAI deployment name. This is your deployment name, NOT the base model name.
	 *  Example: if you deployed gpt-4o as 'my-gpt4-deployment', enter 'my-gpt4-deployment'. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "API|Azure OpenAI",
		meta = (DisplayName = "Azure Deployment Name",
		ToolTip = "Your Azure deployment name (NOT the base model name like 'gpt-4o').\nExample: 'my-gpt4-deployment', 'prod-gpt4o', etc.\nFind it in: Azure portal → OpenAI resource → Model deployments"))
	FString AzureDeploymentName;

	/** Azure OpenAI resource base URL.
	 *  Format: https://{your-resource-name}.openai.azure.com
	 *  Example: https://my-company-openai.openai.azure.com */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "API|Azure OpenAI",
		meta = (DisplayName = "Azure Resource Base URL",
		ToolTip = "Your Azure OpenAI resource endpoint.\nFormat: https://{resource-name}.openai.azure.com\nFind it in: Azure portal → OpenAI resource → Resource Management → Keys and Endpoint"))
	FString AzureBaseUrl;

	/** Azure OpenAI API version. Used as the ?api-version= query parameter.
	 *  Recommended: 2024-02-01 for GA models, 2024-05-01-preview for preview features. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "API|Azure OpenAI",
		meta = (DisplayName = "Azure API Version",
		ToolTip = "The api-version query parameter for Azure OpenAI.\nRecommended: 2024-02-01 (stable) or 2024-05-01-preview (for preview features).\nSee: https://learn.microsoft.com/azure/ai-services/openai/reference"))
	FString AzureApiVersion;

	// ============================================================================
	// Google Gemini (gemini-2.5-pro, gemini-2.5-flash, gemini-3.x, etc.)
	// ============================================================================

	/** Google AI Studio API key. Required when ActiveProvider = Google. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "API|Google Gemini",
		meta = (DisplayName = "Gemini API Key", PasswordField = true,
		ToolTip = "Get your key from https://aistudio.google.com/app/apikey"))
	FString GeminiApiKey;

	/** Gemini model — select from the dropdown or type a custom model ID */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "API|Google Gemini",
		meta = (DisplayName = "Gemini Model",
		GetOptions = "GetGeminiModelOptions",
		ToolTip = "Select a model from the dropdown or type a custom model ID."))
	FString GeminiModelId;

	/** Gemini base URL. Leave empty for official API (generativelanguage.googleapis.com).
	 *  Override for Vertex AI endpoints. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "API|Google Gemini",
		meta = (DisplayName = "Gemini Base URL (empty = official)"))
	FString GeminiBaseUrl;

	/** Reasoning/thinking budget for Gemini 2.5+ models that support it.
	 *  Set to 0 to disable thinking. Ignored by non-thinking models. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "API|Google Gemini",
		meta = (DisplayName = "Gemini Thinking Budget (tokens)", ClampMin = "0", ClampMax = "32768",
		ToolTip = "Gemini 2.5 Pro/Flash: thinking tokens (0=off). Gemini 3.x: set ReasoningEffort instead."))
	int32 GeminiThinkingBudgetTokens;

	/** Reasoning effort for Gemini 3.x models (gemini-3.1-pro, gemini-3-flash, etc.) */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "API|Google Gemini",
		meta = (DisplayName = "Gemini Reasoning Effort (3.x models only)"))
	EAutonomixReasoningEffort GeminiReasoningEffort;

	// ============================================================================
	// DeepSeek (deepseek-chat, deepseek-reasoner)
	// ============================================================================

	/** DeepSeek API key. Required when ActiveProvider = DeepSeek. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "API|DeepSeek",
		meta = (DisplayName = "DeepSeek API Key", PasswordField = true))
	FString DeepSeekApiKey;

	/** DeepSeek model — select from the dropdown or type a custom model ID */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "API|DeepSeek",
		meta = (DisplayName = "DeepSeek Model",
		GetOptions = "GetDeepSeekModelOptions",
		ToolTip = "Select a model from the dropdown or type a custom model ID."))
	FString DeepSeekModelId;

	/** DeepSeek base URL. Default: https://api.deepseek.com/v1 */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "API|DeepSeek",
		meta = (DisplayName = "DeepSeek Base URL"))
	FString DeepSeekBaseUrl;

	// ============================================================================
	// Mistral AI (mistral-large, codestral, etc.)
	// ============================================================================

	/** Mistral API key. Required when ActiveProvider = Mistral. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "API|Mistral",
		meta = (DisplayName = "Mistral API Key", PasswordField = true))
	FString MistralApiKey;

	/** Mistral model — select from the dropdown or type a custom model ID */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "API|Mistral",
		meta = (DisplayName = "Mistral Model",
		GetOptions = "GetMistralModelOptions",
		ToolTip = "Select a model from the dropdown or type a custom model ID."))
	FString MistralModelId;

	// ============================================================================
	// xAI (Grok-2, Grok-3, etc.)
	// ============================================================================

	/** xAI API key. Required when ActiveProvider = xAI. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "API|xAI",
		meta = (DisplayName = "xAI API Key", PasswordField = true))
	FString xAIApiKey;

	/** xAI model — select from the dropdown or type a custom model ID */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "API|xAI",
		meta = (DisplayName = "xAI Model",
		GetOptions = "GetxAIModelOptions",
		ToolTip = "Select a model from the dropdown or type a custom model ID."))
	FString xAIModelId;

	// ============================================================================
	// OpenRouter (aggregates hundreds of models)
	// ============================================================================

	/** OpenRouter API key. Required when ActiveProvider = OpenRouter. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "API|OpenRouter",
		meta = (DisplayName = "OpenRouter API Key", PasswordField = true,
		ToolTip = "Get your key from https://openrouter.ai/keys"))
	FString OpenRouterApiKey;

	/** OpenRouter model ID. Examples: anthropic/claude-sonnet-4-6, openai/gpt-4o, google/gemini-2.5-pro */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "API|OpenRouter",
		meta = (DisplayName = "OpenRouter Model ID",
		ToolTip = "Full model path as shown on openrouter.ai, e.g. anthropic/claude-sonnet-4-6"))
	FString OpenRouterModelId;

	// ============================================================================
	// Ollama (local model server — NO API KEY REQUIRED)
	// ============================================================================

	/** Ollama server base URL. Default: http://localhost:11434 */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "API|Ollama",
		meta = (DisplayName = "Ollama Base URL",
		ToolTip = "No API key needed — Ollama runs locally on your machine.\nDefault: http://localhost:11434\n\nSetup:\n1. Install Ollama from https://ollama.com\n2. Run: ollama pull <model> (e.g. ollama pull devstral:24b)\n3. Ollama starts automatically on localhost:11434\n4. Select 'Ollama (Local)' as your provider in Autonomix"))
	FString OllamaBaseUrl;

	/** Ollama model ID. Examples: llama3.1, qwen2.5-coder:7b, deepseek-r1:8b */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "API|Ollama",
		meta = (DisplayName = "Ollama Model ID",
		ToolTip = "The model name exactly as shown by 'ollama list'.\nMust be pulled first via: ollama pull <model>\n\nRecommended for Autonomix:\n  devstral:24b (best coding, 24GB VRAM)\n  qwen2.5-coder:32b (strong coding, 32GB VRAM)\n  llama3.1:8b (fast, 8GB VRAM)"))
	FString OllamaModelId;

	/** Ollama context window size in tokens. Default: 8192. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "API|Ollama",
		meta = (DisplayName = "Ollama Context Size (tokens)", ClampMin = "512", ClampMax = "131072",
		ToolTip = "Context window size for the Ollama model.\nHigher values use more VRAM but allow longer conversations.\nDefault: 8192. Most models support up to 32768 or 131072."))
	int32 OllamaContextSize;

	// ============================================================================
	// LM Studio (local model server — NO API KEY REQUIRED)
	// ============================================================================

	/** LM Studio server base URL. Default: http://localhost:1234 */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "API|LM Studio",
		meta = (DisplayName = "LM Studio Base URL",
		ToolTip = "No API key needed — LM Studio runs locally on your machine.\nDefault: http://localhost:1234\n\nSetup:\n1. Download LM Studio from https://lmstudio.ai\n2. Load a model in LM Studio\n3. Enable 'Local Server' in LM Studio (starts on port 1234)\n4. Select 'LM Studio (Local)' as your provider in Autonomix"))
	FString LMStudioBaseUrl;

	/** LM Studio model identifier as shown in the LM Studio UI */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "API|LM Studio",
		meta = (DisplayName = "LM Studio Model ID",
		ToolTip = "The model identifier as shown in LM Studio's model list.\nExample: mistralai/devstral-small-2505\n\nThe model must be loaded and the Local Server must be running."))
	FString LMStudioModelId;

	// ============================================================================
	// Custom OpenAI-Compatible Endpoint (LiteLLM, Groq, Together, SambaNova, etc.)
	// ============================================================================

	/** Custom endpoint base URL (must be OpenAI-compatible: POST /v1/chat/completions) */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "API|Custom",
		meta = (DisplayName = "Custom Base URL",
		ToolTip = "Any OpenAI-compatible endpoint, e.g. https://api.groq.com/openai/v1"))
	FString CustomBaseUrl;

	/** Custom endpoint API key (empty for local endpoints that don't require auth) */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "API|Custom",
		meta = (DisplayName = "Custom API Key", PasswordField = true))
	FString CustomApiKey;

	/** Custom model ID as required by the endpoint */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "API|Custom",
		meta = (DisplayName = "Custom Model ID",
		ToolTip = "Model identifier as required by the custom endpoint"))
	FString CustomEndpointModelId;

	// ============================================================================
	// GitHub Copilot
	// ============================================================================

	/** The model to use when using GitHub Copilot */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "API|GitHub Copilot",
		meta = (DisplayName = "Copilot Model", GetOptions = "GetCopilotModelOptions", ToolTip = "Select from available GitHub Copilot models."))
	FString CopilotModelId;

	/** System-wide GitHub Copilot token cache */
	UPROPERTY(Config, BlueprintReadOnly, Category = "Hidden")
	FString CopilotCachedDeviceCode;

	/** Cached list of dynamically queried models */
	UPROPERTY(Config, BlueprintReadOnly, Category = "Hidden")
	TArray<FString> CopilotAvailableModels;

	// ============================================================================
	// Global Model Settings (applies across all providers)
	// ============================================================================

	/** Maximum tokens for AI responses (across all providers) */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "API|Model",
		meta = (DisplayName = "Max Response Tokens", ClampMin = "256", ClampMax = "200000"))
	int32 MaxResponseTokens;

	/** HTTP request timeout in seconds */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "API|Connection",
		meta = (DisplayName = "Request Timeout (seconds)", ClampMin = "10", ClampMax = "600"))
	int32 RequestTimeoutSeconds;

	// ============================================================================
	// Safety & Security Settings
	// ============================================================================

	/** Security mode controlling what capabilities Autonomix is allowed to use.
	 *  Defaults to Sandbox for safety. Developer mode requires explicit opt-in. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Safety|Security",
		meta = (DisplayName = "Security Mode",
		ToolTip = "Sandbox: No C++/builds/shell. Advanced: Full asset editing. Developer: Full power including C++ and builds."))
	EAutonomixSecurityMode SecurityMode;

	/** Maximum number of automatic retries when the AI generates invalid code */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Safety",
		meta = (DisplayName = "Max Auto-Retries", ClampMin = "0", ClampMax = "10"))
	int32 MaxAutoRetries;

	/** Auto-approve low-risk actions without confirmation dialog */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Safety",
		meta = (DisplayName = "Auto-Approve Low Risk Actions"))
	bool bAutoApproveLowRisk;

	/** Auto-approve ALL tool calls without showing the approval panel.
	 *  When enabled, tools execute immediately without user confirmation.
	 *  Useful for experienced users who trust the AI's judgment. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Safety",
		meta = (DisplayName = "Auto-Approve All Tools",
		ToolTip = "Skip the approve/reject dialog and execute all tool calls immediately. Use with caution."))
	bool bAutoApproveAllTools;

	/** Auto-approve read-only tools (context queries, file reads) without confirmation.
	 *  Safe to enable -- these tools don't modify anything. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Safety",
		meta = (DisplayName = "Auto-Approve Read-Only Tools",
		ToolTip = "Automatically approve tools that only read data (context, file listing, etc)."))
	bool bAutoApproveReadOnlyTools;

	/** Maximum number of consecutive auto-approved tool calls before requiring confirmation.
	 *  Set to 0 for unlimited. Prevents runaway agentic loops. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Safety",
		meta = (DisplayName = "Max Consecutive Auto-Approved Requests", ClampMin = "0", ClampMax = "100"))
	int32 MaxConsecutiveAutoApprovedRequests;

	/** Maximum cumulative API cost (USD) before requiring user confirmation to continue.
	 *  Set to 0 for unlimited. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Safety|Cost",
		meta = (DisplayName = "Max Auto-Approved Cost (USD)", ClampMin = "0.0", ClampMax = "1000.0"))
	float MaxAutoApprovedCostDollars;

	/** Require typed confirmation for critical-risk actions */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Safety",
		meta = (DisplayName = "Require Typed Confirmation for Critical Actions"))
	bool bRequireTypedConfirmation;

	/** Enable automatic file backups before modifications */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Safety",
		meta = (DisplayName = "Enable Auto-Backup"))
	bool bEnableAutoBackup;

	/** Maximum number of backup snapshots to retain */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Safety",
		meta = (DisplayName = "Max Backup Count", ClampMin = "5", ClampMax = "500"))
	int32 MaxBackupCount;

	/** Enable source control auto-checkout before file modification */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Safety",
		meta = (DisplayName = "Auto-Checkout from Source Control"))
	bool bAutoCheckout;

	/** Allow external process execution (UAT builds). If false, blocks all CreateProc calls.
	 *  Defaults to false for Marketplace safety. Enable explicitly for build automation. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Safety|Security",
		meta = (DisplayName = "Allow External Process Execution",
		ToolTip = "WARNING: Enables UAT/build process spawning. Disabled by default for safety."))
	bool bAllowExternalProcessExecution;

	// ============================================================================
	// Protected Files (read-only for AI)
	// ============================================================================

	/** Additional file path patterns that the AI may read but must never modify.
	 *  Uses glob syntax: *.uplugin, Config/Default*.ini, Source/[Project].Build.cs
	 *
	 *  The following paths are always protected by default regardless of this list:
	 *  *.uplugin, *.uproject, *.Build.cs, *.Target.cs, Config/DefaultEngine.ini,
	 *  Config/DefaultEditor.ini, Config/DefaultGame.ini, .gitignore, .autonomixignore
	 *
	 *  Write attempts to protected paths always return EAutonomixRiskLevel::Critical. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Safety|Protected Files",
		meta = (DisplayName = "Additional Protected File Patterns",
		ToolTip = "Glob patterns for files the AI may read but must never write. Defaults cover *.uplugin, *.uproject, *.Build.cs, core Config/*.ini files."))
	TArray<FString> AdditionalProtectedPaths;

	/** Override the default protected paths list with only the paths in AdditionalProtectedPaths.
	 *  When false (default), AdditionalProtectedPaths supplements the built-in defaults.
	 *  When true, ONLY AdditionalProtectedPaths is used — use with caution. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Safety|Protected Files",
		meta = (DisplayName = "Override Default Protected Paths",
		ToolTip = "When enabled, replaces the built-in protected file list with only your custom patterns."))
	bool bOverrideDefaultProtectedPaths = false;

	// ============================================================================
	// Privacy
	// ============================================================================

	/** Whether the user has accepted the privacy disclosure on first launch.
	 *  Required before any data is sent to the Anthropic API. */
	UPROPERTY(Config, BlueprintReadOnly, Category = "Privacy")
	bool bHasAcceptedPrivacyDisclosure;

	// ============================================================================
	// Cost & Rate Limiting
	// ============================================================================

	/** Daily token usage limit (0 = unlimited) */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Cost",
		meta = (DisplayName = "Daily Token Limit", ClampMin = "0", ClampMax = "10000000"))
	int32 DailyTokenLimit;

	/** Show estimated cost per request in the UI */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Cost",
		meta = (DisplayName = "Show Cost Estimates"))
	bool bShowCostEstimates;

	/** Show the cost of each request alongside the response */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Cost",
		meta = (DisplayName = "Show Per-Request Cost"))
	bool bShowPerRequestCost;

	// ============================================================================
	// Context Settings
	// ============================================================================

	/** Maximum token budget for project context in each AI request */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Context",
		meta = (DisplayName = "Context Token Budget", ClampMin = "1000", ClampMax = "100000"))
	int32 ContextTokenBudget;

	/** Include source file tree in context */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Context",
		meta = (DisplayName = "Include Source Tree"))
	bool bIncludeSourceTree;

	/** Include asset registry summary in context */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Context",
		meta = (DisplayName = "Include Asset Summary"))
	bool bIncludeAssetSummary;

	/** Include project settings snapshot in context */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Context",
		meta = (DisplayName = "Include Settings Snapshot"))
	bool bIncludeSettingsSnapshot;

	/** Include class hierarchy in context */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Context",
		meta = (DisplayName = "Include Class Hierarchy"))
	bool bIncludeClassHierarchy;

	// ============================================================================
	// Context Management (Auto-Condense & Truncation)
	// ============================================================================

	/** Automatically condense the conversation when approaching the context window limit.
	 *  Uses an LLM call to summarize the conversation and replace old messages with
	 *  a concise summary (fresh start model). Falls back to sliding window truncation
	 *  if condensation fails. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Context|Management",
		meta = (DisplayName = "Auto-Condense Context",
		ToolTip = "Automatically summarize old messages when context usage exceeds the threshold."))
	bool bAutoCondenseContext;

	/** Context usage percentage (0-100%) at which auto-condensation triggers.
	 *  Default 80% leaves room for the response before hitting the context limit. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Context|Management",
		meta = (DisplayName = "Auto-Condense Threshold (%)", ClampMin = "5", ClampMax = "100",
		EditCondition = "bAutoCondenseContext", EditConditionHides))
	int32 AutoCondenseThresholdPercent;

	// ============================================================================
	// UI Settings
	// ============================================================================

	/** Font size for chat messages */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "UI",
		meta = (DisplayName = "Chat Font Size", ClampMin = "8", ClampMax = "24"))
	int32 ChatFontSize;

	/** Show timestamps on messages */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "UI",
		meta = (DisplayName = "Show Timestamps"))
	bool bShowTimestamps;

	/** Enable streaming text display (token-by-token) */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "UI",
		meta = (DisplayName = "Enable Streaming Display"))
	bool bEnableStreamingDisplay;

	// ============================================================================
	// Tool Enable/Disable
	// ============================================================================

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Tools",
		meta = (DisplayName = "Enable Blueprint Tools"))
	bool bEnableBlueprintTools;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Tools",
		meta = (DisplayName = "Enable C++ Tools"))
	bool bEnableCppTools;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Tools",
		meta = (DisplayName = "Enable Material Tools"))
	bool bEnableMaterialTools;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Tools",
		meta = (DisplayName = "Enable Import Tools"))
	bool bEnableImportTools;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Tools",
		meta = (DisplayName = "Enable Level Tools"))
	bool bEnableLevelTools;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Tools",
		meta = (DisplayName = "Enable Settings Tools"))
	bool bEnableSettingsTools;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Tools",
		meta = (DisplayName = "Enable Build Tools"))
	bool bEnableBuildTools;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Tools",
		meta = (DisplayName = "Enable Performance Tools"))
	bool bEnablePerformanceTools;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Tools",
		meta = (DisplayName = "Enable Python Scripting",
		ToolTip = "Allow AI to write and execute Python scripts via the Python Editor Script Plugin. Requires Developer security mode."))
	bool bEnablePythonTools;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Tools",
		meta = (DisplayName = "Enable Viewport Capture (Vision)",
		ToolTip = "Allow AI to capture editor viewport screenshots for visual analysis. Read-only."))
	bool bEnableViewportCapture;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Tools",
		meta = (DisplayName = "Enable DataTable Tools"))
	bool bEnableDataTableTools;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Tools",
		meta = (DisplayName = "Enable Behavior Tree / AI Tools"))
	bool bEnableBehaviorTreeTools;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Tools",
		meta = (DisplayName = "Enable Sequencer / Cinematics Tools"))
	bool bEnableSequencerTools;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Tools",
		meta = (DisplayName = "Enable PIE Testing Automation",
		ToolTip = "Allow AI to start/stop Play-In-Editor sessions and simulate input. Requires Developer security mode."))
	bool bEnablePIETools;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Tools",
		meta = (DisplayName = "Enable Gameplay Ability System (GAS) Tools",
		ToolTip = "Tools for creating AttributeSets, GameplayEffects, GameplayAbilities, tags, and ASC setup."))
	bool bEnableGASTools;

	// ============================================================================
	// Model Dropdown Options (used by UPROPERTY GetOptions meta)
	// ============================================================================

	/** Returns known OpenAI model IDs for the settings dropdown */
	UFUNCTION()
	TArray<FString> GetOpenAIModelOptions() const;

	/** Returns known GitHub Copilot model IDs for the settings dropdown */
	UFUNCTION()
	TArray<FString> GetCopilotModelOptions() const;

	/** Returns known Gemini model IDs for the settings dropdown */
	UFUNCTION()
	TArray<FString> GetGeminiModelOptions() const;

	/** Returns known DeepSeek model IDs for the settings dropdown */
	UFUNCTION()
	TArray<FString> GetDeepSeekModelOptions() const;

	/** Returns known Mistral model IDs for the settings dropdown */
	UFUNCTION()
	TArray<FString> GetMistralModelOptions() const;

	/** Returns known xAI model IDs for the settings dropdown */
	UFUNCTION()
	TArray<FString> GetxAIModelOptions() const;

	// ============================================================================
	// Utility
	// ============================================================================

	static const UAutonomixDeveloperSettings* Get();
	bool IsApiKeySet() const;
	bool IsActiveProviderApiKeySet() const;
	FString GetActiveApiKey() const;
	FString GetEffectiveEndpoint() const;
	FString GetEffectiveModel() const;
	FString GetModelDisplayName() const;
	static FString ModelEnumToApiString(EAutonomixClaudeModel Model);
	bool IsToolCategoryAllowed(EAutonomixActionCategory Category) const;
	bool IsIniSectionAllowed(const FString& Section) const;

	// UDeveloperSettings interface
	virtual FName GetContainerName() const override;
	virtual FName GetCategoryName() const override;
	virtual FName GetSectionName() const override;

#if WITH_EDITOR
	virtual FText GetSectionText() const override;
	virtual FText GetSectionDescription() const override;

	/** Validate settings on edit -- enforces thinking token constraints
	 *  and Developer mode escalation confirmation dialog. */
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
};
