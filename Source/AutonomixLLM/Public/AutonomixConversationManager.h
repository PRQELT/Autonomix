// Copyright Autonomix. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AutonomixTypes.h"

/**
 * Manages conversation history, context window budgeting,
 * message pruning, and persistence for the AI interaction session.
 *
 * v2.3: Added SaveSession/LoadSession for conversation persistence/export.
 * Uses ContentBlocksJson on assistant messages for structural fidelity.
 *
 * v3.0: Added GetEffectiveHistory() (non-destructive condense/truncate filter)
 * and TruncateConversation() (sliding window with truncation markers).
 * Messages are never deleted -- only tagged with CondenseParent/TruncationParent.
 */
class AUTONOMIXLLM_API FAutonomixConversationManager
{
public:
	FAutonomixConversationManager();
	~FAutonomixConversationManager();

	/** Add a user message to the conversation */
	FAutonomixMessage& AddUserMessage(const FString& Content);

	/** Add an assistant message to the conversation (text-only, no content blocks) */
	FAutonomixMessage& AddAssistantMessage(const FString& Content);

	/** Add a full assistant message preserving ContentBlocksJson for API round-tripping.
	 *  CRITICAL: Must be used when the assistant response contains tool_use blocks,
	 *  otherwise the tool_result messages will reference non-existent tool_use_ids
	 *  and Claude will reject with HTTP 400 "unexpected tool_use_id". */
	FAutonomixMessage& AddAssistantMessageFull(const FAutonomixMessage& SourceMessage);

	/** Add a system message to the conversation */
	FAutonomixMessage& AddSystemMessage(const FString& Content);

	/** Add a tool result message to the conversation */
	FAutonomixMessage& AddToolResultMessage(const FString& ToolUseId, const FString& Content, bool bIsError = false);

	/**
	 * Detect orphaned tool_use blocks in the conversation history and inject synthetic
	 * tool_result messages for each one.
	 *
	 * An orphaned tool_use is an assistant message containing tool_use content blocks
	 * (in ContentBlocksJson) that have no matching tool_result message following them.
	 * This happens when the editor is closed or the task is interrupted mid-agentic-loop.
	 *
	 * For each orphaned tool_use ID, a synthetic tool_result message is added:
	 *   "Task was interrupted before this tool call could be completed."
	 *
	 * This prevents API errors from Anthropic (strict alternation) and OpenAI
	 * (rejects orphaned function calls).
	 *
	 * @return Number of synthetic tool_result messages injected
	 */
	int32 InjectSyntheticToolResultsForOrphans();

	/** Append streaming text to the current assistant message */
	void AppendStreamingText(const FGuid& MessageId, const FString& DeltaText);

	/** Mark a streaming message as complete */
	void FinalizeStreamingMessage(const FGuid& MessageId);

	/** Get the full conversation history (including condensed/truncated messages) */
	const TArray<FAutonomixMessage>& GetHistory() const { return History; }

	// ============================================================================
	// Context Management: Effective History (v3.0)
	// ============================================================================

	/**
	 * Get the "effective" history to send to the API.
	 *
	 * Implements the Roo Code getEffectiveApiHistory() logic:
	 * - If a summary message (bIsSummary=true) exists, return from the summary onwards
	 * - Filter out messages whose CondenseParent points to an existing summary
	 * - Filter out messages whose TruncationParent points to an existing truncation marker
	 * - Filter out orphaned tool_result blocks that reference condensed-away tool_uses
	 *
	 * This gives Claude a "fresh start" after condensation while keeping full history
	 * stored locally for rewind/export.
	 *
	 * @return Filtered history safe to send to the Claude API
	 */
	TArray<FAutonomixMessage> GetEffectiveHistory() const;

