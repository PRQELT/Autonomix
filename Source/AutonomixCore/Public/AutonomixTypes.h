// Copyright Autonomix. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "AutonomixTypes.generated.h"

// ============================================================================
// Enumerations
// ============================================================================

/** Risk level for AI-driven actions. Determines the confirmation gate behavior. */
UENUM(BlueprintType)
enum class EAutonomixRiskLevel : uint8
{
	/** Auto-approve with log entry. e.g. creating a new empty Blueprint */
	Low			UMETA(DisplayName = "Low"),

	/** Show plan preview, require one-click approval */
	Medium		UMETA(DisplayName = "Medium"),

	/** Show full diff preview, require explicit confirmation */
	High		UMETA(DisplayName = "High"),

	/** Show warning dialog, require typed confirmation phrase */
	Critical	UMETA(DisplayName = "Critical")
};

/** Role of a message in the conversation */
UENUM(BlueprintType)
enum class EAutonomixMessageRole : uint8
{
	User		UMETA(DisplayName = "User"),
	Assistant	UMETA(DisplayName = "Assistant"),
	System		UMETA(DisplayName = "System"),
	ToolResult	UMETA(DisplayName = "Tool Result"),
	Error		UMETA(DisplayName = "Error"),
	None 		UMETA(DisplayName = "None")
};

/** Type of action the AI is requesting */
UENUM(BlueprintType)
enum class EAutonomixActionCategory : uint8
{
	Blueprint		UMETA(DisplayName = "Blueprint"),
	Cpp				UMETA(DisplayName = "C++"),
	Material		UMETA(DisplayName = "Material"),
	Mesh			UMETA(DisplayName = "Mesh"),
	Texture			UMETA(DisplayName = "Texture"),
	Audio			UMETA(DisplayName = "Audio"),
	Animation		UMETA(DisplayName = "Animation"),
	Level			UMETA(DisplayName = "Level"),
	Settings		UMETA(DisplayName = "Settings"),
	Build			UMETA(DisplayName = "Build"),
	Performance		UMETA(DisplayName = "Performance"),
	SourceControl	UMETA(DisplayName = "Source Control"),
	FileSystem		UMETA(DisplayName = "File System"),
	General			UMETA(DisplayName = "General")
};

/** Status of an action execution */
UENUM(BlueprintType)
enum class EAutonomixActionStatus : uint8
{
	Pending			UMETA(DisplayName = "Pending"),
	AwaitingApproval UMETA(DisplayName = "Awaiting Approval"),
	InProgress		UMETA(DisplayName = "In Progress"),
	Succeeded		UMETA(DisplayName = "Succeeded"),
	Failed			UMETA(DisplayName = "Failed"),
	Cancelled		UMETA(DisplayName = "Cancelled"),
	Retrying		UMETA(DisplayName = "Retrying")
};

/** SSE event type from Claude's streaming API */
UENUM()
enum class EAutonomixSSEEventType : uint8
{
	MessageStart,
	ContentBlockStart,
	ContentBlockDelta,
	ContentBlockStop,
	MessageDelta,
	MessageStop,
	Ping,
	Error,
	Unknown
};

/** Security mode controlling what actions Autonomix is allowed to perform */
UENUM(BlueprintType)
enum class EAutonomixSecurityMode : uint8
{
	/** Marketplace-safe. No C++, no shell, no config edits outside plugin namespace */
	Sandbox		UMETA(DisplayName = "Sandbox"),

	/** Allows file edits in Source/, Blueprint/Material/Level full access */
	Advanced	UMETA(DisplayName = "Advanced"),

	/** Full power: C++ generation, UAT builds, Live Coding, project file regen */
	Developer	UMETA(DisplayName = "Developer")
};

/**
 * AI behavioral mode for the current conversation tab.
 * Each mode provides a different role definition and restricts the available tool set.
 * The AI can switch modes via the switch_mode tool.
 * Ported and adapted from Roo Code's modes.ts (code/architect/ask/debug modes).
 */
UENUM(BlueprintType)
enum class EAutonomixAgentMode : uint8
{
	/** All tools available. Default mode for general UE development. */
	General		UMETA(DisplayName = "General"),

	/** Blueprint tools only. No C++ write access. Best for pure Blueprint work. */
	Blueprint	UMETA(DisplayName = "Blueprint"),

	/** Full C++ code tools + build tools. No Blueprint/asset writes. */
	CppCode		UMETA(DisplayName = "C++ Code"),

	/** Read-only analysis mode. No write tools. Planning, explaining, reviewing. */
	Architect	UMETA(DisplayName = "Architect"),

	/** Read + execute (run tests/builds). For diagnosing compile errors + crashes. */
	Debug		UMETA(DisplayName = "Debug"),

	/** Asset-focused tools: materials, textures, meshes, audio. No code tools. */
	Asset		UMETA(DisplayName = "Asset"),

