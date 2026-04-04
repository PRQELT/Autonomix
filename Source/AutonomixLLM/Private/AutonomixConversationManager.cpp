// Copyright Autonomix. All Rights Reserved.

#include "AutonomixConversationManager.h"
#include "AutonomixCoreModule.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/Guid.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

static const int32 DefaultMaxTokenBudget = 100000;
static const int32 ApproxCharsPerToken = 4;

FAutonomixConversationManager::FAutonomixConversationManager()
	: MaxTokenBudget(DefaultMaxTokenBudget)
{
}

FAutonomixConversationManager::~FAutonomixConversationManager()
{
}

FAutonomixMessage& FAutonomixConversationManager::AddUserMessage(const FString& Content)
{
	FAutonomixMessage& Msg = History.AddDefaulted_GetRef();
	Msg.MessageId = FGuid::NewGuid();
	Msg.Role = EAutonomixMessageRole::User;
	Msg.Content = Content;
	Msg.Timestamp = FDateTime::UtcNow();
	OnMessageAdded.Broadcast(Msg);
	return Msg;
}

FAutonomixMessage& FAutonomixConversationManager::AddAssistantMessage(const FString& Content)
{
	FAutonomixMessage& Msg = History.AddDefaulted_GetRef();
	Msg.MessageId = FGuid::NewGuid();
	Msg.Role = EAutonomixMessageRole::Assistant;
	Msg.Content = Content;
	Msg.Timestamp = FDateTime::UtcNow();
	OnMessageAdded.Broadcast(Msg);
	return Msg;
}

FAutonomixMessage& FAutonomixConversationManager::AddAssistantMessageFull(const FAutonomixMessage& SourceMessage)
{
	FAutonomixMessage& Msg = History.AddDefaulted_GetRef();
	Msg.MessageId = SourceMessage.MessageId;
	Msg.Role = EAutonomixMessageRole::Assistant;
	Msg.Content = SourceMessage.Content;
	Msg.ContentBlocksJson = SourceMessage.ContentBlocksJson;
	Msg.ReasoningContent = SourceMessage.ReasoningContent;
	Msg.ToolUseId = SourceMessage.ToolUseId;
	Msg.ToolName = SourceMessage.ToolName;
	Msg.Timestamp = FDateTime::UtcNow();
	Msg.bIsStreaming = false;
	OnMessageAdded.Broadcast(Msg);
	return Msg;
}

FAutonomixMessage& FAutonomixConversationManager::AddSystemMessage(const FString& Content)
{
	FAutonomixMessage& Msg = History.AddDefaulted_GetRef();
	Msg.MessageId = FGuid::NewGuid();
	Msg.Role = EAutonomixMessageRole::System;
	Msg.Content = Content;
	Msg.Timestamp = FDateTime::UtcNow();
	OnMessageAdded.Broadcast(Msg);
	return Msg;
}

FAutonomixMessage& FAutonomixConversationManager::AddToolResultMessage(const FString& ToolUseId, const FString& Content, bool bIsError)
{
	FAutonomixMessage& Msg = History.AddDefaulted_GetRef();
	Msg.MessageId = FGuid::NewGuid();
	Msg.Role = EAutonomixMessageRole::ToolResult;
	Msg.Content = Content;
	Msg.ToolUseId = ToolUseId;
	Msg.Timestamp = FDateTime::UtcNow();
	OnMessageAdded.Broadcast(Msg);
	return Msg;
}

void FAutonomixConversationManager::AppendStreamingText(const FGuid& MessageId, const FString& DeltaText)
{
	FAutonomixMessage* Msg = GetMessageById(MessageId);
	if (Msg)
	{
		Msg->Content += DeltaText;
	}
}

void FAutonomixConversationManager::FinalizeStreamingMessage(const FGuid& MessageId)
{
	FAutonomixMessage* Msg = GetMessageById(MessageId);
	if (Msg)
	{
		Msg->bIsStreaming = false;
	}
}

// ============================================================================
// Tool Result Eviction — Token Optimization (Phase 1D)
// ============================================================================

