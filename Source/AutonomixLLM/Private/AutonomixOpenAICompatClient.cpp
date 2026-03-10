// Copyright Autonomix. All Rights Reserved.

#include "AutonomixOpenAICompatClient.h"
#include "AutonomixCoreModule.h"
#include "AutonomixSettings.h"
#include "HttpModule.h"
#include "Interfaces/IHttpResponse.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Async/Async.h"

FAutonomixOpenAICompatClient::FAutonomixOpenAICompatClient()
	: BaseUrl(TEXT("https://api.openai.com/v1"))
	, ModelId(TEXT("gpt-4o"))
	, Provider(EAutonomixProvider::OpenAI)
	, MaxTokens(8192)
	, ReasoningEffort(EAutonomixReasoningEffort::Disabled)
	, bStreamingEnabled(true)
	, bRequestInFlight(false)
	, bRequestCancelled(false)
	, LastBytesReceived(0)
	, ConsecutiveRateLimits(0)
{
}

FAutonomixOpenAICompatClient::~FAutonomixOpenAICompatClient()
{
	CancelRequest();
}

void FAutonomixOpenAICompatClient::SetEndpoint(const FString& InBaseUrl) { BaseUrl = InBaseUrl; }
void FAutonomixOpenAICompatClient::SetApiKey(const FString& InApiKey) { ApiKey = InApiKey; }
void FAutonomixOpenAICompatClient::SetModel(const FString& InModelId) { ModelId = InModelId; }
void FAutonomixOpenAICompatClient::SetProvider(EAutonomixProvider InProvider) { Provider = InProvider; }
void FAutonomixOpenAICompatClient::SetMaxTokens(int32 InMaxTokens) { MaxTokens = InMaxTokens; }
void FAutonomixOpenAICompatClient::SetReasoningEffort(EAutonomixReasoningEffort InEffort) { ReasoningEffort = InEffort; }
void FAutonomixOpenAICompatClient::SetStreamingEnabled(bool bEnabled) { bStreamingEnabled = bEnabled; }

FString FAutonomixOpenAICompatClient::ReasoningEffortToString(EAutonomixReasoningEffort Effort)
{
	switch (Effort)
	{
	case EAutonomixReasoningEffort::Low:    return TEXT("low");
	case EAutonomixReasoningEffort::High:   return TEXT("high");
	case EAutonomixReasoningEffort::Medium: return TEXT("medium");
	default:                                return TEXT("");
	}
}

void FAutonomixOpenAICompatClient::SendMessage(
	const TArray<FAutonomixMessage>& ConversationHistory,
	const FString& SystemPrompt,
	const TArray<TSharedPtr<FJsonObject>>& ToolSchemas)
{
	if (bRequestInFlight)
	{
		UE_LOG(LogAutonomix, Warning, TEXT("OpenAICompatClient: Request already in flight. Ignoring."));
		return;
	}

	if (ApiKey.IsEmpty() && Provider != EAutonomixProvider::Ollama && Provider != EAutonomixProvider::LMStudio)
	{
		UE_LOG(LogAutonomix, Error, TEXT("OpenAICompatClient: API key not set for provider %d."), (int32)Provider);
		ErrorReceivedDelegate.Broadcast(FAutonomixHTTPError::ConnectionFailed());
		RequestCompletedDelegate.Broadcast(false);
		return;
	}

	// Store for retry
	RetryHistory = ConversationHistory;
	RetrySystemPrompt = SystemPrompt;
	RetryToolSchemas = ToolSchemas;

	TSharedPtr<FJsonObject> Body = BuildRequestBody(ConversationHistory, SystemPrompt, ToolSchemas);
	FString BodyString;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&BodyString);
	FJsonSerializer::Serialize(Body.ToSharedRef(), Writer);

	// Build URL: BaseUrl already has /v1, append /chat/completions
	FString Url = BaseUrl;
	if (!Url.EndsWith(TEXT("/"))) Url += TEXT("/");
	Url += TEXT("chat/completions");

	CurrentRequest = FHttpModule::Get().CreateRequest();
	CurrentRequest->SetURL(Url);
	CurrentRequest->SetVerb(TEXT("POST"));
	CurrentRequest->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
	if (!ApiKey.IsEmpty())
	{
		CurrentRequest->SetHeader(TEXT("Authorization"), FString::Printf(TEXT("Bearer %s"), *ApiKey));
	}

	// OpenRouter requires extra headers
	if (Provider == EAutonomixProvider::OpenRouter)
	{
		CurrentRequest->SetHeader(TEXT("HTTP-Referer"), TEXT("https://autonomix-ue.dev"));
		CurrentRequest->SetHeader(TEXT("X-Title"), TEXT("Autonomix UE Plugin"));
	}

	CurrentRequest->SetContentAsString(BodyString);
	const UAutonomixDeveloperSettings* Settings = UAutonomixDeveloperSettings::Get();
	CurrentRequest->SetTimeout((float)(Settings ? Settings->RequestTimeoutSeconds : 120));

	CurrentMessageId = FGuid::NewGuid();
	CurrentAssistantContent.Empty();
	CurrentReasoningContent.Empty();
	PendingToolCallStates.Empty();
	LastBytesReceived = 0;
	bRequestInFlight = true;
	bRequestCancelled = false;
	SSELineBuffer.Empty();
	RawByteBuffer.Empty();

	CurrentRequest->OnRequestProgress64().BindRaw(this, &FAutonomixOpenAICompatClient::HandleRequestProgress);
	CurrentRequest->OnProcessRequestComplete().BindRaw(this, &FAutonomixOpenAICompatClient::HandleRequestComplete);

	if (CurrentRequest->ProcessRequest())
	{
		RequestStartedDelegate.Broadcast();
		UE_LOG(LogAutonomix, Log, TEXT("OpenAICompatClient: Request started — Provider=%d Model=%s"), (int32)Provider, *ModelId);
	}
	else
	{
		bRequestInFlight = false;
		ErrorReceivedDelegate.Broadcast(FAutonomixHTTPError::ConnectionFailed());
		RequestCompletedDelegate.Broadcast(false);
	}
}

