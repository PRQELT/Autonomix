// Copyright Autonomix. All Rights Reserved.

#include "AutonomixClaudeClient.h"
#include "AutonomixCoreModule.h"
#include "AutonomixSettings.h"
#include "AutonomixToolResultValidator.h"
#include "HttpModule.h"
#include "Interfaces/IHttpResponse.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Async/Async.h"

static const FString DefaultEndpoint = TEXT("https://api.anthropic.com/v1/messages");
static const FString AnthropicVersion = TEXT("2023-06-01");

FAutonomixClaudeClient::FAutonomixClaudeClient()
	: Endpoint(DefaultEndpoint)
	, Model(TEXT("claude-sonnet-4-6"))
	, MaxTokens(8192)
	, bRequestInFlight(false)
	, bRequestCancelled(false)
	, LastBytesReceived(0)
	, bBuildingToolCall(false)
	, ConsecutiveRateLimits(0)
	, ContextWindowRetryCount(0)
{
}

FAutonomixClaudeClient::~FAutonomixClaudeClient()
{
	CancelRequest();
}

void FAutonomixClaudeClient::SendMessage(
	const TArray<FAutonomixMessage>& ConversationHistory,
	const FString& SystemPrompt,
	const TArray<TSharedPtr<FJsonObject>>& ToolSchemas)
{
	// Reset context window retry count on new top-level SendMessage call only.
	// RetryWithTrimmedHistory calls SendMessageInternal to avoid resetting this counter.
	ContextWindowRetryCount = 0;
	SendMessageInternal(ConversationHistory, SystemPrompt, ToolSchemas);
}

void FAutonomixClaudeClient::SendMessageInternal(
	const TArray<FAutonomixMessage>& ConversationHistory,
	const FString& SystemPrompt,
	const TArray<TSharedPtr<FJsonObject>>& ToolSchemas)
{
	if (bRequestInFlight)
	{
		UE_LOG(LogAutonomix, Warning, TEXT("ClaudeClient: Request already in flight. Ignoring new request."));
		return;
	}

	if (ApiKey.IsEmpty())
	{
		UE_LOG(LogAutonomix, Error, TEXT("ClaudeClient: API key is not set. Cannot send request."));
		ErrorReceivedDelegate.Broadcast(FAutonomixHTTPError::ConnectionFailed(TEXT("Anthropic")));
		RequestCompletedDelegate.Broadcast(false);
		return;
	}

	// Store for potential retry on rate limit or context window exceeded
	PendingRetryHistory = ConversationHistory;
	PendingRetrySystemPrompt = SystemPrompt;
	PendingRetryToolSchemas = ToolSchemas;

	const UAutonomixDeveloperSettings* Settings = UAutonomixDeveloperSettings::Get();

	TSharedPtr<FJsonObject> RequestBody = BuildRequestBody(ConversationHistory, SystemPrompt, ToolSchemas);
	if (!RequestBody.IsValid())
	{
		UE_LOG(LogAutonomix, Error, TEXT("ClaudeClient: Failed to build request body."));
		RequestCompletedDelegate.Broadcast(false);
		return;
	}

	FString RequestBodyString;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&RequestBodyString);
	FJsonSerializer::Serialize(RequestBody.ToSharedRef(), Writer);

	CurrentRequest = FHttpModule::Get().CreateRequest();
	CurrentRequest->SetURL(Endpoint);
	CurrentRequest->SetVerb(TEXT("POST"));
	CurrentRequest->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
	CurrentRequest->SetHeader(TEXT("x-api-key"), ApiKey);
	CurrentRequest->SetHeader(TEXT("anthropic-version"), AnthropicVersion);

	// Build beta flags — Roo Code pattern: always include fine-grained-tool-streaming,
	// conditionally add context-1m and interleaved-thinking based on model + settings.
	FString BetaFlags = TEXT("fine-grained-tool-streaming-2025-05-14");

	// 1M context beta (matches Roo Code anthropic.ts 'context-1m-2025-08-07' gate)
	// Models: claude-sonnet-4-6, claude-sonnet-4-5, claude-sonnet-4-20250514, claude-opus-4-6
	if (Settings && Settings->ContextWindow == EAutonomixContextWindow::Extended_1M)
	{
		if (Model.Contains(TEXT("sonnet-4-6")) || Model.Contains(TEXT("sonnet-4-5")) ||
			Model.Contains(TEXT("sonnet-4-20250514")) || Model.Contains(TEXT("opus-4-6")))
		{
			BetaFlags += TEXT(",context-1m-2025-08-07");
			UE_LOG(LogAutonomix, Log, TEXT("ClaudeClient: 1M context beta enabled for %s"), *Model);
		}
	}

	// Extended thinking — claude-3-7-sonnet requires interleaved-thinking beta
	if (Settings && Settings->bEnableExtendedThinking && Model.Contains(TEXT("3-7-sonnet")))
	{
		BetaFlags += TEXT(",interleaved-thinking-2025-05-14");
	}

	CurrentRequest->SetHeader(TEXT("anthropic-beta"), BetaFlags);
	CurrentRequest->SetContentAsString(RequestBodyString);

	if (Settings)
	{
		CurrentRequest->SetTimeout(Settings->RequestTimeoutSeconds);
	}

	CurrentRequest->OnRequestProgress64().BindRaw(this, &FAutonomixClaudeClient::HandleRequestProgress);
	CurrentRequest->OnProcessRequestComplete().BindRaw(this, &FAutonomixClaudeClient::HandleRequestComplete);

	// Reset streaming state
	SSEParser.Reset();
	LastBytesReceived = 0;
	RawByteBuffer.Empty();
	CurrentAssistantContent.Empty();
	PendingToolCalls.Empty();
	CurrentToolCall = FAutonomixToolCall();
	CurrentToolCallInput.Empty();
	bBuildingToolCall = false;
	bRequestCancelled = false;
	CurrentMessageId = FGuid::NewGuid();
	LastTokenUsage = FAutonomixTokenUsage();
	LastStopReason.Empty();

	bRequestInFlight = true;
	RequestStartedDelegate.Broadcast();
	CurrentRequest->ProcessRequest();

	UE_LOG(LogAutonomix, Log, TEXT("ClaudeClient: Request sent to %s with model %s"), *Endpoint, *Model);
}