void FAutonomixConversationManager::EvictOldToolResults(TArray<FAutonomixMessage>& Messages)
{
	// Threshold: tool results larger than this (in chars) are eligible for eviction.
	// ~500 tokens at 4 chars/token = 2000 chars. Only results that have been
	// "consumed" (followed by an assistant message) are evicted.
	static constexpr int32 EvictionThresholdChars = 2000;

	// Walk backwards to find the LAST tool_result index that has a subsequent
	// assistant message. We never evict the most recent tool_result because
	// the AI hasn't responded to it yet.
	int32 LastAssistantIdx = -1;
	for (int32 i = Messages.Num() - 1; i >= 0; --i)
	{
		if (Messages[i].Role == EAutonomixMessageRole::Assistant)
		{
			LastAssistantIdx = i;
			break;
		}
	}

	if (LastAssistantIdx < 0) return; // No assistant messages — nothing to evict

	int32 EvictedCount = 0;
	int32 TokensSaved = 0;

	for (int32 i = 0; i < LastAssistantIdx; ++i)
	{
		FAutonomixMessage& Msg = Messages[i];
		if (Msg.Role != EAutonomixMessageRole::ToolResult) continue;
		if (Msg.Content.Len() <= EvictionThresholdChars) continue;

		// Check there's an assistant message after this tool result
		// (i.e., the AI has already consumed this result)
		bool bConsumed = false;
		for (int32 j = i + 1; j <= LastAssistantIdx; ++j)
		{
			if (Messages[j].Role == EAutonomixMessageRole::Assistant)
			{
				bConsumed = true;
				break;
			}
		}
		if (!bConsumed) continue;

		// Build a compact summary from the first ~200 chars
		int32 OriginalLen = Msg.Content.Len();
		FString Summary;

		// Try to extract a meaningful first line
		int32 NewlineIdx = INDEX_NONE;
		Msg.Content.FindChar(TEXT('\n'), NewlineIdx);
		if (NewlineIdx != INDEX_NONE && NewlineIdx < 200)
		{
			Summary = Msg.Content.Left(NewlineIdx);
		}
		else
		{
			Summary = Msg.Content.Left(200);
			if (OriginalLen > 200) Summary += TEXT("...");
		}

		// Look for success/error indicators
		bool bHasSuccess = Msg.Content.Contains(TEXT("SUCCESS")) || Msg.Content.Contains(TEXT("success"));
		bool bHasError = Msg.Content.Contains(TEXT("ERROR")) || Msg.Content.Contains(TEXT("FAILED"));
		FString StatusHint = bHasError ? TEXT(" [had errors]") : (bHasSuccess ? TEXT(" [succeeded]") : TEXT(""));

		int32 EstTokensSaved = (OriginalLen - Summary.Len()) / ApproxCharsPerToken;
		TokensSaved += EstTokensSaved;

		Msg.Content = FString::Printf(
			TEXT("[evicted — %d chars, ~%d tokens saved%s] %s"),
			OriginalLen, EstTokensSaved, *StatusHint, *Summary);

		EvictedCount++;
	}

	if (EvictedCount > 0)
	{
		UE_LOG(LogAutonomix, Log,
			TEXT("ConversationManager: Evicted %d old tool result(s), saving ~%d tokens."),
			EvictedCount, TokensSaved);
	}
}

// ============================================================================
// Context Management: GetEffectiveHistory (v3.0)
// Ported from Roo Code's getEffectiveApiHistory() in condense/index.ts
// ============================================================================