void FAutonomixOpenAICompatClient::CancelRequest()
{
	if (CurrentRequest.IsValid() && bRequestInFlight)
	{
		bRequestCancelled = true;
		CurrentRequest->CancelRequest();
		bRequestInFlight = false;
	}
}

bool FAutonomixOpenAICompatClient::IsRequestInFlight() const
{
	return bRequestInFlight;
}

TSharedPtr<FJsonObject> FAutonomixOpenAICompatClient::BuildRequestBody(
	const TArray<FAutonomixMessage>& History,
	const FString& SystemPrompt,
	const TArray<TSharedPtr<FJsonObject>>& ToolSchemas) const
{
	TSharedPtr<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetStringField(TEXT("model"), ModelId);
	Body->SetNumberField(TEXT("max_tokens"), (double)MaxTokens);
	Body->SetBoolField(TEXT("stream"), bStreamingEnabled);

	// Temperature: o-series models don't support temperature
	bool bIsReasoningModel = (ReasoningEffort != EAutonomixReasoningEffort::Disabled);
	if (!bIsReasoningModel)
	{
		Body->SetNumberField(TEXT("temperature"), 0.0);
	}

	// Reasoning effort (o3, o4-mini, deepseek-reasoner)
	if (ReasoningEffort != EAutonomixReasoningEffort::Disabled)
	{
		FString EffortStr = ReasoningEffortToString(ReasoningEffort);
		if (!EffortStr.IsEmpty())
		{
			Body->SetStringField(TEXT("reasoning_effort"), EffortStr);
		}
	}

	// Messages array (system + history)
	Body->SetArrayField(TEXT("messages"), ConvertMessagesToJson(History, SystemPrompt));

	// Tools
	if (ToolSchemas.Num() > 0)
	{
		Body->SetArrayField(TEXT("tools"), ConvertToolSchemas(ToolSchemas));
		Body->SetStringField(TEXT("tool_choice"), TEXT("auto"));
	}

	return Body;
}

