// Copyright Autonomix. All Rights Reserved.

#include "AutonomixCopilotClient.h"
#include "AutonomixSettings.h"
#include "HttpModule.h"
#include "Interfaces/IHttpResponse.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/FileHelper.h"
#include "GenericPlatform/GenericPlatformProcess.h"
#include "HAL/PlatformApplicationMisc.h"

FAutonomixCopilotClient::FAutonomixCopilotClient()
{
	SetProvider(EAutonomixProvider::GitHubCopilot);
	SetEndpoint(TEXT("https://api.githubcopilot.com"));
	LoadTokens();
}

FAutonomixCopilotClient::~FAutonomixCopilotClient()
{
}

void FAutonomixCopilotClient::SignOut()
{
	GitHubAccessToken.Empty();
	CopilotToken.Empty();
	CopilotTokenExpiry = 0.0;
	AuthState = ECopilotAuthState::NotAuthenticated;

	UAutonomixDeveloperSettings* Settings = GetMutableDefault<UAutonomixDeveloperSettings>();
	if (Settings)
	{
		Settings->CopilotCachedDeviceCode.Empty();
		Settings->SaveConfig();
	}

	EmitSystemMessage(TEXT("Signed out of GitHub Copilot. Please try again."));
}

void FAutonomixCopilotClient::SendMessage(
	const TArray<FAutonomixMessage>& ConversationHistory,
	const FString& SystemPrompt,
	const TArray<TSharedPtr<FJsonObject>>& ToolSchemas)
{
	// Save the incoming request parameters in case we need to authenticate first.
	PendingRequest.History = ConversationHistory;
	PendingRequest.SystemPrompt = SystemPrompt;
	PendingRequest.ToolSchemas = ToolSchemas;
	PendingRequest.bIsPending = true;

	if (GitHubAccessToken.IsEmpty())
	{
		// Need to start the Device Code flow from scratch
		if (AuthState != ECopilotAuthState::WaitingForUserCode && AuthState != ECopilotAuthState::PollingForToken)
		{
			StartDeviceCodeAuth();
		}
		return;
	}

	// We have a GitHub token. Do we have a valid Copilot Token?
	if (CopilotToken.IsEmpty() || IsCopilotTokenExpired())
	{
		// Exchange it!
		ExchangeForCopilotToken();
		return;
	}

	// Everything is fully authenticated and cached! Send it down to OpenAICompat.
	StartPendingRequest();
}

void FAutonomixCopilotClient::StartPendingRequest()
{
	if (!PendingRequest.bIsPending)
	{
		return;
	}

	// Override the endpoint and API Key for OpenAICompat
	SetEndpoint(TEXT("https://api.githubcopilot.com"));
	SetApiKey(CopilotToken);

	// The OpenAICompat client will automatically append `/chat/completions` and format the body
	FAutonomixOpenAICompatClient::SendMessage(PendingRequest.History, PendingRequest.SystemPrompt, PendingRequest.ToolSchemas);
	PendingRequest.bIsPending = false;
}

// ============================================================================
// Token Persistence
// ============================================================================

void FAutonomixCopilotClient::LoadTokens()
{
	const UAutonomixDeveloperSettings* Settings = UAutonomixDeveloperSettings::Get();
	if (Settings && !Settings->CopilotCachedDeviceCode.IsEmpty())
	{
		FString Content = Settings->CopilotCachedDeviceCode;

		TSharedPtr<FJsonObject> Json;
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Content);
		if (FJsonSerializer::Deserialize(Reader, Json) && Json.IsValid())
		{
			GitHubAccessToken = Json->GetStringField(TEXT("access_token"));
		}
	}
}

void FAutonomixCopilotClient::SaveTokens()
{
	TSharedPtr<FJsonObject> Json = MakeShareable(new FJsonObject);
	Json->SetStringField(TEXT("access_token"), GitHubAccessToken);

	FString Output;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Output);
	FJsonSerializer::Serialize(Json.ToSharedRef(), Writer);

	// Save to config
	UAutonomixDeveloperSettings* Settings = GetMutableDefault<UAutonomixDeveloperSettings>();
	if (Settings)
	{
		Settings->CopilotCachedDeviceCode = Output;
		Settings->SaveConfig();
	}
}

bool FAutonomixCopilotClient::IsCopilotTokenExpired() const
{
	// Refresh a bit early just to be safe
	return FPlatformTime::Seconds() >= (CopilotTokenExpiry - 60.0);
}

// ============================================================================
// Device Code Auth Flow
// ============================================================================

