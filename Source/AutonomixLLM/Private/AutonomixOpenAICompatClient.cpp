// Copyright Autonomix. All Rights Reserved.

#include "AutonomixOpenAICompatClient.h"
#include "AutonomixCoreModule.h"
#include "AutonomixSettings.h"
#include "HttpModule.h"
#include "Interfaces/IHttpResponse.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Misc/SecureHash.h"
#include "Async/Async.h"

// ==========================================================================
// Call ID sanitization for OpenAI Responses API
// Ported from Roo Code utils/tool-id.ts sanitizeOpenAiCallId()
// ==========================================================================

/** Sanitize a tool call ID to match OpenAI's validation pattern: ^[a-zA-Z0-9_-]+$
 *  and truncate to 64 characters max. Uses MD5 hash suffix for uniqueness when truncating. */
static FString SanitizeCallId(const FString& Id)
{
	static constexpr int32 MaxLength = 64;

	// Step 1: Replace any invalid characters with underscore
	FString Sanitized;
	Sanitized.Reserve(Id.Len());
	for (TCHAR Ch : Id)
	{
		if ((Ch >= TEXT('a') && Ch <= TEXT('z')) ||
			(Ch >= TEXT('A') && Ch <= TEXT('Z')) ||
			(Ch >= TEXT('0') && Ch <= TEXT('9')) ||
			Ch == TEXT('_') || Ch == TEXT('-'))
		{
			Sanitized.AppendChar(Ch);
		}
		else
		{
			Sanitized.AppendChar(TEXT('_'));
		}
	}

	// Step 2: Truncate with hash suffix if needed
	if (Sanitized.Len() <= MaxLength)
	{
		return Sanitized;
	}

	// Use 8-char MD5 hash suffix for uniqueness
	FString Hash = FMD5::HashAnsiString(*Id).Left(8);
	int32 PrefixLen = MaxLength - 1 - 8; // 1 for separator underscore
	return Sanitized.Left(PrefixLen) + TEXT("_") + Hash;
}

/** Get a human-readable provider name for error messages */
static FString GetProviderDisplayName(EAutonomixProvider Provider)
{
	switch (Provider)
	{
	case EAutonomixProvider::OpenAI:     return TEXT("OpenAI");
	case EAutonomixProvider::Azure:      return TEXT("Azure OpenAI");
	case EAutonomixProvider::DeepSeek:   return TEXT("DeepSeek");
	case EAutonomixProvider::Mistral:    return TEXT("Mistral");
	case EAutonomixProvider::xAI:        return TEXT("xAI");
	case EAutonomixProvider::OpenRouter: return TEXT("OpenRouter");
	case EAutonomixProvider::Ollama:     return TEXT("Ollama");
	case EAutonomixProvider::LMStudio:   return TEXT("LM Studio");
	case EAutonomixProvider::Custom:     return TEXT("Custom API");
	default:                             return TEXT("API");
	}
}

/**
 * Detect if a URL is an Azure OpenAI endpoint.
 * Ported from Roo Code openai.ts _isAzureOpenAI() — checks if the host ends with .azure.com.
 * This also catches the case where a user set OpenAI provider but supplied an Azure URL,
 * allowing seamless fallback to Azure wire format regardless of provider selection.
 */