void FAutonomixClaudeClient::CancelRequest()
{
	if (CurrentRequest.IsValid() && bRequestInFlight)
	{
		// CRITICAL (Gemini): Set bRequestCancelled BEFORE calling CancelRequest()
		// to prevent the race condition where HandleRequestComplete fires one last time
		bRequestCancelled = true;
		CurrentRequest->CancelRequest();
		bRequestInFlight = false;
		UE_LOG(LogAutonomix, Log, TEXT("ClaudeClient: Request cancelled by user."));
		RequestCompletedDelegate.Broadcast(false);
	}
}

bool FAutonomixClaudeClient::IsRequestInFlight() const
{
	return bRequestInFlight;
}

void FAutonomixClaudeClient::SetEndpoint(const FString& InEndpoint) { Endpoint = InEndpoint; }
void FAutonomixClaudeClient::SetApiKey(const FString& InApiKey) { ApiKey = InApiKey; }
void FAutonomixClaudeClient::SetModel(const FString& InModel) { Model = InModel; }
void FAutonomixClaudeClient::SetMaxTokens(int32 InMaxTokens) { MaxTokens = FMath::Clamp(InMaxTokens, 1, 200000); }

// ============================================================================
// Request Body Construction
// ============================================================================

TSharedPtr<FJsonObject> FAutonomixClaudeClient::BuildRequestBody(
	const TArray<FAutonomixMessage>& ConversationHistory,
	const FString& SystemPrompt,
	const TArray<TSharedPtr<FJsonObject>>& ToolSchemas) const
{
	TSharedPtr<FJsonObject> Body = MakeShared<FJsonObject>();
	const UAutonomixDeveloperSettings* Settings = UAutonomixDeveloperSettings::Get();

	Body->SetStringField(TEXT("model"), Model);
	Body->SetNumberField(TEXT("max_tokens"), MaxTokens);
	Body->SetBoolField(TEXT("stream"), true);

	if (!SystemPrompt.IsEmpty())
	{
		Body->SetStringField(TEXT("system"), SystemPrompt);
	}

	// Extended Thinking -- with validated constraints
	if (Settings && Settings->bEnableExtendedThinking)
	{
		// Enforce: budget_tokens >= 1024, max_tokens > budget_tokens
		int32 SafeBudget = FMath::Max(1024, Settings->ThinkingBudgetTokens);
		if (MaxTokens <= SafeBudget)
		{
			UE_LOG(LogAutonomix, Warning, TEXT("ClaudeClient: max_tokens (%d) must be > budget_tokens (%d). Adjusting."),
				MaxTokens, SafeBudget);
		}

		TSharedPtr<FJsonObject> ThinkingObj = MakeShared<FJsonObject>();
		ThinkingObj->SetStringField(TEXT("type"), TEXT("enabled"));
		ThinkingObj->SetNumberField(TEXT("budget_tokens"), SafeBudget);
		Body->SetObjectField(TEXT("thinking"), ThinkingObj);
	}

	TArray<TSharedPtr<FJsonValue>> MessagesArray = ConvertMessagesToJson(ConversationHistory);

	// CRITICAL: Validate and fix tool_result IDs before sending to Claude.
	// This prevents HTTP 400 "unexpected tool_use_id" errors caused by:
	// - ID mismatches between tool_use and tool_result blocks
	// - Duplicate tool_results
	// - Orphaned tool_use blocks without results (e.g. from interrupted executions)
	FAutonomixToolResultValidator::ValidateAndFixToolResults(MessagesArray);

	Body->SetArrayField(TEXT("messages"), MessagesArray);

	if (ToolSchemas.Num() > 0)
	{
		TArray<TSharedPtr<FJsonValue>> ToolsArray;
		for (const auto& Schema : ToolSchemas)
		{
			ToolsArray.Add(MakeShared<FJsonValueObject>(Schema));
		}
		Body->SetArrayField(TEXT("tools"), ToolsArray);
	}

	return Body;
}

