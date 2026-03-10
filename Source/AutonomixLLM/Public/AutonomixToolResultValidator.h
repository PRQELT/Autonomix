// Copyright Autonomix. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AutonomixTypes.h"

/**
 * Validates and fixes tool_result IDs in conversation history before sending to Claude.
 *
 * Ported from Roo Code's validateToolResultIds.ts + getEffectiveApiHistory() orphan filter.
 *
 * Handles:
 * - Pass 0 (global orphan removal): Removes tool_result blocks in ANY user message whose
 *   tool_use_id does not exist in any assistant message in the current array. This is
 *   critical after context-window trimming where the oldest assistant messages are removed
 *   but the following user tool_result messages remain. Matches Roo Code's
 *   getEffectiveApiHistory() orphan filter logic.
 * - Deduplication of tool_result blocks with the same tool_use_id
 * - ID mismatch fixing by position (when IDs don't match between assistant tool_use and user tool_result)
 * - Injection of synthetic results for orphaned tool_use blocks that have no tool_result
 *
 * Call ValidateAndFixToolResults() on the JSON messages array before every API call.
 */
class AUTONOMIXLLM_API FAutonomixToolResultValidator
{
public:
	/**
	 * Validates and fixes tool_result IDs in the messages JSON array.
	 *
	 * Pass 0: Global orphan removal — collects all tool_use IDs across all assistant
	 * messages, then removes any tool_result blocks in user messages that reference
	 * unknown IDs. Empty user messages after this pass are removed entirely.
	 * This prevents HTTP 400 "unexpected tool_use_id" errors after context trimming.
	 *
	 * Then for each assistant→user pair:
	 *  1. Deduplicates tool_results with the same tool_use_id
	 *  2. Fixes mismatched IDs by position
	 *  3. Injects synthetic results for any orphaned tool_uses
	 *
	 * @param InOutMessages - The JSON messages array (role/content objects) to validate in-place.
	 *                        This is the array passed to the Claude API.
	 */
	static void ValidateAndFixToolResults(TArray<TSharedPtr<FJsonValue>>& InOutMessages);

private:
	/** Validate and fix a single assistant→user tool_result pair */
	static void ValidateAssistantUserPair(TArray<TSharedPtr<FJsonValue>>& InOutMessages, int32 AssistantIdx);

	/** Extract tool_use IDs from an assistant message's content array */
	static TArray<FString> ExtractToolUseIds(const TArray<TSharedPtr<FJsonValue>>& ContentArray);

	/** Extract tool_result objects from a user message's content array */
	static TArray<TSharedPtr<FJsonObject>> ExtractToolResults(const TArray<TSharedPtr<FJsonValue>>& ContentArray);

	/** Find the index of the last assistant message in the messages array */
	static int32 FindLastAssistantMessageIndex(const TArray<TSharedPtr<FJsonValue>>& Messages);

	/** Find the index of the first user message after a given index */
	static int32 FindNextUserMessageIndex(const TArray<TSharedPtr<FJsonValue>>& Messages, int32 AfterIndex);
};