static bool IsAzureUrl(const FString& Url)
{
	if (Url.IsEmpty()) return false;
	// Parse just the host from the URL. Azure resource URLs look like:
	// https://my-resource.openai.azure.com
	// https://my-resource.openai.azure.com/openai/deployments/...
	FString Lower = Url.ToLower();
	return Lower.Contains(TEXT(".azure.com")) || Lower.Contains(TEXT(".openai.azure.com"));
}

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
void FAutonomixOpenAICompatClient::SetAzureApiVersion(const FString& InApiVersion) { AzureApiVersion = InApiVersion; }
void FAutonomixOpenAICompatClient::SetOllamaContextSize(int32 InNumCtx) { OllamaNumCtx = InNumCtx; }

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

	// ---- Azure pre-flight validation ----
	// Azure requires base URL + deployment name + api-version; give a clear error early.
	const bool bIsAzureProvider = (Provider == EAutonomixProvider::Azure);
	const bool bIsAzureUrl      = IsAzureUrl(BaseUrl);  // auto-detect even with OpenAI provider
	bIsAzureRequest              = bIsAzureProvider || bIsAzureUrl;

	if (bIsAzureRequest)
	{
		if (BaseUrl.IsEmpty())
		{
			FAutonomixHTTPError Err;
			Err.Type = EAutonomixHTTPErrorType::InvalidResponse;
			Err.UserFriendlyMessage = TEXT("Azure OpenAI: Base URL is empty.\n\n")
				TEXT("Set it to: https://{your-resource-name}.openai.azure.com\n")
				TEXT("Edit in: Project Settings \u2192 Plugins \u2192 Autonomix \u2192 API | Azure OpenAI");
			ErrorReceivedDelegate.Broadcast(Err);
			RequestCompletedDelegate.Broadcast(false);
			return;
		}
		if (ModelId.IsEmpty())
		{
			FAutonomixHTTPError Err;
			Err.Type = EAutonomixHTTPErrorType::InvalidResponse;
			Err.UserFriendlyMessage = TEXT("Azure OpenAI: Deployment Name is empty.\n\n")
				TEXT("Set it to your Azure deployment name (NOT the base model name like 'gpt-4o').\n")
				TEXT("Example: 'my-gpt4-deployment', 'prod-gpt4o'\n")
				TEXT("Edit in: Project Settings \u2192 Plugins \u2192 Autonomix \u2192 API | Azure OpenAI");
			ErrorReceivedDelegate.Broadcast(Err);
			RequestCompletedDelegate.Broadcast(false);
			return;
		}
		if (ApiKey.IsEmpty())
		{
			UE_LOG(LogAutonomix, Error, TEXT("OpenAICompatClient: Azure API key not set."));
			FAutonomixHTTPError Err;
			Err.Type = EAutonomixHTTPErrorType::Unauthorized;
			Err.UserFriendlyMessage = TEXT("Azure OpenAI: API Key is empty.\n\n")
				TEXT("Enter your Azure API key (from Azure portal \u2192 OpenAI resource \u2192 Keys and Endpoint).\n")
				TEXT("Edit in: Project Settings \u2192 Plugins \u2192 Autonomix \u2192 API | Azure OpenAI");
			ErrorReceivedDelegate.Broadcast(Err);
			RequestCompletedDelegate.Broadcast(false);
			return;
		}
	}
	else if (ApiKey.IsEmpty() && Provider != EAutonomixProvider::Ollama && Provider != EAutonomixProvider::LMStudio)
	{
		UE_LOG(LogAutonomix, Error, TEXT("OpenAICompatClient: API key not set for provider %d."), (int32)Provider);
		ErrorReceivedDelegate.Broadcast(FAutonomixHTTPError::ConnectionFailed(GetProviderDisplayName(Provider)));
		RequestCompletedDelegate.Broadcast(false);
		return;
	}

	// Store for retry
	RetryHistory = ConversationHistory;
	RetrySystemPrompt = SystemPrompt;
	RetryToolSchemas = ToolSchemas;

	// ---- API type selection ----
	// Responses API (/v1/responses): official OpenAI only (GPT-5.x, GPT-4.1, o-series).
	// Azure, Ollama, LMStudio, Custom do NOT support the Responses API.
	// User report (Issue #9): Ollama logs show requests to BOTH /v1/chat/completions
	// AND /v1/responses — the latter is invalid for Ollama.
	const bool bIsLocalProvider = (Provider == EAutonomixProvider::Ollama ||
	                               Provider == EAutonomixProvider::LMStudio ||
	                               Provider == EAutonomixProvider::Custom);
	bUseResponsesAPI = (Provider == EAutonomixProvider::OpenAI) && !bIsAzureRequest && !bIsLocalProvider;

	TSharedPtr<FJsonObject> Body = BuildRequestBody(ConversationHistory, SystemPrompt, ToolSchemas);
	FString BodyString;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&BodyString);
	FJsonSerializer::Serialize(Body.ToSharedRef(), Writer);

	// ---- URL construction ----
	FString Url;
	if (bIsAzureRequest)
	{
		// Azure URL format (ported from Roo Code AzureOpenAI client):
		//   https://{resource}.openai.azure.com/openai/deployments/{deployment-name}/chat/completions?api-version={version}
		// The base URL may or may not include trailing slash or /openai path.
		FString AzureBase = BaseUrl;
		// Strip trailing slash for clean concatenation
		while (AzureBase.EndsWith(TEXT("/"))) AzureBase.RemoveAt(AzureBase.Len() - 1);
		// Strip any existing /openai/deployments path the user may have included
		// (we build the full path ourselves to ensure correctness)
		{
			int32 DeployIdx = INDEX_NONE;
			AzureBase.FindLastChar(TEXT('/'), DeployIdx);
			// Only strip if the user appended /openai... path
			FString Lower = AzureBase.ToLower();
			int32 OpenAIIdx = Lower.Find(TEXT("/openai"), ESearchCase::IgnoreCase, ESearchDir::FromStart);
			if (OpenAIIdx != INDEX_NONE)
			{
				AzureBase = AzureBase.Left(OpenAIIdx);
			}
		}

		// Build full Azure deployment URL
		Url = FString::Printf(TEXT("%s/openai/deployments/%s/chat/completions"), *AzureBase, *ModelId);

		// Append api-version query parameter (required by Azure)
		FString ApiVersion = AzureApiVersion.IsEmpty() ? TEXT("2024-02-01") : AzureApiVersion;
		Url += FString::Printf(TEXT("?api-version=%s"), *ApiVersion);

		UE_LOG(LogAutonomix, Log, TEXT("OpenAICompatClient: Azure request URL: %s"), *Url);
	}
	else
	{
		Url = BaseUrl;
		if (!Url.EndsWith(TEXT("/"))) Url += TEXT("/");
		if (bUseResponsesAPI)
		{
			Url += TEXT("responses");
		}
		else
		{
			Url += TEXT("chat/completions");
		}
	}

	CurrentRequest = FHttpModule::Get().CreateRequest();
	CurrentRequest->SetURL(Url);
	CurrentRequest->SetVerb(TEXT("POST"));
	CurrentRequest->SetHeader(TEXT("Content-Type"), TEXT("application/json"));

	if (bIsAzureRequest)
	{
		// Azure uses 'api-key' header, NOT 'Authorization: Bearer'.
		// This is the primary difference from the official OpenAI API.
		// Ported from Roo Code openai.ts AzureOpenAI client which passes apiKey directly
		// (the AzureOpenAI SDK sets 'api-key' internally).
		CurrentRequest->SetHeader(TEXT("api-key"), ApiKey);
		UE_LOG(LogAutonomix, Log, TEXT("OpenAICompatClient: Using Azure auth (api-key header)."));
	}
	else if (!ApiKey.IsEmpty())
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

	// Local model inference on consumer GPUs can take 60-180+ seconds for
	// large prompts. The default 120s timeout is too short — use 300s minimum.
	// Roo Code native-ollama.ts: "The ollama npm package handles timeouts internally"
	// (i.e. no explicit short timeout). Cloud providers keep the user's configured timeout.
	float TimeoutSec = (float)(Settings ? Settings->RequestTimeoutSeconds : 120);
	if (bIsLocalProvider && TimeoutSec < 300.0f)
	{
		TimeoutSec = 300.0f;
		UE_LOG(LogAutonomix, Log, TEXT("OpenAICompatClient: Local provider timeout increased to 300s."));
	}
	CurrentRequest->SetTimeout(TimeoutSec);

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
		ErrorReceivedDelegate.Broadcast(FAutonomixHTTPError::ConnectionFailed(GetProviderDisplayName(Provider)));
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
	if (bUseResponsesAPI)
	{
		return BuildResponsesAPIBody(History, SystemPrompt, ToolSchemas);
	}
	return BuildChatCompletionsBody(History, SystemPrompt, ToolSchemas);
}

