// Copyright Autonomix. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AutonomixOpenAICompatClient.h"
#include "Containers/Ticker.h"

// GitHub Copilot OAuth and API endpoints (static auth endpoints only)
namespace CopilotEndpoints
{
	static const FString DeviceCodeURL      = TEXT("https://github.com/login/device/code");
	static const FString AccessTokenURL     = TEXT("https://github.com/login/oauth/access_token");
	static const FString CopilotTokenURL    = TEXT("https://api.github.com/copilot_internal/v2/token");
	static const FString ClientID           = TEXT("Iv1.b507a08c87ecfe98");
}

/** Auth state for the device code flow */
enum class ECopilotAuthState : uint8
{
	NotAuthenticated,
	WaitingForUserCode,
	PollingForToken,
	Authenticated,
	Error
};

/**
 * GitHub Copilot client for Autonomix.
 * Wraps the FAutonomixOpenAICompatClient to reuse its robust SSE and tool parsing logic,
 * but intercepts requests to ensure the GitHub Copilot Device Code Auth flow is active,
 * automatically polling for OAuth tokens and internal Copilot tokens before passing the 
 * final API key down to the base class.
 */
class AUTONOMIXLLM_API FAutonomixCopilotClient : public FAutonomixOpenAICompatClient
{
public:
	FAutonomixCopilotClient();
	virtual ~FAutonomixCopilotClient();

	// FAutonomixOpenAICompatClient interface
	virtual void SendMessage(
		const TArray<FAutonomixMessage>& ConversationHistory,
		const FString& SystemPrompt,
		const TArray<TSharedPtr<FJsonObject>>& ToolSchemas
	) override;

	/** Clear saved tokens and start over */
	void SignOut();

private:
	// === Authentication Flow ===
	void LoadTokens();
	void SaveTokens();
	bool IsCopilotTokenExpired() const;

	void StartDeviceCodeAuth();
	void OnDeviceCodeResponse(FHttpRequestPtr HttpReq, FHttpResponsePtr HttpResp, bool bSuccess);
	
	void PollForAccessToken();
	void OnAccessTokenResponse(FHttpRequestPtr HttpReq, FHttpResponsePtr HttpResp, bool bSuccess);
	
	void ExchangeForCopilotToken();
	void OnCopilotTokenResponse(FHttpRequestPtr HttpReq, FHttpResponsePtr HttpResp, bool bSuccess);

	void FetchAvailableModels();
	void OnModelsResponse(FHttpRequestPtr HttpReq, FHttpResponsePtr HttpResp, bool bSuccess);

	void StartPendingRequest();

	/** Emit a system message back into the chat window to guide the user to sign in */
	void EmitSystemMessage(const FString& Message);

	// State
	ECopilotAuthState AuthState = ECopilotAuthState::NotAuthenticated;

	FString GitHubAccessToken;
	FString CopilotToken;
	double CopilotTokenExpiry = 0.0;
	
	FString DeviceCode;
	int32 PollInterval = 5;

	// Request buffering - we only queue the single active message request while authenticating
	struct FPendingRequest
	{
		TArray<FAutonomixMessage> History;
		FString SystemPrompt;
		TArray<TSharedPtr<FJsonObject>> ToolSchemas;
		bool bIsPending = false;
	};
	FPendingRequest PendingRequest;
};