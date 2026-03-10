// Copyright Autonomix. All Rights Reserved.

#include "AutonomixToolResultValidator.h"
#include "AutonomixCoreModule.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonSerializer.h"

void FAutonomixToolResultValidator::ValidateAndFixToolResults(TArray<TSharedPtr<FJsonValue>>& InOutMessages)
{
	if (InOutMessages.Num() == 0) return;

	// -----------------------------------------------------------------------
	// Pass 0: Global orphan tool_result removal.
	//
	// Ported from Roo Code's getEffectiveApiHistory() orphan-filter logic.
	//
	// When HandleContextWindowExceeded trims the oldest messages from history,
	// the first remaining user message may contain tool_result blocks whose
	// tool_use_id referenced an assistant message that was just trimmed away.
	// Claude rejects this with HTTP 400:
	//   "messages.N.content.0: unexpected 'tool_use_id' found in 'tool_result' blocks"
	//
	// Fix: collect ALL tool_use IDs visible in the current messages array,
	// then scan every user message and drop any tool_result block whose
	// tool_use_id is not in that global set.
	// If a user message's entire content array becomes empty after this filter,
	// remove the message itself.
	// -----------------------------------------------------------------------
	{
		// Step A: Collect all tool_use IDs from all assistant messages
		TSet<FString> GlobalToolUseIds;
		for (const TSharedPtr<FJsonValue>& MsgVal : InOutMessages)
		{
			const TSharedPtr<FJsonObject>* MsgObj = nullptr;
			if (!MsgVal->TryGetObject(MsgObj)) continue;

			FString Role;
			(*MsgObj)->TryGetStringField(TEXT("role"), Role);
			if (Role != TEXT("assistant")) continue;

			const TArray<TSharedPtr<FJsonValue>>* ContentArr = nullptr;
			if (!(*MsgObj)->TryGetArrayField(TEXT("content"), ContentArr)) continue;

			for (const TSharedPtr<FJsonValue>& Block : *ContentArr)
			{
				const TSharedPtr<FJsonObject>* BlockObj = nullptr;
				if (!Block->TryGetObject(BlockObj)) continue;

				FString BlockType;
				(*BlockObj)->TryGetStringField(TEXT("type"), BlockType);
				if (BlockType == TEXT("tool_use"))
				{
					FString Id;
					(*BlockObj)->TryGetStringField(TEXT("id"), Id);
					if (!Id.IsEmpty()) GlobalToolUseIds.Add(Id);
				}
			}
		}

		// Step B: For every user message, remove tool_result blocks with unknown IDs
		TArray<int32> EmptyUserMsgIndices; // indices to remove entirely if content becomes empty
		for (int32 Idx = 0; Idx < InOutMessages.Num(); ++Idx)
		{
			const TSharedPtr<FJsonObject>* MsgObj = nullptr;
			if (!InOutMessages[Idx]->TryGetObject(MsgObj)) continue;

			FString Role;
			(*MsgObj)->TryGetStringField(TEXT("role"), Role);
			if (Role != TEXT("user")) continue;

			const TArray<TSharedPtr<FJsonValue>>* ContentArr = nullptr;
			if (!(*MsgObj)->TryGetArrayField(TEXT("content"), ContentArr)) continue;

			// Check if any tool_result in this message is orphaned
			bool bHasOrphan = false;
			for (const TSharedPtr<FJsonValue>& Block : *ContentArr)
			{
				const TSharedPtr<FJsonObject>* BlockObj = nullptr;
				if (!Block->TryGetObject(BlockObj)) continue;
				FString BlockType;
				(*BlockObj)->TryGetStringField(TEXT("type"), BlockType);
				if (BlockType == TEXT("tool_result"))
				{
					FString TRId;
					(*BlockObj)->TryGetStringField(TEXT("tool_use_id"), TRId);
					if (!GlobalToolUseIds.Contains(TRId))
					{
						bHasOrphan = true;
						break;
					}
				}
			}

			if (!bHasOrphan) continue; // Nothing to fix

			// Filter out orphaned tool_result blocks
			TArray<TSharedPtr<FJsonValue>> FilteredContent;
			int32 OrphanCount = 0;
			for (const TSharedPtr<FJsonValue>& Block : *ContentArr)
			{
				const TSharedPtr<FJsonObject>* BlockObj = nullptr;
				if (!Block->TryGetObject(BlockObj))
				{
					FilteredContent.Add(Block);
					continue;
				}
				FString BlockType;
				(*BlockObj)->TryGetStringField(TEXT("type"), BlockType);
				if (BlockType == TEXT("tool_result"))
				{
					FString TRId;
					(*BlockObj)->TryGetStringField(TEXT("tool_use_id"), TRId);
					if (!GlobalToolUseIds.Contains(TRId))
					{
						OrphanCount++;
						UE_LOG(LogAutonomix, Warning,
							TEXT("ToolResultValidator [Pass 0]: Removing orphaned tool_result at msg[%d] "
							     "tool_use_id='%s' (referenced assistant was trimmed away)."),
							Idx, *TRId);
						continue; // Drop this block
					}
				}
				FilteredContent.Add(Block);
			}

			if (FilteredContent.Num() == 0)
			{
				// Entire user message is now empty — mark for removal
				EmptyUserMsgIndices.Add(Idx);
				UE_LOG(LogAutonomix, Warning,
					TEXT("ToolResultValidator [Pass 0]: User message at index %d became empty after "
					     "orphan removal (%d orphans). Will remove message."),
					Idx, OrphanCount);
			}
			else
			{
				// Update the message with filtered content
				TSharedPtr<FJsonObject> UpdatedMsg = MakeShared<FJsonObject>();
				UpdatedMsg->SetStringField(TEXT("role"), TEXT("user"));
				UpdatedMsg->SetArrayField(TEXT("content"), FilteredContent);
				InOutMessages[Idx] = MakeShared<FJsonValueObject>(UpdatedMsg);
				UE_LOG(LogAutonomix, Log,
					TEXT("ToolResultValidator [Pass 0]: Removed %d orphaned tool_result block(s) from msg[%d]."),
					OrphanCount, Idx);
			}
		}

		// Step C: Remove now-empty user messages (iterate in reverse to preserve indices)
		for (int32 k = EmptyUserMsgIndices.Num() - 1; k >= 0; --k)
		{
			InOutMessages.RemoveAt(EmptyUserMsgIndices[k]);
		}
	}

	// CRITICAL: Validate ALL assistant→user pairs, not just the last one.
	// After save/load cycles, any pair in the history may have empty/invalid tool_use_ids
	// which Claude rejects with HTTP 400.
	for (int32 AssistantIdx = 0; AssistantIdx < InOutMessages.Num(); ++AssistantIdx)
	{
		const TSharedPtr<FJsonObject>* CheckMsgObj = nullptr;
		if (!InOutMessages[AssistantIdx]->TryGetObject(CheckMsgObj)) continue;
		FString CheckRole;
		(*CheckMsgObj)->TryGetStringField(TEXT("role"), CheckRole);
		if (CheckRole != TEXT("assistant")) continue;

		ValidateAssistantUserPair(InOutMessages, AssistantIdx);
	}

	// CRITICAL: Handle the case where the LAST message is an assistant with tool_use blocks
	// but has NO following user/tool_result message (session interrupted mid-loop).
	// Claude rejects this with HTTP 400: "An assistant message with 'tool_calls' must be
	// followed by tool messages responding to each 'tool_call_id'".
	//
	// Fix: inject a synthetic user message containing tool_results for all orphaned tool_uses.
	int32 LastAssistantIdx = FindLastAssistantMessageIndex(InOutMessages);
	if (LastAssistantIdx < 0) return;

	// Only act if the assistant message is the very last message (no following user message)
	int32 NextUserIdx = FindNextUserMessageIndex(InOutMessages, LastAssistantIdx);
	if (NextUserIdx != -1) return;  // There's already a following user message — handled above

	// Get the last assistant message and extract its tool_use IDs
	const TSharedPtr<FJsonObject>* AssistantMsgObj = nullptr;
	if (!InOutMessages[LastAssistantIdx]->TryGetObject(AssistantMsgObj)) return;

	const TArray<TSharedPtr<FJsonValue>>* ContentArray = nullptr;
	if (!(*AssistantMsgObj)->TryGetArrayField(TEXT("content"), ContentArray)) return;

	TArray<FString> ToolUseIds = ExtractToolUseIds(*ContentArray);
	if (ToolUseIds.Num() == 0) return;  // No tool_use blocks — nothing to do

	// Inject a synthetic user message with tool_results for every orphaned tool_use
	UE_LOG(LogAutonomix, Warning,
		TEXT("ToolResultValidator: Last assistant message has %d orphaned tool_use(s) with no tool_results "
		     "(session was interrupted mid-loop). Injecting synthetic tool_results to allow conversation to continue."),
		ToolUseIds.Num());

	TArray<TSharedPtr<FJsonValue>> SyntheticContent;
	for (const FString& UseId : ToolUseIds)
	{
		TSharedPtr<FJsonObject> SyntheticResult = MakeShared<FJsonObject>();
		SyntheticResult->SetStringField(TEXT("type"), TEXT("tool_result"));
		SyntheticResult->SetStringField(TEXT("tool_use_id"), UseId);
		SyntheticResult->SetStringField(TEXT("content"),
			TEXT("Session was interrupted before this tool could complete. Please retry the operation."));
		SyntheticContent.Add(MakeShared<FJsonValueObject>(SyntheticResult));
	}

	TSharedPtr<FJsonObject> SyntheticUserMsg = MakeShared<FJsonObject>();
	SyntheticUserMsg->SetStringField(TEXT("role"), TEXT("user"));
	SyntheticUserMsg->SetArrayField(TEXT("content"), SyntheticContent);

	InOutMessages.Add(MakeShared<FJsonValueObject>(SyntheticUserMsg));

	UE_LOG(LogAutonomix, Log,
		TEXT("ToolResultValidator: Injected synthetic user message with %d tool_result(s) after interrupted assistant message."),
		ToolUseIds.Num());
}

