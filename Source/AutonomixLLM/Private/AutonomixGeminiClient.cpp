// Copyright Autonomix. All Rights Reserved.

#include "AutonomixGeminiClient.h"
#include "AutonomixCoreModule.h"
#include "AutonomixSettings.h"
#include "HttpModule.h"
#include "Interfaces/IHttpResponse.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

FAutonomixGeminiClient::FAutonomixGeminiClient()
	: ModelId(TEXT("gemini-2.5-pro"))
	, BaseUrl(TEXT(""))
	, MaxTokens(65536)
	, ThinkingBudgetTokens(8192)
	, ReasoningEffort(EAutonomixReasoningEffort::Disabled)
	, bRequestInFlight(false)
	, bRequestCancelled(false)
	, LastBytesReceived(0)
{
}

FAutonomixGeminiClient::~FAutonomixGeminiClient()
{
	CancelRequest();
}

void FAutonomixGeminiClient::SetApiKey(const FString& InApiKey) { ApiKey = InApiKey; }
void FAutonomixGeminiClient::SetModel(const FString& InModelId) { ModelId = InModelId; }
void FAutonomixGeminiClient::SetBaseUrl(const FString& InBaseUrl) { BaseUrl = InBaseUrl; }
void FAutonomixGeminiClient::SetMaxTokens(int32 InMaxTokens) { MaxTokens = InMaxTokens; }
void FAutonomixGeminiClient::SetThinkingBudget(int32 InBudget) { ThinkingBudgetTokens = InBudget; }
void FAutonomixGeminiClient::SetReasoningEffort(EAutonomixReasoningEffort InEffort) { ReasoningEffort = InEffort; }

FString FAutonomixGeminiClient::ReasoningEffortToGeminiString(EAutonomixReasoningEffort Effort)
{
	// Gemini thinkingLevel uses lowercase strings (matches Google GenAI SDK)
	switch (Effort)
	{
	case EAutonomixReasoningEffort::Low:    return TEXT("low");
	case EAutonomixReasoningEffort::Medium: return TEXT("medium");
	case EAutonomixReasoningEffort::High:   return TEXT("high");
	default:                                return TEXT("");
	}
}

void FAutonomixGeminiClient::SendMessage(
	const TArray<FAutonomixMessage>& ConversationHistory,
	const FString& SystemPrompt,
	const TArray<TSharedPtr<FJsonObject>>& ToolSchemas)
{
	if (bRequestInFlight)
	{
		UE_LOG(LogAutonomix, Warning, TEXT("GeminiClient: Request already in flight. Ignoring."));
		return;
	}

	if (ApiKey.IsEmpty())
	{
		UE_LOG(LogAutonomix, Error, TEXT("GeminiClient: API key not set."));
		ErrorReceivedDelegate.Broadcast(FAutonomixHTTPError::ConnectionFailed(TEXT("Google Gemini")));
		RequestCompletedDelegate.Broadcast(false);
		return;
	}

	TSharedPtr<FJsonObject> Body = BuildRequestBody(ConversationHistory, SystemPrompt, ToolSchemas);
	FString BodyString;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&BodyString);
	FJsonSerializer::Serialize(Body.ToSharedRef(), Writer);

	// URL: https://generativelanguage.googleapis.com/v1beta/models/{model}:streamGenerateContent?key={key}
	FString EffectiveBase = BaseUrl.IsEmpty() ?
		TEXT("https://generativelanguage.googleapis.com") : BaseUrl;
	FString Url = FString::Printf(TEXT("%s/v1beta/models/%s:streamGenerateContent?key=%s&alt=sse"),
		*EffectiveBase, *ModelId, *ApiKey);

	CurrentRequest = FHttpModule::Get().CreateRequest();
	CurrentRequest->SetURL(Url);
	CurrentRequest->SetVerb(TEXT("POST"));
	CurrentRequest->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
	CurrentRequest->SetContentAsString(BodyString);

	const UAutonomixDeveloperSettings* Settings = UAutonomixDeveloperSettings::Get();
	const float TimeoutSec = Settings ? (float)Settings->RequestTimeoutSeconds : 120.0f;
	CurrentRequest->SetTimeout(TimeoutSec);
	CurrentRequest->SetActivityTimeout(TimeoutSec);

	CurrentMessageId = FGuid::NewGuid();
	CurrentAssistantContent.Empty();
	PendingToolCalls.Empty();
	NdjsonLineBuffer.Empty();
	LastBytesReceived = 0;
	bRequestInFlight = true;
	bRequestCancelled = false;

	CurrentRequest->OnRequestProgress64().BindRaw(this, &FAutonomixGeminiClient::HandleRequestProgress);
	CurrentRequest->OnProcessRequestComplete().BindRaw(this, &FAutonomixGeminiClient::HandleRequestComplete);

	if (CurrentRequest->ProcessRequest())
	{
		RequestStartedDelegate.Broadcast();
		UE_LOG(LogAutonomix, Log, TEXT("GeminiClient: Request started — Model=%s"), *ModelId);
	}
	else
	{
		bRequestInFlight = false;
		ErrorReceivedDelegate.Broadcast(FAutonomixHTTPError::ConnectionFailed(TEXT("Google Gemini")));
		RequestCompletedDelegate.Broadcast(false);
	}
}