// ==========================================================================
// Schema sanitization for OpenAI Responses API
// Ported from Roo Code openai-native.ts ensureAllRequired() + ensureAdditionalPropertiesFalse()
// ==========================================================================

/** Recursively checks if a schema contains any free-form objects (type:"object" without "properties").
 *  Such schemas are incompatible with strict:true because OpenAI requires required=[all keys]. */
static bool HasFreeFormObjects(const TSharedPtr<FJsonObject>& Schema)
{
	if (!Schema.IsValid()) return false;

	FString Type;
	Schema->TryGetStringField(TEXT("type"), Type);

	if (Type == TEXT("object"))
	{
		const TSharedPtr<FJsonObject>* PropsObj = nullptr;
		if (!Schema->TryGetObjectField(TEXT("properties"), PropsObj))
		{
			// type:"object" with no "properties" field = free-form dictionary
			return true;
		}

		// Check nested properties recursively
		TArray<FString> AllKeys;
		(*PropsObj)->Values.GetKeys(AllKeys);
		for (const FString& Key : AllKeys)
		{
			const TSharedPtr<FJsonObject>* PropObj = nullptr;
			if ((*PropsObj)->TryGetObjectField(Key, PropObj))
			{
				if (HasFreeFormObjects(*PropObj))
				{
					return true;
				}
			}
		}
	}
	else if (Type == TEXT("array"))
	{
		const TSharedPtr<FJsonObject>* ItemsObj = nullptr;
		if (Schema->TryGetObjectField(TEXT("items"), ItemsObj))
		{
			if (HasFreeFormObjects(*ItemsObj))
			{
				return true;
			}
		}
	}

	return false;
}

/** Recursively adds additionalProperties:false and required:[all keys] to JSON schemas.
 *  Used for strict:true tools. OpenAI Responses API rejects schemas missing these fields.
 *  Ported from Roo Code openai-native.ts ensureAllRequired(). */
static void EnsureStrictSchema(TSharedPtr<FJsonObject> Schema)
{
	if (!Schema.IsValid()) return;

	FString Type;
	Schema->TryGetStringField(TEXT("type"), Type);
	if (Type != TEXT("object")) return;

	// Add additionalProperties: false
	Schema->SetBoolField(TEXT("additionalProperties"), false);

	// Make all properties required
	const TSharedPtr<FJsonObject>* PropsObj = nullptr;
	if (Schema->TryGetObjectField(TEXT("properties"), PropsObj))
	{
		TArray<FString> AllKeys;
		(*PropsObj)->Values.GetKeys(AllKeys);

		// Set required = all keys
		TArray<TSharedPtr<FJsonValue>> RequiredArr;
		for (const FString& Key : AllKeys)
		{
			RequiredArr.Add(MakeShared<FJsonValueString>(Key));
		}
		Schema->SetArrayField(TEXT("required"), RequiredArr);

		// Recurse into nested objects and array items
		for (const FString& Key : AllKeys)
		{
			const TSharedPtr<FJsonObject>* PropObj = nullptr;
			if ((*PropsObj)->TryGetObjectField(Key, PropObj))
			{
				FString PropType;
				(*PropObj)->TryGetStringField(TEXT("type"), PropType);
				if (PropType == TEXT("object"))
				{
					EnsureStrictSchema(const_cast<TSharedPtr<FJsonObject>&>(*PropObj));
				}
				else if (PropType == TEXT("array"))
				{
					const TSharedPtr<FJsonObject>* ItemsObj = nullptr;
					if ((*PropObj)->TryGetObjectField(TEXT("items"), ItemsObj))
					{
						FString ItemType;
						(*ItemsObj)->TryGetStringField(TEXT("type"), ItemType);
						if (ItemType == TEXT("object"))
						{
							EnsureStrictSchema(const_cast<TSharedPtr<FJsonObject>&>(*ItemsObj));
						}
					}
				}
			}
		}
	}
}

/** Recursively adds additionalProperties:false to all object schemas without modifying required.
 *  Used for strict:false tools that have free-form objects (type:"object" without "properties").
 *  Ported from Roo Code openai-native.ts ensureAdditionalPropertiesFalse(). */
static void EnsureAdditionalPropertiesFalse(TSharedPtr<FJsonObject> Schema)
{
	if (!Schema.IsValid()) return;

	FString Type;
	Schema->TryGetStringField(TEXT("type"), Type);
	if (Type != TEXT("object")) return;

	// Add additionalProperties: false
	Schema->SetBoolField(TEXT("additionalProperties"), false);

	// Recurse into nested objects and array items (but do NOT modify required)
	const TSharedPtr<FJsonObject>* PropsObj = nullptr;
	if (Schema->TryGetObjectField(TEXT("properties"), PropsObj))
	{
		TArray<FString> AllKeys;
		(*PropsObj)->Values.GetKeys(AllKeys);

		for (const FString& Key : AllKeys)
		{
			const TSharedPtr<FJsonObject>* PropObj = nullptr;
			if ((*PropsObj)->TryGetObjectField(Key, PropObj))
			{
				FString PropType;
				(*PropObj)->TryGetStringField(TEXT("type"), PropType);
				if (PropType == TEXT("object"))
				{
					EnsureAdditionalPropertiesFalse(const_cast<TSharedPtr<FJsonObject>&>(*PropObj));
				}
				else if (PropType == TEXT("array"))
				{
					const TSharedPtr<FJsonObject>* ItemsObj = nullptr;
					if ((*PropObj)->TryGetObjectField(TEXT("items"), ItemsObj))
					{
						FString ItemType;
						(*ItemsObj)->TryGetStringField(TEXT("type"), ItemType);
						if (ItemType == TEXT("object"))
						{
							EnsureAdditionalPropertiesFalse(const_cast<TSharedPtr<FJsonObject>&>(*ItemsObj));
						}
					}
				}
			}
		}
	}
}