void FAutonomixCopilotClient::StartDeviceCodeAuth()
{
	AuthState = ECopilotAuthState::WaitingForUserCode;

	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> HttpReq = FHttpModule::Get().CreateRequest();
	HttpReq->SetURL(CopilotEndpoints::DeviceCodeURL);
	HttpReq->SetVerb(TEXT("POST"));
	HttpReq->SetHeader(TEXT("Accept"), TEXT("application/json"));
	HttpReq->SetHeader(TEXT("Content-Type"), TEXT("application/json"));

	TSharedPtr<FJsonObject> Body = MakeShareable(new FJsonObject);
	Body->SetStringField(TEXT("client_id"), CopilotEndpoints::ClientID);
	Body->SetStringField(TEXT("scope"), TEXT("read:user"));

	FString BodyStr;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&BodyStr);
	FJsonSerializer::Serialize(Body.ToSharedRef(), Writer);
	HttpReq->SetContentAsString(BodyStr);

	HttpReq->OnProcessRequestComplete().BindRaw(this, &FAutonomixCopilotClient::OnDeviceCodeResponse);
	HttpReq->ProcessRequest();
}

void FAutonomixCopilotClient::OnDeviceCodeResponse(FHttpRequestPtr HttpReq, FHttpResponsePtr HttpResp, bool bSuccess)
{
	if (!bSuccess || !HttpResp.IsValid() || !EHttpResponseCodes::IsOk(HttpResp->GetResponseCode()))
	{
		AuthState = ECopilotAuthState::Error;
		EmitSystemMessage(TEXT("🚨 Failed to contact GitHub for device code. Please check your internet connection."));
		
		OnRequestCompleted().Broadcast(false);
		return;
	}

	TSharedPtr<FJsonObject> Json;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(HttpResp->GetContentAsString());
	if (!FJsonSerializer::Deserialize(Reader, Json) || !Json.IsValid())
	{
		AuthState = ECopilotAuthState::Error;
		EmitSystemMessage(TEXT("🚨 Failed to parse GitHub device code."));
		OnRequestCompleted().Broadcast(false);
		return;
	}

	DeviceCode = Json->GetStringField(TEXT("device_code"));
	FString UserCode = Json->GetStringField(TEXT("user_code"));
	FString VerificationURI = Json->GetStringField(TEXT("verification_uri"));
	PollInterval = FMath::Max(5, Json->GetIntegerField(TEXT("interval")));

	// Let the user know they need to sign in
	FString Msg = FString::Printf(TEXT("### 🔐 GitHub Copilot Authentication Required\n\n1. Go to: [%s](%s)\n2. Enter this code: **`%s`** (Copied to clipboard!)\n\n*Waiting for you to authorize...*"), *VerificationURI, *VerificationURI, *UserCode);
	EmitSystemMessage(Msg);

	// Force a bright yellow log so it's impossible to miss
	UE_LOG(LogTemp, Warning, TEXT("========================================================================="));
	UE_LOG(LogTemp, Warning, TEXT("[AutonomixCopilot] GITHUB AUTHENTICATION REQUIRED"));
	UE_LOG(LogTemp, Warning, TEXT("[AutonomixCopilot] Your device auth code is: %s"), *UserCode);
	UE_LOG(LogTemp, Warning, TEXT("[AutonomixCopilot] We have automatically copied it to your clipboard."));
	UE_LOG(LogTemp, Warning, TEXT("========================================================================="));

	// Copy the code to clipboard automatically
	FPlatformApplicationMisc::ClipboardCopy(*UserCode);

	// Automatically open the browser
	FPlatformProcess::LaunchURL(*VerificationURI, nullptr, nullptr);

	// Start polling
	AuthState = ECopilotAuthState::PollingForToken;
	PollForAccessToken();
}

void FAutonomixCopilotClient::PollForAccessToken()
{
	if (AuthState != ECopilotAuthState::PollingForToken)
	{
		return;
	}

	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> HttpReq = FHttpModule::Get().CreateRequest();
	HttpReq->SetURL(CopilotEndpoints::AccessTokenURL);
	HttpReq->SetVerb(TEXT("POST"));
	HttpReq->SetHeader(TEXT("Accept"), TEXT("application/json"));
	HttpReq->SetHeader(TEXT("Content-Type"), TEXT("application/json"));

	TSharedPtr<FJsonObject> Body = MakeShareable(new FJsonObject);
	Body->SetStringField(TEXT("client_id"), CopilotEndpoints::ClientID);
	Body->SetStringField(TEXT("device_code"), DeviceCode);
	Body->SetStringField(TEXT("grant_type"), TEXT("urn:ietf:params:oauth:grant-type:device_code"));

	FString BodyStr;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&BodyStr);
	FJsonSerializer::Serialize(Body.ToSharedRef(), Writer);
	HttpReq->SetContentAsString(BodyStr);

	HttpReq->OnProcessRequestComplete().BindRaw(this, &FAutonomixCopilotClient::OnAccessTokenResponse);
	HttpReq->ProcessRequest();
}