void FAutonomixGeminiClient::CancelRequest()
{
	if (CurrentRequest.IsValid() && bRequestInFlight)
	{
		bRequestCancelled = true;
		CurrentRequest->CancelRequest();
		bRequestInFlight = false;
	}
}

bool FAutonomixGeminiClient::IsRequestInFlight() const
{
	return bRequestInFlight;
}

TSharedPtr<FJsonObject> FAutonomixGeminiClient::BuildRequestBody(
	const TArray<FAutonomixMessage>& History,
	const FString& SystemPrompt,
	const TArray<TSharedPtr<FJsonObject>>& ToolSchemas) const
{
	TSharedPtr<FJsonObject> Body = MakeShared<FJsonObject>();

	// System instruction
	if (!SystemPrompt.IsEmpty())
	{
		TSharedPtr<FJsonObject> SysInst = MakeShared<FJsonObject>();
		TSharedPtr<FJsonObject> SysPart = MakeShared<FJsonObject>();
		SysPart->SetStringField(TEXT("text"), SystemPrompt);
		TArray<TSharedPtr<FJsonValue>> SysParts;
		SysParts.Add(MakeShared<FJsonValueObject>(SysPart));
		SysInst->SetArrayField(TEXT("parts"), SysParts);
		Body->SetObjectField(TEXT("system_instruction"), SysInst);
	}

	// Contents (conversation history)
	Body->SetArrayField(TEXT("contents"), ConvertContents(History));

	// Generation config
	TSharedPtr<FJsonObject> GenConfig = MakeShared<FJsonObject>();
	GenConfig->SetNumberField(TEXT("maxOutputTokens"), (double)MaxTokens);
	GenConfig->SetNumberField(TEXT("temperature"), 0.0);

	// Thinking config — model-aware gating (ported from Roo Code's getGeminiReasoning)
	// Only Gemini 2.5+ models support thinkingConfig. Sending it to older models
	// (1.5, 2.0) causes 400 errors like "Unknown name at 'generation_config'".
	// Budget-based (2.5): thinkingConfig.thinkingBudget = N
	// Effort-based (3.x): thinkingConfig.thinkingLevel = "low"/"medium"/"high"
	// These are mutually exclusive — budget for 2.5, level for 3.x.
	bool bIsBudgetModel = ModelId.Contains(TEXT("2.5-"));
	bool bIsEffortModel = ModelId.Contains(TEXT("3.")) || ModelId.Contains(TEXT("3-"));

	if (bIsBudgetModel && ThinkingBudgetTokens > 0)
	{
		// Gemini 2.5 Pro/Flash: budget-based thinking
		TSharedPtr<FJsonObject> ThinkConfig = MakeShared<FJsonObject>();
		ThinkConfig->SetNumberField(TEXT("thinkingBudget"), (double)ThinkingBudgetTokens);
		ThinkConfig->SetBoolField(TEXT("includeThoughts"), true);
		GenConfig->SetObjectField(TEXT("thinkingConfig"), ThinkConfig);
	}
	else if (bIsEffortModel && ReasoningEffort != EAutonomixReasoningEffort::Disabled)
	{
		// Gemini 3.x: effort/level-based thinking (low/medium/high)
		FString LevelStr = ReasoningEffortToGeminiString(ReasoningEffort);
		if (!LevelStr.IsEmpty())
		{
			TSharedPtr<FJsonObject> ThinkConfig = MakeShared<FJsonObject>();
			ThinkConfig->SetStringField(TEXT("thinkingLevel"), LevelStr);
			ThinkConfig->SetBoolField(TEXT("includeThoughts"), true);
			GenConfig->SetObjectField(TEXT("thinkingConfig"), ThinkConfig);
		}
	}
	// For non-thinking models (1.5, 2.0, etc.): no thinkingConfig sent — avoids API errors

	Body->SetObjectField(TEXT("generationConfig"), GenConfig);

	// Tools (function declarations)
	if (ToolSchemas.Num() > 0)
	{
		TSharedPtr<FJsonObject> ToolsObj = ConvertToolSchemas(ToolSchemas);
		TArray<TSharedPtr<FJsonValue>> ToolsArray;
		ToolsArray.Add(MakeShared<FJsonValueObject>(ToolsObj));
		Body->SetArrayField(TEXT("tools"), ToolsArray);

		TSharedPtr<FJsonObject> ToolConfig = MakeShared<FJsonObject>();
		TSharedPtr<FJsonObject> FuncCallingConfig = MakeShared<FJsonObject>();
		FuncCallingConfig->SetStringField(TEXT("mode"), TEXT("AUTO"));
		ToolConfig->SetObjectField(TEXT("function_calling_config"), FuncCallingConfig);
		Body->SetObjectField(TEXT("tool_config"), ToolConfig);
	}

	return Body;
}