TArray<TSharedPtr<FJsonValue>> FAutonomixClaudeClient::ConvertMessagesToJson(
	const TArray<FAutonomixMessage>& Messages) const
{
	TArray<TSharedPtr<FJsonValue>> Result;

	int32 i = 0;
	while (i < Messages.Num())
	{
		const FAutonomixMessage& Msg = Messages[i];

		// Skip truncation markers -- they're internal tracking only
		if (Msg.bIsTruncationMarker)
		{
			i++;
			continue;
		}

		if (Msg.Role == EAutonomixMessageRole::ToolResult)
		{
			TSharedPtr<FJsonObject> UserObj = MakeShared<FJsonObject>();
			UserObj->SetStringField(TEXT("role"), TEXT("user"));
			TArray<TSharedPtr<FJsonValue>> ContentArray;

			while (i < Messages.Num() && Messages[i].Role == EAutonomixMessageRole::ToolResult)
			{
				if (Messages[i].bIsTruncationMarker)
				{
					i++;
					continue;
				}

				TSharedPtr<FJsonObject> ToolResultObj = MakeShared<FJsonObject>();
				ToolResultObj->SetStringField(TEXT("type"), TEXT("tool_result"));

				// Sanitize tool_use_id: Claude requires pattern ^[a-zA-Z0-9_-]+$
				// After save/load cycles, IDs may be empty or contain invalid characters
				FString SafeToolUseId = Messages[i].ToolUseId;
				if (SafeToolUseId.IsEmpty())
				{
					SafeToolUseId = FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphens);
					UE_LOG(LogAutonomix, Warning, TEXT("ClaudeClient: Empty tool_use_id at message %d, generated placeholder: %s"), i, *SafeToolUseId);
				}
				else
				{
					// Strip any characters that don't match [a-zA-Z0-9_-]
					FString Sanitized;
					for (TCHAR Ch : SafeToolUseId)
					{
						if (FChar::IsAlnum(Ch) || Ch == TEXT('_') || Ch == TEXT('-'))
						{
							Sanitized.AppendChar(Ch);
						}
					}
					if (Sanitized.IsEmpty())
					{
						Sanitized = FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphens);
						UE_LOG(LogAutonomix, Warning, TEXT("ClaudeClient: tool_use_id '%s' had no valid chars, generated placeholder: %s"), *SafeToolUseId, *Sanitized);
					}
					SafeToolUseId = Sanitized;
				}

				ToolResultObj->SetStringField(TEXT("tool_use_id"), SafeToolUseId);
				ToolResultObj->SetStringField(TEXT("content"), Messages[i].Content);

				if (Messages[i].ToolName == TEXT("error"))
				{
					ToolResultObj->SetBoolField(TEXT("is_error"), true);
				}

				ContentArray.Add(MakeShared<FJsonValueObject>(ToolResultObj));
				i++;
			}

			if (ContentArray.Num() > 0)
			{
				UserObj->SetArrayField(TEXT("content"), ContentArray);
				Result.Add(MakeShared<FJsonValueObject>(UserObj));
			}
		}
		else if (Msg.Role == EAutonomixMessageRole::User)
		{
			TSharedPtr<FJsonObject> MsgObj = MakeShared<FJsonObject>();
			MsgObj->SetStringField(TEXT("role"), TEXT("user"));
			MsgObj->SetStringField(TEXT("content"), Msg.Content);
			Result.Add(MakeShared<FJsonValueObject>(MsgObj));
			i++;
		}
		else if (Msg.Role == EAutonomixMessageRole::Assistant)
		{
			TSharedPtr<FJsonObject> MsgObj = MakeShared<FJsonObject>();
			MsgObj->SetStringField(TEXT("role"), TEXT("assistant"));

			// Use structured content blocks if available (preserves tool_use blocks)
			if (!Msg.ContentBlocksJson.IsEmpty())
			{
				TArray<TSharedPtr<FJsonValue>> ParsedBlocks;
				TSharedRef<TJsonReader<>> BlockReader = TJsonReaderFactory<>::Create(Msg.ContentBlocksJson);
				if (FJsonSerializer::Deserialize(BlockReader, ParsedBlocks))
				{
					// Sanitize tool_use block IDs to match Claude's pattern ^[a-zA-Z0-9_-]+$
					for (TSharedPtr<FJsonValue>& Block : ParsedBlocks)
					{
						const TSharedPtr<FJsonObject>* BlockObj = nullptr;
						if (!Block->TryGetObject(BlockObj)) continue;
						FString BlockType;
						(*BlockObj)->TryGetStringField(TEXT("type"), BlockType);
						if (BlockType == TEXT("tool_use"))
						{
							FString OrigId;
							(*BlockObj)->TryGetStringField(TEXT("id"), OrigId);
							FString SanitizedId;
							for (TCHAR Ch : OrigId)
							{
								if (FChar::IsAlnum(Ch) || Ch == TEXT('_') || Ch == TEXT('-'))
								{
									SanitizedId.AppendChar(Ch);
								}
							}
							if (SanitizedId.IsEmpty())
							{
								SanitizedId = FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphens);
								UE_LOG(LogAutonomix, Warning, TEXT("ClaudeClient: Empty/invalid tool_use id '%s', generated placeholder: %s"), *OrigId, *SanitizedId);
							}
							if (SanitizedId != OrigId)
							{
								// Must create a mutable copy
								TSharedPtr<FJsonObject> FixedBlock = MakeShared<FJsonObject>();
								for (const auto& Pair : (*BlockObj)->Values)
								{
									FixedBlock->SetField(Pair.Key, Pair.Value);
								}
								FixedBlock->SetStringField(TEXT("id"), SanitizedId);
								Block = MakeShared<FJsonValueObject>(FixedBlock);
							}
						}
					}
					MsgObj->SetArrayField(TEXT("content"), ParsedBlocks);
				}
				else
				{
					// Fallback to flat text if JSON parse fails
					MsgObj->SetStringField(TEXT("content"), Msg.Content);
				}
			}
			else
			{
				MsgObj->SetStringField(TEXT("content"), Msg.Content);
			}

			Result.Add(MakeShared<FJsonValueObject>(MsgObj));
			i++;
		}
		else
		{
			i++;
		}
	}

	return Result;
}