TArray<FAutonomixMessage> FAutonomixConversationManager::GetEffectiveHistory() const
{
	// Find the most recent summary message
	int32 LastSummaryIdx = -1;
	for (int32 i = History.Num() - 1; i >= 0; --i)
	{
		if (History[i].bIsSummary)
		{
			LastSummaryIdx = i;
			break;
		}
	}

	if (LastSummaryIdx != -1)
	{
		// Fresh start model: return only messages from the summary onwards
		TArray<FAutonomixMessage> MessagesFromSummary(History.GetData() + LastSummaryIdx, History.Num() - LastSummaryIdx);

		// Collect all tool_use IDs from assistant messages in the result slice
		// to filter out orphan tool_results that reference condensed-away tool_uses
		TSet<FString> ToolUseIds;
		for (const FAutonomixMessage& Msg : MessagesFromSummary)
		{
			if (Msg.Role == EAutonomixMessageRole::Assistant && !Msg.ContentBlocksJson.IsEmpty())
			{
				// Parse content blocks to extract tool_use IDs
				TArray<TSharedPtr<FJsonValue>> ContentBlocks;
				TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Msg.ContentBlocksJson);
				if (FJsonSerializer::Deserialize(Reader, ContentBlocks))
				{
					for (const TSharedPtr<FJsonValue>& Block : ContentBlocks)
					{
						const TSharedPtr<FJsonObject>* BlockObj = nullptr;
						if (Block->TryGetObject(BlockObj))
						{
							FString BlockType;
							(*BlockObj)->TryGetStringField(TEXT("type"), BlockType);
							if (BlockType == TEXT("tool_use"))
							{
								FString ToolUseId;
								(*BlockObj)->TryGetStringField(TEXT("id"), ToolUseId);
								if (!ToolUseId.IsEmpty())
								{
									ToolUseIds.Add(ToolUseId);
								}
							}
						}
					}
				}
			}
		}

		// Filter out orphan tool_result messages referencing condensed tool_uses
		TArray<FAutonomixMessage> Filtered;
		for (const FAutonomixMessage& Msg : MessagesFromSummary)
		{
			if (Msg.Role == EAutonomixMessageRole::ToolResult)
			{
				// Include only if the tool_use_id is in our visible set
				if (!Msg.ToolUseId.IsEmpty() && !ToolUseIds.Contains(Msg.ToolUseId))
				{
					continue; // Orphaned tool_result — filter out
				}
			}
			Filtered.Add(Msg);
		}

		// Also filter truncated messages within the summary range
		TSet<FString> ExistingTruncationIds;
		for (const FAutonomixMessage& Msg : Filtered)
		{
			if (Msg.bIsTruncationMarker && !Msg.TruncationId.IsEmpty())
			{
				ExistingTruncationIds.Add(Msg.TruncationId);
			}
		}

		TArray<FAutonomixMessage> Result;
		for (const FAutonomixMessage& Msg : Filtered)
		{
			if (!Msg.TruncationParent.IsEmpty() && ExistingTruncationIds.Contains(Msg.TruncationParent))
			{
				continue; // Truncated
			}
			Result.Add(Msg);
		}

		EvictOldToolResults(Result);
		return Result;
	}

	// No summary — filter based on CondenseParent and TruncationParent
	TSet<FString> ExistingSummaryIds;
	TSet<FString> ExistingTruncationIds;

	for (const FAutonomixMessage& Msg : History)
	{
		if (Msg.bIsSummary && !Msg.CondenseId.IsEmpty())
		{
			ExistingSummaryIds.Add(Msg.CondenseId);
		}
		if (Msg.bIsTruncationMarker && !Msg.TruncationId.IsEmpty())
		{
			ExistingTruncationIds.Add(Msg.TruncationId);
		}
	}

	TArray<FAutonomixMessage> Result;
	for (const FAutonomixMessage& Msg : History)
	{
		// Filter out condensed messages whose summary still exists
		if (!Msg.CondenseParent.IsEmpty() && ExistingSummaryIds.Contains(Msg.CondenseParent))
		{
			continue;
		}
		// Filter out truncated messages whose marker still exists
		if (!Msg.TruncationParent.IsEmpty() && ExistingTruncationIds.Contains(Msg.TruncationParent))
		{
			continue;
		}
		Result.Add(Msg);
	}

	// CRITICAL: Ensure the effective history has no assistant messages with
	// unresolved tool_use blocks (ContentBlocksJson with tool_use entries) but no
	// matching tool_result messages.
	//
	// This can happen when:
	//   - The session was interrupted (engine crash, force-close) mid-agentic-loop
	//     after the assistant response was saved but before tool_results were added
	//   - The user resumes a conversation in a new session
	//
	// Claude returns HTTP 400: "An assistant message with 'tool_calls' must be followed
	// by tool messages responding to each 'tool_call_id'".
	//
	// Fix: For any assistant message with tool_use blocks where the matching
	// tool_results are missing, inject synthetic tool_result messages so Claude
	// can continue the conversation cleanly.
	//
	// EXCEPTION: Tool uses in the last assistant message that contains tool_use blocks
	// are pending execution in the current agentic loop iteration and should not be
	// treated as orphaned. Only tool_uses from earlier assistant messages that lack
	// matching tool_results are truly orphaned (e.g., from interrupted sessions).
	if (Result.Num() > 0)
	{
		// Step 1: Collect all tool_result IDs already present in the effective history
		TSet<FString> ExistingToolResultIds;
		for (const FAutonomixMessage& R : Result)
		{
			if (R.Role == EAutonomixMessageRole::ToolResult && !R.ToolUseId.IsEmpty())
			{
				ExistingToolResultIds.Add(R.ToolUseId);
			}
		}

		// Step 2: Find the index of the last assistant message that contains tool_use blocks.
		// Tool uses in this message are pending execution and should NOT be treated as orphaned.
		int32 LastAssistantWithToolUseIdx = -1;
		for (int32 i = Result.Num() - 1; i >= 0; --i)
		{
			if (Result[i].Role == EAutonomixMessageRole::Assistant && !Result[i].ContentBlocksJson.IsEmpty())
			{
				TArray<TSharedPtr<FJsonValue>> CheckBlocks;
				TSharedRef<TJsonReader<>> CheckReader = TJsonReaderFactory<>::Create(Result[i].ContentBlocksJson);
				if (FJsonSerializer::Deserialize(CheckReader, CheckBlocks))
				{
					bool bHasToolUse = false;
					for (const TSharedPtr<FJsonValue>& Block : CheckBlocks)
					{
						const TSharedPtr<FJsonObject>* BlockObj = nullptr;
						if (Block->TryGetObject(BlockObj))
						{
							FString BlockType;
							(*BlockObj)->TryGetStringField(TEXT("type"), BlockType);
							if (BlockType == TEXT("tool_use"))
							{
								bHasToolUse = true;
								break;
							}
						}
					}
					if (bHasToolUse)
					{
						LastAssistantWithToolUseIdx = i;
						break;
					}
				}
			}
		}

		// Step 3: Scan all assistant messages with tool_use blocks EXCEPT the last one
		// (which has pending tool executions) and collect orphaned tool_use IDs
		TArray<FString> OrphanedToolUseIds;
		for (int32 i = 0; i < Result.Num(); ++i)
		{
			// Skip the last assistant message with tool_use — those are pending execution
			if (i == LastAssistantWithToolUseIdx) continue;

			const FAutonomixMessage& Msg = Result[i];
			if (Msg.Role != EAutonomixMessageRole::Assistant || Msg.ContentBlocksJson.IsEmpty()) continue;

			TArray<TSharedPtr<FJsonValue>> ContentBlocks;
			TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Msg.ContentBlocksJson);
			if (!FJsonSerializer::Deserialize(Reader, ContentBlocks)) continue;

			for (const TSharedPtr<FJsonValue>& Block : ContentBlocks)
			{
				const TSharedPtr<FJsonObject>* BlockObj = nullptr;
				if (!Block->TryGetObject(BlockObj)) continue;
				FString BlockType;
				(*BlockObj)->TryGetStringField(TEXT("type"), BlockType);
				if (BlockType == TEXT("tool_use"))
				{
					FString Id;
					(*BlockObj)->TryGetStringField(TEXT("id"), Id);
					if (!Id.IsEmpty() && !ExistingToolResultIds.Contains(Id))
					{
						OrphanedToolUseIds.Add(Id);
					}
				}
			}
		}

		// Step 4: Inject synthetic tool_results for truly orphaned tool_uses
		if (OrphanedToolUseIds.Num() > 0)
		{
			UE_LOG(LogAutonomix, Warning,
				TEXT("ConversationManager::GetEffectiveHistory: Found %d orphaned tool_use(s) from earlier assistant messages with no tool_results. "
				     "Session was likely interrupted. Injecting synthetic tool_results."),
				OrphanedToolUseIds.Num());

			for (const FString& UseId : OrphanedToolUseIds)
			{
				FAutonomixMessage SyntheticResult;
				SyntheticResult.MessageId = FGuid::NewGuid();
				SyntheticResult.Role = EAutonomixMessageRole::ToolResult;
				SyntheticResult.ToolUseId = UseId;
				SyntheticResult.Content = TEXT("Session was interrupted before this tool could complete. Please retry the last operation.");
				SyntheticResult.Timestamp = FDateTime::UtcNow();
				Result.Add(SyntheticResult);
			}
		}
	}

	EvictOldToolResults(Result);
	return Result;
}