	/**
	 * Orchestrator mode: coordinates complex multi-step projects by breaking them
	 * into sub-tasks and delegating to other modes via the new_task tool.
	 * Has access to all read tools + new_task + switch_mode. No direct write tools.
	 * Ported from Roo Code's 🪃 Orchestrator mode.
	 */
	Orchestrator UMETA(DisplayName = "Orchestrator"),
};

/** AI provider selection — determines which API endpoint and wire format to use */
UENUM(BlueprintType)
enum class EAutonomixProvider : uint8
{
	/** Anthropic Claude (claude-sonnet-4-6, claude-opus-4-6, claude-3-5-sonnet, etc.) */
	Anthropic		UMETA(DisplayName = "Anthropic (Claude)"),

	/** OpenAI GPT / o-series (gpt-4o, gpt-4.1, o3, o4-mini, etc.) — official OpenAI API only */
	OpenAI			UMETA(DisplayName = "OpenAI (GPT / o-series)"),

	/**
	 * Microsoft Azure OpenAI Service.
	 *
	 * Azure uses a different auth model and URL structure than the official OpenAI API:
	 *   - Auth: 'api-key: {key}' header (NOT 'Authorization: Bearer {key}')
	 *   - URL:  https://{resource}.openai.azure.com/openai/deployments/{deployment-name}
	 *   - API version query param: ?api-version=2024-02-01
	 *   - Chat Completions API only (does NOT support the Responses API /v1/responses)
	 *   - Model ID = your deployment name (not the base OpenAI model name)
	 *
	 * Also auto-detected: if you use the OpenAI provider with an Azure base URL
	 * (*.openai.azure.com), Autonomix automatically switches to Azure wire format.
	 */
	Azure			UMETA(DisplayName = "Azure OpenAI"),

	/** Google Gemini (gemini-2.5-pro, gemini-2.5-flash, gemini-3.x, etc.) */
	Google			UMETA(DisplayName = "Google (Gemini)"),

	/** DeepSeek (deepseek-chat, deepseek-reasoner) */
	DeepSeek		UMETA(DisplayName = "DeepSeek"),

	/** Mistral AI (mistral-large, codestral, etc.) */
	Mistral			UMETA(DisplayName = "Mistral AI"),

	/** xAI Grok (grok-2, grok-3, etc.) */
	xAI				UMETA(DisplayName = "xAI (Grok)"),

	/** OpenRouter — aggregates hundreds of models under one API key */
	OpenRouter		UMETA(DisplayName = "OpenRouter"),

	/** Ollama — local model serving (localhost) */
	Ollama			UMETA(DisplayName = "Ollama (Local)"),

	/** LM Studio — local model serving (localhost) */
	LMStudio		UMETA(DisplayName = "LM Studio (Local)"),

	/** Custom — any OpenAI-compatible endpoint (LiteLLM, Groq, Together, etc.) */
	Custom			UMETA(DisplayName = "Custom (OpenAI-Compatible)"),

	/** GitHub Copilot (uses device flow + token exchange) */
	GitHubCopilot	UMETA(DisplayName = "GitHub Copilot (Student/Pro)")
};

/** Reasoning effort level for models that support it (OpenAI o-series, Gemini 2.5+, DeepSeek-R1) */
UENUM(BlueprintType)
enum class EAutonomixReasoningEffort : uint8
{
	/** No reasoning / thinking (use for fast, cheap responses) */
	Disabled	UMETA(DisplayName = "Disabled"),

	/** Low effort — minimal thinking tokens, fastest */
	Low			UMETA(DisplayName = "Low"),

	/** Medium effort — balanced thinking/speed */
	Medium		UMETA(DisplayName = "Medium"),

	/** High effort — maximum thinking tokens, slowest/most accurate */
	High		UMETA(DisplayName = "High")
};

/** Available Claude model presets (kept for backward compatibility) */
UENUM(BlueprintType)
enum class EAutonomixClaudeModel : uint8
{
	/** Claude Sonnet 4.6 -- fast, capable, cost-effective (default) */
	Sonnet_4_6		UMETA(DisplayName = "Claude Sonnet 4.6"),

	/** Claude Sonnet 4.5 -- balanced performance */
	Sonnet_4_5		UMETA(DisplayName = "Claude Sonnet 4.5"),

	/** Claude Opus 4.6 -- most capable, highest reasoning */
	Opus_4_6		UMETA(DisplayName = "Claude Opus 4.6"),

	/** Claude Opus 4.5 -- multi-modal, deep reasoning */
	Opus_4_5		UMETA(DisplayName = "Claude Opus 4.5"),

	/** Claude Haiku 4 -- fastest, cheapest */
	Haiku_4			UMETA(DisplayName = "Claude Haiku 4"),

	/** Custom model ID -- use CustomModelId string */
	Custom			UMETA(DisplayName = "Custom Model")
};