TArray<TSharedPtr<FJsonValue>> FAutonomixGeminiClient::ConvertContents(
	const TArray<FAutonomixMessage>& Messages) const
{
	TArray<TSharedPtr<FJsonValue>> Result;

	for (const FAutonomixMessage& Msg : Messages)
	{
		TSharedPtr<FJsonObject> Content = MakeShared<FJsonObject>();
		TArray<TSharedPtr<FJsonValue>> Parts;

		switch (Msg.Role)
		{
		case EAutonomixMessageRole::User:
		{
			Content->SetStringField(TEXT("role"), TEXT("user"));
			TSharedPtr<FJsonObject> TextPart = MakeShared<FJsonObject>();
			TextPart->SetStringField(TEXT("text"), Msg.Content);
			Parts.Add(MakeShared<FJsonValueObject>(TextPart));
			break;
		}
		case EAutonomixMessageRole::Assistant:
		{
			Content->SetStringField(TEXT("role"), TEXT("model"));
			if (!Msg.ContentBlocksJson.IsEmpty())
			{
				// Parse content blocks for tool_use
				TArray<TSharedPtr<FJsonValue>> Blocks;
				TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Msg.ContentBlocksJson);
				if (FJsonSerializer::Deserialize(Reader, Blocks))
				{
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
							TSharedPtr<FJsonObject> Part = MakeShared<FJsonObject>();
							Part->SetStringField(TEXT("text"), Text);
							Parts.Add(MakeShared<FJsonValueObject>(Part));
						}
						else if (Type == TEXT("tool_use"))
						{
							// Anthropic tool_use → Gemini functionCall part
							FString TUName;
							Block->TryGetStringField(TEXT("name"), TUName);
							const TSharedPtr<FJsonObject>* InputObj = nullptr;
							Block->TryGetObjectField(TEXT("input"), InputObj);

							TSharedPtr<FJsonObject> FuncCallPart = MakeShared<FJsonObject>();
							TSharedPtr<FJsonObject> FuncCall = MakeShared<FJsonObject>();
							FuncCall->SetStringField(TEXT("name"), TUName);
							if (InputObj && InputObj->IsValid())
								FuncCall->SetObjectField(TEXT("args"), *InputObj);
							FuncCallPart->SetObjectField(TEXT("functionCall"), FuncCall);

							// The Gemini 3/2.5 API natively requires sequential and parallel tool histories to come 
							// bundled with a "thought_signature", or it throws HTTP 400 Bad Request.
							// Setting this documented bypass string fixes tool continuation parsing.
							FuncCallPart->SetStringField(TEXT("thought_signature"), TEXT("skip_thought_signature_validator"));

							Parts.Add(MakeShared<FJsonValueObject>(FuncCallPart));
						}
					}
				}
			}
			if (Parts.Num() == 0)
			{
				TSharedPtr<FJsonObject> TextPart = MakeShared<FJsonObject>();
				TextPart->SetStringField(TEXT("text"), Msg.Content);
				Parts.Add(MakeShared<FJsonValueObject>(TextPart));
			}
			break;
		}
		case EAutonomixMessageRole::ToolResult:
		{
			// Gemini function response: role=user, parts=[{functionResponse}]
			Content->SetStringField(TEXT("role"), TEXT("user"));

			TSharedPtr<FJsonObject> FuncRespPart = MakeShared<FJsonObject>();
			TSharedPtr<FJsonObject> FuncResp = MakeShared<FJsonObject>();
			FuncResp->SetStringField(TEXT("name"), Msg.ToolName.IsEmpty() ? Msg.ToolUseId : Msg.ToolName);

			TSharedPtr<FJsonObject> ResponseContent = MakeShared<FJsonObject>();
			ResponseContent->SetStringField(TEXT("result"), Msg.Content);
			FuncResp->SetObjectField(TEXT("response"), ResponseContent);
			FuncRespPart->SetObjectField(TEXT("functionResponse"), FuncResp);
			Parts.Add(MakeShared<FJsonValueObject>(FuncRespPart));
			break;
		}
		default:
			continue;
		}

		if (Parts.Num() > 0)
		{
			Content->SetArrayField(TEXT("parts"), Parts);
			Result.Add(MakeShared<FJsonValueObject>(Content));
		}
	}
	return Result;
}