// ============================================================================
// Non-Destructive Truncation (v3.0)
// Ported from Roo Code's truncateConversation() in context-management/index.ts
// ============================================================================

int32 FAutonomixConversationManager::TruncateConversation(float FracToRemove)
{
	FracToRemove = FMath::Clamp(FracToRemove, 0.0f, 1.0f);

	// Generate a unique truncation ID
	FString TruncationId = GenerateUniqueId();

	// Find visible messages (not already truncated, not truncation markers)
	TArray<int32> VisibleIndices;
	for (int32 i = 0; i < History.Num(); ++i)
	{
		if (History[i].TruncationParent.IsEmpty() && !History[i].bIsTruncationMarker)
		{
			VisibleIndices.Add(i);
		}
	}

	const int32 VisibleCount = VisibleIndices.Num();
	if (VisibleCount <= 1) return 0;

	// Calculate messages to remove (rounded to even, skip first visible)
	int32 RawToRemove = FMath::FloorToInt((VisibleCount - 1) * FracToRemove);
	int32 MessagesToRemove = RawToRemove - (RawToRemove % 2); // Round to even

	if (MessagesToRemove <= 0) return 0;

	// Tag messages with TruncationParent (skip index 0 = first visible message)
	TSet<int32> IndicesToTruncate;
	for (int32 j = 1; j <= MessagesToRemove && j < VisibleIndices.Num(); ++j)
	{
		IndicesToTruncate.Add(VisibleIndices[j]);
	}

	for (int32 i = 0; i < History.Num(); ++i)
	{
		if (IndicesToTruncate.Contains(i))
		{
			History[i].TruncationParent = TruncationId;
		}
	}

	// Insert truncation marker at the boundary (between last truncated and first kept)
	int32 FirstKeptVisibleIndex = (MessagesToRemove + 1 < VisibleIndices.Num())
		? VisibleIndices[MessagesToRemove + 1]
		: History.Num();

	FAutonomixMessage TruncationMarker;
	TruncationMarker.MessageId = FGuid::NewGuid();
	TruncationMarker.Role = EAutonomixMessageRole::System;
	TruncationMarker.Content = FString::Printf(
		TEXT("[Sliding window truncation: %d messages hidden to reduce context]"),
		MessagesToRemove);
	TruncationMarker.Timestamp = FDateTime::UtcNow();
	TruncationMarker.bIsTruncationMarker = true;
	TruncationMarker.TruncationId = TruncationId;

	History.Insert(TruncationMarker, FirstKeptVisibleIndex);

	UE_LOG(LogAutonomix, Log,
		TEXT("ConversationManager: Truncated %d messages (truncationId=%s). Total history: %d"),
		MessagesToRemove, *TruncationId, History.Num());

	return MessagesToRemove;
}