/** Context window size configuration */
UENUM(BlueprintType)
enum class EAutonomixContextWindow : uint8
{
	/** Standard 200K context window */
	Standard_200K	UMETA(DisplayName = "200K (Standard)"),

	/** Extended 1M context window (beta, higher cost) */
	Extended_1M		UMETA(DisplayName = "1M (Extended -- Beta)")
};

// ============================================================================
// Model Capability Info
// ============================================================================

/**
 * Capability metadata for a specific model.
 * Modeled after Roo Code's ModelInfo type.
 * Populated by FAutonomixModelRegistry for known models; filled with defaults for unknown models.
 */
USTRUCT(BlueprintType)
struct AUTONOMIXCORE_API FAutonomixModelInfo
{
	GENERATED_BODY()

	/** Provider this model belongs to */
	UPROPERTY(BlueprintReadOnly, Category = "Autonomix|Model")
	EAutonomixProvider Provider = EAutonomixProvider::Anthropic;

	/** Model identifier string as used in API calls */
	UPROPERTY(BlueprintReadOnly, Category = "Autonomix|Model")
	FString ModelId;

	/** Human-readable display name */
	UPROPERTY(BlueprintReadOnly, Category = "Autonomix|Model")
	FString DisplayName;

	/** Maximum context window in tokens (default 200K) */
	UPROPERTY(BlueprintReadOnly, Category = "Autonomix|Model")
	int32 ContextWindow = 200000;

	/** Maximum output tokens per response */
	UPROPERTY(BlueprintReadOnly, Category = "Autonomix|Model")
	int32 MaxOutputTokens = 8192;

	/** Maximum thinking/reasoning tokens (0 = not supported) */
	UPROPERTY(BlueprintReadOnly, Category = "Autonomix|Model")
	int32 MaxThinkingTokens = 0;

	/** True if model supports Anthropic extended thinking (budget_tokens) */
	UPROPERTY(BlueprintReadOnly, Category = "Autonomix|Model")
	bool bSupportsReasoningBudget = false;

	/** True if model supports reasoning effort levels (OpenAI o-series, Gemini 2.5+, DeepSeek-R1) */
	UPROPERTY(BlueprintReadOnly, Category = "Autonomix|Model")
	bool bSupportsReasoningEffort = false;

	/** True if model supports Anthropic's 1M context beta flag */
	UPROPERTY(BlueprintReadOnly, Category = "Autonomix|Model")
	bool bSupports1MContext = false;

	/** True if model accepts image inputs */
	UPROPERTY(BlueprintReadOnly, Category = "Autonomix|Model")
	bool bSupportsImages = true;

	/** True if model supports prompt caching */
	UPROPERTY(BlueprintReadOnly, Category = "Autonomix|Model")
	bool bSupportsPromptCache = false;

	/** Input price per 1M tokens (USD) */
	UPROPERTY(BlueprintReadOnly, Category = "Autonomix|Model")
	float InputPricePerMillion = 0.0f;

	/** Output price per 1M tokens (USD) */
	UPROPERTY(BlueprintReadOnly, Category = "Autonomix|Model")
	float OutputPricePerMillion = 0.0f;

	/** Cache write price per 1M tokens (USD, 0 if not supported) */
	UPROPERTY(BlueprintReadOnly, Category = "Autonomix|Model")
	float CacheWritesPricePerMillion = 0.0f;

	/** Cache read price per 1M tokens (USD, 0 if not supported) */
	UPROPERTY(BlueprintReadOnly, Category = "Autonomix|Model")
	float CacheReadsPricePerMillion = 0.0f;

	FAutonomixModelInfo() = default;
};

/** HTTP error classification for user-friendly messaging */
UENUM()
enum class EAutonomixHTTPErrorType : uint8
{
	None,
	Unauthorized,		 // 401
	RateLimited,		 // 429
	ServerError,		 // 500+
	Timeout,			 // Request timed out
	NetworkError,		 // Connection failed
	InvalidResponse,	 // Malformed response
	ContextWindowExceeded, // 400 with "context_length_exceeded" or similar — triggers forced reduction + retry
	Unknown
};

// ============================================================================
// Structures
// ============================================================================

/** A single message in the chat conversation */
USTRUCT(BlueprintType)
struct AUTONOMIXCORE_API FAutonomixMessage
{
	GENERATED_BODY()

	/** Unique ID for this message */
	UPROPERTY(BlueprintReadOnly, Category = "Autonomix", Meta = (IgnoreForMemberInitializationTest))
	FGuid MessageId;

	/** Role of the sender */
	UPROPERTY(BlueprintReadOnly, Category = "Autonomix")
	EAutonomixMessageRole Role;

	/** Text content of the message */
	UPROPERTY(BlueprintReadOnly, Category = "Autonomix")
	FString Content;