// ==========================================================================
// Responses API (OpenAI native — GPT-5.x, GPT-4.1, o-series with tools)
// Ported from Roo Code openai-native.ts buildRequestBody + formatFullConversation
// ==========================================================================

TSharedPtr<FJsonObject> FAutonomixOpenAICompatClient::BuildResponsesAPIBody(
	const TArray<FAutonomixMessage>& History,
	const FString& SystemPrompt,
	const TArray<TSharedPtr<FJsonObject>>& ToolSchemas) const
{
	TSharedPtr<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetStringField(TEXT("model"), ModelId);
	Body->SetBoolField(TEXT("stream"), bStreamingEnabled);
	Body->SetBoolField(TEXT("store"), false);  // Stateless operation

	// System prompt goes in top-level 'instructions' field (not as a message)
	if (!SystemPrompt.IsEmpty())
	{
		Body->SetStringField(TEXT("instructions"), SystemPrompt);
	}

	// max_output_tokens (Responses API uses this, not max_tokens or max_completion_tokens)
	Body->SetNumberField(TEXT("max_output_tokens"), (double)MaxTokens);

	// Detect if model supports reasoning effort (ported from Roo Code model info):
	// - o-series (o1, o3, o4) → YES
	// - GPT-5.x → YES
	// - gpt-4o, gpt-4.1 (including mini/nano) → NO
	// - codex models → NO (they are code-specific, not reasoning)
	bool bModelSupportsReasoning =
		ModelId.StartsWith(TEXT("o1")) ||
		ModelId.StartsWith(TEXT("o3")) ||
		ModelId.StartsWith(TEXT("o4")) ||
		ModelId.StartsWith(TEXT("gpt-5"));

	// Effective reasoning effort: auto-disable for non-reasoning models
	EAutonomixReasoningEffort EffectiveReasoning =
		bModelSupportsReasoning ? ReasoningEffort : EAutonomixReasoningEffort::Disabled;

	// Temperature: reasoning models don't support temperature
	bool bModelSupportsTemp = !bModelSupportsReasoning &&
		(EffectiveReasoning == EAutonomixReasoningEffort::Disabled);
	if (bModelSupportsTemp)
	{
		Body->SetNumberField(TEXT("temperature"), 0.0);
	}

	// Reasoning effort (only for models that support it)
	if (EffectiveReasoning != EAutonomixReasoningEffort::Disabled)
	{
		TSharedPtr<FJsonObject> ReasoningObj = MakeShared<FJsonObject>();
		FString EffortStr = ReasoningEffortToString(EffectiveReasoning);
		if (!EffortStr.IsEmpty())
		{
			ReasoningObj->SetStringField(TEXT("effort"), EffortStr);
		}
		Body->SetObjectField(TEXT("reasoning"), ReasoningObj);
	}

	// Input array (Responses API format: messages + function_call + function_call_output items)
	Body->SetArrayField(TEXT("input"), ConvertToResponsesInput(History));

	// Tools (Responses API format: same as Chat Completions but with optional 'strict' field)
	if (ToolSchemas.Num() > 0)
	{
		TArray<TSharedPtr<FJsonValue>> ToolsArray;
		for (const TSharedPtr<FJsonObject>& Schema : ToolSchemas)
		{
			if (!Schema.IsValid()) continue;

			TSharedPtr<FJsonObject> Tool = MakeShared<FJsonObject>();
			Tool->SetStringField(TEXT("type"), TEXT("function"));

			FString Name, Desc;
			Schema->TryGetStringField(TEXT("name"), Name);
			Schema->TryGetStringField(TEXT("description"), Desc);
			Tool->SetStringField(TEXT("name"), Name);
			if (!Desc.IsEmpty()) Tool->SetStringField(TEXT("description"), Desc);

			const TSharedPtr<FJsonObject>* InputSchema = nullptr;
			if (Schema->TryGetObjectField(TEXT("input_schema"), InputSchema))
			{
				// Deep clone the schema so we don't mutate the original
				FString SchemaStr;
				TSharedRef<TJsonWriter<>> SchemaWriter = TJsonWriterFactory<>::Create(&SchemaStr);
				FJsonSerializer::Serialize(InputSchema->ToSharedRef(), SchemaWriter);
				TSharedPtr<FJsonObject> ClonedSchema;
				TSharedRef<TJsonReader<>> SchemaReader = TJsonReaderFactory<>::Create(SchemaStr);
				FJsonSerializer::Deserialize(SchemaReader, ClonedSchema);

				// Two-path approach ported from Roo Code openai-native.ts:
				// - Tools with well-defined schemas: strict:true + ensureAllRequired
				// - Tools with free-form objects (type:"object" without "properties"):
				//   strict:false + ensureAdditionalPropertiesFalse
				const bool bHasFreeForm = HasFreeFormObjects(ClonedSchema);
				if (bHasFreeForm)
				{
					EnsureAdditionalPropertiesFalse(ClonedSchema);
				}
				else
				{
					EnsureStrictSchema(ClonedSchema);
				}
				Tool->SetObjectField(TEXT("parameters"), ClonedSchema);
				Tool->SetBoolField(TEXT("strict"), !bHasFreeForm);
			}
			else
			{
				// No input_schema — use strict:false
				Tool->SetBoolField(TEXT("strict"), false);
			}

			ToolsArray.Add(MakeShared<FJsonValueObject>(Tool));
		}
		Body->SetArrayField(TEXT("tools"), ToolsArray);
	}

	return Body;
}