// ============================================================================
// Cleanup After Truncation (v3.0)
// Ported from Roo Code's cleanupAfterTruncation() in condense/index.ts
// ============================================================================

void FAutonomixConversationManager::CleanupAfterTruncation()
{
	// Collect all condense/truncation IDs that still exist
	TSet<FString> ExistingSummaryIds;
	TSet<FString> ExistingTruncationIds;

	for (const FAutonomixMessage& Msg : History)
	{
		if (Msg.bIsSummary && !Msg.CondenseId.IsEmpty())
		{
			ExistingSummaryIds.Add(Msg.CondenseId);
		}
		if (Msg.bIsTruncationMarker && !Msg.TruncationId.IsEmpty())
		{
			ExistingTruncationIds.Add(Msg.TruncationId);
		}
	}

	// Clear orphaned parent references
	for (FAutonomixMessage& Msg : History)
	{
		if (!Msg.CondenseParent.IsEmpty() && !ExistingSummaryIds.Contains(Msg.CondenseParent))
		{
			Msg.CondenseParent.Empty();
		}
		if (!Msg.TruncationParent.IsEmpty() && !ExistingTruncationIds.Contains(Msg.TruncationParent))
		{
			Msg.TruncationParent.Empty();
		}
	}
}

// ============================================================================
// Legacy Pruning (kept for backward compat, superseded by GetEffectiveHistory)
// ============================================================================