TArray<TSharedPtr<FJsonValue>> FAutonomixOpenAICompatClient::ConvertMessagesToJson(
	const TArray<FAutonomixMessage>& Messages,
	const FString& SystemPrompt) const
{
	TArray<TSharedPtr<FJsonValue>> Result;

	// Inject system message first
	if (!SystemPrompt.IsEmpty())
	{
		TSharedPtr<FJsonObject> SysMsg = MakeShared<FJsonObject>();
		SysMsg->SetStringField(TEXT("role"), TEXT("system"));
		SysMsg->SetStringField(TEXT("content"), SystemPrompt);
		Result.Add(MakeShared<FJsonValueObject>(SysMsg));
	}

	for (const FAutonomixMessage& Msg : Messages)
	{
		TSharedPtr<FJsonObject> MsgObj = MakeShared<FJsonObject>();

		switch (Msg.Role)
		{
		case EAutonomixMessageRole::User:
		{
			MsgObj->SetStringField(TEXT("role"), TEXT("user"));
			MsgObj->SetStringField(TEXT("content"), Msg.Content);
			Result.Add(MakeShared<FJsonValueObject>(MsgObj));
			break;
		}
		case EAutonomixMessageRole::Assistant:
		{
			MsgObj->SetStringField(TEXT("role"), TEXT("assistant"));
			// If ContentBlocksJson has tool_calls, parse them
			if (!Msg.ContentBlocksJson.IsEmpty())
			{
				// Try to deserialize as array and look for tool_use blocks → map to OpenAI tool_calls
				TArray<TSharedPtr<FJsonValue>> Blocks;
				TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Msg.ContentBlocksJson);
				if (FJsonSerializer::Deserialize(Reader, Blocks))
				{
					// Build OpenAI tool_calls array from tool_use blocks
					TArray<TSharedPtr<FJsonValue>> ToolCallsArray;
					FString TextContent;
					for (const TSharedPtr<FJsonValue>& BlockVal : Blocks)
					{
						TSharedPtr<FJsonObject> Block = BlockVal->AsObject();
						if (!Block.IsValid()) continue;
						FString Type;
						Block->TryGetStringField(TEXT("type"), Type);
						if (Type == TEXT("text"))
						{
							FString Text;
							Block->TryGetStringField(TEXT("text"), Text);
							TextContent += Text;
						}
						else if (Type == TEXT("tool_use"))
						{
							// Convert Anthropic tool_use → OpenAI tool_calls entry
							FString TUId, TUName;
							Block->TryGetStringField(TEXT("id"), TUId);
							Block->TryGetStringField(TEXT("name"), TUName);
							const TSharedPtr<FJsonObject>* InputObj = nullptr;
							FString InputStr;
							if (Block->TryGetObjectField(TEXT("input"), InputObj))
							{
								TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&InputStr);
								FJsonSerializer::Serialize(InputObj->ToSharedRef(), W);
							}
							TSharedPtr<FJsonObject> TC = MakeShared<FJsonObject>();
							TC->SetStringField(TEXT("id"), TUId);
							TC->SetStringField(TEXT("type"), TEXT("function"));
							TSharedPtr<FJsonObject> Func = MakeShared<FJsonObject>();
							Func->SetStringField(TEXT("name"), TUName);
							Func->SetStringField(TEXT("arguments"), InputStr);
							TC->SetObjectField(TEXT("function"), Func);
							ToolCallsArray.Add(MakeShared<FJsonValueObject>(TC));
						}
					}
					if (!TextContent.IsEmpty())
						MsgObj->SetStringField(TEXT("content"), TextContent);
					else
						MsgObj->SetField(TEXT("content"), MakeShared<FJsonValueNull>());
					if (ToolCallsArray.Num() > 0)
						MsgObj->SetArrayField(TEXT("tool_calls"), ToolCallsArray);
				}
				else
				{
					MsgObj->SetStringField(TEXT("content"), Msg.Content);
				}
			}
			else
				{
					MsgObj->SetStringField(TEXT("content"), Msg.Content);
				}
	
				// DeepSeek reasoning_content: when using deepseek-reasoner with thinking mode,
				// ALL assistant messages in history MUST include reasoning_content field.
				// The API rejects messages missing this field even if empty string.
				// Ported from Roo Code's convertToR1Format which preserves reasoning_content.
				// See: https://api-docs.deepseek.com/guides/thinking_mode#tool-calls
				//
				// Check both explicit reasoning effort setting AND model ID containing "reasoner"
				// because DeepSeek enables thinking automatically for reasoner models.
				bool bIsDeepSeekReasoner = (Provider == EAutonomixProvider::DeepSeek) &&
					(ReasoningEffort != EAutonomixReasoningEffort::Disabled || ModelId.Contains(TEXT("reasoner")));
				if (bIsDeepSeekReasoner)
				{
					// Must always be present — use stored value or empty string
					MsgObj->SetStringField(TEXT("reasoning_content"), Msg.ReasoningContent);
				}
	
				Result.Add(MakeShared<FJsonValueObject>(MsgObj));
				break;
		}
		case EAutonomixMessageRole::ToolResult:
		{
			// OpenAI tool result: role=tool, tool_call_id, content
			MsgObj->SetStringField(TEXT("role"), TEXT("tool"));
			MsgObj->SetStringField(TEXT("tool_call_id"), Msg.ToolUseId);
			MsgObj->SetStringField(TEXT("content"), Msg.Content);
			Result.Add(MakeShared<FJsonValueObject>(MsgObj));
			break;
		}
		default:
			break;
		}
	}

	// =========================================================================
	// CRITICAL SANITIZATION PASS (ported from Roo Code's convertToOpenAiMessages)
	// =========================================================================
	// OpenAI/DeepSeek API requires:
	//   1. Every "role":"tool" message MUST be preceded by an "assistant" message with tool_calls
	//      containing a matching tool_call_id
	//   2. Every assistant message with tool_calls MUST be followed by "role":"tool" messages
	//      responding to each tool_call_id
	//
	// Old saved conversations may have:
	//   - ToolResult messages without a preceding assistant tool_calls (ContentBlocksJson was empty)
	//   - Assistant messages with tool_calls but no following tool messages (interrupted session)
	//
	// Fix: Collect valid tool_call_ids from assistant messages, drop orphaned tool messages,
	// and strip tool_calls from assistants that have no following tool responses.

	// Pass 1: Collect tool_call_ids declared by each assistant message
	TSet<FString> AllDeclaredToolCallIds;
	for (const TSharedPtr<FJsonValue>& MsgVal : Result)
	{
		const TSharedPtr<FJsonObject>* MsgObj = nullptr;
		if (!MsgVal->TryGetObject(MsgObj)) continue;

		FString Role;
		(*MsgObj)->TryGetStringField(TEXT("role"), Role);
		if (Role != TEXT("assistant")) continue;

		const TArray<TSharedPtr<FJsonValue>>* ToolCalls = nullptr;
		if ((*MsgObj)->TryGetArrayField(TEXT("tool_calls"), ToolCalls))
		{
			for (const TSharedPtr<FJsonValue>& TCVal : *ToolCalls)
			{
				const TSharedPtr<FJsonObject>* TCObj = nullptr;
				if (TCVal->TryGetObject(TCObj))
				{
					FString TcId;
					(*TCObj)->TryGetStringField(TEXT("id"), TcId);
					if (!TcId.IsEmpty())
					{
						AllDeclaredToolCallIds.Add(TcId);
					}
				}
			}
		}
	}

	// Pass 2: Collect tool_call_ids that have actual tool responses
	TSet<FString> RespondedToolCallIds;
	for (const TSharedPtr<FJsonValue>& MsgVal : Result)
	{
		const TSharedPtr<FJsonObject>* MsgObj = nullptr;
		if (!MsgVal->TryGetObject(MsgObj)) continue;

		FString Role;
		(*MsgObj)->TryGetStringField(TEXT("role"), Role);
		if (Role != TEXT("tool")) continue;

		FString ToolCallId;
		(*MsgObj)->TryGetStringField(TEXT("tool_call_id"), ToolCallId);
		if (!ToolCallId.IsEmpty() && AllDeclaredToolCallIds.Contains(ToolCallId))
		{
			RespondedToolCallIds.Add(ToolCallId);
		}
	}

	// Pass 3: Build sanitized result — drop orphaned tool messages and strip
	// tool_calls from assistant messages that have no responses
	TArray<TSharedPtr<FJsonValue>> Sanitized;
	int32 DroppedToolMessages = 0;
	int32 StrippedAssistantToolCalls = 0;

	for (const TSharedPtr<FJsonValue>& MsgVal : Result)
	{
		const TSharedPtr<FJsonObject>* MsgObjPtr = nullptr;
		if (!MsgVal->TryGetObject(MsgObjPtr))
		{
			Sanitized.Add(MsgVal);
			continue;
		}

		FString Role;
		(*MsgObjPtr)->TryGetStringField(TEXT("role"), Role);

		if (Role == TEXT("tool"))
		{
			// Drop tool messages whose tool_call_id doesn't exist in any assistant's tool_calls
			FString ToolCallId;
			(*MsgObjPtr)->TryGetStringField(TEXT("tool_call_id"), ToolCallId);
			if (!AllDeclaredToolCallIds.Contains(ToolCallId))
			{
				UE_LOG(LogAutonomix, Warning,
					TEXT("OpenAICompatClient: Dropping orphaned tool message (tool_call_id=%s) — no matching tool_calls in assistant."),
					*ToolCallId);
				DroppedToolMessages++;
				continue;
			}
			Sanitized.Add(MsgVal);
		}
		else if (Role == TEXT("assistant"))
		{
			const TArray<TSharedPtr<FJsonValue>>* ToolCalls = nullptr;
			if ((*MsgObjPtr)->TryGetArrayField(TEXT("tool_calls"), ToolCalls) && ToolCalls->Num() > 0)
			{
				// Check if ANY of this assistant's tool_calls have responses
				bool bHasAnyResponse = false;
				for (const TSharedPtr<FJsonValue>& TCVal : *ToolCalls)
				{
					const TSharedPtr<FJsonObject>* TCObj = nullptr;
					if (TCVal->TryGetObject(TCObj))
					{
						FString TcId;
						(*TCObj)->TryGetStringField(TEXT("id"), TcId);
						if (RespondedToolCallIds.Contains(TcId))
						{
							bHasAnyResponse = true;
							break;
						}
					}
				}

				if (!bHasAnyResponse)
				{
					// Strip tool_calls from this assistant message — keep only content
					TSharedPtr<FJsonObject> CleanMsg = MakeShared<FJsonObject>();
					CleanMsg->SetStringField(TEXT("role"), TEXT("assistant"));
					FString Content;
					if ((*MsgObjPtr)->TryGetStringField(TEXT("content"), Content))
					{
						CleanMsg->SetStringField(TEXT("content"), Content.IsEmpty() ? TEXT("(tool execution was interrupted)") : Content);
					}
					else
					{
						CleanMsg->SetStringField(TEXT("content"), TEXT("(tool execution was interrupted)"));
					}
					Sanitized.Add(MakeShared<FJsonValueObject>(CleanMsg));
					StrippedAssistantToolCalls++;

					UE_LOG(LogAutonomix, Warning,
						TEXT("OpenAICompatClient: Stripped tool_calls from assistant message — no matching tool responses found."));
				}
				else
				{
					Sanitized.Add(MsgVal);
				}
			}
			else
			{
				Sanitized.Add(MsgVal);
			}
		}
		else
		{
			Sanitized.Add(MsgVal);
		}
	}

	if (DroppedToolMessages > 0 || StrippedAssistantToolCalls > 0)
	{
		UE_LOG(LogAutonomix, Warning,
			TEXT("OpenAICompatClient: Sanitized conversation history — dropped %d orphaned tool message(s), stripped tool_calls from %d assistant message(s)."),
			DroppedToolMessages, StrippedAssistantToolCalls);
	}

	return Sanitized;
}