// ============================================================================
// Raw byte buffering for UTF-8 safe SSE streaming
// ============================================================================

void FAutonomixClaudeClient::HandleRequestProgress(
	FHttpRequestPtr Request, uint64 BytesSent, uint64 BytesReceived)
{
	// CRITICAL (Gemini): Check bRequestCancelled FIRST to prevent race condition
	if (bRequestCancelled || !Request.IsValid()) return;

	FHttpResponsePtr Response = Request->GetResponse();
	if (!Response.IsValid()) return;

	const TArray<uint8>& RawContent = Response->GetContent();
	if (RawContent.Num() <= LastBytesReceived) return;

	int32 NewByteCount = RawContent.Num() - LastBytesReceived;
	const uint8* NewBytes = RawContent.GetData() + LastBytesReceived;
	LastBytesReceived = RawContent.Num();

	RawByteBuffer.Append(NewBytes, NewByteCount);

	int32 LastNewlinePos = -1;
	for (int32 j = RawByteBuffer.Num() - 1; j >= 0; --j)
	{
		if (RawByteBuffer[j] == '\n')
		{
			LastNewlinePos = j;
			break;
		}
	}

	if (LastNewlinePos < 0) return;

	int32 SafeByteCount = LastNewlinePos + 1;
	FString SafeChunk;
	FUTF8ToTCHAR Converter(reinterpret_cast<const ANSICHAR*>(RawByteBuffer.GetData()), SafeByteCount);
	SafeChunk = FString(Converter.Length(), Converter.Get());

	RawByteBuffer.RemoveAt(0, SafeByteCount);

	if (SafeChunk.IsEmpty()) return;

	TArray<FAutonomixSSEEvent> Events;
	SSEParser.ProcessChunk(SafeChunk, Events);

	if (Events.Num() > 0)
	{
		ProcessSSEEvents(Events);
	}
}

// ============================================================================
// Request Completion
// ============================================================================