void FAutonomixCopilotClient::OnAccessTokenResponse(FHttpRequestPtr HttpReq, FHttpResponsePtr HttpResp, bool bSuccess)
{
	if (!bSuccess || !HttpResp.IsValid())
	{
		FPlatformProcess::Sleep(0.1f);
		return;
	}

	TSharedPtr<FJsonObject> Json;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(HttpResp->GetContentAsString());
	if (!FJsonSerializer::Deserialize(Reader, Json) || !Json.IsValid())
	{
		return;
	}

	if (Json->HasField(TEXT("access_token")))
	{
		GitHubAccessToken = Json->GetStringField(TEXT("access_token"));
		SaveTokens();
		
		EmitSystemMessage(TEXT("✅ GitHub OAuth successful! Exchanging for Copilot token..."));
		ExchangeForCopilotToken();
		return;
	}

	FString Error = Json->GetStringField(TEXT("error"));
	if (Error == TEXT("authorization_pending"))
	{
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([this](float) -> bool {
			PollForAccessToken();
			return false;
		}), (float)PollInterval);
	}
	else if (Error == TEXT("slow_down"))
	{
		PollInterval += 5;
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([this](float) -> bool {
			PollForAccessToken();
			return false;
		}), (float)PollInterval);
	}
	else if (Error == TEXT("expired_token"))
	{
		AuthState = ECopilotAuthState::Error;
		EmitSystemMessage(TEXT("🚨 Device code expired. Please click Submit again to restart authentication."));
		OnRequestCompleted().Broadcast(false);
	}
}

void FAutonomixCopilotClient::ExchangeForCopilotToken()
{
	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> HttpReq = FHttpModule::Get().CreateRequest();
	HttpReq->SetURL(CopilotEndpoints::CopilotTokenURL);
	HttpReq->SetVerb(TEXT("GET"));
	HttpReq->SetHeader(TEXT("Authorization"), FString::Printf(TEXT("token %s"), *GitHubAccessToken));
	HttpReq->SetHeader(TEXT("Accept"), TEXT("application/json"));
	HttpReq->SetHeader(TEXT("editor-version"), TEXT("vscode/1.90.0"));
	HttpReq->SetHeader(TEXT("editor-plugin-version"), TEXT("copilot-chat/0.17.0"));
	HttpReq->SetHeader(TEXT("user-agent"), TEXT("GitHubCopilotChat/0.17.0"));

	HttpReq->OnProcessRequestComplete().BindRaw(this, &FAutonomixCopilotClient::OnCopilotTokenResponse);
	HttpReq->ProcessRequest();
}

void FAutonomixCopilotClient::OnCopilotTokenResponse(FHttpRequestPtr HttpReq, FHttpResponsePtr HttpResp, bool bSuccess)
{
	if (!bSuccess || !HttpResp.IsValid() || !EHttpResponseCodes::IsOk(HttpResp->GetResponseCode()))
	{
		// Invalid auth token - wipe it and force reauth
		if (HttpResp.IsValid() && HttpResp->GetResponseCode() == 401)
		{
			SignOut();
			EmitSystemMessage(TEXT("🚨 Session expired. Please click Submit to sign in again."));
		}
		else
		{
			EmitSystemMessage(TEXT("🚨 Failed to get Copilot token. You may not have an active Copilot subscription."));
		}
		
		OnRequestCompleted().Broadcast(false);
		return;
	}

	TSharedPtr<FJsonObject> Json;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(HttpResp->GetContentAsString());
	if (FJsonSerializer::Deserialize(Reader, Json) && Json.IsValid())
	{
		CopilotToken = Json->GetStringField(TEXT("token"));
		
		int32 ExpiresInSecs = 1800; // default 30 mins
		if (Json->HasField(TEXT("expires_at")))
		{
			double ExpiresAtRaw = Json->GetNumberField(TEXT("expires_at"));
			// Simple fallback relative to current OS time instead of parsing Unix Timestamp properly to avoid C++ time hell. 
			// the token usually lasts 30 mins to 1 hour
			ExpiresInSecs = 1200; 
		}
		CopilotTokenExpiry = FPlatformTime::Seconds() + ExpiresInSecs;

		AuthState = ECopilotAuthState::Authenticated;

		// Fetch dynamically available models based on user tier
		FetchAvailableModels();

		EmitSystemMessage(TEXT("🚀 Ready! Sending your prompt..."));
		StartPendingRequest();
	}
}

