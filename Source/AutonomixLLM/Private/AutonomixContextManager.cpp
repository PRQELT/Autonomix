// Copyright Autonomix. All Rights Reserved.

#include "AutonomixContextManager.h"
#include "AutonomixInterfaces.h"
#include "AutonomixConversationManager.h"
#include "AutonomixTokenCounter.h"
#include "AutonomixSettings.h"
#include "AutonomixCoreModule.h"

FAutonomixContextManager::FAutonomixContextManager(
	TSharedPtr<IAutonomixLLMClient> InLLMClient,
	TSharedPtr<FAutonomixConversationManager> InConversationManager)
	: LLMClient(InLLMClient)
	, ConversationManager(InConversationManager)
	, bIsManaging(false)
{
	Condenser = MakeShared<FAutonomixContextCondenser>(InLLMClient, InConversationManager);
}

FAutonomixContextManager::~FAutonomixContextManager()
{
}

// ============================================================================
// ManageContext — Called after each successful API response
// Ported from Roo Code's manageContext() in context-management/index.ts
// ============================================================================

void FAutonomixContextManager::ManageContext(
	const FString& SystemPrompt,
	const FAutonomixTokenUsage& LastTokenUsage,
	TFunction<void(const FAutonomixContextManagementResult&)> OnComplete)
{
	if (bIsManaging)
	{
		UE_LOG(LogAutonomix, Warning, TEXT("ContextManager: Already managing context. Ignoring."));
		FAutonomixContextManagementResult EmptyResult;
		OnComplete(EmptyResult);
		return;
	}

	if (!ConversationManager.IsValid())
	{
		FAutonomixContextManagementResult EmptyResult;
		OnComplete(EmptyResult);
		return;
	}

	const UAutonomixDeveloperSettings* Settings = UAutonomixDeveloperSettings::Get();
	if (!Settings)
	{
		FAutonomixContextManagementResult EmptyResult;
		OnComplete(EmptyResult);
		return;
	}

	// Compute current token usage
	const bool bIsExtendedWindow = (Settings->ContextWindow == EAutonomixContextWindow::Extended_1M);
	const int32 ContextWindowTokens = FAutonomixTokenCounter::GetContextWindowTokens(bIsExtendedWindow);
	const int32 MaxResponseTokens = Settings->MaxResponseTokens;

	// Total tokens = last API response input + output tokens
	const int32 TotalTokens = LastTokenUsage.InputTokens + LastTokenUsage.OutputTokens;

	// Calculate effective history token estimate
	TArray<FAutonomixMessage> EffectiveHistory = ConversationManager->GetEffectiveHistory();
	const int32 EstimatedTokens = FAutonomixTokenCounter::EstimateTokens(EffectiveHistory)
		+ FAutonomixTokenCounter::EstimateTokens(SystemPrompt);

	// Use the larger of reported tokens and estimated tokens
	const int32 PrevContextTokens = FMath::Max(TotalTokens, EstimatedTokens);

	// Allowed tokens = context window * (1 - buffer %) - reserved for response
	const float TokenBuffer = 1.0f - TokenBufferPercent;
	const int32 AllowedTokens = FMath::FloorToInt(ContextWindowTokens * TokenBuffer) - MaxResponseTokens;
	const float ContextPercent = FAutonomixTokenCounter::GetContextUsagePercent(PrevContextTokens, ContextWindowTokens);

	UE_LOG(LogAutonomix, Log,
		TEXT("ContextManager: context=%d/%d tokens (%.1f%%), allowed=%d, autoCondense=%s, threshold=%d%%"),
		PrevContextTokens, ContextWindowTokens, ContextPercent,
		AllowedTokens, Settings->bAutoCondenseContext ? TEXT("true") : TEXT("false"),
		Settings->AutoCondenseThresholdPercent);

	// Check if we need to manage context
	const bool bNeedsCondense = Settings->bAutoCondenseContext
		&& ContextPercent >= static_cast<float>(Settings->AutoCondenseThresholdPercent);
	const bool bNeedsTruncate = PrevContextTokens > AllowedTokens;

	if (!bNeedsCondense && !bNeedsTruncate)
	{
		// No management needed
		FAutonomixContextManagementResult EmptyResult;
		EmptyResult.PrevContextTokens = PrevContextTokens;
		EmptyResult.ContextPercent = ContextPercent;
		OnComplete(EmptyResult);
		return;
	}

	bIsManaging = true;

	// Try condensation first if enabled
	if (bNeedsCondense && Condenser.IsValid() && !Condenser->IsCondensing())
	{
		UE_LOG(LogAutonomix, Log,
			TEXT("ContextManager: Context at %.1f%% >= %d%% threshold. Attempting condensation..."),
			ContextPercent, Settings->AutoCondenseThresholdPercent);

		Condenser->SummarizeConversation(SystemPrompt,
			[this, PrevContextTokens, ContextPercent, AllowedTokens, OnComplete]
			(const FAutonomixCondenseResult& CondenseResult)
			{
				bIsManaging = false;

				if (CondenseResult.bSuccess)
				{
					FAutonomixContextManagementResult Result;
					Result.bDidManage = true;
					Result.bDidCondense = true;
					Result.PrevContextTokens = PrevContextTokens;
					Result.ContextPercent = ContextPercent;
					Result.NewContextTokens = CondenseResult.NewContextTokens;
					OnComplete(Result);
				}
				else
				{
					// Condensation failed — fall back to truncation if needed
					UE_LOG(LogAutonomix, Warning,
						TEXT("ContextManager: Condensation failed (%s). Falling back to truncation if needed."),
						*CondenseResult.ErrorMessage);

					FAutonomixContextManagementResult Result;
					Result.bDidManage = false;
					Result.PrevContextTokens = PrevContextTokens;
					Result.ContextPercent = ContextPercent;
					Result.ErrorMessage = CondenseResult.ErrorMessage;

					if (PrevContextTokens > AllowedTokens)
					{
						TruncateConversation(TruncationFrac, Result);
					}

					OnComplete(Result);
				}
			});
	}
	else if (bNeedsTruncate)
	{
		// Auto-condense disabled or condenser busy — use truncation directly
		UE_LOG(LogAutonomix, Log,
			TEXT("ContextManager: Context exceeds allowed tokens (%d > %d). Applying truncation."),
			PrevContextTokens, AllowedTokens);

		FAutonomixContextManagementResult Result;
		Result.PrevContextTokens = PrevContextTokens;
		Result.ContextPercent = ContextPercent;
		TruncateConversation(TruncationFrac, Result);

		bIsManaging = false;
		OnComplete(Result);
	}
	else
	{
		bIsManaging = false;
		FAutonomixContextManagementResult EmptyResult;
		EmptyResult.PrevContextTokens = PrevContextTokens;
		EmptyResult.ContextPercent = ContextPercent;
		OnComplete(EmptyResult);
	}
}

// ============================================================================
// TruncateConversation — Non-destructive sliding window
// ============================================================================

void FAutonomixContextManager::TruncateConversation(float FracToRemove, FAutonomixContextManagementResult& OutResult)
{
	if (!ConversationManager.IsValid()) return;

	int32 MessagesRemoved = ConversationManager->TruncateConversation(FracToRemove);

	if (MessagesRemoved > 0)
	{
		OutResult.bDidManage = true;
		OutResult.bDidTruncate = true;
		OutResult.MessagesRemoved = MessagesRemoved;

		// Re-estimate token count after truncation
		TArray<FAutonomixMessage> EffectiveHistory = ConversationManager->GetEffectiveHistory();
		OutResult.NewContextTokens = FAutonomixTokenCounter::EstimateTokens(EffectiveHistory);

		UE_LOG(LogAutonomix, Log,
			TEXT("ContextManager: Truncated %d messages. Estimated new context: %d tokens."),
			MessagesRemoved, OutResult.NewContextTokens);
	}
}
