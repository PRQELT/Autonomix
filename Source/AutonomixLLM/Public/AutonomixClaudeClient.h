// Copyright Autonomix. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AutonomixInterfaces.h"
#include "AutonomixTypes.h"
#include "AutonomixSSEParser.h"
#include "Interfaces/IHttpRequest.h"

/**
 * Claude API client implementing the IAutonomixLLMClient interface.
 *
 * v2.3: Added context window exceeded error handling.
 *   When Claude returns HTTP 400 with "context_length_exceeded" (or equivalent),
 *   the client fires OnContextWindowExceeded delegate instead of a generic error.
 *   The owner (SAutonomixMainPanel) is responsible for calling ForcedContextReduction
 *   on the ContextManager and re-sending. The client tracks retry count.
 *
 * v2.2: Raw byte buffering, rate limit backoff, stop_reason handling,
 *   bRequestCancelled race-condition guard.
 */
class AUTONOMIXLLM_API FAutonomixClaudeClient : public IAutonomixLLMClient, public TSharedFromThis<FAutonomixClaudeClient>
{
public:
	// Maximum automatic context-window retries before surfacing as a hard error
	static constexpr int32 MaxContextWindowRetries = 3;

	// Fraction of messages to keep on context window overflow (keep 75%, remove 25%)
	static constexpr float ContextWindowForcedKeepFraction = 0.75f;

	FAutonomixClaudeClient();
	virtual ~FAutonomixClaudeClient();

	// IAutonomixLLMClient interface
	virtual void SendMessage(
		const TArray<FAutonomixMessage>& ConversationHistory,
		const FString& SystemPrompt,
		const TArray<TSharedPtr<FJsonObject>>& ToolSchemas
	) override;
	virtual void CancelRequest() override;
	virtual bool IsRequestInFlight() const override;
	/** Override: returns this — safe typed downcast without dynamic_cast (RTTI disabled in UE). */
	virtual FAutonomixClaudeClient* AsClaudeClient() override { return this; }

	virtual FOnAutonomixStreamingText& OnStreamingText() override { return StreamingTextDelegate; }
	virtual FOnAutonomixToolCallReceived& OnToolCallReceived() override { return ToolCallReceivedDelegate; }
	virtual FOnAutonomixMessageAdded& OnMessageComplete() override { return MessageCompleteDelegate; }
	virtual FOnAutonomixRequestStarted& OnRequestStarted() override { return RequestStartedDelegate; }
	virtual FOnAutonomixRequestCompleted& OnRequestCompleted() override { return RequestCompletedDelegate; }
	virtual FOnAutonomixErrorReceived& OnErrorReceived() override { return ErrorReceivedDelegate; }
	virtual FOnAutonomixTokenUsageUpdated& OnTokenUsageUpdated() override { return TokenUsageUpdatedDelegate; }

	void SetEndpoint(const FString& InEndpoint);
	void SetApiKey(const FString& InApiKey);
	void SetModel(const FString& InModel);
	void SetMaxTokens(int32 InMaxTokens);

	const TArray<FAutonomixToolCall>& GetPendingToolCalls() const { return PendingToolCalls; }
	const FAutonomixTokenUsage& GetLastTokenUsage() const { return LastTokenUsage; }

	/**
	 * Context window overflow delegate.
	 * Fired when HTTP 400 with context_length_exceeded is received.
	 * Payload = number of retries already attempted.
	 * The owner MUST respond by calling TrimHistoryAndRetry() or CancelRequest().
	 */
	DECLARE_DELEGATE_OneParam(FOnContextWindowExceeded, int32 /*RetryCount*/);
	FOnContextWindowExceeded OnContextWindowExceeded;

	/**
	 * Called by the owner after trimming history to retry the request.
	 * Resends the same request with the provided trimmed history.
	 * Only valid to call from within the OnContextWindowExceeded handler.
	 */
	void RetryWithTrimmedHistory(const TArray<FAutonomixMessage>& TrimmedHistory);

