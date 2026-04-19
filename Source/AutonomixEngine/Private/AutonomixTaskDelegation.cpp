// Copyright Autonomix. All Rights Reserved.

#include "AutonomixTaskDelegation.h"

FAutonomixTaskDelegation::FAutonomixTaskDelegation()
{
}

FAutonomixTaskDelegation::~FAutonomixTaskDelegation()
{
}

bool FAutonomixTaskDelegation::CreateSubTask(
	const FString& ParentTabId,
	EAutonomixAgentMode Mode,
	const FString& Message,
	const FString& InitialTodos,
	FAutonomixSubTask& OutSubTask
)
{
	// Check nesting depth
	if (GetNestingDepth(ParentTabId) >= MaxNestingDepth)
	{
		UE_LOG(LogTemp, Warning, TEXT("AutonomixTaskDelegation: Max nesting depth (%d) reached for tab %s"),
			MaxNestingDepth, *ParentTabId);
		return false;
	}

	// Check if parent already has an active child
	if (HasActiveChildTask(ParentTabId))
	{
		UE_LOG(LogTemp, Warning, TEXT("AutonomixTaskDelegation: Tab %s already has an active child task"),
			*ParentTabId);
		return false;
	}

	// Create the sub-task record
	FAutonomixSubTask NewSubTask;
	NewSubTask.ParentTabId = ParentTabId;
	NewSubTask.Mode = Mode;
	NewSubTask.Message = Message;
	NewSubTask.InitialTodos = InitialTodos;
	NewSubTask.Status = EAutonomixSubTaskStatus::Pending;

	// The ChildTabId will be set by SAutonomixMainPanel after creating the new tab
	// For now, leave it empty — it will be set via SetChildTabId()

	SubTasks.Add(NewSubTask);
	OutSubTask = NewSubTask;

	UE_LOG(LogTemp, Log, TEXT("AutonomixTaskDelegation: Created sub-task %s for parent tab %s (mode: %d)"),
		*NewSubTask.SubTaskId, *ParentTabId, (int32)Mode);

	return true;
}

void FAutonomixTaskDelegation::OnChildTaskCompleted(
	const FString& ChildTabId,
	bool bSuccess,
	const FString& ResultMessage
)
{
	// Find the sub-task associated with this child tab
	FString* SubTaskIdPtr = ChildTabToSubTaskMap.Find(ChildTabId);
	if (!SubTaskIdPtr)
	{
		UE_LOG(LogTemp, Warning, TEXT("AutonomixTaskDelegation: No sub-task found for child tab %s"), *ChildTabId);
		return;
	}

	// Find and update the sub-task
	for (FAutonomixSubTask& SubTask : SubTasks)
	{
		if (SubTask.SubTaskId == *SubTaskIdPtr)
		{
			SubTask.Status = bSuccess ? EAutonomixSubTaskStatus::Completed : EAutonomixSubTaskStatus::Failed;
			SubTask.ResultMessage = ResultMessage;
			SubTask.CompletedAt = FDateTime::UtcNow();

			// Remove from active child map
			ParentToActiveChildMap.Remove(SubTask.ParentTabId);

			UE_LOG(LogTemp, Log, TEXT("AutonomixTaskDelegation: Sub-task %s completed (success=%d) for parent tab %s"),
				*SubTask.SubTaskId, bSuccess, *SubTask.ParentTabId);

			// Fire completion delegate
			OnSubTaskCompleted.ExecuteIfBound(SubTask.SubTaskId, bSuccess, ResultMessage);
			return;
		}
	}
}

void FAutonomixTaskDelegation::CancelSubTask(const FString& SubTaskId)
{
	for (FAutonomixSubTask& SubTask : SubTasks)
	{
		if (SubTask.SubTaskId == SubTaskId)
		{
			SubTask.Status = EAutonomixSubTaskStatus::Cancelled;
			SubTask.CompletedAt = FDateTime::UtcNow();

			ParentToActiveChildMap.Remove(SubTask.ParentTabId);

			if (!SubTask.ChildTabId.IsEmpty())
			{
				ChildTabToSubTaskMap.Remove(SubTask.ChildTabId);
			}

			UE_LOG(LogTemp, Log, TEXT("AutonomixTaskDelegation: Cancelled sub-task %s"), *SubTaskId);
			return;
		}
	}
}

const FAutonomixSubTask* FAutonomixTaskDelegation::GetSubTask(const FString& SubTaskId) const
{
	for (const FAutonomixSubTask& SubTask : SubTasks)
	{
		if (SubTask.SubTaskId == SubTaskId)
		{
			return &SubTask;
		}
	}
	return nullptr;
}

TArray<FAutonomixSubTask*> FAutonomixTaskDelegation::GetActiveSubTasksForTab(const FString& ParentTabId)
{
	TArray<FAutonomixSubTask*> Active;
	for (FAutonomixSubTask& SubTask : SubTasks)
	{
		if (SubTask.ParentTabId == ParentTabId &&
			(SubTask.Status == EAutonomixSubTaskStatus::Pending ||
			 SubTask.Status == EAutonomixSubTaskStatus::Active))
		{
			Active.Add(&SubTask);
		}
	}
	return Active;
}

const FAutonomixSubTask* FAutonomixTaskDelegation::GetSubTaskByChildTab(const FString& ChildTabId) const
{
	const FString* SubTaskIdPtr = ChildTabToSubTaskMap.Find(ChildTabId);
	if (!SubTaskIdPtr) return nullptr;

	for (const FAutonomixSubTask& SubTask : SubTasks)
	{
		if (SubTask.SubTaskId == *SubTaskIdPtr)
		{
			return &SubTask;
		}
	}
	return nullptr;
}

bool FAutonomixTaskDelegation::SetChildTabId(const FString& SubTaskId, const FString& ChildTabId)
{
	for (FAutonomixSubTask& SubTask : SubTasks)
	{
		if (SubTask.SubTaskId == SubTaskId)
		{
			SubTask.ChildTabId = ChildTabId;
			SubTask.Status = EAutonomixSubTaskStatus::Active;

			ChildTabToSubTaskMap.Add(ChildTabId, SubTaskId);
			ParentToActiveChildMap.Add(SubTask.ParentTabId, ChildTabId);

			UE_LOG(LogTemp, Log, TEXT("AutonomixTaskDelegation: Linked child tab %s to sub-task %s"),
				*ChildTabId, *SubTaskId);
			return true;
		}
	}
	return false;
}

bool FAutonomixTaskDelegation::HasActiveChildTask(const FString& TabId) const
{
	return ParentToActiveChildMap.Contains(TabId);
}

int32 FAutonomixTaskDelegation::GetNestingDepth(const FString& TabId) const
{
	// Walk up the parent chain
	int32 Depth = 0;
	FString CurrentTabId = TabId;

	while (Depth < MaxNestingDepth)
	{
		// Find if this tab is a child of some parent
		bool bFoundParent = false;
		for (const FAutonomixSubTask& SubTask : SubTasks)
		{
			if (SubTask.ChildTabId == CurrentTabId)
			{
				CurrentTabId = SubTask.ParentTabId;
				Depth++;
				bFoundParent = true;
				break;
			}
		}
		if (!bFoundParent) break;
	}

	return Depth;
}