void FAutonomixClaudeClient::HandleRequestComplete(
	FHttpRequestPtr Request, FHttpResponsePtr Response, bool bConnectedSuccessfully)
{
	// CRITICAL (Gemini): First check -- if cancelled, exit cleanly.
	// CancelRequest() can trigger this delegate one more time.
	// Do NOT broadcast errors, do NOT trigger retry pipeline.
	if (bRequestCancelled)
	{
		bRequestInFlight = false;
		return;
	}

	bRequestInFlight = false;

	if (!bConnectedSuccessfully || !Response.IsValid())
	{
		UE_LOG(LogAutonomix, Error, TEXT("ClaudeClient: Connection failed."));
		FAutonomixHTTPError Err = FAutonomixHTTPError::ConnectionFailed(TEXT("Anthropic"));
		ErrorReceivedDelegate.Broadcast(Err);
		RequestCompletedDelegate.Broadcast(false);
		return;
	}

	const int32 ResponseCode = Response->GetResponseCode();

	if (ResponseCode == 429)
	{
		ConsecutiveRateLimits++;
		if (ConsecutiveRateLimits <= 5)
		{
			ScheduleRetryWithBackoff();
			return;
		}
	}

	// -----------------------------------------------------------------------
	// Context window exceeded detection (HTTP 400 with specific error body)
	// Anthropic returns: {"type":"error","error":{"type":"invalid_request_error",
	//   "message":"prompt is too long: ... tokens > ... maximum"}}
	// -----------------------------------------------------------------------
	if (ResponseCode == 400)
	{
		FString ResponseBody = Response->GetContentAsString();
		if (IsContextWindowExceededError(ResponseCode, ResponseBody))
		{
			ContextWindowRetryCount++;

			if (ContextWindowRetryCount <= MaxContextWindowRetries)
			{
				UE_LOG(LogAutonomix, Warning,
					TEXT("ClaudeClient: Context window exceeded (HTTP 400). Firing OnContextWindowExceeded delegate (retry %d/%d)."),
					ContextWindowRetryCount, MaxContextWindowRetries);

				// Fire delegate — owner (SAutonomixMainPanel) MUST call RetryWithTrimmedHistory()
				// or CancelRequest() in response
				OnContextWindowExceeded.ExecuteIfBound(ContextWindowRetryCount);
				return;  // Don't fire generic error — owner handles it
			}
			else
			{
				// Exhausted retries — surface as hard error
				UE_LOG(LogAutonomix, Error,
					TEXT("ClaudeClient: Context window exceeded after %d retries. Giving up."),
					MaxContextWindowRetries);

				FAutonomixHTTPError Err;
				Err.Type = EAutonomixHTTPErrorType::ContextWindowExceeded;
				Err.UserFriendlyMessage = FString::Printf(
					TEXT("Context window is full and could not be reduced after %d attempts.\n"
					     "Try starting a new conversation or enabling Auto-Condense Context in settings."),
					MaxContextWindowRetries
				);
				ErrorReceivedDelegate.Broadcast(Err);
				RequestCompletedDelegate.Broadcast(false);
				return;
			}
		}
	}

	if (ResponseCode != 200)
	{
		FString ResponseBody = Response->GetContentAsString().Left(500);
		UE_LOG(LogAutonomix, Error, TEXT("ClaudeClient: HTTP %d -- %s"), ResponseCode, *ResponseBody);

		FAutonomixHTTPError Err = FAutonomixHTTPError::FromStatusCode(ResponseCode, ResponseBody, TEXT("Anthropic"));
		ErrorReceivedDelegate.Broadcast(Err);
		RequestCompletedDelegate.Broadcast(false);
		return;
	}

	ConsecutiveRateLimits = 0;

	// Flush remaining bytes
	if (RawByteBuffer.Num() > 0)
	{
		FString Remainder;
		FUTF8ToTCHAR Converter(reinterpret_cast<const ANSICHAR*>(RawByteBuffer.GetData()), RawByteBuffer.Num());
		Remainder = FString(Converter.Length(), Converter.Get());
		RawByteBuffer.Empty();

		if (!Remainder.IsEmpty())
		{
			TArray<FAutonomixSSEEvent> RemainingEvents;
			SSEParser.ProcessChunk(Remainder + TEXT("\n\n"), RemainingEvents);
			if (RemainingEvents.Num() > 0)
			{
				ProcessSSEEvents(RemainingEvents);
			}
		}
	}

	// CRITICAL (Gemini): Check stop_reason for max_tokens truncation.
	// If the response was cut off, discard any incomplete tool calls.
	if (LastStopReason == TEXT("max_tokens"))
	{
		if (PendingToolCalls.Num() > 0)
		{
			UE_LOG(LogAutonomix, Warning,
				TEXT("ClaudeClient: Response truncated (max_tokens). Discarding %d incomplete tool calls."),
				PendingToolCalls.Num());
			PendingToolCalls.Empty();
		}

		// Notify the user
		FAutonomixHTTPError Err;
		Err.Type = EAutonomixHTTPErrorType::InvalidResponse;
		Err.UserFriendlyMessage = TEXT("Response was truncated due to token limit. Try increasing Max Response Tokens in settings, or simplify your request.");
		ErrorReceivedDelegate.Broadcast(Err);
	}

	FinalizeResponse();
	RequestCompletedDelegate.Broadcast(true);

	UE_LOG(LogAutonomix, Log, TEXT("ClaudeClient: Request completed (stop_reason=%s). %d tool calls, %d input tokens, %d output tokens."),
		*LastStopReason, PendingToolCalls.Num(), LastTokenUsage.InputTokens, LastTokenUsage.OutputTokens);
}

