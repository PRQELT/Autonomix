// Copyright Autonomix. All Rights Reserved.

#include "AutonomixActionRouter.h"
#include "AutonomixCoreModule.h"

FAutonomixActionRouter::FAutonomixActionRouter() {}
FAutonomixActionRouter::~FAutonomixActionRouter() {}

void FAutonomixActionRouter::RegisterExecutor(TSharedRef<IAutonomixActionExecutor> Executor)
{
	for (const FString& ToolName : Executor->GetSupportedToolNames())
	{
		ExecutorMap.Add(ToolName, Executor);
		UE_LOG(LogAutonomix, Log, TEXT("ActionRouter: Registered executor for tool '%s'"), *ToolName);
	}
}

FAutonomixActionResult FAutonomixActionRouter::RouteToolCall(const FAutonomixToolCall& ToolCall)
{
	TSharedPtr<IAutonomixActionExecutor> Executor = FindExecutorForTool(ToolCall.ToolName);
	if (!Executor.IsValid())
	{
		FAutonomixActionResult Result;
		Result.bSuccess = false;
		Result.Errors.Add(FString::Printf(TEXT("No executor registered for tool: %s"), *ToolCall.ToolName));
		return Result;
	}

	// CRITICAL: Inject the tool name into the params so executors can dispatch.
	// The executor may handle multiple tools (e.g. read_config_value + write_config_value)
	// and needs to know which one was called.
	TSharedRef<FJsonObject> ParamsWithToolName = MakeShared<FJsonObject>();
	if (ToolCall.InputParams.IsValid())
	{
		// Copy all fields from original params
		for (const auto& Pair : ToolCall.InputParams->Values)
		{
			ParamsWithToolName->SetField(Pair.Key, Pair.Value);
		}
	}
	ParamsWithToolName->SetStringField(TEXT("_tool_name"), ToolCall.ToolName);

	return Executor->ExecuteAction(ParamsWithToolName);
}

FAutonomixActionPlan FAutonomixActionRouter::PreviewToolCall(const FAutonomixToolCall& ToolCall)
{
	TSharedPtr<IAutonomixActionExecutor> Executor = FindExecutorForTool(ToolCall.ToolName);
	if (!Executor.IsValid())
	{
		FAutonomixActionPlan Plan;
		Plan.Summary = FString::Printf(TEXT("ERROR: No executor for tool '%s'"), *ToolCall.ToolName);
		return Plan;
	}

	TSharedRef<FJsonObject> ParamsWithToolName = MakeShared<FJsonObject>();
	if (ToolCall.InputParams.IsValid())
	{
		for (const auto& Pair : ToolCall.InputParams->Values)
		{
			ParamsWithToolName->SetField(Pair.Key, Pair.Value);
		}
	}
	ParamsWithToolName->SetStringField(TEXT("_tool_name"), ToolCall.ToolName);

	return Executor->PreviewAction(ParamsWithToolName);
}

TArray<FName> FAutonomixActionRouter::GetRegisteredExecutorNames() const
{
	TArray<FName> Names;
	for (const auto& Pair : ExecutorMap) { Names.AddUnique(Pair.Value->GetActionName()); }
	return Names;
}

TSharedPtr<IAutonomixActionExecutor> FAutonomixActionRouter::FindExecutorForTool(const FString& ToolName) const
{
	const TSharedRef<IAutonomixActionExecutor>* Found = ExecutorMap.Find(ToolName);
	return Found ? TSharedPtr<IAutonomixActionExecutor>(*Found) : nullptr;
}