	/** Timestamp of when the message was created */
	UPROPERTY(BlueprintReadOnly, Category = "Autonomix")
	FDateTime Timestamp;

	/** Optional: tool use ID (for tool_result messages) */
	UPROPERTY(BlueprintReadOnly, Category = "Autonomix")
	FString ToolUseId;

	/** Optional: tool name that was called */
	UPROPERTY(BlueprintReadOnly, Category = "Autonomix")
	FString ToolName;

	/** Whether this message is still being streamed */
	UPROPERTY(BlueprintReadOnly, Category = "Autonomix")
	bool bIsStreaming = false;

	UPROPERTY(BlueprintReadOnly, Category = "Autonomix")
	bool bIsCollapsible = false;

	// ---- Context Management Tags (non-destructive condense/truncate) ----

	/** If set, this message has been condensed (replaced by a summary).
	 *  Value is the CondenseId of the summary message that replaced it.
	 *  Messages with a valid CondenseParent are filtered out by GetEffectiveHistory(). */
	FString CondenseParent;

	/** True if this message IS a summary produced by context condensation.
	 *  Summary messages have role=User and contain the conversation summary. */
	bool bIsSummary = false;

	/** Unique ID for this summary -- other messages reference this via CondenseParent. */
	FString CondenseId;

	/** If set, this message has been hidden by sliding-window truncation.
	 *  Value is the TruncationId of the truncation marker that hid it. */
	FString TruncationParent;

	/** True if this message is a truncation marker inserted during sliding-window truncation. */
	bool bIsTruncationMarker = false;

	/** Unique ID for this truncation marker -- other messages reference this via TruncationParent. */
	FString TruncationId;

	/** Serialized JSON array of content blocks for assistant messages.
	 *  Preserves structural fidelity: text blocks + tool_use blocks.
	 *  Used for proper API round-tripping and conversation replay/export.
	 *  Empty for non-assistant messages. */
	FString ContentBlocksJson;

	/** DeepSeek reasoning_content from thinking mode (deepseek-reasoner).
	 *  Required by DeepSeek API: when thinking mode is enabled, ALL assistant messages
	 *  must include reasoning_content when replayed in conversation history.
	 *  Empty for non-DeepSeek providers or non-reasoning models. */
	FString ReasoningContent;

	FAutonomixMessage()
		: MessageId(FGuid::NewGuid())
		, Role(EAutonomixMessageRole::User)
		, Timestamp(FDateTime::UtcNow())
	{
	}

	FAutonomixMessage(EAutonomixMessageRole InRole, const FString& InContent)
		: MessageId(FGuid::NewGuid())
		, Role(InRole)
		, Content(InContent)
		, Timestamp(FDateTime::UtcNow())
	{
	}
};

/** Represents a single tool call from the AI */
USTRUCT(BlueprintType)
struct AUTONOMIXCORE_API FAutonomixToolCall
{
	GENERATED_BODY()

	/** The tool use ID from Claude's response */
	UPROPERTY(BlueprintReadOnly, Category = "Autonomix")
	FString ToolUseId;

	/** Name of the tool being called */
	UPROPERTY(BlueprintReadOnly, Category = "Autonomix")
	FString ToolName;

	/** The raw JSON input parameters from the AI */
	TSharedPtr<FJsonObject> InputParams;

	/** Parsed action category */
	UPROPERTY(BlueprintReadOnly, Category = "Autonomix")
	EAutonomixActionCategory Category = EAutonomixActionCategory::General;

	FAutonomixToolCall() = default;
};

/** Represents a single discrete action within an action plan */
USTRUCT(BlueprintType)
struct AUTONOMIXCORE_API FAutonomixAction
{
	GENERATED_BODY()

	/** Unique ID for this action */
	UPROPERTY(BlueprintReadOnly, Category = "Autonomix", Meta = (IgnoreForMemberInitializationTest))
	FGuid ActionId;

	/** Human-readable description of this action */
	UPROPERTY(BlueprintReadOnly, Category = "Autonomix")
	FString Description;

	/** Risk level of this action */
	UPROPERTY(BlueprintReadOnly, Category = "Autonomix")
	EAutonomixRiskLevel RiskLevel = EAutonomixRiskLevel::Low;

	/** Current execution status */
	UPROPERTY(BlueprintReadOnly, Category = "Autonomix")
	EAutonomixActionStatus Status = EAutonomixActionStatus::Pending;

	/** Category of the action */
	UPROPERTY(BlueprintReadOnly, Category = "Autonomix")
	EAutonomixActionCategory Category = EAutonomixActionCategory::General;

	/** The tool call that initiated this action */
	FAutonomixToolCall ToolCall;

	/** List of file paths that will be affected */
	UPROPERTY(BlueprintReadOnly, Category = "Autonomix")
	TArray<FString> AffectedPaths;