void FAutonomixClaudeClient::RetryWithTrimmedHistory(const TArray<FAutonomixMessage>& TrimmedHistory)
{
	if (bRequestInFlight)
	{
		UE_LOG(LogAutonomix, Warning, TEXT("ClaudeClient: RetryWithTrimmedHistory called while request in flight. Ignoring."));
		return;
	}

	UE_LOG(LogAutonomix, Log,
		TEXT("ClaudeClient: Retrying with trimmed history (%d -> %d messages, retry %d/%d)."),
		PendingRetryHistory.Num(), TrimmedHistory.Num(),
		ContextWindowRetryCount, MaxContextWindowRetries);

	// Update stored history for the retry
	PendingRetryHistory = TrimmedHistory;

	// CRITICAL FIX: Use SendMessageInternal here, NOT SendMessage.
	// SendMessage resets ContextWindowRetryCount = 0 which would bypass the MaxContextWindowRetries
	// guard and create an infinite retry loop. SendMessageInternal preserves the counter.
	SendMessageInternal(TrimmedHistory, PendingRetrySystemPrompt, PendingRetryToolSchemas);
}

bool FAutonomixClaudeClient::IsContextWindowExceededError(int32 HttpCode, const FString& ResponseBody) const
{
	if (HttpCode != 400) return false;

	// Parse the error body to check for context-related error messages
	TSharedPtr<FJsonObject> ErrorJson;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ResponseBody);
	if (!FJsonSerializer::Deserialize(Reader, ErrorJson) || !ErrorJson.IsValid())
	{
		return false;
	}

	// Check error.type == "invalid_request_error"
	const TSharedPtr<FJsonObject>* ErrorObj = nullptr;
	if (!ErrorJson->TryGetObjectField(TEXT("error"), ErrorObj))
	{
		return false;
	}

	FString ErrorType;
	(*ErrorObj)->TryGetStringField(TEXT("type"), ErrorType);
	if (ErrorType != TEXT("invalid_request_error"))
	{
		return false;
	}

	// Check message for context/token related keywords
	FString ErrorMessage;
	(*ErrorObj)->TryGetStringField(TEXT("message"), ErrorMessage);
	ErrorMessage = ErrorMessage.ToLower();

	return ErrorMessage.Contains(TEXT("too long"))
		|| ErrorMessage.Contains(TEXT("context_length"))
		|| ErrorMessage.Contains(TEXT("context window"))
		|| ErrorMessage.Contains(TEXT("prompt is too long"))
		|| ErrorMessage.Contains(TEXT("maximum context"))
		|| ErrorMessage.Contains(TEXT("tokens > "))
		|| ErrorMessage.Contains(TEXT("exceeds the maximum"));
}

void FAutonomixClaudeClient::ScheduleRetryWithBackoff()
{
	float DelaySeconds = FMath::Pow(2.0f, ConsecutiveRateLimits - 1);
	UE_LOG(LogAutonomix, Warning, TEXT("ClaudeClient: Rate limited (429). Retrying in %.0f seconds (attempt %d/5)."),
		DelaySeconds, ConsecutiveRateLimits);

	FAutonomixHTTPError RateLimitInfo;
	RateLimitInfo.Type = EAutonomixHTTPErrorType::RateLimited;
	RateLimitInfo.UserFriendlyMessage = FString::Printf(
		TEXT("Rate limited by Anthropic. Retrying in %.0f seconds... (attempt %d/5)"), DelaySeconds, ConsecutiveRateLimits);
	ErrorReceivedDelegate.Broadcast(RateLimitInfo);

	// CRITICAL FIX: Capture a TWeakPtr to avoid use-after-free if the client is destroyed
	// before the timer fires. The raw [this] capture was a crash hazard.
	TWeakPtr<FAutonomixClaudeClient> WeakSelf = AsShared();

	AsyncTask(ENamedThreads::GameThread, [WeakSelf, DelaySeconds]()
	{
		FTSTicker::GetCoreTicker().AddTicker(
			FTickerDelegate::CreateLambda([WeakSelf](float DeltaTime) -> bool
			{
				TSharedPtr<FAutonomixClaudeClient> Self = WeakSelf.Pin();
				if (Self.IsValid() && !Self->PendingRetryHistory.IsEmpty())
				{
					// Rate limit retry uses SendMessageInternal to preserve rate limit context
					Self->SendMessageInternal(
						Self->PendingRetryHistory,
						Self->PendingRetrySystemPrompt,
						Self->PendingRetryToolSchemas);
				}
				return false;
			}),
			DelaySeconds
		);
	});
}

// ============================================================================
// SSE Event Processing
// ============================================================================

void FAutonomixClaudeClient::ProcessSSEEvents(const TArray<FAutonomixSSEEvent>& Events)
{
	for (const FAutonomixSSEEvent& Event : Events)
	{
		HandleSSEEvent(Event);
	}
}

