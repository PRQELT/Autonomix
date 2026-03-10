// Copyright Autonomix. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AutonomixTypes.h"

/**
 * Metadata record for a saved conversation session (task).
 * Plain C++ struct — no UHT/GENERATED_BODY needed since this is
 * a persistence-only record, not exposed to Blueprint.
 */
struct AUTONOMIXENGINE_API FAutonomixTaskHistoryItem
{
	/** Unique tab/session ID */
	FString TabId;

	/** Display title */
	FString Title;

	/** When the task was created */
	FDateTime CreatedAt;

	/** When the task was last active */
	FDateTime LastActiveAt;

	/** Total tokens used in this session */
	FAutonomixTokenUsage TotalTokenUsage;

	/** Total cost in USD */
	float TotalCostUSD = 0.0f;

	/** Number of messages in the session */
	int32 MessageCount = 0;

	/** Agent mode used */
	EAutonomixAgentMode Mode = EAutonomixAgentMode::General;

	/** First user message (for preview) */
	FString FirstUserMessage;

	/** Path to the saved conversation JSON file */
	FString ConversationFilePath;

	FAutonomixTaskHistoryItem()
		: CreatedAt(FDateTime::UtcNow())
		, LastActiveAt(FDateTime::UtcNow())
	{}
};

/**
 * Persists and queries task history across all conversation sessions.
 *
 * Ported/adapted from Roo Code's TaskHistoryStore.ts.
 * Provides a browsable history of all past Autonomix sessions.
 *
 * STORAGE:
 *   Saved/Autonomix/TaskHistory/task_history.json
 *   Each entry records: tab ID, title, timestamps, token usage, cost, message count
 *
 * USAGE:
 *   - Call RecordTask() when a tab starts or after each completion
 *   - Call GetHistory() to populate the history panel
 *   - Call LoadTask() to resume a past session
 */
class AUTONOMIXENGINE_API FAutonomixTaskHistory
{
public:
	FAutonomixTaskHistory();
	~FAutonomixTaskHistory();

	/** Initialize — creates or loads the history file */
	bool Initialize();

	/** Record a new or updated task entry */
	void RecordTask(const FAutonomixTaskHistoryItem& Item);

	/** Update an existing task's metadata */
	void UpdateTask(const FString& TabId, const FAutonomixTaskHistoryItem& UpdatedItem);

	/** Remove a task from history */
	void RemoveTask(const FString& TabId);

	/** Get all history items, most recent first */
	TArray<FAutonomixTaskHistoryItem> GetHistory() const;

	/** Get a specific task by ID */
	const FAutonomixTaskHistoryItem* GetTask(const FString& TabId) const;

	/** Get total session count */
	int32 GetTaskCount() const { return HistoryItems.Num(); }

	/** Clear all history */
	void ClearHistory();

	/** Export history as a readable text summary */
	FString ExportAsText() const;

	/** Get path to history JSON file */
	static FString GetHistoryFilePath();

private:
	void LoadFromDisk();
	void SaveToDisk() const;

	TArray<FAutonomixTaskHistoryItem> HistoryItems;
	bool bIsInitialized = false;
};