TArray<FAutonomixMessage> FAutonomixConversationManager::GetPrunedHistory(int32 MaxTokens) const
{
	TArray<FAutonomixMessage> Pruned;
	int32 TotalTokens = 0;

	for (int32 i = History.Num() - 1; i >= 0; --i)
	{
		int32 MsgTokens = EstimateMessageTokens(History[i]);
		if (TotalTokens + MsgTokens > MaxTokens && Pruned.Num() > 0)
		{
			break;
		}
		Pruned.Insert(History[i], 0);
		TotalTokens += MsgTokens;
	}

	return Pruned;
}

void FAutonomixConversationManager::ClearHistory()
{
	History.Empty();
}

int32 FAutonomixConversationManager::EstimateTotalTokens() const
{
	int32 Total = 0;
	for (const FAutonomixMessage& Msg : History)
	{
		Total += EstimateMessageTokens(Msg);
	}
	return Total;
}

void FAutonomixConversationManager::SetMaxTokenBudget(int32 InMaxTokens)
{
	MaxTokenBudget = FMath::Max(1000, InMaxTokens);
}

FAutonomixMessage* FAutonomixConversationManager::GetMessageById(const FGuid& MessageId)
{
	return History.FindByPredicate([&MessageId](const FAutonomixMessage& Msg)
	{
		return Msg.MessageId == MessageId;
	});
}

FAutonomixMessage* FAutonomixConversationManager::GetLastMessage()
{
	return History.Num() > 0 ? &History.Last() : nullptr;
}

int32 FAutonomixConversationManager::EstimateMessageTokens(const FAutonomixMessage& Message)
{
	return (Message.Content.Len() / ApproxCharsPerToken) + 10;
}

FString FAutonomixConversationManager::GenerateUniqueId()
{
	return FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphens);
}

// ============================================================================
// Conversation Persistence (v2.3)
// ============================================================================

FString FAutonomixConversationManager::GetConversationSaveDir()
{
	return FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Autonomix"), TEXT("Conversations"));
}

TSharedPtr<FJsonObject> FAutonomixConversationManager::MessageToJson(const FAutonomixMessage& Message)
{
	TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();

	Obj->SetStringField(TEXT("message_id"), Message.MessageId.ToString());
	Obj->SetStringField(TEXT("content"), Message.Content);
	Obj->SetStringField(TEXT("timestamp"), Message.Timestamp.ToIso8601());
	Obj->SetStringField(TEXT("tool_use_id"), Message.ToolUseId);
	Obj->SetStringField(TEXT("tool_name"), Message.ToolName);

	// Context management fields
	Obj->SetStringField(TEXT("condense_parent"), Message.CondenseParent);
	Obj->SetBoolField(TEXT("is_summary"), Message.bIsSummary);
	Obj->SetStringField(TEXT("condense_id"), Message.CondenseId);
	Obj->SetStringField(TEXT("truncation_parent"), Message.TruncationParent);
	Obj->SetBoolField(TEXT("is_truncation_marker"), Message.bIsTruncationMarker);
	Obj->SetStringField(TEXT("truncation_id"), Message.TruncationId);

	// Role as string
	FString RoleStr;
	switch (Message.Role)
	{
	case EAutonomixMessageRole::User: RoleStr = TEXT("user"); break;
	case EAutonomixMessageRole::Assistant: RoleStr = TEXT("assistant"); break;
	case EAutonomixMessageRole::System: RoleStr = TEXT("system"); break;
	case EAutonomixMessageRole::ToolResult: RoleStr = TEXT("tool_result"); break;
	case EAutonomixMessageRole::Error: RoleStr = TEXT("error"); break;
	default: RoleStr = TEXT("unknown"); break;
	}
	Obj->SetStringField(TEXT("role"), RoleStr);

	// Preserve structured content blocks for assistant messages
	if (!Message.ContentBlocksJson.IsEmpty())
	{
		Obj->SetStringField(TEXT("content_blocks_json"), Message.ContentBlocksJson);
	}

	// Preserve DeepSeek reasoning_content for thinking mode replay
	if (!Message.ReasoningContent.IsEmpty())
	{
		Obj->SetStringField(TEXT("reasoning_content"), Message.ReasoningContent);
	}

	return Obj;
}