void FAutonomixClaudeClient::HandleSSEEvent(const FAutonomixSSEEvent& Event)
{
	switch (Event.Type)
	{
	case EAutonomixSSEEventType::MessageStart:
		if (Event.JsonData.IsValid())
		{
			ExtractTokenUsage(Event.JsonData);
		}
		break;

	case EAutonomixSSEEventType::ContentBlockStart:
		if (Event.JsonData.IsValid())
		{
			const TSharedPtr<FJsonObject>* ContentBlock = nullptr;
			if (Event.JsonData->TryGetObjectField(TEXT("content_block"), ContentBlock))
			{
				FString BlockType;
				if ((*ContentBlock)->TryGetStringField(TEXT("type"), BlockType))
				{
					if (BlockType == TEXT("tool_use"))
					{
						CurrentToolCall = FAutonomixToolCall();
						(*ContentBlock)->TryGetStringField(TEXT("id"), CurrentToolCall.ToolUseId);
						(*ContentBlock)->TryGetStringField(TEXT("name"), CurrentToolCall.ToolName);
						CurrentToolCallInput.Empty();
						bBuildingToolCall = true;
					}
					else if (BlockType == TEXT("thinking"))
					{
						UE_LOG(LogAutonomix, Verbose, TEXT("ClaudeClient: Thinking block started."));
					}
					else if (BlockType == TEXT("text"))
					{
						bBuildingToolCall = false;
					}
				}
			}
		}
		break;

	case EAutonomixSSEEventType::ContentBlockDelta:
		if (Event.JsonData.IsValid())
		{
			const TSharedPtr<FJsonObject>* DeltaObj = nullptr;
			if (Event.JsonData->TryGetObjectField(TEXT("delta"), DeltaObj))
			{
				FString DeltaType;
				(*DeltaObj)->TryGetStringField(TEXT("type"), DeltaType);

				if (DeltaType == TEXT("text_delta"))
				{
					FString DeltaText;
					if ((*DeltaObj)->TryGetStringField(TEXT("text"), DeltaText))
					{
						CurrentAssistantContent += DeltaText;
						StreamingTextDelegate.Broadcast(CurrentMessageId, DeltaText);
					}
				}
				else if (DeltaType == TEXT("input_json_delta"))
				{
					FString PartialJson;
					if ((*DeltaObj)->TryGetStringField(TEXT("partial_json"), PartialJson))
					{
						CurrentToolCallInput += PartialJson;
					}
				}
				else if (DeltaType == TEXT("thinking_delta"))
				{
					FString ThinkingText;
					if ((*DeltaObj)->TryGetStringField(TEXT("thinking"), ThinkingText))
					{
						UE_LOG(LogAutonomix, Verbose, TEXT("ClaudeClient: Thinking: %s"), *ThinkingText.Left(100));
					}
				}
			}
		}
		break;

	case EAutonomixSSEEventType::ContentBlockStop:
		if (bBuildingToolCall && !CurrentToolCall.ToolUseId.IsEmpty())
		{
			// CRITICAL (Gemini): Graceful JSON failure handling.
			// If the response was truncated (max_tokens), the JSON may be incomplete.
			// FJsonSerializer::Deserialize will fail -- we must handle this gracefully.
			TSharedPtr<FJsonObject> InputJson;
			TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(CurrentToolCallInput);
			if (FJsonSerializer::Deserialize(Reader, InputJson) && InputJson.IsValid())
			{
				CurrentToolCall.InputParams = InputJson;
				PendingToolCalls.Add(CurrentToolCall);
				UE_LOG(LogAutonomix, Log, TEXT("ClaudeClient: Tool call accumulated: %s (id: %s)"),
					*CurrentToolCall.ToolName, *CurrentToolCall.ToolUseId);
			}
			else
			{
				// JSON parse failed -- likely truncated by max_tokens
				UE_LOG(LogAutonomix, Warning,
					TEXT("ClaudeClient: Failed to parse tool input JSON for %s (likely truncated). Discarding tool call. Input: %s"),
					*CurrentToolCall.ToolName, *CurrentToolCallInput.Left(200));

				// Do NOT add to PendingToolCalls -- this is an incomplete tool call
			}

			CurrentToolCall = FAutonomixToolCall();
			CurrentToolCallInput.Empty();
			bBuildingToolCall = false;
		}
		break;

	case EAutonomixSSEEventType::MessageDelta:
		if (Event.JsonData.IsValid())
		{
			ExtractTokenUsage(Event.JsonData);

			// Extract stop_reason for max_tokens detection
			const TSharedPtr<FJsonObject>* DeltaObj = nullptr;
			if (Event.JsonData->TryGetObjectField(TEXT("delta"), DeltaObj))
			{
				(*DeltaObj)->TryGetStringField(TEXT("stop_reason"), LastStopReason);
			}
		}
		break;

	case EAutonomixSSEEventType::MessageStop:
		break;

	case EAutonomixSSEEventType::Error:
		if (Event.JsonData.IsValid())
		{
			FString ErrorType, ErrorMessage;
			const TSharedPtr<FJsonObject>* ErrorObj = nullptr;
			if (Event.JsonData->TryGetObjectField(TEXT("error"), ErrorObj))
			{
				(*ErrorObj)->TryGetStringField(TEXT("type"), ErrorType);
				(*ErrorObj)->TryGetStringField(TEXT("message"), ErrorMessage);
			}
			UE_LOG(LogAutonomix, Error, TEXT("ClaudeClient: SSE Error: %s -- %s"), *ErrorType, *ErrorMessage);

			FAutonomixHTTPError Err;
			Err.Type = EAutonomixHTTPErrorType::ServerError;
			Err.RawMessage = ErrorMessage;
			Err.UserFriendlyMessage = FString::Printf(TEXT("Claude API error: %s"), *ErrorMessage);
			ErrorReceivedDelegate.Broadcast(Err);
		}
		break;

	case EAutonomixSSEEventType::Ping:
		break;

	default:
		break;
	}
}