	/** List of assets that will be affected */
	UPROPERTY(BlueprintReadOnly, Category = "Autonomix")
	TArray<FString> AffectedAssets;

	FAutonomixAction()
		: ActionId(FGuid::NewGuid())
	{
	}
};

/** Collection of ordered actions forming a plan */
USTRUCT(BlueprintType)
struct AUTONOMIXCORE_API FAutonomixActionPlan
{
	GENERATED_BODY()

	/** Unique ID for this plan */
	UPROPERTY(BlueprintReadOnly, Category = "Autonomix", Meta = (IgnoreForMemberInitializationTest))
	FGuid PlanId;

	/** Human-readable summary of the full plan */
	UPROPERTY(BlueprintReadOnly, Category = "Autonomix")
	FString Summary;

	/** Ordered list of actions in this plan */
	UPROPERTY(BlueprintReadOnly, Category = "Autonomix")
	TArray<FAutonomixAction> Actions;

	/** Maximum risk level across all actions */
	UPROPERTY(BlueprintReadOnly, Category = "Autonomix")
	EAutonomixRiskLevel MaxRiskLevel = EAutonomixRiskLevel::Low;

	/** Undo group name for rolling back the entire plan */
	UPROPERTY(BlueprintReadOnly, Category = "Autonomix")
	FString UndoGroupName;

	FAutonomixActionPlan()
		: PlanId(FGuid::NewGuid())
	{
	}
};

/** Result of executing a single action */
USTRUCT(BlueprintType)
struct AUTONOMIXCORE_API FAutonomixActionResult
{
	GENERATED_BODY()

	/** Whether the action succeeded */
	UPROPERTY(BlueprintReadOnly, Category = "Autonomix")
	bool bSuccess = false;

	/** The action that was executed */
	UPROPERTY(BlueprintReadOnly, Category = "Autonomix", Meta = (IgnoreForMemberInitializationTest))
	FGuid ActionId;

	/** Human-readable result message */
	UPROPERTY(BlueprintReadOnly, Category = "Autonomix")
	FString ResultMessage;

	/** Error messages if the action failed */
	UPROPERTY(BlueprintReadOnly, Category = "Autonomix")
	TArray<FString> Errors;

	/** Warning messages */
	UPROPERTY(BlueprintReadOnly, Category = "Autonomix")
	TArray<FString> Warnings;

	/** Paths of files that were created or modified */
	UPROPERTY(BlueprintReadOnly, Category = "Autonomix")
	TArray<FString> ModifiedPaths;

	/** Paths of assets that were created or modified */
	UPROPERTY(BlueprintReadOnly, Category = "Autonomix")
	TArray<FString> ModifiedAssets;

	/** Paths of backup files that were created */
	UPROPERTY(BlueprintReadOnly, Category = "Autonomix")
	TArray<FString> BackupPaths;

	/** Time taken to execute in seconds */
	UPROPERTY(BlueprintReadOnly, Category = "Autonomix")
	float ExecutionTimeSeconds = 0.0f;
};

/** An SSE event parsed from the Claude streaming response */
struct AUTONOMIXCORE_API FAutonomixSSEEvent
{
	EAutonomixSSEEventType Type = EAutonomixSSEEventType::Unknown;
	FString RawData;
	TSharedPtr<FJsonObject> JsonData;
	int32 ContentBlockIndex = -1;

	FAutonomixSSEEvent() = default;
};

/** File entry in the project context tree */
USTRUCT(BlueprintType)
struct AUTONOMIXCORE_API FAutonomixFileEntry
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Autonomix")
	FString Path;

	UPROPERTY(BlueprintReadOnly, Category = "Autonomix")
	FString Extension;

	UPROPERTY(BlueprintReadOnly, Category = "Autonomix")
	bool bIsDirectory = false;

	UPROPERTY(BlueprintReadOnly, Category = "Autonomix")
	int64 FileSize = 0;
};

/** Asset entry in the project context */
USTRUCT(BlueprintType)
struct AUTONOMIXCORE_API FAutonomixAssetEntry
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Autonomix")
	FString AssetPath;

	UPROPERTY(BlueprintReadOnly, Category = "Autonomix")
	FString AssetName;

	UPROPERTY(BlueprintReadOnly, Category = "Autonomix")
	FString AssetClass;

	UPROPERTY(BlueprintReadOnly, Category = "Autonomix")
	TArray<FString> Dependencies;
};

/** Record of a single action execution for the audit journal */
USTRUCT(BlueprintType)
struct AUTONOMIXCORE_API FAutonomixActionExecutionRecord
{
	GENERATED_BODY()

	/** Unique ID for this execution record */
	UPROPERTY(BlueprintReadOnly, Category = "Autonomix", Meta = (IgnoreForMemberInitializationTest))
	FGuid RecordId;

	/** Name of the tool that was executed */
	UPROPERTY(BlueprintReadOnly, Category = "Autonomix")
	FString ToolName;