TArray<TSharedPtr<FJsonValue>> FAutonomixOpenAICompatClient::ConvertToolSchemas(
	const TArray<TSharedPtr<FJsonObject>>& ToolSchemas) const
{
	TArray<TSharedPtr<FJsonValue>> Result;
	for (const TSharedPtr<FJsonObject>& Schema : ToolSchemas)
	{
		if (!Schema.IsValid()) continue;
		// Schema is already in Anthropic tool format: {name, description, input_schema}
		// Convert to OpenAI: {type: "function", function: {name, description, parameters}}
		TSharedPtr<FJsonObject> Tool = MakeShared<FJsonObject>();
		Tool->SetStringField(TEXT("type"), TEXT("function"));

		TSharedPtr<FJsonObject> FuncDef = MakeShared<FJsonObject>();
		FString Name, Desc;
		Schema->TryGetStringField(TEXT("name"), Name);
		Schema->TryGetStringField(TEXT("description"), Desc);
		FuncDef->SetStringField(TEXT("name"), Name);
		FuncDef->SetStringField(TEXT("description"), Desc);

		// input_schema → parameters
		const TSharedPtr<FJsonObject>* InputSchema = nullptr;
		if (Schema->TryGetObjectField(TEXT("input_schema"), InputSchema))
		{
			FuncDef->SetObjectField(TEXT("parameters"), *InputSchema);
		}
		Tool->SetObjectField(TEXT("function"), FuncDef);
		Result.Add(MakeShared<FJsonValueObject>(Tool));
	}
	return Result;
}