TSharedPtr<FJsonObject> FAutonomixGeminiClient::ConvertToolSchemas(
	const TArray<TSharedPtr<FJsonObject>>& ToolSchemas) const
{
	TArray<TSharedPtr<FJsonValue>> Declarations;
	for (const TSharedPtr<FJsonObject>& Schema : ToolSchemas)
	{
		if (!Schema.IsValid()) continue;
		// Anthropic {name, description, input_schema} → Gemini functionDeclaration {name, description, parameters}
		TSharedPtr<FJsonObject> Decl = MakeShared<FJsonObject>();
		FString Name, Desc;
		Schema->TryGetStringField(TEXT("name"), Name);
		Schema->TryGetStringField(TEXT("description"), Desc);
		Decl->SetStringField(TEXT("name"), Name);
		Decl->SetStringField(TEXT("description"), Desc);
		const TSharedPtr<FJsonObject>* InputSchema = nullptr;
		if (Schema->TryGetObjectField(TEXT("input_schema"), InputSchema))
		{
			Decl->SetObjectField(TEXT("parameters"), *InputSchema);
		}
		Declarations.Add(MakeShared<FJsonValueObject>(Decl));
	}
	TSharedPtr<FJsonObject> ToolsObj = MakeShared<FJsonObject>();
	ToolsObj->SetArrayField(TEXT("function_declarations"), Declarations);
	return ToolsObj;
}

