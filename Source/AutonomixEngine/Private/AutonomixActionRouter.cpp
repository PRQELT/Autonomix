// Copyright Autonomix. All Rights Reserved.

#include "AutonomixActionRouter.h"
#include "AutonomixCoreModule.h"
#include "Misc/PackageName.h"

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

// ============================================================================
// Centralized asset_path validation
// ============================================================================
// Many tools accept an "asset_path" parameter (e.g. /Game/UI/WBP_MainMenu).
// If the LLM sends an invalid path (e.g. "/", "", "MainMenu"), UE's native
// CreatePackage() / FPackageName::LongPackageNameToFilename() will trigger a
// FMessageDialog::Open() — a MODAL dialog that freezes the editor with:
//   "Input '/' contains fewer than the minimum number of characters 4 for LongPackageNames"
//
// We intercept this here BEFORE dispatching to any executor, using
// FPackageName::IsValidLongPackageName() which is SAFE (returns bool + reason,
// no dialog). This prevents the modal freeze and returns a clear error to the
// LLM so it can retry with a correct path.
// ============================================================================

static bool ValidateAssetPathParam(const TSharedPtr<FJsonObject>& Params, FString& OutError)
{
	if (!Params.IsValid()) return true; // No params, nothing to validate

	FString AssetPath;
	if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
	{
		return true; // No asset_path param — tool doesn't need one
	}

	// Empty path
	if (AssetPath.IsEmpty())
	{
		OutError = TEXT("asset_path is empty. You must provide a valid Unreal asset path starting with /Game/. Example: /Game/UI/WBP_MainMenu");
		return false;
	}

	// Must start with /Game/
	if (!AssetPath.StartsWith(TEXT("/Game/")))
	{
		OutError = FString::Printf(
			TEXT("asset_path '%s' is invalid — it must start with /Game/. Example: /Game/UI/WBP_MainMenu or /Game/Blueprints/BP_MyActor"),
			*AssetPath);
		return false;
	}

	// Strip the optional ".ObjectName" suffix from full object references.
	// UE5 has two asset reference formats:
	//   1. Package path:        /Game/Blueprints/BP_Character
	//   2. Full object ref:     /Game/Blueprints/BP_Character.BP_Character
	// FPackageName::IsValidLongPackageName() validates PACKAGE names only and
	// rejects the '.' in format #2. The AI model often sends format #2, so we
	// strip the suffix for validation purposes. The original path (with dot) is
	// preserved in the params — LoadObject() accepts both formats.
	FString PackagePath = AssetPath;
	int32 LastSlash = INDEX_NONE;
	PackagePath.FindLastChar(TEXT('/'), LastSlash);
	if (LastSlash != INDEX_NONE)
	{
		int32 DotIndex = PackagePath.Find(TEXT("."), ESearchCase::CaseSensitive, ESearchDir::FromStart, LastSlash);
		if (DotIndex != INDEX_NONE)
		{
			PackagePath = PackagePath.Left(DotIndex);
		}
	}

	// Use UE's built-in validation (safe — no dialog, just returns bool + reason)
	FText Reason;
	if (!FPackageName::IsValidLongPackageName(PackagePath, false, &Reason))
	{
		OutError = FString::Printf(
			TEXT("asset_path '%s' is not a valid Unreal package path: %s. Use a path like /Game/UI/WBP_MainMenu"),
			*AssetPath, *Reason.ToString());
		return false;
	}

	return true;
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

	// CRITICAL: Validate asset_path before dispatching to prevent UE modal dialog freeze.
	// UE's CreatePackage() / FPackageName functions show FMessageDialog::Open() on invalid
	// paths, which freezes the editor. We catch this early and return a clean error.
	{
		FString ValidationError;
		if (!ValidateAssetPathParam(ToolCall.InputParams, ValidationError))
		{
			FAutonomixActionResult Result;
			Result.bSuccess = false;
			Result.Errors.Add(ValidationError);
			UE_LOG(LogAutonomix, Warning, TEXT("ActionRouter: Blocked tool '%s' — invalid asset_path: %s"),
				*ToolCall.ToolName, *ValidationError);
			return Result;
		}
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

	// Validate asset_path for preview too
	{
		FString ValidationError;
		if (!ValidateAssetPathParam(ToolCall.InputParams, ValidationError))
		{
			FAutonomixActionPlan Plan;
			Plan.Summary = FString::Printf(TEXT("ERROR: %s"), *ValidationError);
			return Plan;
		}
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

TArray<FString> FAutonomixActionRouter::GetRegisteredToolNames() const
{
	TArray<FString> Names;
	ExecutorMap.GetKeys(Names);
	return Names;
}

TSharedPtr<IAutonomixActionExecutor> FAutonomixActionRouter::FindExecutorForTool(const FString& ToolName) const
{
	const TSharedRef<IAutonomixActionExecutor>* Found = ExecutorMap.Find(ToolName);
	return Found ? TSharedPtr<IAutonomixActionExecutor>(*Found) : nullptr;
}