void FAutonomixToolResultValidator::ValidateAssistantUserPair(TArray<TSharedPtr<FJsonValue>>& InOutMessages, int32 AssistantIdx)
{
	// Get assistant message object
	const TSharedPtr<FJsonObject>* AssistantMsgObj = nullptr;
	if (!InOutMessages[AssistantIdx]->TryGetObject(AssistantMsgObj) || !AssistantMsgObj->IsValid())
	{
		return;
	}

	// Extract tool_use IDs from assistant message
	const TArray<TSharedPtr<FJsonValue>>* AssistantContent = nullptr;
	if (!(*AssistantMsgObj)->TryGetArrayField(TEXT("content"), AssistantContent))
	{
		return;
	}

	TArray<FString> ToolUseIds = ExtractToolUseIds(*AssistantContent);
	if (ToolUseIds.Num() == 0) return; // No tool_use blocks — nothing to validate

	// Find next user message after assistant (should be the tool_result user message)
	int32 NextUserIdx = FindNextUserMessageIndex(InOutMessages, AssistantIdx);
	if (NextUserIdx == -1) return;

	// Get user message object
	const TSharedPtr<FJsonObject>* UserMsgObj = nullptr;
	if (!InOutMessages[NextUserIdx]->TryGetObject(UserMsgObj) || !UserMsgObj->IsValid())
	{
		return;
	}

	// Check user message role
	FString UserRole;
	(*UserMsgObj)->TryGetStringField(TEXT("role"), UserRole);
	if (UserRole != TEXT("user")) return;

	const TArray<TSharedPtr<FJsonValue>>* UserContent = nullptr;
	if (!(*UserMsgObj)->TryGetArrayField(TEXT("content"), UserContent))
	{
		// CRITICAL FIX (ported from Roo Code validateToolResultIds.ts):
		// The next user message has plain string content (not an array), meaning
		// it's a fresh user prompt — NOT tool_results.
		//
		// Conversation shape:
		//   assistant: { content: [text, tool_use_1, tool_use_2] }
		//   user:      { content: "do whatever you want" }   <-- plain text!
		//
		// Claude rejects this because the tool_uses have no tool_results.
		//
		// Fix: Convert the user message to array content with synthetic tool_results
		// prepended, then append the original text as a text block.
		FString UserTextContent;
		(*UserMsgObj)->TryGetStringField(TEXT("content"), UserTextContent);

		UE_LOG(LogAutonomix, Warning,
			TEXT("ToolResultValidator: Assistant has %d tool_use(s) but next user message is plain text (interrupted session). "
			     "Injecting synthetic tool_result(s) into the user message."),
			ToolUseIds.Num());

		TArray<TSharedPtr<FJsonValue>> NewContent;

		// Prepend synthetic tool_results for every tool_use
		for (const FString& UseId : ToolUseIds)
		{
			TSharedPtr<FJsonObject> SyntheticResult = MakeShared<FJsonObject>();
			SyntheticResult->SetStringField(TEXT("type"), TEXT("tool_result"));
			SyntheticResult->SetStringField(TEXT("tool_use_id"), UseId);
			SyntheticResult->SetStringField(TEXT("content"),
				TEXT("Tool execution was interrupted before completion."));
			NewContent.Add(MakeShared<FJsonValueObject>(SyntheticResult));
		}

		// Append the original user text as a text block
		if (!UserTextContent.IsEmpty())
		{
			TSharedPtr<FJsonObject> TextBlock = MakeShared<FJsonObject>();
			TextBlock->SetStringField(TEXT("type"), TEXT("text"));
			TextBlock->SetStringField(TEXT("text"), UserTextContent);
			NewContent.Add(MakeShared<FJsonValueObject>(TextBlock));
		}

		// Replace the user message with the array-content version
		TSharedPtr<FJsonObject> UpdatedUserMsg = MakeShared<FJsonObject>();
		UpdatedUserMsg->SetStringField(TEXT("role"), TEXT("user"));
		UpdatedUserMsg->SetArrayField(TEXT("content"), NewContent);
		InOutMessages[NextUserIdx] = MakeShared<FJsonValueObject>(UpdatedUserMsg);
		return;  // Fully handled
	}

	// ----------------------------------------------------------------
	// Step 1: Deduplicate tool_result blocks with the same tool_use_id
	// ----------------------------------------------------------------
	TSet<FString> SeenToolResultIds;
	TArray<TSharedPtr<FJsonValue>> DeduplicatedContent;

	for (const TSharedPtr<FJsonValue>& Block : *UserContent)
	{
		const TSharedPtr<FJsonObject>* BlockObj = nullptr;
		if (!Block->TryGetObject(BlockObj)) 
		{
			DeduplicatedContent.Add(Block);
			continue;
		}

		FString BlockType;
		(*BlockObj)->TryGetStringField(TEXT("type"), BlockType);

		if (BlockType == TEXT("tool_result"))
		{
			FString ToolResultId;
			(*BlockObj)->TryGetStringField(TEXT("tool_use_id"), ToolResultId);

			if (SeenToolResultIds.Contains(ToolResultId))
			{
				UE_LOG(LogAutonomix, Warning,
					TEXT("ToolResultValidator: Deduplicating tool_result with id=%s"), *ToolResultId);
				continue; // Skip duplicate
			}
			SeenToolResultIds.Add(ToolResultId);
		}
		DeduplicatedContent.Add(Block);
	}

	// Extract tool_result blocks after deduplication
	TArray<TSharedPtr<FJsonObject>> ToolResults = ExtractToolResults(DeduplicatedContent);

	// Build valid ID set
	TSet<FString> ValidToolUseIds(ToolUseIds);

	// Check for ID mismatches
	bool bHasInvalidIds = false;
	for (const TSharedPtr<FJsonObject>& TR : ToolResults)
	{
		FString Id;
		TR->TryGetStringField(TEXT("tool_use_id"), Id);
		if (!ValidToolUseIds.Contains(Id))
		{
			bHasInvalidIds = true;
			break;
		}
	}

	// Check for missing tool_results
	TSet<FString> ExistingResultIds;
	for (const TSharedPtr<FJsonObject>& TR : ToolResults)
	{
		FString Id;
		TR->TryGetStringField(TEXT("tool_use_id"), Id);
		ExistingResultIds.Add(Id);
	}

	TArray<FString> MissingToolUseIds;
	for (const FString& UseId : ToolUseIds)
	{
		if (!ExistingResultIds.Contains(UseId))
		{
			MissingToolUseIds.Add(UseId);
		}
	}

	// If no issues, just apply deduplication and return
	if (!bHasInvalidIds && MissingToolUseIds.Num() == 0)
	{
		if (DeduplicatedContent.Num() != UserContent->Num())
		{
			// Re-set the deduplicated content
			TSharedPtr<FJsonObject> UpdatedUserMsg = MakeShared<FJsonObject>();
			UpdatedUserMsg->SetStringField(TEXT("role"), TEXT("user"));
			UpdatedUserMsg->SetArrayField(TEXT("content"), DeduplicatedContent);
			InOutMessages[NextUserIdx] = MakeShared<FJsonValueObject>(UpdatedUserMsg);
		}
		return;
	}

	// ----------------------------------------------------------------
	// Step 2: Fix mismatched IDs by position + inject missing tool_results
	// ----------------------------------------------------------------
	if (bHasInvalidIds)
	{
		UE_LOG(LogAutonomix, Warning,
			TEXT("ToolResultValidator: Detected tool_result ID mismatch. tool_use IDs=[%s]"),
			*FString::Join(ToolUseIds, TEXT(", ")));
	}
	if (MissingToolUseIds.Num() > 0)
	{
		UE_LOG(LogAutonomix, Warning,
			TEXT("ToolResultValidator: Missing tool_results for tool_use IDs=[%s]. Injecting synthetic results."),
			*FString::Join(MissingToolUseIds, TEXT(", ")));
	}

	// Fix IDs by position
	TSet<FString> UsedToolUseIds;
	TArray<TSharedPtr<FJsonValue>> CorrectedContent;

	int32 ToolResultIndex = 0;
	for (const TSharedPtr<FJsonValue>& Block : DeduplicatedContent)
	{
		const TSharedPtr<FJsonObject>* BlockObj = nullptr;
		if (!Block->TryGetObject(BlockObj))
		{
			CorrectedContent.Add(Block);
			continue;
		}

		FString BlockType;
		(*BlockObj)->TryGetStringField(TEXT("type"), BlockType);

		if (BlockType != TEXT("tool_result"))
		{
			CorrectedContent.Add(Block);
			continue;
		}

		FString CurrentId;
		(*BlockObj)->TryGetStringField(TEXT("tool_use_id"), CurrentId);

		// If ID is valid and not yet used, keep it
		if (ValidToolUseIds.Contains(CurrentId) && !UsedToolUseIds.Contains(CurrentId))
		{
			UsedToolUseIds.Add(CurrentId);
			CorrectedContent.Add(Block);
			ToolResultIndex++;
			continue;
		}

		// Try to match by position
		if (ToolResultIndex < ToolUseIds.Num())
		{
			FString CorrectId = ToolUseIds[ToolResultIndex];
			if (!UsedToolUseIds.Contains(CorrectId))
			{
				// Create a copy with the corrected ID
				TSharedPtr<FJsonObject> FixedBlock = MakeShared<FJsonObject>();
				for (const auto& Pair : (*BlockObj)->Values)
				{
					FixedBlock->SetField(Pair.Key, Pair.Value);
				}
				FixedBlock->SetStringField(TEXT("tool_use_id"), CorrectId);

				UsedToolUseIds.Add(CorrectId);
				CorrectedContent.Add(MakeShared<FJsonValueObject>(FixedBlock));

				UE_LOG(LogAutonomix, Log,
					TEXT("ToolResultValidator: Fixed tool_result ID at position %d: %s -> %s"),
					ToolResultIndex, *CurrentId, *CorrectId);

				ToolResultIndex++;
				continue;
			}
		}

		// No valid match — drop this tool_result
		UE_LOG(LogAutonomix, Warning,
			TEXT("ToolResultValidator: Dropping unmatched tool_result with id=%s"), *CurrentId);
		ToolResultIndex++;
	}

	// ----------------------------------------------------------------
	// Step 3: Inject synthetic tool_results for any still-missing tool_uses
	// ----------------------------------------------------------------
	TSet<FString> CoveredIds;
	for (const TSharedPtr<FJsonValue>& Block : CorrectedContent)
	{
		const TSharedPtr<FJsonObject>* BlockObj = nullptr;
		if (!Block->TryGetObject(BlockObj)) continue;

		FString BlockType;
		(*BlockObj)->TryGetStringField(TEXT("type"), BlockType);
		if (BlockType == TEXT("tool_result"))
		{
			FString Id;
			(*BlockObj)->TryGetStringField(TEXT("tool_use_id"), Id);
			CoveredIds.Add(Id);
		}
	}

	TArray<TSharedPtr<FJsonValue>> FinalContent;

	for (const FString& UseId : ToolUseIds)
	{
		if (!CoveredIds.Contains(UseId))
		{
			TSharedPtr<FJsonObject> SyntheticResult = MakeShared<FJsonObject>();
			SyntheticResult->SetStringField(TEXT("type"), TEXT("tool_result"));
			SyntheticResult->SetStringField(TEXT("tool_use_id"), UseId);
			SyntheticResult->SetStringField(TEXT("content"),
				TEXT("Tool execution was interrupted before completion."));

			FinalContent.Add(MakeShared<FJsonValueObject>(SyntheticResult));

			UE_LOG(LogAutonomix, Log,
				TEXT("ToolResultValidator: Injected synthetic tool_result for orphaned tool_use id=%s"), *UseId);
		}
	}

	// Append corrected content after synthetic results
	FinalContent.Append(CorrectedContent);

	// Update the user message in the messages array
	TSharedPtr<FJsonObject> UpdatedUserMsg = MakeShared<FJsonObject>();
	UpdatedUserMsg->SetStringField(TEXT("role"), TEXT("user"));
	UpdatedUserMsg->SetArrayField(TEXT("content"), FinalContent);
	InOutMessages[NextUserIdx] = MakeShared<FJsonValueObject>(UpdatedUserMsg);
}