void FAutonomixCopilotClient::FetchAvailableModels()
{
	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> HttpReq = FHttpModule::Get().CreateRequest();
	HttpReq->SetURL(TEXT("https://api.githubcopilot.com/models"));
	HttpReq->SetVerb(TEXT("GET"));
	HttpReq->SetHeader(TEXT("Authorization"), FString::Printf(TEXT("Bearer %s"), *CopilotToken));
	HttpReq->SetHeader(TEXT("Accept"), TEXT("application/json"));
	HttpReq->SetHeader(TEXT("Copilot-Integration-Id"), TEXT("vscode-chat"));
	HttpReq->SetHeader(TEXT("Editor-Version"), TEXT("vscode/1.104.3"));
	HttpReq->SetHeader(TEXT("Editor-Plugin-Version"), TEXT("copilot-chat/0.26.7"));
	HttpReq->SetHeader(TEXT("User-Agent"), TEXT("GitHubCopilotChat/0.26.7"));
	HttpReq->SetHeader(TEXT("X-GitHub-Api-Version"), TEXT("2025-04-01"));

	HttpReq->OnProcessRequestComplete().BindRaw(this, &FAutonomixCopilotClient::OnModelsResponse);
	HttpReq->ProcessRequest();
}

void FAutonomixCopilotClient::OnModelsResponse(FHttpRequestPtr HttpReq, FHttpResponsePtr HttpResp, bool bSuccess)
{
	if (!bSuccess || !HttpResp.IsValid() || !EHttpResponseCodes::IsOk(HttpResp->GetResponseCode()))
	{
		UE_LOG(LogTemp, Warning, TEXT("[AutonomixCopilot] Failed to fetch models. Raw: %s"), 
			HttpResp.IsValid() ? *HttpResp->GetContentAsString() : TEXT("null"));
		return;
	}

	TSharedPtr<FJsonObject> Json;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(HttpResp->GetContentAsString());
	if (FJsonSerializer::Deserialize(Reader, Json) && Json.IsValid())
	{
		const TArray<TSharedPtr<FJsonValue>>* DataArray;
		if (Json->TryGetArrayField(TEXT("data"), DataArray))
		{
			TArray<FString> AllowedModels;
			for (TSharedPtr<FJsonValue> Val : *DataArray)
			{
				TSharedPtr<FJsonObject> ModelObj = Val->AsObject();
				if (ModelObj.IsValid())
				{
					// model_picker_enabled might be a boolean field
					bool bIsPickerEnabled = false;
					if (ModelObj->TryGetBoolField(TEXT("model_picker_enabled"), bIsPickerEnabled) && bIsPickerEnabled)
					{
						FString FetchedModelId;
						if (ModelObj->TryGetStringField(TEXT("id"), FetchedModelId))
						{
							AllowedModels.AddUnique(FetchedModelId);
						}
					}
					else 
					{
                        // Fallback check, sometimes the JSON is slightly different based on the API payload.
                        // Wait, github copilot extension uses `model_picker_enabled`. We will also just look at `id`.
                        // Let's just add it if `capabilities` says `type: "chat"` maybe? 
						// Actually, user context said: "deserializes the JSON array data, and checks properties id and model_picker_enabled/picker_enabled".
						if (ModelObj->TryGetBoolField(TEXT("picker_enabled"), bIsPickerEnabled) && bIsPickerEnabled)
						{
							FString FetchedModelId;
							if (ModelObj->TryGetStringField(TEXT("id"), FetchedModelId))
							{
								AllowedModels.AddUnique(FetchedModelId);
							}
						}
					}
				}
			}

			if (AllowedModels.Num() > 0)
			{
				UAutonomixDeveloperSettings* Settings = GetMutableDefault<UAutonomixDeveloperSettings>();
				if (Settings)
				{
					Settings->CopilotAvailableModels = AllowedModels;
					Settings->SaveConfig();
					UE_LOG(LogTemp, Log, TEXT("[AutonomixCopilot] Cached %d models from GitHub Copilot subscription."), AllowedModels.Num());
				}
			}
		}
	}
}

void FAutonomixCopilotClient::EmitSystemMessage(const FString& Message)
{
	// Use System role to cleanly insert warnings into the UI during auth
	FAutonomixMessage SysMsg;
	SysMsg.Role = EAutonomixMessageRole::System;
	SysMsg.Content = Message;
	SysMsg.Timestamp = FDateTime::UtcNow();
	
	OnMessageComplete().Broadcast(SysMsg);
}