FAutonomixMessage FAutonomixConversationManager::MessageFromJson(const TSharedPtr<FJsonObject>& JsonObj)
{
	FAutonomixMessage Msg;

	if (!JsonObj.IsValid()) return Msg;

	FString IdStr;
	if (JsonObj->TryGetStringField(TEXT("message_id"), IdStr))
	{
		FGuid::Parse(IdStr, Msg.MessageId);
	}

	JsonObj->TryGetStringField(TEXT("content"), Msg.Content);
	JsonObj->TryGetStringField(TEXT("tool_use_id"), Msg.ToolUseId);
	JsonObj->TryGetStringField(TEXT("tool_name"), Msg.ToolName);

	// Context management fields
	JsonObj->TryGetStringField(TEXT("condense_parent"), Msg.CondenseParent);
	JsonObj->TryGetBoolField(TEXT("is_summary"), Msg.bIsSummary);
	JsonObj->TryGetStringField(TEXT("condense_id"), Msg.CondenseId);
	JsonObj->TryGetStringField(TEXT("truncation_parent"), Msg.TruncationParent);
	JsonObj->TryGetBoolField(TEXT("is_truncation_marker"), Msg.bIsTruncationMarker);
	JsonObj->TryGetStringField(TEXT("truncation_id"), Msg.TruncationId);

	FString TimestampStr;
	if (JsonObj->TryGetStringField(TEXT("timestamp"), TimestampStr))
	{
		FDateTime::ParseIso8601(*TimestampStr, Msg.Timestamp);
	}

	FString RoleStr;
	if (JsonObj->TryGetStringField(TEXT("role"), RoleStr))
	{
		if (RoleStr == TEXT("user")) Msg.Role = EAutonomixMessageRole::User;
		else if (RoleStr == TEXT("assistant")) Msg.Role = EAutonomixMessageRole::Assistant;
		else if (RoleStr == TEXT("system")) Msg.Role = EAutonomixMessageRole::System;
		else if (RoleStr == TEXT("tool_result")) Msg.Role = EAutonomixMessageRole::ToolResult;
		else if (RoleStr == TEXT("error")) Msg.Role = EAutonomixMessageRole::Error;
	}

	JsonObj->TryGetStringField(TEXT("content_blocks_json"), Msg.ContentBlocksJson);
	JsonObj->TryGetStringField(TEXT("reasoning_content"), Msg.ReasoningContent);

	return Msg;
}

bool FAutonomixConversationManager::SaveSession(const FString& FilePath) const
{
	// Ensure directory exists
	FString Dir = FPaths::GetPath(FilePath);
	IFileManager::Get().MakeDirectory(*Dir, true);

	// Build JSON
	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("format_version"), TEXT("1.1.0"));
	Root->SetStringField(TEXT("saved_at"), FDateTime::UtcNow().ToIso8601());
	Root->SetNumberField(TEXT("message_count"), History.Num());

	TArray<TSharedPtr<FJsonValue>> MessagesArray;
	for (const FAutonomixMessage& Msg : History)
	{
		// Skip streaming/internal messages
		if (Msg.bIsStreaming) continue;

		TSharedPtr<FJsonObject> MsgJson = MessageToJson(Msg);
		MessagesArray.Add(MakeShared<FJsonValueObject>(MsgJson));
	}
	Root->SetArrayField(TEXT("messages"), MessagesArray);

	// Serialize
	FString OutputString;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutputString);
	FJsonSerializer::Serialize(Root.ToSharedRef(), Writer);

	bool bSuccess = FFileHelper::SaveStringToFile(OutputString, *FilePath,
		FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);

	if (bSuccess)
	{
		UE_LOG(LogAutonomix, Log, TEXT("ConversationManager: Saved %d messages to %s"),
			MessagesArray.Num(), *FilePath);
	}
	else
	{
		UE_LOG(LogAutonomix, Error, TEXT("ConversationManager: Failed to save to %s"), *FilePath);
	}

	return bSuccess;
}

