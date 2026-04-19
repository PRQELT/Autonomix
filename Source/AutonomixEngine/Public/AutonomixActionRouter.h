// Copyright Autonomix. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AutonomixTypes.h"
#include "AutonomixInterfaces.h"

/**
 * Routes tool calls from the AI to the appropriate action executor.
 */
class AUTONOMIXENGINE_API FAutonomixActionRouter
{
public:
	FAutonomixActionRouter();
	~FAutonomixActionRouter();

	/** Register an action executor */
	void RegisterExecutor(TSharedRef<IAutonomixActionExecutor> Executor);

	/** Route a tool call to the appropriate executor */
	FAutonomixActionResult RouteToolCall(const FAutonomixToolCall& ToolCall);

	/** Preview a tool call without executing */
	FAutonomixActionPlan PreviewToolCall(const FAutonomixToolCall& ToolCall);

	/** Get all registered executor names */
	TArray<FName> GetRegisteredExecutorNames() const;

	/** Get all registered tool names (keys of the executor map) */
	TArray<FString> GetRegisteredToolNames() const;

	/** Find executor by tool name */
	TSharedPtr<IAutonomixActionExecutor> FindExecutorForTool(const FString& ToolName) const;

private:
	/** Map of tool name -> executor */
	TMap<FString, TSharedRef<IAutonomixActionExecutor>> ExecutorMap;
};