TArray<TSharedPtr<FJsonValue>> FAutonomixOpenAICompatClient::ConvertToResponsesInput(
	const TArray<FAutonomixMessage>& Messages) const
{
	// Ported from Roo Code openai-native.ts formatFullConversation
	// Responses API uses: {role: "user", content: [{type: "input_text", text}]}
	//                     {role: "assistant", content: [{type: "output_text", text}]}
	//                     {type: "function_call", call_id, name, arguments}
	//                     {type: "function_call_output", call_id, output}
	TArray<TSharedPtr<FJsonValue>> Result;

	for (const FAutonomixMessage& Msg : Messages)
	{
		switch (Msg.Role)
		{
		case EAutonomixMessageRole::User:
		{
			TSharedPtr<FJsonObject> Item = MakeShared<FJsonObject>();
			Item->SetStringField(TEXT("role"), TEXT("user"));
			TArray<TSharedPtr<FJsonValue>> ContentArr;
			TSharedPtr<FJsonObject> TextPart = MakeShared<FJsonObject>();
			TextPart->SetStringField(TEXT("type"), TEXT("input_text"));
			TextPart->SetStringField(TEXT("text"), Msg.Content);
			ContentArr.Add(MakeShared<FJsonValueObject>(TextPart));
			Item->SetArrayField(TEXT("content"), ContentArr);
			Result.Add(MakeShared<FJsonValueObject>(Item));
			break;
		}
		case EAutonomixMessageRole::Assistant:
		{
			// Check for tool_use blocks in ContentBlocksJson
			if (!Msg.ContentBlocksJson.IsEmpty())
			{
				TArray<TSharedPtr<FJsonValue>> Blocks;
				TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Msg.ContentBlocksJson);
				if (FJsonSerializer::Deserialize(Reader, Blocks))
				{
					// Add text content as assistant message
					TArray<TSharedPtr<FJsonValue>> ContentArr;
					TArray<TSharedPtr<FJsonValue>> ToolCalls;

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
							TSharedPtr<FJsonObject> TextPart = MakeShared<FJsonObject>();
							TextPart->SetStringField(TEXT("type"), TEXT("output_text"));
							TextPart->SetStringField(TEXT("text"), Text);
							ContentArr.Add(MakeShared<FJsonValueObject>(TextPart));
						}
						else if (Type == TEXT("tool_use"))
						{
							// Convert to Responses API function_call item
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

							TSharedPtr<FJsonObject> FuncCall = MakeShared<FJsonObject>();
							FuncCall->SetStringField(TEXT("type"), TEXT("function_call"));
							FuncCall->SetStringField(TEXT("call_id"), SanitizeCallId(TUId));
							FuncCall->SetStringField(TEXT("name"), TUName);
							FuncCall->SetStringField(TEXT("arguments"), InputStr);
							ToolCalls.Add(MakeShared<FJsonValueObject>(FuncCall));
						}
					}

					// Add assistant message with text content
					if (ContentArr.Num() > 0)
					{
						TSharedPtr<FJsonObject> AssistantItem = MakeShared<FJsonObject>();
						AssistantItem->SetStringField(TEXT("role"), TEXT("assistant"));
						AssistantItem->SetArrayField(TEXT("content"), ContentArr);
						Result.Add(MakeShared<FJsonValueObject>(AssistantItem));
					}

					// Add tool calls as separate items
					for (const TSharedPtr<FJsonValue>& TC : ToolCalls)
					{
						Result.Add(TC);
					}
				}
			}
			else if (!Msg.Content.IsEmpty())
			{
				TSharedPtr<FJsonObject> Item = MakeShared<FJsonObject>();
				Item->SetStringField(TEXT("role"), TEXT("assistant"));
				TArray<TSharedPtr<FJsonValue>> ContentArr;
				TSharedPtr<FJsonObject> TextPart = MakeShared<FJsonObject>();
				TextPart->SetStringField(TEXT("type"), TEXT("output_text"));
				TextPart->SetStringField(TEXT("text"), Msg.Content);
				ContentArr.Add(MakeShared<FJsonValueObject>(TextPart));
				Item->SetArrayField(TEXT("content"), ContentArr);
				Result.Add(MakeShared<FJsonValueObject>(Item));
			}
			break;
		}
		case EAutonomixMessageRole::ToolResult:
		{
			// Responses API: function_call_output item
			TSharedPtr<FJsonObject> Item = MakeShared<FJsonObject>();
			Item->SetStringField(TEXT("type"), TEXT("function_call_output"));
			Item->SetStringField(TEXT("call_id"), SanitizeCallId(Msg.ToolUseId));
			Item->SetStringField(TEXT("output"), Msg.Content);
			Result.Add(MakeShared<FJsonValueObject>(Item));
			break;
		}
		default:
			break;
		}
	}

	// CRITICAL: OpenAI Responses API requires every function_call to have a matching
	// function_call_output. If a conversation was interrupted mid-tool-execution
	// (or loaded from a Claude session), orphaned function_calls cause:
	// "No tool output found for function call toolu_XXX"
	// Scan the result and inject synthetic outputs for orphaned function_calls.
	TSet<FString> FunctionCallIds;
	TSet<FString> FunctionOutputIds;
	for (const TSharedPtr<FJsonValue>& Val : Result)
	{
		TSharedPtr<FJsonObject> Obj = Val->AsObject();
		if (!Obj.IsValid()) continue;
		FString Type;
		Obj->TryGetStringField(TEXT("type"), Type);
		if (Type == TEXT("function_call"))
		{
			FString CallId;
			Obj->TryGetStringField(TEXT("call_id"), CallId);
			if (!CallId.IsEmpty()) FunctionCallIds.Add(CallId);
		}
		else if (Type == TEXT("function_call_output"))
		{
			FString CallId;
			Obj->TryGetStringField(TEXT("call_id"), CallId);
			if (!CallId.IsEmpty()) FunctionOutputIds.Add(CallId);
		}
	}

	// Direction 1: Inject synthetic function_call_output for orphaned function_calls
	for (const FString& CallId : FunctionCallIds)
	{
		if (!FunctionOutputIds.Contains(CallId))
		{
			TSharedPtr<FJsonObject> SyntheticOutput = MakeShared<FJsonObject>();
			SyntheticOutput->SetStringField(TEXT("type"), TEXT("function_call_output"));
			SyntheticOutput->SetStringField(TEXT("call_id"), CallId);
			SyntheticOutput->SetStringField(TEXT("output"), TEXT("(tool execution was interrupted)"));
			Result.Add(MakeShared<FJsonValueObject>(SyntheticOutput));
			UE_LOG(LogAutonomix, Warning,
				TEXT("OpenAICompatClient: Injected synthetic function_call_output for orphaned call_id=%s"), *CallId);
		}
	}

	// Direction 2: Remove orphaned function_call_outputs that have no matching function_call.
	// This happens when resuming a Claude session — ToolResult messages reference toolu_XXX IDs
	// but the corresponding assistant tool_use blocks may be corrupted or missing.
	Result.RemoveAll([&FunctionCallIds](const TSharedPtr<FJsonValue>& Val) -> bool
	{
		TSharedPtr<FJsonObject> Obj = Val->AsObject();
		if (!Obj.IsValid()) return false;
		FString Type;
		Obj->TryGetStringField(TEXT("type"), Type);
		if (Type != TEXT("function_call_output")) return false;
		FString CallId;
		Obj->TryGetStringField(TEXT("call_id"), CallId);
		if (CallId.IsEmpty()) return true; // Remove empty-ID outputs
		if (!FunctionCallIds.Contains(CallId))
		{
			UE_LOG(LogAutonomix, Warning,
				TEXT("OpenAICompatClient: Removed orphaned function_call_output with no matching call: call_id=%s"), *CallId);
			return true; // Remove — no matching function_call exists
		}
		return false;
	});

	return Result;
}