void FAutonomixClaudeClient::ExtractTokenUsage(const TSharedPtr<FJsonObject>& JsonData)
{
	if (!JsonData.IsValid()) return;

	const TSharedPtr<FJsonObject>* MessageObj = nullptr;
	if (JsonData->TryGetObjectField(TEXT("message"), MessageObj))
	{
		const TSharedPtr<FJsonObject>* UsageObj = nullptr;
		if ((*MessageObj)->TryGetObjectField(TEXT("usage"), UsageObj))
		{
			int32 InputTokens = 0;
			(*UsageObj)->TryGetNumberField(TEXT("input_tokens"), InputTokens);
			if (InputTokens > 0) LastTokenUsage.InputTokens = InputTokens;

			int32 CacheCreation = 0, CacheRead = 0;
			(*UsageObj)->TryGetNumberField(TEXT("cache_creation_input_tokens"), CacheCreation);
			(*UsageObj)->TryGetNumberField(TEXT("cache_read_input_tokens"), CacheRead);
			LastTokenUsage.CacheCreationInputTokens = CacheCreation;
			LastTokenUsage.CacheReadInputTokens = CacheRead;
		}
	}

	const TSharedPtr<FJsonObject>* UsageObj = nullptr;
	if (JsonData->TryGetObjectField(TEXT("usage"), UsageObj))
	{
		int32 OutputTokens = 0;
		(*UsageObj)->TryGetNumberField(TEXT("output_tokens"), OutputTokens);
		if (OutputTokens > 0) LastTokenUsage.OutputTokens = OutputTokens;
	}
}

void FAutonomixClaudeClient::FinalizeResponse()
{
	FAutonomixMessage AssistantMsg(EAutonomixMessageRole::Assistant, CurrentAssistantContent);
	AssistantMsg.MessageId = CurrentMessageId;

	// Build structured content blocks JSON for proper API round-tripping.
	// This preserves tool_use blocks alongside text blocks so the conversation
	// can be replayed, exported, or re-sent with full structural fidelity.
	TArray<TSharedPtr<FJsonValue>> ContentBlocks;

	// Add text block if there's any text content
	if (!CurrentAssistantContent.IsEmpty())
	{
		TSharedPtr<FJsonObject> TextBlock = MakeShared<FJsonObject>();
		TextBlock->SetStringField(TEXT("type"), TEXT("text"));
		TextBlock->SetStringField(TEXT("text"), CurrentAssistantContent);
		ContentBlocks.Add(MakeShared<FJsonValueObject>(TextBlock));
	}

	// Add tool_use blocks
	for (const FAutonomixToolCall& ToolCall : PendingToolCalls)
	{
		TSharedPtr<FJsonObject> ToolBlock = MakeShared<FJsonObject>();
		ToolBlock->SetStringField(TEXT("type"), TEXT("tool_use"));
		ToolBlock->SetStringField(TEXT("id"), ToolCall.ToolUseId);
		ToolBlock->SetStringField(TEXT("name"), ToolCall.ToolName);
		if (ToolCall.InputParams.IsValid())
		{
			ToolBlock->SetObjectField(TEXT("input"), ToolCall.InputParams);
		}
		ContentBlocks.Add(MakeShared<FJsonValueObject>(ToolBlock));
	}

	// Serialize content blocks to JSON string
	if (ContentBlocks.Num() > 0)
	{
		FString BlocksStr;
		TSharedRef<TJsonWriter<>> BlocksWriter = TJsonWriterFactory<>::Create(&BlocksStr);
		FJsonSerializer::Serialize(ContentBlocks, BlocksWriter);
		AssistantMsg.ContentBlocksJson = BlocksStr;
	}

	MessageCompleteDelegate.Broadcast(AssistantMsg);

	for (const FAutonomixToolCall& ToolCall : PendingToolCalls)
	{
		ToolCallReceivedDelegate.Broadcast(ToolCall);
	}

	if (LastTokenUsage.TotalTokens() > 0)
	{
		TokenUsageUpdatedDelegate.Broadcast(LastTokenUsage);
	}
}
