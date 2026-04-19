// Copyright Autonomix. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AutonomixInterfaces.h"
#include "AutonomixTypes.h"
#include "AutonomixSSEParser.h"
#include "Interfaces/IHttpRequest.h"

/**
 * OpenAI-compatible API client.
 *
 * Covers: OpenAI (GPT-5.x, GPT-4.1, o3, o4-mini), DeepSeek, Mistral, xAI (Grok),
 *         OpenRouter, Ollama, LM Studio, and any custom OpenAI-compatible endpoint.
 *
 * Wire format:
 *   - OpenAI native: POST /v1/responses (Responses API) — required for GPT-5.x + tools
 *   - All others:    POST /v1/chat/completions (Chat Completions API)
 *
 * SSE streaming: "data: {json}" events, terminated by "data: [DONE]".
 *
 * Adapted from Roo Code's openai-native.ts (Responses API) + base-openai-compatible-provider.ts
 */
class AUTONOMIXLLM_API FAutonomixOpenAICompatClient : public IAutonomixLLMClient
{
public:
	FAutonomixOpenAICompatClient();
	virtual ~FAutonomixOpenAICompatClient();

	// IAutonomixLLMClient interface
	virtual void SendMessage(
		const TArray<FAutonomixMessage>& ConversationHistory,
		const FString& SystemPrompt,
		const TArray<TSharedPtr<FJsonObject>>& ToolSchemas
	) override;
	virtual void CancelRequest() override;
	virtual bool IsRequestInFlight() const override;
	virtual FOnAutonomixStreamingText& OnStreamingText() override { return StreamingTextDelegate; }
	virtual FOnAutonomixToolCallReceived& OnToolCallReceived() override { return ToolCallReceivedDelegate; }
	virtual FOnAutonomixMessageAdded& OnMessageComplete() override { return MessageCompleteDelegate; }
	virtual FOnAutonomixRequestStarted& OnRequestStarted() override { return RequestStartedDelegate; }
	virtual FOnAutonomixRequestCompleted& OnRequestCompleted() override { return RequestCompletedDelegate; }
	virtual FOnAutonomixErrorReceived& OnErrorReceived() override { return ErrorReceivedDelegate; }
	virtual FOnAutonomixTokenUsageUpdated& OnTokenUsageUpdated() override { return TokenUsageUpdatedDelegate; }

	// Configuration
	void SetEndpoint(const FString& InBaseUrl);
	void SetApiKey(const FString& InApiKey);
	void SetModel(const FString& InModelId);
	void SetProvider(EAutonomixProvider InProvider);
	void SetMaxTokens(int32 InMaxTokens);

	/** Set reasoning effort for o-series / DeepSeek-R1 models.
	 *  EAutonomixReasoningEffort::Disabled = no reasoning_effort field sent (GPT-4o etc.) */
	void SetReasoningEffort(EAutonomixReasoningEffort InEffort);

	/** Enable/disable streaming (default: enabled) */
	void SetStreamingEnabled(bool bEnabled);

	/** Set the Ollama num_ctx value (context window size in tokens).
	 *  When > 0, injected into the request body as {"options":{"num_ctx": N}}.
	 *  Ollama defaults to 2048 if not set, which causes instant overflow with Autonomix's prompts. */
	void SetOllamaContextSize(int32 InNumCtx);

	/**
	 * Set the Azure OpenAI API version string (e.g. "2024-02-01").
	 * When non-empty, switches to Azure wire format:
	 *   - 'api-key' auth header instead of 'Authorization: Bearer'
	 *   - ?api-version= query parameter appended to URL
	 *   - Chat Completions API only (Responses API is NOT supported by Azure)
	 *   - URL path: {base}/openai/deployments/{deployment}/chat/completions?api-version=...
	 * Ported from Roo Code openai.ts AzureOpenAI client detection + _isAzureOpenAI().
	 */
	void SetAzureApiVersion(const FString& InApiVersion);

	const FAutonomixTokenUsage& GetLastTokenUsage() const { return LastTokenUsage; }

private:
	/** Build the request body — dispatches to Responses API or Chat Completions based on bUseResponsesAPI */
	TSharedPtr<FJsonObject> BuildRequestBody(
		const TArray<FAutonomixMessage>& History,
		const FString& SystemPrompt,
		const TArray<TSharedPtr<FJsonObject>>& ToolSchemas
	) const;

	// ---- Chat Completions API (legacy providers) ----

	/** Build POST /v1/chat/completions request body */
	TSharedPtr<FJsonObject> BuildChatCompletionsBody(
		const TArray<FAutonomixMessage>& History,
		const FString& SystemPrompt,
		const TArray<TSharedPtr<FJsonObject>>& ToolSchemas
	) const;