	/** Current context window retry count (reset on new SendMessage call) */
	int32 GetContextWindowRetryCount() const { return ContextWindowRetryCount; }

private:
	/**
	 * Internal send implementation that does NOT reset ContextWindowRetryCount.
	 * Called by RetryWithTrimmedHistory and ScheduleRetryWithBackoff so that
	 * retry counters are preserved across retry calls.
	 * The public SendMessage() resets ContextWindowRetryCount then delegates here.
	 */
	void SendMessageInternal(
		const TArray<FAutonomixMessage>& ConversationHistory,
		const FString& SystemPrompt,
		const TArray<TSharedPtr<FJsonObject>>& ToolSchemas
	);

	TSharedPtr<FJsonObject> BuildRequestBody(
		const TArray<FAutonomixMessage>& ConversationHistory,
		const FString& SystemPrompt,
		const TArray<TSharedPtr<FJsonObject>>& ToolSchemas
	) const;

	TArray<TSharedPtr<FJsonValue>> ConvertMessagesToJson(
		const TArray<FAutonomixMessage>& Messages
	) const;

	void HandleRequestProgress(FHttpRequestPtr Request, uint64 BytesSent, uint64 BytesReceived);
	void HandleRequestComplete(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bConnectedSuccessfully);
	void ProcessSSEEvents(const TArray<FAutonomixSSEEvent>& Events);
	void HandleSSEEvent(const FAutonomixSSEEvent& Event);
	void ExtractTokenUsage(const TSharedPtr<FJsonObject>& JsonData);
	void FinalizeResponse();
	void ScheduleRetryWithBackoff();

	/**
	 * Detect context window exceeded error from HTTP 400 response body.
	 * Anthropic sends: {"type":"error","error":{"type":"invalid_request_error","message":"..."}}
	 * where message contains "context" or "tokens" keywords.
	 */
	bool IsContextWindowExceededError(int32 HttpCode, const FString& ResponseBody) const;

	// Configuration
	FString Endpoint;
	FString ApiKey;
	FString Model;
	int32 MaxTokens;

	// Request state
	TSharedPtr<IHttpRequest, ESPMode::ThreadSafe> CurrentRequest;
	bool bRequestInFlight;
	bool bRequestCancelled;  // CRITICAL: Guards against race condition in HandleRequestComplete

	// SSE parsing
	FAutonomixSSEParser SSEParser;
	int32 LastBytesReceived;
	TArray<uint8> RawByteBuffer;

	// Response accumulation
	FGuid CurrentMessageId;
	FString CurrentAssistantContent;
	TArray<FAutonomixToolCall> PendingToolCalls;
	FAutonomixToolCall CurrentToolCall;
	FString CurrentToolCallInput;
	bool bBuildingToolCall;
	FAutonomixTokenUsage LastTokenUsage;

	// stop_reason tracking — detects max_tokens truncation
	FString LastStopReason;

	// Rate limit backoff state
	int32 ConsecutiveRateLimits;
	TArray<FAutonomixMessage> PendingRetryHistory;
	FString PendingRetrySystemPrompt;
	TArray<TSharedPtr<FJsonObject>> PendingRetryToolSchemas;

	// Context window exceeded retry state
	int32 ContextWindowRetryCount;

	// Delegates
	FOnAutonomixStreamingText StreamingTextDelegate;
	FOnAutonomixToolCallReceived ToolCallReceivedDelegate;
	FOnAutonomixMessageAdded MessageCompleteDelegate;
	FOnAutonomixRequestStarted RequestStartedDelegate;
	FOnAutonomixRequestCompleted RequestCompletedDelegate;
	FOnAutonomixErrorReceived ErrorReceivedDelegate;
	FOnAutonomixTokenUsageUpdated TokenUsageUpdatedDelegate;
};
