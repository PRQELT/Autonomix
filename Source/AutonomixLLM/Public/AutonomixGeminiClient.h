// Copyright Autonomix. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AutonomixInterfaces.h"
#include "AutonomixTypes.h"
#include "Interfaces/IHttpRequest.h"

/**
 * Google Gemini API client.
 *
 * Wire format: POST generativelanguage.googleapis.com/v1beta/models/{model}:streamGenerateContent
 * Authentication: ?key=<API_KEY> query parameter (Google AI Studio) or Bearer token (Vertex).
 *
 * Supports:
 *   - Gemini 2.5 Pro/Flash: thinking budget (thinkingConfig.thinkingBudget)
 *   - Gemini 3.x: reasoning effort (thinkingConfig.thoughtsConfig.reasoningEffort = LOW/MEDIUM/HIGH)
 *   - Tool calling via functionDeclarations (maps to Autonomix tool schemas)
 *   - Streaming via SSE (ndjson chunks with generationComplete)
 *
 * Adapted from Roo Code's GeminiHandler — adapted for C++ UE5 HTTP pipeline.
 */
class AUTONOMIXLLM_API FAutonomixGeminiClient : public IAutonomixLLMClient
{
public:
	FAutonomixGeminiClient();
	virtual ~FAutonomixGeminiClient();

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
	void SetApiKey(const FString& InApiKey);
	void SetModel(const FString& InModelId);
	void SetBaseUrl(const FString& InBaseUrl);  // empty = official API
	void SetMaxTokens(int32 InMaxTokens);
	void SetThinkingBudget(int32 InBudgetTokens); // 0 = disable, >0 = budget (Gemini 2.5)
	void SetReasoningEffort(EAutonomixReasoningEffort InEffort); // Gemini 3.x

	const FAutonomixTokenUsage& GetLastTokenUsage() const { return LastTokenUsage; }

private:
	/** Build the generateContent request body */
	TSharedPtr<FJsonObject> BuildRequestBody(
		const TArray<FAutonomixMessage>& History,
		const FString& SystemPrompt,
		const TArray<TSharedPtr<FJsonObject>>& ToolSchemas
	) const;

	/** Convert history to Gemini 'contents' format */
	TArray<TSharedPtr<FJsonValue>> ConvertContents(const TArray<FAutonomixMessage>& Messages) const;

	/** Convert tool schemas to Gemini functionDeclarations */
	TSharedPtr<FJsonObject> ConvertToolSchemas(const TArray<TSharedPtr<FJsonObject>>& ToolSchemas) const;

	/** Reasoning effort → Gemini API string (LOW/MEDIUM/HIGH) */
	static FString ReasoningEffortToGeminiString(EAutonomixReasoningEffort Effort);

	// HTTP handlers
	void HandleRequestProgress(FHttpRequestPtr Request, uint64 BytesSent, uint64 BytesReceived);
	void HandleRequestComplete(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bConnected);

	/** Process a single ndjson line from the streaming response */
	void ProcessResponseChunk(const FString& JsonLine);

	/** Extract parts from a Gemini candidates[0].content.parts array */
	void ProcessCandidateParts(const TArray<TSharedPtr<FJsonValue>>& Parts);

	void ExtractTokenUsage(const TSharedPtr<FJsonObject>& UsageMetadata);
	void FinalizeResponse();

	// Configuration
	FString ApiKey;
	FString ModelId;
	FString BaseUrl;     // empty = https://generativelanguage.googleapis.com
	int32 MaxTokens;
	int32 ThinkingBudgetTokens; // 0 = disabled
	EAutonomixReasoningEffort ReasoningEffort;

	// Request state
	TSharedPtr<IHttpRequest, ESPMode::ThreadSafe> CurrentRequest;
	bool bRequestInFlight;
	bool bRequestCancelled;
	FString NdjsonLineBuffer;
	int32 LastBytesReceived;

	// Response accumulation
	FGuid CurrentMessageId;
	FString CurrentAssistantContent;
	FAutonomixTokenUsage LastTokenUsage;

	// Pending tool calls from functionCall parts
	TArray<FAutonomixToolCall> PendingToolCalls;

	// Delegates
	FOnAutonomixStreamingText StreamingTextDelegate;
	FOnAutonomixToolCallReceived ToolCallReceivedDelegate;
	FOnAutonomixMessageAdded MessageCompleteDelegate;
	FOnAutonomixRequestStarted RequestStartedDelegate;
	FOnAutonomixRequestCompleted RequestCompletedDelegate;
	FOnAutonomixErrorReceived ErrorReceivedDelegate;
	FOnAutonomixTokenUsageUpdated TokenUsageUpdatedDelegate;
};
