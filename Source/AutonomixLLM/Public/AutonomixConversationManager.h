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

	/** Save the current conversation to a JSON file.
	 *  Preserves all message fields including ContentBlocksJson for structural fidelity.
	 *  @return true if saved successfully */
	bool SaveSession(const FString& FilePath) const;

	/** Load a conversation from a JSON file.
	 *  Replaces the current history with the loaded messages.
	 *  @return true if loaded successfully */
	bool LoadSession(const FString& FilePath);

	/** Export the conversation to a human-readable text format.
	 *  @return The formatted conversation text */
	FString ExportAsText() const;

	/** Get the default save directory for conversations */
	static FString GetConversationSaveDir();

	/** Delegate: fired when a new message is added */
	FOnAutonomixMessageAdded OnMessageAdded;

private:
	/** Full conversation history */
	TArray<FAutonomixMessage> History;

	/** Maximum token budget for pruning */
	int32 MaxTokenBudget;

	/** Estimate tokens for a single message (rough: ~4 chars per token) */
	static int32 EstimateMessageTokens(const FAutonomixMessage& Message);

	/** Serialize a message to JSON */
	static TSharedPtr<FJsonObject> MessageToJson(const FAutonomixMessage& Message);

	/** Deserialize a message from JSON */
	static FAutonomixMessage MessageFromJson(const TSharedPtr<FJsonObject>& JsonObj);

	/** Generate a new unique truncation/condense ID */
	static FString GenerateUniqueId();
};