	/** Convert Autonomix history to OpenAI messages array */
	TArray<TSharedPtr<FJsonValue>> ConvertMessagesToJson(
		const TArray<FAutonomixMessage>& Messages,
		const FString& SystemPrompt
	) const;

	/** Convert Autonomix tool schemas to OpenAI Chat Completions tools format */
	TArray<TSharedPtr<FJsonValue>> ConvertToolSchemas(
		const TArray<TSharedPtr<FJsonObject>>& ToolSchemas
	) const;

	// ---- Responses API (OpenAI native, GPT-5.x) ----

	/** Build POST /v1/responses request body (Roo Code openai-native.ts) */
	TSharedPtr<FJsonObject> BuildResponsesAPIBody(
		const TArray<FAutonomixMessage>& History,
		const FString& SystemPrompt,
		const TArray<TSharedPtr<FJsonObject>>& ToolSchemas
	) const;

	/** Convert history to Responses API input array format */
	TArray<TSharedPtr<FJsonValue>> ConvertToResponsesInput(
		const TArray<FAutonomixMessage>& Messages
	) const;

	/** Process a Responses API SSE event (different event types from Chat Completions) */
	void ProcessResponsesSSEEvent(const FString& DataJson);

	/** Convert EAutonomixReasoningEffort to API string ("low"/"medium"/"high") */
	static FString ReasoningEffortToString(EAutonomixReasoningEffort Effort);

	// HTTP handlers
	void HandleRequestProgress(FHttpRequestPtr Request, uint64 BytesSent, uint64 BytesReceived);
	void HandleRequestComplete(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bConnected);

	// SSE processing
	void ProcessSSEChunk(const FString& RawData);
	void ProcessSSEEvent(const FString& DataJson);
	void ParseToolCallDelta(const TSharedPtr<FJsonObject>& Delta, int32 ToolCallIndex);
	void FlushPendingToolCall();
	void FinalizeResponse();

	// Token usage
	void ExtractTokenUsage(const TSharedPtr<FJsonObject>& UsageObj);

	// Configuration
	FString BaseUrl;
	FString ApiKey;
	FString ModelId;
	EAutonomixProvider Provider;
	int32 MaxTokens;
	EAutonomixReasoningEffort ReasoningEffort;
	bool bStreamingEnabled;
	bool bUseResponsesAPI = false;  // Set per-request: true for OpenAI native (Responses API)

	/** Ollama num_ctx context size. When > 0, injected into the request body. */
	int32 OllamaNumCtx = 0;

	/** Azure API version string (e.g. "2024-02-01").
	 *  When non-empty, Azure wire format is used: 'api-key' header, ?api-version= param,
	 *  Chat Completions only (no Responses API). Empty = standard OpenAI wire format.
	 *  Ported from Roo Code openai.ts azureOpenAiDefaultApiVersion usage. */
	FString AzureApiVersion;

	/** True if the current request is using Azure wire format.
	 *  Set in SendMessage() based on Provider == Azure OR auto-detected Azure URL.
	 *  When true: uses 'api-key' header, appends ?api-version=..., forces Chat Completions. */
	bool bIsAzureRequest = false;

	// Request state
	TSharedPtr<IHttpRequest, ESPMode::ThreadSafe> CurrentRequest;
	bool bRequestInFlight;
	bool bRequestCancelled;

	// SSE parsing
	FString SSELineBuffer;
	int32 LastBytesReceived;
	TArray<uint8> RawByteBuffer;

	// Response accumulation
	FGuid CurrentMessageId;
	FString CurrentAssistantContent;
	FString CurrentReasoningContent;  // DeepSeek reasoning_content accumulator
	FAutonomixTokenUsage LastTokenUsage;

	// Tool call accumulation (OpenAI sends tool calls as streaming deltas)
	// OpenAI tool call format: {id, type, function: {name, arguments_delta}}
	struct FPendingToolCallState
	{
		int32 Index = -1;
		FString ToolUseId;
		FString ToolName;
		FString ArgumentsAccumulated;
	};
	TArray<FPendingToolCallState> PendingToolCallStates;

	// Rate limit backoff
	int32 ConsecutiveRateLimits;
	TArray<FAutonomixMessage> RetryHistory;
	FString RetrySystemPrompt;
	TArray<TSharedPtr<FJsonObject>> RetryToolSchemas;

	// Delegates
	FOnAutonomixStreamingText StreamingTextDelegate;
	FOnAutonomixToolCallReceived ToolCallReceivedDelegate;
	FOnAutonomixMessageAdded MessageCompleteDelegate;
	FOnAutonomixRequestStarted RequestStartedDelegate;
	FOnAutonomixRequestCompleted RequestCompletedDelegate;
	FOnAutonomixErrorReceived ErrorReceivedDelegate;
	FOnAutonomixTokenUsageUpdated TokenUsageUpdatedDelegate;
};