void FAutonomixOpenAICompatClient::ProcessResponsesSSEEvent(const FString& DataJson)
{
	// Ported from Roo Code openai-native.ts processEvent + handleStreamResponse
	// Responses API sends typed events like: response.output_text.delta, response.output_item.done, etc.
	TSharedPtr<FJsonObject> Obj;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(DataJson);
	if (!FJsonSerializer::Deserialize(Reader, Obj) || !Obj.IsValid()) return;

	FString EventType;
	Obj->TryGetStringField(TEXT("type"), EventType);

	// Text deltas
	if (EventType == TEXT("response.output_text.delta") || EventType == TEXT("response.text.delta"))
	{
		FString Delta;
		Obj->TryGetStringField(TEXT("delta"), Delta);
		if (!Delta.IsEmpty())
		{
			CurrentAssistantContent += Delta;
			StreamingTextDelegate.Broadcast(CurrentMessageId, Delta);
		}
		return;
	}

	// Tool call argument deltas — accumulate
	if (EventType == TEXT("response.function_call_arguments.delta") ||
		EventType == TEXT("response.tool_call_arguments.delta"))
	{
		FString ArgsDelta;
		Obj->TryGetStringField(TEXT("delta"), ArgsDelta);
		if (!ArgsDelta.IsEmpty() && PendingToolCallStates.Num() > 0)
		{
			PendingToolCallStates.Last().ArgumentsAccumulated += ArgsDelta;
		}
		return;
	}

	// Output item added — captures function_call identity
	if (EventType == TEXT("response.output_item.added"))
	{
		const TSharedPtr<FJsonObject>* ItemObj = nullptr;
		if (Obj->TryGetObjectField(TEXT("item"), ItemObj))
		{
			FString ItemType;
			(*ItemObj)->TryGetStringField(TEXT("type"), ItemType);
			if (ItemType == TEXT("function_call"))
			{
				FPendingToolCallState State;
				(*ItemObj)->TryGetStringField(TEXT("call_id"), State.ToolUseId);
				(*ItemObj)->TryGetStringField(TEXT("name"), State.ToolName);
				State.Index = PendingToolCallStates.Num();
				PendingToolCallStates.Add(State);
			}
		}
		return;
	}

	// Output item done — function_call complete (fallback for non-streaming tool calls)
	if (EventType == TEXT("response.output_item.done"))
	{
		const TSharedPtr<FJsonObject>* ItemObj = nullptr;
		if (Obj->TryGetObjectField(TEXT("item"), ItemObj))
		{
			FString ItemType;
			(*ItemObj)->TryGetStringField(TEXT("type"), ItemType);

			if (ItemType == TEXT("function_call"))
			{
				FString CallId, Name, ArgsStr;
				(*ItemObj)->TryGetStringField(TEXT("call_id"), CallId);
				(*ItemObj)->TryGetStringField(TEXT("name"), Name);
				(*ItemObj)->TryGetStringField(TEXT("arguments"), ArgsStr);

				// Check if we already have this from streaming deltas
				bool bAlreadyStreamed = false;
				for (const FPendingToolCallState& S : PendingToolCallStates)
				{
					if (S.ToolUseId == CallId && !S.ArgumentsAccumulated.IsEmpty())
					{
						bAlreadyStreamed = true;
						break;
					}
				}

				if (!bAlreadyStreamed && !Name.IsEmpty())
				{
					FPendingToolCallState State;
					State.ToolUseId = CallId;
					State.ToolName = Name;
					State.ArgumentsAccumulated = ArgsStr;
					State.Index = PendingToolCallStates.Num();
					PendingToolCallStates.Add(State);
				}
			}
			// Text output done — fallback for non-streaming text
			else if ((ItemType == TEXT("text") || ItemType == TEXT("output_text")) && CurrentAssistantContent.IsEmpty())
			{
				FString Text;
				(*ItemObj)->TryGetStringField(TEXT("text"), Text);
				if (!Text.IsEmpty())
				{
					CurrentAssistantContent += Text;
					StreamingTextDelegate.Broadcast(CurrentMessageId, Text);
				}
			}
		}
		return;
	}

	// Response done/completed — extract usage
	if (EventType == TEXT("response.done") || EventType == TEXT("response.completed"))
	{
		const TSharedPtr<FJsonObject>* RespObj = nullptr;
		if (Obj->TryGetObjectField(TEXT("response"), RespObj))
		{
			const TSharedPtr<FJsonObject>* UsageObj = nullptr;
			if ((*RespObj)->TryGetObjectField(TEXT("usage"), UsageObj))
			{
				ExtractTokenUsage(*UsageObj);
			}
		}
		return;
	}

	// Error event
	if (EventType == TEXT("response.error") || EventType == TEXT("error"))
	{
		const TSharedPtr<FJsonObject>* ErrObj = nullptr;
		FString ErrMsg;
		if (Obj->TryGetObjectField(TEXT("error"), ErrObj))
		{
			(*ErrObj)->TryGetStringField(TEXT("message"), ErrMsg);
		}
		if (ErrMsg.IsEmpty()) Obj->TryGetStringField(TEXT("message"), ErrMsg);
		if (!ErrMsg.IsEmpty())
		{
			UE_LOG(LogAutonomix, Error, TEXT("OpenAICompatClient: Responses API error: %s"), *ErrMsg);
		}
		return;
	}

	// Ignore: response.created, response.in_progress, response.queued, response.content_part.*
}