void FAutonomixOpenAICompatClient::HandleRequestProgress(
	FHttpRequestPtr Request, uint64 BytesSent, uint64 BytesReceived)
{
	if (bRequestCancelled) return;

	const TArray<uint8>& ResponseBytes = Request->GetResponse() ?
		Request->GetResponse()->GetContent() : TArray<uint8>();

	if ((int32)BytesReceived > LastBytesReceived && ResponseBytes.Num() > 0)
	{
		// Process only the new bytes
		int32 NewStart = LastBytesReceived;
		int32 NewCount = ResponseBytes.Num() - NewStart;
		LastBytesReceived = ResponseBytes.Num();

		if (NewCount > 0)
		{
			FString NewData = FString(NewCount, UTF8_TO_TCHAR(
				reinterpret_cast<const char*>(ResponseBytes.GetData() + NewStart)));
			ProcessSSEChunk(NewData);
		}
	}
}

void FAutonomixOpenAICompatClient::HandleRequestComplete(
	FHttpRequestPtr Request, FHttpResponsePtr Response, bool bConnected)
{
	bRequestInFlight = false;
	if (bRequestCancelled) return;

	if (!bConnected || !Response.IsValid())
	{
		ErrorReceivedDelegate.Broadcast(FAutonomixHTTPError::ConnectionFailed());
		RequestCompletedDelegate.Broadcast(false);
		return;
	}

	int32 Code = Response->GetResponseCode();
	if (Code == 429)
	{
		ConsecutiveRateLimits++;
		// Simple backoff: try again after a delay
		UE_LOG(LogAutonomix, Warning, TEXT("OpenAICompatClient: Rate limited (429). Retry %d."), ConsecutiveRateLimits);
		FAutonomixHTTPError Err;
		Err.Type = EAutonomixHTTPErrorType::RateLimited;
		Err.StatusCode = 429;
		Err.UserFriendlyMessage = TEXT("Rate limited. Please wait a moment before retrying.");
		ErrorReceivedDelegate.Broadcast(Err);
		RequestCompletedDelegate.Broadcast(false);
		return;
	}

	if (Code != 200)
	{
		FAutonomixHTTPError Err = FAutonomixHTTPError::FromStatusCode(Code, Response->GetContentAsString());
		ErrorReceivedDelegate.Broadcast(Err);
		RequestCompletedDelegate.Broadcast(false);
		return;
	}

	ConsecutiveRateLimits = 0;

	// Process any remaining SSE data
	FString Body = Response->GetContentAsString();
	ProcessSSEChunk(Body);
	FinalizeResponse();
}