void FAutonomixGeminiClient::HandleRequestProgress(
	FHttpRequestPtr Request, uint64 BytesSent, uint64 BytesReceived)
{
	if (bRequestCancelled) return;

	const TArray<uint8>& ResponseBytes = Request->GetResponse() ?
		Request->GetResponse()->GetContent() : TArray<uint8>();

	if ((int32)BytesReceived > LastBytesReceived && ResponseBytes.Num() > 0)
	{
		int32 NewStart = LastBytesReceived;
		int32 NewCount = ResponseBytes.Num() - NewStart;
		LastBytesReceived = ResponseBytes.Num();

		if (NewCount > 0)
		{
			FString NewData = FString(NewCount, UTF8_TO_TCHAR(
				reinterpret_cast<const char*>(ResponseBytes.GetData() + NewStart)));
			// Buffer and process line by line (ndjson SSE)
			NdjsonLineBuffer += NewData;
			FString Line, Remaining;
			while (NdjsonLineBuffer.Split(TEXT("\n"), &Line, &Remaining))
			{
				Line.TrimEndInline();
				NdjsonLineBuffer = Remaining;
				if (Line.StartsWith(TEXT("data: ")))
				{
					FString JsonData = Line.Mid(6).TrimStartAndEnd();
					if (!JsonData.IsEmpty() && JsonData != TEXT("[DONE]"))
					{
						ProcessResponseChunk(JsonData);
					}
				}
				else if (!Line.IsEmpty() && Line[0] == TEXT('{'))
				{
					ProcessResponseChunk(Line);
				}
			}
		}
	}
}

void FAutonomixGeminiClient::HandleRequestComplete(
	FHttpRequestPtr Request, FHttpResponsePtr Response, bool bConnected)
{
	bRequestInFlight = false;
	if (bRequestCancelled) return;

	if (!bConnected || !Response.IsValid())
	{
		ErrorReceivedDelegate.Broadcast(FAutonomixHTTPError::ConnectionFailed(TEXT("Google Gemini")));
		RequestCompletedDelegate.Broadcast(false);
		return;
	}

	int32 Code = Response->GetResponseCode();
	if (Code != 200)
	{
		FAutonomixHTTPError Err = FAutonomixHTTPError::FromStatusCode(Code, Response->GetContentAsString(), TEXT("Google Gemini"));
		ErrorReceivedDelegate.Broadcast(Err);
		RequestCompletedDelegate.Broadcast(false);
		return;
	}

	// Process any remaining buffered data
	if (!NdjsonLineBuffer.TrimStartAndEnd().IsEmpty())
	{
		ProcessResponseChunk(NdjsonLineBuffer);
	}

	FinalizeResponse();
}

void FAutonomixGeminiClient::ProcessResponseChunk(const FString& JsonLine)
{
	TSharedPtr<FJsonObject> Obj;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonLine);
	if (!FJsonSerializer::Deserialize(Reader, Obj) || !Obj.IsValid()) return;

	// usageMetadata
	const TSharedPtr<FJsonObject>* UsageMeta = nullptr;
	if (Obj->TryGetObjectField(TEXT("usageMetadata"), UsageMeta))
	{
		ExtractTokenUsage(*UsageMeta);
	}

	// candidates[0].content.parts
	const TArray<TSharedPtr<FJsonValue>>* Candidates = nullptr;
	if (!Obj->TryGetArrayField(TEXT("candidates"), Candidates) || Candidates->Num() == 0) return;

	const TSharedPtr<FJsonObject>* Candidate = nullptr;
	if (!(*Candidates)[0]->TryGetObject(Candidate)) return;

	const TSharedPtr<FJsonObject>* Content = nullptr;
	if (!(*Candidate)->TryGetObjectField(TEXT("content"), Content)) return;

	const TArray<TSharedPtr<FJsonValue>>* Parts = nullptr;
	if (!(*Content)->TryGetArrayField(TEXT("parts"), Parts)) return;

	ProcessCandidateParts(*Parts);
}