// ==========================================================================
// Chat Completions API (legacy — DeepSeek, Mistral, xAI, Ollama, etc.)
// ==========================================================================

TSharedPtr<FJsonObject> FAutonomixOpenAICompatClient::BuildChatCompletionsBody(
	const TArray<FAutonomixMessage>& History,
	const FString& SystemPrompt,
	const TArray<TSharedPtr<FJsonObject>>& ToolSchemas) const
{
	TSharedPtr<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetStringField(TEXT("model"), ModelId);
	Body->SetNumberField(TEXT("max_tokens"), (double)MaxTokens);
	Body->SetBoolField(TEXT("stream"), bStreamingEnabled);

	// Opt in to streaming usage
	if (bStreamingEnabled)
	{
		TSharedPtr<FJsonObject> StreamOpts = MakeShared<FJsonObject>();
		StreamOpts->SetBoolField(TEXT("include_usage"), true);
		Body->SetObjectField(TEXT("stream_options"), StreamOpts);
	}

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

	// =========================================================================
	// Ollama num_ctx: Controls the context window size for the model.
	//
	// CRITICAL: Ollama defaults to num_ctx=2048 if not specified, which is
	// far too small for Autonomix's prompts (system prompt + tools + history
	// = 8K-30K+ tokens). The model silently truncates, causing nonsensical
	// responses or immediate context overflow errors.
	//
	// Roo Code native-ollama.ts passes this via: options: { num_ctx: N }
	// Ollama's OpenAI-compatible endpoint accepts it in the same place.
	// =========================================================================
	if (OllamaNumCtx > 0 && (Provider == EAutonomixProvider::Ollama || Provider == EAutonomixProvider::LMStudio))
	{
		TSharedPtr<FJsonObject> OllamaOptions = MakeShared<FJsonObject>();
		OllamaOptions->SetNumberField(TEXT("num_ctx"), (double)OllamaNumCtx);
		Body->SetObjectField(TEXT("options"), OllamaOptions);
		UE_LOG(LogAutonomix, Log, TEXT("OpenAICompatClient: Ollama num_ctx set to %d."), OllamaNumCtx);
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
		ErrorReceivedDelegate.Broadcast(FAutonomixHTTPError::ConnectionFailed(GetProviderDisplayName(Provider)));
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
		Err.UserFriendlyMessage = FString::Printf(
			TEXT("Rate limited by %s. Please wait a moment before retrying."), *GetProviderDisplayName(Provider));
		ErrorReceivedDelegate.Broadcast(Err);
		RequestCompletedDelegate.Broadcast(false);
		return;
	}

	if (Code != 200)
	{
		FAutonomixHTTPError Err = FAutonomixHTTPError::FromStatusCode(Code, Response->GetContentAsString(), GetProviderDisplayName(Provider));
		ErrorReceivedDelegate.Broadcast(Err);
		RequestCompletedDelegate.Broadcast(false);
		return;
	}

	ConsecutiveRateLimits = 0;

	// =========================================================================
	// NON-STREAMING RESPONSE PARSING
	//
	// When streaming is disabled (Ollama, LM Studio, Custom endpoints), the
	// response body is a complete JSON object — NOT SSE events. The SSE progress
	// handler never fires, so we must parse the full body here.
	//
	// Chat Completions non-streaming format:
	// {
	//   "choices": [{"message": {"role": "assistant", "content": "...", "tool_calls": [...]}}],
	//   "usage": {"prompt_tokens": N, "completion_tokens": N}
	// }
	// =========================================================================
	if (!bStreamingEnabled)
	{
		FString FullBody = Response->GetContentAsString();
		UE_LOG(LogAutonomix, Log, TEXT("OpenAICompatClient: Non-streaming response received (%d bytes)."), FullBody.Len());

		TSharedPtr<FJsonObject> ResponseObj;
		TSharedRef<TJsonReader<>> JsonReader = TJsonReaderFactory<>::Create(FullBody);
		if (FJsonSerializer::Deserialize(JsonReader, ResponseObj) && ResponseObj.IsValid())
		{
			// Extract usage
			const TSharedPtr<FJsonObject>* UsageObj = nullptr;
			if (ResponseObj->TryGetObjectField(TEXT("usage"), UsageObj))
			{
				ExtractTokenUsage(*UsageObj);
			}

			// Extract choices[0].message
			const TArray<TSharedPtr<FJsonValue>>* Choices = nullptr;
			if (ResponseObj->TryGetArrayField(TEXT("choices"), Choices) && Choices->Num() > 0)
			{
				const TSharedPtr<FJsonObject>* ChoiceObj = nullptr;
				if ((*Choices)[0]->TryGetObject(ChoiceObj))
				{
					const TSharedPtr<FJsonObject>* MessageObj = nullptr;
					if ((*ChoiceObj)->TryGetObjectField(TEXT("message"), MessageObj))
					{
						// Text content
						FString Content;
						if ((*MessageObj)->TryGetStringField(TEXT("content"), Content) && !Content.IsEmpty())
						{
							CurrentAssistantContent = Content;
							StreamingTextDelegate.Broadcast(CurrentMessageId, Content);
						}

						// Tool calls
						const TArray<TSharedPtr<FJsonValue>>* ToolCalls = nullptr;
						if ((*MessageObj)->TryGetArrayField(TEXT("tool_calls"), ToolCalls))
						{
							for (const TSharedPtr<FJsonValue>& TCVal : *ToolCalls)
							{
								const TSharedPtr<FJsonObject>* TCObj = nullptr;
								if (!TCVal->TryGetObject(TCObj)) continue;

								FPendingToolCallState State;
								(*TCObj)->TryGetStringField(TEXT("id"), State.ToolUseId);
								State.Index = PendingToolCallStates.Num();

								const TSharedPtr<FJsonObject>* FuncObj = nullptr;
								if ((*TCObj)->TryGetObjectField(TEXT("function"), FuncObj))
								{
									(*FuncObj)->TryGetStringField(TEXT("name"), State.ToolName);
									(*FuncObj)->TryGetStringField(TEXT("arguments"), State.ArgumentsAccumulated);
								}

								PendingToolCallStates.Add(State);
							}
						}
					}
				}
			}
		}
		else
		{
			UE_LOG(LogAutonomix, Error, TEXT("OpenAICompatClient: Failed to parse non-streaming response JSON."));
		}

		FinalizeResponse();
		return;
	}

	// =========================================================================
	// STREAMING RESPONSE FLUSH (SSE mode only)
	// =========================================================================
	// CRITICAL FIX: Do NOT re-process the full response body here.
	// HandleRequestProgress() already processed all bytes incrementally via
	// LastBytesReceived offset tracking. Calling ProcessSSEChunk(FullBody) would
	// re-feed the entire response into SSELineBuffer, causing DOUBLED content
	// and DOUBLED tool calls — the root cause of GitHub Issue #1 ("OpenAI doesn't work").
	//
	// Instead, only flush the remaining incomplete line buffer (the last partial
	// line that didn't end with \n during streaming). This matches Roo Code's
	// pattern where the stream iterator naturally delivers remaining data.
	if (!SSELineBuffer.IsEmpty())
	{
		// Force-flush: treat remaining buffer as a complete line
		FString Remaining = SSELineBuffer;
		SSELineBuffer.Empty();
		Remaining.TrimEndInline();
		if (Remaining.StartsWith(TEXT("data: ")))
		{
			FString JsonData = Remaining.Mid(6).TrimStartAndEnd();
			if (JsonData != TEXT("[DONE]") && !JsonData.IsEmpty())
			{
				// Must dispatch to the correct handler based on API type
				if (bUseResponsesAPI)
				{
					ProcessResponsesSSEEvent(JsonData);
				}
				else
				{
					ProcessSSEEvent(JsonData);
				}
			}
		}
	}
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
			if (JsonData != TEXT("[DONE]") && !JsonData.IsEmpty())
			{
				if (bUseResponsesAPI)
				{
					ProcessResponsesSSEEvent(JsonData);
				}
				else
				{
					ProcessSSEEvent(JsonData);
				}
			}
		}
		// Responses API may also send event: lines (SSE event field) — ignore them
		// The actual data is in the "data:" lines
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

	// Ported from Roo Code openai-native.ts normalizeUsage():
	// Chat Completions API uses: prompt_tokens / completion_tokens
	// Responses API uses:        input_tokens / output_tokens
	// Handle both formats with fallback.
	double Input = 0, Output = 0;
	if (!UsageObj->TryGetNumberField(TEXT("input_tokens"), Input))
	{
		UsageObj->TryGetNumberField(TEXT("prompt_tokens"), Input);
	}
	if (!UsageObj->TryGetNumberField(TEXT("output_tokens"), Output))
	{
		UsageObj->TryGetNumberField(TEXT("completion_tokens"), Output);
	}
	LastTokenUsage.InputTokens = (int32)Input;
	LastTokenUsage.OutputTokens = (int32)Output;
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