	/** The tool_use_id from Claude's response */
	UPROPERTY(BlueprintReadOnly, Category = "Autonomix")
	FString ToolUseId;

	/** Raw JSON input parameters (serialized) */
	UPROPERTY(BlueprintReadOnly, Category = "Autonomix")
	FString InputJson;

	/** Result message */
	UPROPERTY(BlueprintReadOnly, Category = "Autonomix")
	FString ResultMessage;

	/** Whether the action succeeded */
	UPROPERTY(BlueprintReadOnly, Category = "Autonomix")
	bool bSuccess = false;

	/** Whether the tool result was an error (for is_error in tool_result) */
	UPROPERTY(BlueprintReadOnly, Category = "Autonomix")
	bool bIsError = false;

	/** List of files that were modified */
	UPROPERTY(BlueprintReadOnly, Category = "Autonomix")
	TArray<FString> ModifiedFiles;

	/** List of assets that were modified */
	UPROPERTY(BlueprintReadOnly, Category = "Autonomix")
	TArray<FString> ModifiedAssets;

	/** Backup paths for rollback */
	UPROPERTY(BlueprintReadOnly, Category = "Autonomix")
	TArray<FString> BackupPaths;

	/** SHA-1 hash of the primary affected file/asset BEFORE execution */
	UPROPERTY(BlueprintReadOnly, Category = "Autonomix")
	FString PreStateHash;

	/** SHA-1 hash of the primary affected file/asset AFTER execution */
	UPROPERTY(BlueprintReadOnly, Category = "Autonomix")
	FString PostStateHash;

	/** Timestamp of execution */
	UPROPERTY(BlueprintReadOnly, Category = "Autonomix")
	FDateTime Timestamp;

	/** Execution time in seconds */
	UPROPERTY(BlueprintReadOnly, Category = "Autonomix")
	float ExecutionTimeSeconds = 0.0f;

	FAutonomixActionExecutionRecord()
		: RecordId(FGuid::NewGuid())
		, Timestamp(FDateTime::UtcNow())
	{
	}
};

/** Current editor context snapshot -- what the user is looking at */
USTRUCT(BlueprintType)
struct AUTONOMIXCORE_API FAutonomixEditorContext
{
	GENERATED_BODY()

	/** Name of the currently active level/map */
	UPROPERTY(BlueprintReadOnly, Category = "Autonomix")
	FString ActiveLevelName;

	/** Summary of selected actors in the viewport */
	UPROPERTY(BlueprintReadOnly, Category = "Autonomix")
	FString SelectedActorsSummary;

	/** Summary of selected assets in the content browser */
	UPROPERTY(BlueprintReadOnly, Category = "Autonomix")
	FString SelectedAssetsSummary;

	/** List of currently open editor tabs/windows */
	UPROPERTY(BlueprintReadOnly, Category = "Autonomix")
	TArray<FString> OpenEditors;

	/** Current viewport camera location as string */
	UPROPERTY(BlueprintReadOnly, Category = "Autonomix")
	FString ViewportCameraInfo;

	/** Number of actors in the current level */
	UPROPERTY(BlueprintReadOnly, Category = "Autonomix")
	int32 ActorCount = 0;

	/** Build as a context string for the system prompt */
	FString ToContextString() const
	{
		FString Ctx = TEXT("=== Editor Context ===\n");
		Ctx += FString::Printf(TEXT("Active Level: %s\n"), *ActiveLevelName);
		Ctx += FString::Printf(TEXT("Actor Count: %d\n"), ActorCount);
		if (!SelectedActorsSummary.IsEmpty())
			Ctx += FString::Printf(TEXT("Selected Actors: %s\n"), *SelectedActorsSummary);
		if (!SelectedAssetsSummary.IsEmpty())
			Ctx += FString::Printf(TEXT("Selected Assets: %s\n"), *SelectedAssetsSummary);
		if (OpenEditors.Num() > 0)
		{
			Ctx += TEXT("Open Editors: ");
			Ctx += FString::Join(OpenEditors, TEXT(", "));
			Ctx += TEXT("\n");
		}
		return Ctx;
	}
};

/** HTTP error detail for user-friendly error messaging */
struct AUTONOMIXCORE_API FAutonomixHTTPError
{
	EAutonomixHTTPErrorType Type = EAutonomixHTTPErrorType::None;
	int32 StatusCode = 0;
	FString RawMessage;
	FString UserFriendlyMessage;

