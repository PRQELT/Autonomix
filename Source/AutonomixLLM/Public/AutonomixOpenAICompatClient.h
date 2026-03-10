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
 * Covers: OpenAI (GPT-4o, GPT-4.1, o3, o4-mini), DeepSeek, Mistral, xAI (Grok),
 *         OpenRouter, Ollama, LM Studio, and any custom OpenAI-compatible endpoint.
 *
 * Wire format: POST /v1/chat/completions (OpenAI Chat Completions API).
 * SSE streaming: "data: {json}" events, terminated by "data: [DONE]".
 *
 * Reasoning models (o3, o4-mini, deepseek-reasoner):
 *   - Set reasoning_effort field to "low"/"medium"/"high" instead of temperature
 *   - DeepSeek-R1 uses <think>...</think> tags in content stream
 *
 * Adapted from Roo Code's AnthropicHandler → OpenAI-compatible path.
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

	const FAutonomixTokenUsage& GetLastTokenUsage() const { return LastTokenUsage; }

private:
	/** Build the POST /v1/chat/completions request body */
	TSharedPtr<FJsonObject> BuildRequestBody(
		const TArray<FAutonomixMessage>& History,
		const FString& SystemPrompt,
		const TArray<TSharedPtr<FJsonObject>>& ToolSchemas
	) const;

	/** Convert Autonomix history to OpenAI messages array */
	TArray<TSharedPtr<FJsonValue>> ConvertMessagesToJson(
		const TArray<FAutonomixMessage>& Messages,
		const FString& SystemPrompt
	) const;

	/** Convert Autonomix tool schemas to OpenAI tools array */
	TArray<TSharedPtr<FJsonValue>> ConvertToolSchemas(
		const TArray<TSharedPtr<FJsonObject>>& ToolSchemas
	) const;

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
