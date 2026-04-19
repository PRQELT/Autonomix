// Copyright Autonomix. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AutonomixTypes.h"

/**
 * Status of a delegated sub-task.
 */
enum class EAutonomixSubTaskStatus : uint8
{
	/** Sub-task created but not yet started */
	Pending,

	/** Sub-task is actively running */
	Active,

	/** Sub-task completed successfully */
	Completed,

	/** Sub-task was cancelled */
	Cancelled,

	/** Sub-task failed */
	Failed,
};

/**
 * Represents a delegated sub-task spawned via the new_task tool.
 */
struct AUTONOMIXENGINE_API FAutonomixSubTask
{
	/** Unique ID for this sub-task */
	FString SubTaskId;

	/** ID of the parent conversation tab */
	FString ParentTabId;

	/** ID of the child conversation tab created for this sub-task */
	FString ChildTabId;

	/** The mode to run the sub-task in */
	EAutonomixAgentMode Mode = EAutonomixAgentMode::General;

	/** The initial message/instructions for the sub-task */
	FString Message;

	/** Optional initial todo list markdown */
	FString InitialTodos;

	/** Current status */
	EAutonomixSubTaskStatus Status = EAutonomixSubTaskStatus::Pending;

	/** Result message from the sub-task (set on completion) */
	FString ResultMessage;

	/** When the sub-task was created */
	FDateTime CreatedAt;

	/** When the sub-task completed */
	FDateTime CompletedAt;

	FAutonomixSubTask()
		: SubTaskId(FGuid::NewGuid().ToString())
		, CreatedAt(FDateTime::UtcNow())
	{}
};

/**
 * Delegate called when a sub-task completes.
 * Payload: SubTaskId, bSuccess, ResultMessage
 */
DECLARE_DELEGATE_ThreeParams(FOnAutonomixSubTaskCompleted,
	const FString& /*SubTaskId*/,
	bool /*bSuccess*/,
	const FString& /*ResultMessage*/
);

/**
 * Manages AI-initiated sub-task delegation via the new_task tool.
 *
 * Ported and adapted from Roo Code's Task hierarchy system (Task.ts):
 *   - rootTask / parentTask / childTask hierarchy
 *   - new_task tool spawns a child task in a specific mode
 *   - Parent waits for child to complete (via delegate)
 *   - Child reports result back to parent
 *
 * USAGE IN SAutonomixMainPanel:
 *   // When AI calls new_task tool:
 *   FString Result = HandleNewTask(ToolCall);
 *   // This creates a new tab, pauses parent tab's agentic loop,
 *   // and waits for the child tab to call attempt_completion.
 *
 * TAB RELATIONSHIP:
 *   Parent tab → spawns child tab → child runs → calls attempt_completion
 *   → parent's OnSubTaskCompleted fires → parent resumes with child's result
 *
 * Each tab maintains:
 *   - ParentTabId: which tab spawned this tab (empty if root)
 *   - ChildTabId: which tab this tab delegated to (empty if no active child)
 *   - SubTaskId: the delegation record ID
 */
class AUTONOMIXENGINE_API FAutonomixTaskDelegation
{
public:
	FAutonomixTaskDelegation();
	~FAutonomixTaskDelegation();

	// ---- Configuration ----

	/** Maximum nesting depth for sub-tasks (prevents infinite delegation) */
	static constexpr int32 MaxNestingDepth = 5;

	// ---- Sub-task lifecycle ----

	/**
	 * Create a new sub-task delegation record.
	 * Called when the AI invokes the new_task tool.
	 *
	 * @param ParentTabId     ID of the spawning tab
	 * @param Mode            Agent mode for the sub-task
	 * @param Message         Initial message/instructions
	 * @param InitialTodos    Optional todo markdown
	 * @param OutSubTask      The created sub-task record
	 * @return true if creation succeeded
	 */
	bool CreateSubTask(
		const FString& ParentTabId,
		EAutonomixAgentMode Mode,
		const FString& Message,
		const FString& InitialTodos,
		FAutonomixSubTask& OutSubTask
	);

	/**
	 * Called when a child tab's AI calls attempt_completion.
	 * Fires OnSubTaskCompleted delegate on the parent.
	 *
	 * @param ChildTabId      The completing child tab ID
	 * @param bSuccess        Whether the task succeeded
	 * @param ResultMessage   The completion message
	 */
	void OnChildTaskCompleted(
		const FString& ChildTabId,
		bool bSuccess,
		const FString& ResultMessage
	);

	/**
	 * Cancel a pending or active sub-task.
	 */
	void CancelSubTask(const FString& SubTaskId);

	// ---- Query ----

	/** Get sub-task by ID */
	const FAutonomixSubTask* GetSubTask(const FString& SubTaskId) const;

	/** Get all active sub-tasks for a parent tab */
	TArray<FAutonomixSubTask*> GetActiveSubTasksForTab(const FString& ParentTabId);

	/** Get the sub-task associated with a child tab */
	const FAutonomixSubTask* GetSubTaskByChildTab(const FString& ChildTabId) const;

	/** Check if a tab has an active child task (parent is waiting) */
	bool HasActiveChildTask(const FString& TabId) const;

	/** Get nesting depth for a given tab (how many parent levels) */
	int32 GetNestingDepth(const FString& TabId) const;

	/**
	 * Set the child tab ID for a sub-task (called after the child tab is created).
	 * Also registers the tab-to-subtask mapping.
	 */
	bool SetChildTabId(const FString& SubTaskId, const FString& ChildTabId);

	/** Get all sub-tasks (const) */
	const TArray<FAutonomixSubTask>& GetAllSubTasks() const { return SubTasks; }

	// ---- Delegates ----

	/** Fired when a sub-task completes. Bind in SAutonomixMainPanel. */
	FOnAutonomixSubTaskCompleted OnSubTaskCompleted;

private:
	TArray<FAutonomixSubTask> SubTasks;

	/** Map from ChildTabId to SubTaskId for quick lookup */
	TMap<FString, FString> ChildTabToSubTaskMap;

	/** Map from ParentTabId to ChildTabId (one active child per parent) */
	TMap<FString, FString> ParentToActiveChildMap;
};