bool FAutonomixConversationManager::LoadSession(const FString& FilePath)
{
	if (!FPaths::FileExists(FilePath))
	{
		UE_LOG(LogAutonomix, Warning, TEXT("ConversationManager: File not found: %s"), *FilePath);
		return false;
	}

	FString JsonString;
	if (!FFileHelper::LoadFileToString(JsonString, *FilePath))
	{
		UE_LOG(LogAutonomix, Error, TEXT("ConversationManager: Failed to read: %s"), *FilePath);
		return false;
	}

	TSharedPtr<FJsonObject> Root;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
	{
		UE_LOG(LogAutonomix, Error, TEXT("ConversationManager: Failed to parse JSON: %s"), *FilePath);
		return false;
	}

	// Clear current history
	History.Empty();

	const TArray<TSharedPtr<FJsonValue>>* MessagesArray = nullptr;
	if (Root->TryGetArrayField(TEXT("messages"), MessagesArray))
	{
		for (const TSharedPtr<FJsonValue>& MsgVal : *MessagesArray)
		{
			const TSharedPtr<FJsonObject>* MsgObj = nullptr;
			if (MsgVal->TryGetObject(MsgObj) && MsgObj->IsValid())
			{
				FAutonomixMessage Msg = MessageFromJson(*MsgObj);
				History.Add(Msg);
			}
		}
	}

	UE_LOG(LogAutonomix, Log, TEXT("ConversationManager: Loaded %d messages from %s"),
		History.Num(), *FilePath);

	return true;
}

FString FAutonomixConversationManager::ExportAsText() const
{
	FString Output;
	Output += TEXT("=== Autonomix Conversation Export ===\n");
	Output += FString::Printf(TEXT("Messages: %d\n"), History.Num());
	Output += FString::Printf(TEXT("Exported: %s\n\n"), *FDateTime::UtcNow().ToString());

	for (const FAutonomixMessage& Msg : History)
	{
		FString RolePrefix;
		switch (Msg.Role)
		{
		case EAutonomixMessageRole::User: RolePrefix = TEXT("[USER]"); break;
		case EAutonomixMessageRole::Assistant: RolePrefix = TEXT("[ASSISTANT]"); break;
		case EAutonomixMessageRole::System: RolePrefix = TEXT("[SYSTEM]"); break;
		case EAutonomixMessageRole::ToolResult: RolePrefix = FString::Printf(TEXT("[TOOL_RESULT:%s]"), *Msg.ToolUseId); break;
		case EAutonomixMessageRole::Error: RolePrefix = TEXT("[ERROR]"); break;
		default: RolePrefix = TEXT("[UNKNOWN]"); break;
		}

		if (Msg.bIsSummary)
		{
			RolePrefix += TEXT("[SUMMARY]");
		}
		if (Msg.bIsTruncationMarker)
		{
			RolePrefix += TEXT("[TRUNCATION_MARKER]");
		}
		if (!Msg.CondenseParent.IsEmpty())
		{
			RolePrefix += TEXT("[CONDENSED]");
		}
		if (!Msg.TruncationParent.IsEmpty())
		{
			RolePrefix += TEXT("[TRUNCATED]");
		}

		Output += FString::Printf(TEXT("%s %s\n%s\n\n"),
			*Msg.Timestamp.ToString(TEXT("%H:%M:%S")),
			*RolePrefix,
			*Msg.Content);
	}

	return Output;
}