	/** Build a user-friendly message from status code.
	 *  ProviderName is used in the error message so users see the correct provider
	 *  (e.g. "Rate limited by Google Gemini" instead of a hardcoded provider name). */
	static FAutonomixHTTPError FromStatusCode(int32 Code, const FString& ResponseBody, const FString& ProviderName = TEXT("API"))
	{
		FAutonomixHTTPError Err;
		Err.StatusCode = Code;
		Err.RawMessage = ResponseBody;

		// Try to extract a clean error message from the JSON response body.
		// All major providers (Anthropic, OpenAI, Google, DeepSeek) return JSON error bodies.
		// Formats: {"error":{"message":"..."}} (OpenAI/Anthropic) or {"error":{"message":"...", "status":"..."}} (Google)
		FString CleanMessage;
		{
			TSharedPtr<FJsonObject> ErrJson;
			TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ResponseBody);
			if (FJsonSerializer::Deserialize(Reader, ErrJson) && ErrJson.IsValid())
			{
				const TSharedPtr<FJsonObject>* ErrorObj = nullptr;
				if (ErrJson->TryGetObjectField(TEXT("error"), ErrorObj))
				{
					(*ErrorObj)->TryGetStringField(TEXT("message"), CleanMessage);
				}
				// Some APIs put message at root level
				if (CleanMessage.IsEmpty())
				{
					ErrJson->TryGetStringField(TEXT("message"), CleanMessage);
				}
			}
		}

		if (Code == 401 || Code == 403)
		{
			// 401 = Unauthorized (Anthropic, OpenAI, DeepSeek, Mistral, xAI)
			// 403 = Forbidden (Google Gemini uses 403 for invalid/disabled API keys)
			Err.Type = EAutonomixHTTPErrorType::Unauthorized;
			if (!CleanMessage.IsEmpty())
			{
				Err.UserFriendlyMessage = FString::Printf(
					TEXT("%s: %s\nCheck your API key in Project Settings > Plugins > Autonomix."), *ProviderName, *CleanMessage);
			}
			else
			{
				Err.UserFriendlyMessage = FString::Printf(
					TEXT("Invalid API key. Check your %s key in Project Settings > Plugins > Autonomix."), *ProviderName);
			}
		}
		else if (Code == 429)
		{
			Err.Type = EAutonomixHTTPErrorType::RateLimited;
			Err.UserFriendlyMessage = FString::Printf(
				TEXT("Rate limited by %s. Please wait a moment before retrying."), *ProviderName);
		}
		else if (Code == 400)
		{
			Err.Type = EAutonomixHTTPErrorType::InvalidResponse;
			FString Detail = !CleanMessage.IsEmpty() ? CleanMessage : ResponseBody.Left(200);
			Err.UserFriendlyMessage = FString::Printf(TEXT("Bad request: %s"), *Detail);
		}
		else if (Code == 404)
		{
			Err.Type = EAutonomixHTTPErrorType::InvalidResponse;
			// Provide Azure-specific guidance if the provider name contains "Azure" or "OpenAI",
			// since Azure is the most common source of 404s — wrong deployment name, missing
			// api-version, or using the standard OpenAI provider instead of the Azure provider.
			// Ported guidance pattern from Roo Code openai.ts _isAzureOpenAI() detection.
			if (ProviderName.Contains(TEXT("Azure")) || ProviderName.Contains(TEXT("OpenAI")))
			{
				Err.UserFriendlyMessage = FString::Printf(
					TEXT("%s endpoint or model not found (HTTP 404).\n\n")
					TEXT("Common causes:\n")
					TEXT("  \u2022 Wrong model/deployment name — Azure uses your deployment name (e.g. \"my-gpt4\"), not the base model ID (e.g. \"gpt-4o\").\n")
					TEXT("  \u2022 Using OpenAI provider with an Azure URL — switch Provider to 'Azure OpenAI' in settings.\n")
					TEXT("  \u2022 Missing or wrong api-version — set Azure API Version (e.g. 2024-02-01).\n")
					TEXT("  \u2022 Wrong base URL format — Azure URL should be: https://{resource}.openai.azure.com\n\n")
					TEXT("Check your settings in Project Settings \u2192 Plugins \u2192 Autonomix \u2192 API | Azure OpenAI."),
					*ProviderName);
			}
			else
			{
				Err.UserFriendlyMessage = FString::Printf(
					TEXT("%s endpoint or model not found (HTTP 404). Check your model ID and base URL in settings."), *ProviderName);
			}
		}
		else if (Code >= 500)
		{
			Err.Type = EAutonomixHTTPErrorType::ServerError;
			Err.UserFriendlyMessage = FString::Printf(
				TEXT("%s server error (HTTP %d). This is temporary -- please retry."), *ProviderName, Code);
		}
		else
		{
			Err.Type = EAutonomixHTTPErrorType::Unknown;
			FString Detail = !CleanMessage.IsEmpty() ? CleanMessage : ResponseBody.Left(200);
			Err.UserFriendlyMessage = FString::Printf(TEXT("%s HTTP %d: %s"), *ProviderName, Code, *Detail);
		}