void FAutonomixOpenAICompatClient::ProcessSSEChunk(const FString& RawData)
{
	SSELineBuffer += RawData;

	FString Line, Remaining;
	while (SSELineBuffer.Split(TEXT("\n"), &Line, &Remaining))
	{
		Line.TrimEndInline();
		SSELineBuffer = Remaining;

		if (Line.StartsWith(TEXT("data: ")))
		{
			FString JsonData = Line.Mid(6).TrimStartAndEnd();
			if (JsonData != TEXT("[DONE]"))
			{
				ProcessSSEEvent(JsonData);
			}
		}
	}
}

void FAutonomixOpenAICompatClient::ProcessSSEEvent(const FString& DataJson)
{
	TSharedPtr<FJsonObject> Obj;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(DataJson);
	if (!FJsonSerializer::Deserialize(Reader, Obj) || !Obj.IsValid()) return;

	const TArray<TSharedPtr<FJsonValue>>* Choices = nullptr;
	if (!Obj->TryGetArrayField(TEXT("choices"), Choices) || Choices->Num() == 0) return;

	const TSharedPtr<FJsonObject>* ChoiceObj = nullptr;
	if (!(*Choices)[0]->TryGetObject(ChoiceObj)) return;

	const TSharedPtr<FJsonObject>* Delta = nullptr;
	if (!(*ChoiceObj)->TryGetObjectField(TEXT("delta"), Delta)) return;

	// Text content delta
	FString ContentDelta;
	if ((*Delta)->TryGetStringField(TEXT("content"), ContentDelta) && !ContentDelta.IsEmpty())
	{
		CurrentAssistantContent += ContentDelta;
		StreamingTextDelegate.Broadcast(CurrentMessageId, ContentDelta);
	}

	// DeepSeek reasoning_content from thinking mode (interleaved thinking)
	// This is the proper way DeepSeek sends thinking content in streaming.
	// Must be accumulated and stored on the completed message for replay.
	FString ReasoningDelta;
	if ((*Delta)->TryGetStringField(TEXT("reasoning_content"), ReasoningDelta) && !ReasoningDelta.IsEmpty())
	{
		CurrentReasoningContent += ReasoningDelta;
	}

	// Tool call deltas
	const TArray<TSharedPtr<FJsonValue>>* ToolCallDeltas = nullptr;
	if ((*Delta)->TryGetArrayField(TEXT("tool_calls"), ToolCallDeltas))
	{
		for (const TSharedPtr<FJsonValue>& TCVal : *ToolCallDeltas)
		{
			const TSharedPtr<FJsonObject>* TCObj = nullptr;
			if (!TCVal->TryGetObject(TCObj)) continue;

			int32 Index = 0;
			(*TCObj)->TryGetNumberField(TEXT("index"), Index);

			// Extend pending states array if needed
			while (PendingToolCallStates.Num() <= Index)
			{
				PendingToolCallStates.Add(FPendingToolCallState());
				PendingToolCallStates.Last().Index = PendingToolCallStates.Num() - 1;
			}
			FPendingToolCallState& State = PendingToolCallStates[Index];

			// Tool call ID (only in first chunk for this index)
			FString TcId;
			if ((*TCObj)->TryGetStringField(TEXT("id"), TcId) && !TcId.IsEmpty())
			{
				State.ToolUseId = TcId;
			}

			// Function name/arguments delta
			const TSharedPtr<FJsonObject>* FuncObj = nullptr;
			if ((*TCObj)->TryGetObjectField(TEXT("function"), FuncObj))
			{
				FString NameDelta, ArgsDelta;
					(*FuncObj)->TryGetStringField(TEXT("name"), NameDelta);
					(*FuncObj)->TryGetStringField(TEXT("arguments"), ArgsDelta);
					// CRITICAL FIX: Name is sent COMPLETE in the first chunk (not incremental).
					// Using += would double the name on retries/reconnections ("search_assetssearch_assets").
					// Only set the name on the first chunk; subsequent chunks only carry arguments.
					// Ported from Roo Code's NativeToolCallParser which tracks names by index.
					if (!NameDelta.IsEmpty() && State.ToolName.IsEmpty()) State.ToolName = NameDelta;
					if (!ArgsDelta.IsEmpty()) State.ArgumentsAccumulated += ArgsDelta;
			}
		}
	}

	// Usage
	const TSharedPtr<FJsonObject>* UsageObj = nullptr;
	if (Obj->TryGetObjectField(TEXT("usage"), UsageObj))
	{
		ExtractTokenUsage(*UsageObj);
	}
}