TArray<FString> FAutonomixToolResultValidator::ExtractToolUseIds(const TArray<TSharedPtr<FJsonValue>>& ContentArray)
{
	TArray<FString> Ids;
	for (const TSharedPtr<FJsonValue>& Block : ContentArray)
	{
		const TSharedPtr<FJsonObject>* BlockObj = nullptr;
		if (!Block->TryGetObject(BlockObj)) continue;

		FString BlockType;
		(*BlockObj)->TryGetStringField(TEXT("type"), BlockType);
		if (BlockType == TEXT("tool_use"))
		{
			FString Id;
			(*BlockObj)->TryGetStringField(TEXT("id"), Id);
			if (!Id.IsEmpty())
			{
				Ids.Add(Id);
			}
		}
	}
	return Ids;
}

TArray<TSharedPtr<FJsonObject>> FAutonomixToolResultValidator::ExtractToolResults(const TArray<TSharedPtr<FJsonValue>>& ContentArray)
{
	TArray<TSharedPtr<FJsonObject>> Results;
	for (const TSharedPtr<FJsonValue>& Block : ContentArray)
	{
		const TSharedPtr<FJsonObject>* BlockObj = nullptr;
		if (!Block->TryGetObject(BlockObj)) continue;

		FString BlockType;
		(*BlockObj)->TryGetStringField(TEXT("type"), BlockType);
		if (BlockType == TEXT("tool_result"))
		{
			Results.Add(*BlockObj);
		}
	}
	return Results;
}

int32 FAutonomixToolResultValidator::FindLastAssistantMessageIndex(const TArray<TSharedPtr<FJsonValue>>& Messages)
{
	for (int32 i = Messages.Num() - 1; i >= 0; --i)
	{
		const TSharedPtr<FJsonObject>* MsgObj = nullptr;
		if (!Messages[i]->TryGetObject(MsgObj)) continue;

		FString Role;
		(*MsgObj)->TryGetStringField(TEXT("role"), Role);
		if (Role == TEXT("assistant"))
		{
			return i;
		}
	}
	return -1;
}

int32 FAutonomixToolResultValidator::FindNextUserMessageIndex(const TArray<TSharedPtr<FJsonValue>>& Messages, int32 AfterIndex)
{
	for (int32 i = AfterIndex + 1; i < Messages.Num(); ++i)
	{
		const TSharedPtr<FJsonObject>* MsgObj = nullptr;
		if (!Messages[i]->TryGetObject(MsgObj)) continue;

		FString Role;
		(*MsgObj)->TryGetStringField(TEXT("role"), Role);
		if (Role == TEXT("user"))
		{
			return i;
		}
	}
	return -1;
}