		return Err;
	}

	static FAutonomixHTTPError ConnectionFailed(const FString& ProviderName = TEXT("API"))
	{
		FAutonomixHTTPError Err;
		Err.Type = EAutonomixHTTPErrorType::NetworkError;
		Err.UserFriendlyMessage = FString::Printf(
			TEXT("Could not connect to %s. Check your internet connection."), *ProviderName);
		return Err;
	}

	static FAutonomixHTTPError TimedOut()
	{
		FAutonomixHTTPError Err;
		Err.Type = EAutonomixHTTPErrorType::Timeout;
		Err.UserFriendlyMessage = TEXT("Request timed out. Try again or increase timeout in settings.");
		return Err;
	}
};

// ============================================================================
// Todo / Task Tracking Types
// ============================================================================

/** Status of a todo item (mirrors Roo Code's TodoStatus) */
UENUM(BlueprintType)
enum class EAutonomixTodoStatus : uint8
{
	Pending		UMETA(DisplayName = "Pending"),
	InProgress	UMETA(DisplayName = "In Progress"),
	Completed	UMETA(DisplayName = "Completed")
};

/** A single todo/task item for tracking progress through complex tasks.
 *  Modeled after Roo Code's TodoItem interface. */
USTRUCT(BlueprintType)
struct AUTONOMIXCORE_API FAutonomixTodoItem
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Autonomix", Meta = (IgnoreForMemberInitializationTest))
	FString Id;

	UPROPERTY(BlueprintReadOnly, Category = "Autonomix")
	FString Content;

	UPROPERTY(BlueprintReadOnly, Category = "Autonomix")
	EAutonomixTodoStatus Status = EAutonomixTodoStatus::Pending;

	FAutonomixTodoItem() { Id = FGuid::NewGuid().ToString(); }
	FAutonomixTodoItem(const FString& InContent, EAutonomixTodoStatus InStatus = EAutonomixTodoStatus::Pending)
		: Content(InContent), Status(InStatus)
	{
		Id = FGuid::NewGuid().ToString();
	}

	/** Parse status string from tool input: "pending", "in_progress", "completed" */
	static EAutonomixTodoStatus ParseStatus(const FString& StatusStr)
	{
		if (StatusStr.Equals(TEXT("in_progress"), ESearchCase::IgnoreCase)) return EAutonomixTodoStatus::InProgress;
		if (StatusStr.Equals(TEXT("completed"), ESearchCase::IgnoreCase)) return EAutonomixTodoStatus::Completed;
		return EAutonomixTodoStatus::Pending;
	}

	static FString StatusToString(EAutonomixTodoStatus S)
	{
		switch (S)
		{
		case EAutonomixTodoStatus::InProgress: return TEXT("in_progress");
		case EAutonomixTodoStatus::Completed:  return TEXT("completed");
		default:                               return TEXT("pending");
		}
	}
};

/** Token usage info from Claude response headers */
USTRUCT(BlueprintType)
struct AUTONOMIXCORE_API FAutonomixTokenUsage
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Autonomix")
	int32 InputTokens = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Autonomix")
	int32 OutputTokens = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Autonomix")
	int32 CacheCreationInputTokens = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Autonomix")
	int32 CacheReadInputTokens = 0;

	int32 TotalTokens() const { return InputTokens + OutputTokens; }
};

// ============================================================================
// Delegates
// ============================================================================

/** Broadcast when a new chat message is added */
DECLARE_MULTICAST_DELEGATE_OneParam(FOnAutonomixMessageAdded, const FAutonomixMessage&);

/** Broadcast when streaming text is received */
DECLARE_MULTICAST_DELEGATE_TwoParams(FOnAutonomixStreamingText, const FGuid& /*MessageId*/, const FString& /*DeltaText*/);

/** Broadcast when a tool call is received from the AI */
DECLARE_MULTICAST_DELEGATE_OneParam(FOnAutonomixToolCallReceived, const FAutonomixToolCall&);

/** Broadcast when an action plan is ready for preview */
DECLARE_MULTICAST_DELEGATE_OneParam(FOnAutonomixPlanReady, const FAutonomixActionPlan&);

/** Broadcast when an action completes */
DECLARE_MULTICAST_DELEGATE_OneParam(FOnAutonomixActionCompleted, const FAutonomixActionResult&);

/** Broadcast when the AI request starts/completes */
DECLARE_MULTICAST_DELEGATE(FOnAutonomixRequestStarted);
DECLARE_MULTICAST_DELEGATE_OneParam(FOnAutonomixRequestCompleted, bool /*bSuccess*/);

/** Broadcast when an HTTP error is received -- includes user-friendly message */
DECLARE_MULTICAST_DELEGATE_OneParam(FOnAutonomixErrorReceived, const FAutonomixHTTPError& /*Error*/);

/** Broadcast with token usage info after each response */
DECLARE_MULTICAST_DELEGATE_OneParam(FOnAutonomixTokenUsageUpdated, const FAutonomixTokenUsage& /*Usage*/);