void FAutonomixOpenAICompatClient::ExtractTokenUsage(const TSharedPtr<FJsonObject>& UsageObj)
{
	if (!UsageObj.IsValid()) return;
	double Prompt = 0, Completion = 0;
	UsageObj->TryGetNumberField(TEXT("prompt_tokens"), Prompt);
	UsageObj->TryGetNumberField(TEXT("completion_tokens"), Completion);
	LastTokenUsage.InputTokens = (int32)Prompt;
	LastTokenUsage.OutputTokens = (int32)Completion;
	TokenUsageUpdatedDelegate.Broadcast(LastTokenUsage);
}

void FAutonomixOpenAICompatClient::FinalizeResponse()
{
	if (bRequestCancelled) return;

	// CRITICAL FIX (ported from Roo Code's convertToOpenAiMessages pattern):
	// Build ContentBlocksJson in Anthropic format (text + tool_use blocks) so that:
	// 1. Save/load preserves tool calls in the conversation history
	// 2. ConvertMessagesToJson() can reconstruct proper OpenAI tool_calls on replay
	// 3. ConversationManager::GetEffectiveHistory() can detect orphaned tool_uses
	//
	// Without this, tool-only responses were never stored, causing "Messages with role 'tool'
	// must be a response to a preceding message with 'tool_calls'" on the next API call.
	TArray<TSharedPtr<FJsonValue>> ContentBlocks;
	bool bHasToolCalls = false;

	// Add text block if we have any assistant content
	if (!CurrentAssistantContent.IsEmpty())
	{
		TSharedPtr<FJsonObject> TextBlock = MakeShared<FJsonObject>();
		TextBlock->SetStringField(TEXT("type"), TEXT("text"));
		TextBlock->SetStringField(TEXT("text"), CurrentAssistantContent);
		ContentBlocks.Add(MakeShared<FJsonValueObject>(TextBlock));
	}

	// Fire accumulated tool calls AND build tool_use blocks for ContentBlocksJson
	for (FPendingToolCallState& State : PendingToolCallStates)
	{
		if (State.ToolName.IsEmpty()) continue;

		// Parse arguments JSON
		TSharedPtr<FJsonObject> ArgsObj;
		if (!State.ArgumentsAccumulated.IsEmpty())
		{
			TSharedRef<TJsonReader<>> ArgReader = TJsonReaderFactory<>::Create(State.ArgumentsAccumulated);
			FJsonSerializer::Deserialize(ArgReader, ArgsObj);
		}
		if (!ArgsObj.IsValid()) ArgsObj = MakeShared<FJsonObject>();

		FString ToolUseId = State.ToolUseId.IsEmpty() ?
			FGuid::NewGuid().ToString(EGuidFormats::Digits) : State.ToolUseId;

		// Build Anthropic-style tool_use block for ContentBlocksJson
		TSharedPtr<FJsonObject> ToolUseBlock = MakeShared<FJsonObject>();
		ToolUseBlock->SetStringField(TEXT("type"), TEXT("tool_use"));
		ToolUseBlock->SetStringField(TEXT("id"), ToolUseId);
		ToolUseBlock->SetStringField(TEXT("name"), State.ToolName);
		ToolUseBlock->SetObjectField(TEXT("input"), ArgsObj);
		ContentBlocks.Add(MakeShared<FJsonValueObject>(ToolUseBlock));
		bHasToolCalls = true;

		// Add tool_name field for dispatcher
		ArgsObj->SetStringField(TEXT("tool_name"), State.ToolName);

		FAutonomixToolCall ToolCall;
		ToolCall.ToolUseId = ToolUseId;
		ToolCall.ToolName = State.ToolName;
		ToolCall.InputParams = ArgsObj;

		UE_LOG(LogAutonomix, Log, TEXT("OpenAICompatClient: Tool call: %s (id=%s)"),
			*State.ToolName, *ToolCall.ToolUseId);
		ToolCallReceivedDelegate.Broadcast(ToolCall);
	}
	PendingToolCallStates.Empty();

	// CRITICAL FIX: Always fire MessageComplete for tool-only responses.
	// Without this, assistant messages with only tool calls (no text) were never stored
	// in ConversationManager, making subsequent role:tool messages orphaned.
	// This mirrors Roo Code's pattern where every assistant response fires MessageComplete
	// regardless of whether it has text content or only tool_use blocks.
	if (!CurrentAssistantContent.IsEmpty() || bHasToolCalls)
	{
		FAutonomixMessage CompletedMsg(EAutonomixMessageRole::Assistant, CurrentAssistantContent);
		CompletedMsg.MessageId = CurrentMessageId;
		CompletedMsg.ReasoningContent = CurrentReasoningContent;

		// Serialize ContentBlocksJson (Anthropic format) for save/load fidelity
		if (ContentBlocks.Num() > 0)
		{
			FString BlocksStr;
			TSharedRef<TJsonWriter<>> BlocksWriter = TJsonWriterFactory<>::Create(&BlocksStr);
			FJsonSerializer::Serialize(ContentBlocks, BlocksWriter);
			CompletedMsg.ContentBlocksJson = BlocksStr;
		}

		MessageCompleteDelegate.Broadcast(CompletedMsg);
	}

	RequestCompletedDelegate.Broadcast(true);
	CurrentAssistantContent.Empty();
	CurrentReasoningContent.Empty();
}
