// Copyright Autonomix. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AutonomixInterfaces.h"

/**
 * FAutonomixPythonActions
 *
 * Provides the execute_python_script tool — the "escape hatch" that gives the AI
 * access to nearly every Unreal Editor API via Python scripting.
 *
 * Uses IPythonScriptPlugin (UE's built-in Python integration) to execute scripts.
 * The AI writes a temporary Python script, the plugin executes it in-process, and
 * captured stdout/stderr is returned as the tool result.
 *
 * SECURITY:
 *   - Requires SecurityMode == Developer (gated in SafetyGate + here)
 *   - Requires bEnablePythonTools == true in settings
 *   - Scripts are validated against a denylist before execution
 *   - Temp scripts are written to Saved/Autonomix/Scripts/ and cleaned up after
 *   - Execution timeout prevents infinite loops (default: 30 seconds)
 *   - All executions are logged to the ExecutionJournal
 *
 * CAPABILITIES (via unreal module):
 *   - Bulk asset renaming, migration, cleanup
 *   - Niagara system creation and parameter setting
 *   - MetaSound graph manipulation
 *   - Custom editor utility operations
 *   - Any operation exposed to Python via UFUNCTION(BlueprintCallable)
 *   - Data validation and batch processing
 *
 * NOTE: The Python Script Plugin must be enabled in the project for this to work.
 *       If not available, the tool returns a clear error message.
 */
class AUTONOMIXACTIONS_API FAutonomixPythonActions : public IAutonomixActionExecutor
{
public:
	FAutonomixPythonActions();
	virtual ~FAutonomixPythonActions();

	// IAutonomixActionExecutor interface
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
	 * Execute a Python script string via IPythonScriptPlugin.
	 *
	 * 1. Writes script to temp file in Saved/Autonomix/Scripts/
	 * 2. Wraps in output-capture + timeout boilerplate
	 * 3. Executes via IPythonScriptPlugin::ExecPythonCommand
	 * 4. Reads captured output from the output file
	 * 5. Cleans up temp files
	 *
	 * @param Params   JSON with "script" (string, required) and "timeout_seconds" (int, optional)
	 * @param Result   Action result to populate
	 * @return         Populated result
	 */
	FAutonomixActionResult ExecutePythonScript(const TSharedRef<FJsonObject>& Params, FAutonomixActionResult& Result);

	/**
	 * Validate a Python script against the security denylist.
	 * Blocks dangerous operations like os.system, subprocess, shutil.rmtree, etc.
	 *
	 * @param Script       The Python source code to validate
	 * @param OutViolations  Any detected violations
	 * @return             true if the script passes validation
	 */
	bool ValidatePythonScript(const FString& Script, TArray<FString>& OutViolations) const;

	/** Check if the Python Script Plugin is loaded and available */
	bool IsPythonPluginAvailable() const;

	/** Get the temp scripts directory (Saved/Autonomix/Scripts/) */
	FString GetScriptsDir() const;

	/** Indent every line by 4 spaces for embedding inside the try: block */
	static FString IndentScript(const FString& Script);

	/** Patterns that are blocked in Python scripts */
	TArray<FString> ScriptDenylist;

	/** Initialize the denylist patterns */
	void InitializeDenylist();
};