	/**
	 * Non-destructive sliding window truncation.
	 *
	 * Tags the oldest (FracToRemove * 50%) messages with TruncationParent,
	 * inserts a truncation marker, and keeps the first message always.
	 * Tagged messages are filtered out by GetEffectiveHistory().
	 *
	 * @param FracToRemove  Fraction of visible messages (excl. first) to hide (0.0–1.0)
	 * @return Number of messages hidden
	 */
	int32 TruncateConversation(float FracToRemove = 0.5f);

	/**
	 * Clean up orphaned CondenseParent/TruncationParent references.
	 *
	 * Should be called after any operation that removes summary or truncation markers
	 * (e.g., rewind/clear). Messages with orphaned parent refs become active again.
	 */
	void CleanupAfterTruncation();

	/** Get a pruned version of the history that fits within the token budget (legacy) */
	TArray<FAutonomixMessage> GetPrunedHistory(int32 MaxTokenBudget) const;

	/** Clear the conversation history */
	void ClearHistory();

	/** Get the estimated total token count of the current history */
	int32 EstimateTotalTokens() const;

	/** Set the maximum token budget for conversation history */
	void SetMaxTokenBudget(int32 InMaxTokens);

	/** Get the number of messages in history */
	int32 GetMessageCount() const { return History.Num(); }

	/** Get a specific message by ID */
	FAutonomixMessage* GetMessageById(const FGuid& MessageId);

	/** Get the last message in the conversation */
	FAutonomixMessage* GetLastMessage();

	// ============================================================================
	// Conversation Persistence (v2.3)
	// ============================================================================

	/** Save the current conversation to a JSON file (ui_messages.json — full UI state).
	 *  Preserves all message fields including ContentBlocksJson for structural fidelity.
	 *  @return true if saved successfully */
	bool SaveSession(const FString& FilePath) const;

	/** Save LLM-facing API history (api_history.json — Anthropic wire format).
	 *  Only includes messages that would be sent to the API: user, assistant,
	 *  and tool_result messages. Strips UI-only fields.
	 *  @return true if saved successfully */
	bool SaveApiHistory(const FString& FilePath) const;

	/** Load a conversation from a JSON file (either ui_messages.json or legacy format).
	 *  Replaces the current history with the loaded messages.
	 *  @return true if loaded successfully */
	bool LoadSession(const FString& FilePath);

	/** Export the conversation to a human-readable text format.
	 *  @return The formatted conversation text */
	FString ExportAsText() const;

	/** Get the default save directory for conversations */
	static FString GetConversationSaveDir();

	/** Get the base directory for the per-task directory model (v4.0).
	 *  Returns: {ProjectSavedDir}/Autonomix/Tasks/ */
	static FString GetTasksBaseDir();

	/** Delegate: fired when a new message is added */
	FOnAutonomixMessageAdded OnMessageAdded;

private:
	/** Full conversation history */
	TArray<FAutonomixMessage> History;

	/** Maximum token budget for pruning */
	int32 MaxTokenBudget;

	/** Estimate tokens for a single message (rough: ~4 chars per token) */
	static int32 EstimateMessageTokens(const FAutonomixMessage& Message);

	/**
	 * Token optimization: evict (summarize) old verbose tool results in a message array.
	 *
	 * Tool results that have already been processed by the AI (i.e., there is a
	 * subsequent assistant message) and exceed the eviction threshold are replaced
	 * with a compact one-line summary. The ToolUseId is preserved to maintain
	 * API structural validity (Claude requires matching tool_use_id references).
	 *
	 * Only the Content field is modified; the original full results remain in
	 * the stored History for export/replay.
	 *
	 * Called by GetEffectiveHistory() before returning the filtered array.
	 */
	static void EvictOldToolResults(TArray<FAutonomixMessage>& Messages);

	/** Serialize a message to JSON */
	static TSharedPtr<FJsonObject> MessageToJson(const FAutonomixMessage& Message);

	/** Deserialize a message from JSON */
	static FAutonomixMessage MessageFromJson(const TSharedPtr<FJsonObject>& JsonObj);

	/** Generate a new unique truncation/condense ID */
	static FString GenerateUniqueId();
};