void FAutonomixGeminiClient::ProcessCandidateParts(const TArray<TSharedPtr<FJsonValue>>& Parts)
{
	for (const TSharedPtr<FJsonValue>& PartVal : Parts)
	{
		const TSharedPtr<FJsonObject>* PartObj = nullptr;
		if (!PartVal->TryGetObject(PartObj)) continue;

		FString Text;
		if ((*PartObj)->TryGetStringField(TEXT("text"), Text) && !Text.IsEmpty())
		{
			CurrentAssistantContent += Text;
			StreamingTextDelegate.Broadcast(CurrentMessageId, Text);
		}

		const TSharedPtr<FJsonObject>* FuncCall = nullptr;
		if ((*PartObj)->TryGetObjectField(TEXT("functionCall"), FuncCall))
		{
			FString FuncName;
			(*FuncCall)->TryGetStringField(TEXT("name"), FuncName);
			const TSharedPtr<FJsonObject>* ArgsObj = nullptr;
			(*FuncCall)->TryGetObjectField(TEXT("args"), ArgsObj);

			TSharedPtr<FJsonObject> Args = ArgsObj ? *ArgsObj : MakeShared<FJsonObject>();
			Args->SetStringField(TEXT("tool_name"), FuncName);

			FAutonomixToolCall ToolCall;
			ToolCall.ToolUseId = FGuid::NewGuid().ToString(EGuidFormats::Digits);
			ToolCall.ToolName = FuncName;
			ToolCall.InputParams = Args;
			PendingToolCalls.Add(ToolCall);

			UE_LOG(LogAutonomix, Log, TEXT("GeminiClient: Tool call: %s"), *FuncName);
		}

		// Skip thought/thinking parts (they are internal reasoning text)
		FString ThoughtText;
		if ((*PartObj)->TryGetStringField(TEXT("thought"), ThoughtText))
		{
			// Intentionally not broadcast to UI — thinking is internal
		}
	}
}

void FAutonomixGeminiClient::ExtractTokenUsage(const TSharedPtr<FJsonObject>& UsageMetadata)
{
	if (!UsageMetadata.IsValid()) return;
	double Prompt = 0, Candidates = 0;
	UsageMetadata->TryGetNumberField(TEXT("promptTokenCount"), Prompt);
	UsageMetadata->TryGetNumberField(TEXT("candidatesTokenCount"), Candidates);
	LastTokenUsage.InputTokens = (int32)Prompt;
	LastTokenUsage.OutputTokens = (int32)Candidates;
	TokenUsageUpdatedDelegate.Broadcast(LastTokenUsage);
}

void FAutonomixGeminiClient::FinalizeResponse()
{
	if (bRequestCancelled) return;

	// Build ContentBlocksJson in Anthropic format (same pattern as OpenAI compat client)
	// so that save/load preserves tool calls and ConversationManager can detect orphaned tool_uses
	TArray<TSharedPtr<FJsonValue>> ContentBlocks;
	bool bHasToolCalls = false;

	if (!CurrentAssistantContent.IsEmpty())
	{
		TSharedPtr<FJsonObject> TextBlock = MakeShared<FJsonObject>();
		TextBlock->SetStringField(TEXT("type"), TEXT("text"));
		TextBlock->SetStringField(TEXT("text"), CurrentAssistantContent);
		ContentBlocks.Add(MakeShared<FJsonValueObject>(TextBlock));
	}

	// Fire pending tool calls AND build tool_use blocks
	for (const FAutonomixToolCall& TC : PendingToolCalls)
	{
		// Build Anthropic-style tool_use block for ContentBlocksJson
		TSharedPtr<FJsonObject> ToolUseBlock = MakeShared<FJsonObject>();
		ToolUseBlock->SetStringField(TEXT("type"), TEXT("tool_use"));
		ToolUseBlock->SetStringField(TEXT("id"), TC.ToolUseId);
		ToolUseBlock->SetStringField(TEXT("name"), TC.ToolName);
		ToolUseBlock->SetObjectField(TEXT("input"), TC.InputParams.IsValid() ? TC.InputParams : MakeShared<FJsonObject>());
		ContentBlocks.Add(MakeShared<FJsonValueObject>(ToolUseBlock));
		bHasToolCalls = true;

		ToolCallReceivedDelegate.Broadcast(TC);
	}
	PendingToolCalls.Empty();

	// Always fire MessageComplete for tool-only responses (same fix as OpenAI compat client)
	if (!CurrentAssistantContent.IsEmpty() || bHasToolCalls)
	{
		FAutonomixMessage CompletedMsg(EAutonomixMessageRole::Assistant, CurrentAssistantContent);
		CompletedMsg.MessageId = CurrentMessageId;

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
}
