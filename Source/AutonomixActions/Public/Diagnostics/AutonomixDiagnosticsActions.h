// Copyright Autonomix. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AutonomixInterfaces.h"

/**
 * FAutonomixDiagnosticsActions
 *
 * Runtime diagnostics tools:
 *   - read_message_log: Capture the Output Log (errors, warnings, asserts)
 *
 * These are read-only observation tools that let the AI see runtime issues
 * without needing to launch PIE. The Output Log captures Blueprint errors,
 * Accessed None warnings, asset loading failures, and C++ asserts.
 */
class AUTONOMIXACTIONS_API FAutonomixDiagnosticsActions : public IAutonomixActionExecutor
{
public:
	FAutonomixDiagnosticsActions();
	virtual ~FAutonomixDiagnosticsActions();

	virtual FName GetActionName() const override;
	virtual FText GetDisplayName() const override;
	virtual EAutonomixActionCategory GetCategory() const override;
	virtual EAutonomixRiskLevel GetDefaultRiskLevel() const override;
	virtual FAutonomixActionPlan PreviewAction(const TSharedRef<FJsonObject>& Params) override;
	virtual FAutonomixActionResult ExecuteAction(const TSharedRef<FJsonObject>& Params) override;
	virtual bool CanUndo() const override;
	virtual bool UndoAction() override;
	virtual TArray<FString> GetSupportedToolNames() const override;
	virtual bool ValidateParams(const TSharedRef<FJsonObject>& Params, TArray<FString>& OutErrors) const override;

private:
	/**
	 * Read recent entries from the Output Log.
	 *
	 * Captures the last N messages from GLog, optionally filtered by
	 * category (LogBlueprintUserMessages, LogScript, etc.) or severity
	 * (Error, Warning, Display).
	 */
	FAutonomixActionResult ExecuteReadMessageLog(const TSharedRef<FJsonObject>& Params, FAutonomixActionResult& Result);
};
