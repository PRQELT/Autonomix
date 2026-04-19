// Copyright Autonomix. All Rights Reserved.

#include "Python/AutonomixPythonActions.h"
#include "AutonomixCoreModule.h"
#include "AutonomixSettings.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "Misc/Guid.h"
#include "HAL/PlatformFileManager.h"

// IPythonScriptPlugin interface — conditional include
#if WITH_PYTHON
#include "IPythonScriptPlugin.h"
#endif

#define LOCTEXT_NAMESPACE "AutonomixPythonActions"

// ============================================================================
// Lifecycle
// ============================================================================

FAutonomixPythonActions::FAutonomixPythonActions()
{
	InitializeDenylist();
}

FAutonomixPythonActions::~FAutonomixPythonActions() {}

// ============================================================================
// IAutonomixActionExecutor Interface
// ============================================================================

FName FAutonomixPythonActions::GetActionName() const { return FName(TEXT("Python")); }
FText FAutonomixPythonActions::GetDisplayName() const { return LOCTEXT("DisplayName", "Python Scripting"); }
EAutonomixActionCategory FAutonomixPythonActions::GetCategory() const { return EAutonomixActionCategory::General; }
EAutonomixRiskLevel FAutonomixPythonActions::GetDefaultRiskLevel() const { return EAutonomixRiskLevel::High; }
bool FAutonomixPythonActions::CanUndo() const { return false; }
bool FAutonomixPythonActions::UndoAction() { return false; }

TArray<FString> FAutonomixPythonActions::GetSupportedToolNames() const
{
	return { TEXT("execute_python_script") };
}

bool FAutonomixPythonActions::ValidateParams(const TSharedRef<FJsonObject>& Params, TArray<FString>& OutErrors) const
{
	if (!Params->HasField(TEXT("script")))
	{
		OutErrors.Add(TEXT("Missing required field: 'script'"));
		return false;
	}
	return true;
}

FAutonomixActionPlan FAutonomixPythonActions::PreviewAction(const TSharedRef<FJsonObject>& Params)
{
	FAutonomixActionPlan Plan;

	FString Script;
	Params->TryGetStringField(TEXT("script"), Script);

	int32 LineCount = 1;
	for (const TCHAR& Ch : Script) { if (Ch == TEXT('\n')) ++LineCount; }

	Plan.Summary = FString::Printf(TEXT("Execute Python script (%d lines)"), LineCount);
	Plan.MaxRiskLevel = EAutonomixRiskLevel::High;

	FAutonomixAction Action;
	Action.Description = Plan.Summary;
	Action.Category = EAutonomixActionCategory::General;
	Action.RiskLevel = EAutonomixRiskLevel::High;
	Plan.Actions.Add(Action);

	return Plan;
}

FAutonomixActionResult FAutonomixPythonActions::ExecuteAction(const TSharedRef<FJsonObject>& Params)
{
	FAutonomixActionResult Result;
	Result.bSuccess = false;

	// Security gate: require Developer mode
	const UAutonomixDeveloperSettings* Settings = UAutonomixDeveloperSettings::Get();
	if (!Settings || Settings->SecurityMode != EAutonomixSecurityMode::Developer)
	{
		Result.Errors.Add(TEXT("Python script execution requires Developer security mode. "
			"Change this in Project Settings > Plugins > Autonomix > Safety > Security Mode."));
		return Result;
	}

	// Route (only one tool for now)
	FString Action;
	if (!Params->TryGetStringField(TEXT("action"), Action) || Action.IsEmpty())
	{
		Params->TryGetStringField(TEXT("tool_name"), Action);
	}

	return ExecutePythonScript(Params, Result);
}

// ============================================================================
// execute_python_script
// ============================================================================

FAutonomixActionResult FAutonomixPythonActions::ExecutePythonScript(
	const TSharedRef<FJsonObject>& Params,
	FAutonomixActionResult& Result)
{
	// 1. Check Python plugin availability
	if (!IsPythonPluginAvailable())
	{
		Result.Errors.Add(
			TEXT("Python Script Plugin is not available. Enable it in Edit > Plugins > Scripting > Python Editor Script Plugin, then restart the editor."));
		return Result;
	}

	// 2. Get the script content
	FString Script;
	if (!Params->TryGetStringField(TEXT("script"), Script) || Script.IsEmpty())
	{
		Result.Errors.Add(TEXT("Missing or empty 'script' field."));
		return Result;
	}

	// 3. Validate against denylist
	TArray<FString> Violations;
	if (!ValidatePythonScript(Script, Violations))
	{
		Result.Errors.Add(TEXT("Python script failed security validation:"));
		for (const FString& V : Violations)
		{
			Result.Errors.Add(FString::Printf(TEXT("  - %s"), *V));
		}
		return Result;
	}

	// 4. Get timeout
	int32 TimeoutSeconds = 30;
	Params->TryGetNumberField(TEXT("timeout_seconds"), TimeoutSeconds);
	TimeoutSeconds = FMath::Clamp(TimeoutSeconds, 5, 120);

	// 5. Prepare temp file paths
	FString ScriptsDir = GetScriptsDir();
	IFileManager::Get().MakeDirectory(*ScriptsDir, true);

	FString ScriptId = FGuid::NewGuid().ToString(EGuidFormats::Short);
	FString ScriptPath = FPaths::Combine(ScriptsDir, FString::Printf(TEXT("autonomix_script_%s.py"), *ScriptId));
	FString OutputPath = FPaths::Combine(ScriptsDir, FString::Printf(TEXT("autonomix_output_%s.txt"), *ScriptId));

	// 6. Wrap the user script with output capture boilerplate
	// This redirects stdout/stderr to a file so we can capture the output
	FString WrappedScript = FString::Printf(TEXT(
		"import sys\n"
		"import io\n"
		"import traceback\n"
		"\n"
		"_autonomix_output_path = r'%s'\n"
		"_autonomix_stdout = io.StringIO()\n"
		"_autonomix_stderr = io.StringIO()\n"
		"_autonomix_old_stdout = sys.stdout\n"
		"_autonomix_old_stderr = sys.stderr\n"
		"sys.stdout = _autonomix_stdout\n"
		"sys.stderr = _autonomix_stderr\n"
		"\n"
		"try:\n"
		"    # === USER SCRIPT BEGIN ===\n"
		"%s\n"
		"    # === USER SCRIPT END ===\n"
		"except Exception as _autonomix_e:\n"
		"    print(f'ERROR: {type(_autonomix_e).__name__}: {_autonomix_e}', file=sys.stderr)\n"
		"    traceback.print_exc(file=sys.stderr)\n"
		"finally:\n"
		"    sys.stdout = _autonomix_old_stdout\n"
		"    sys.stderr = _autonomix_old_stderr\n"
		"    _out = _autonomix_stdout.getvalue()\n"
		"    _err = _autonomix_stderr.getvalue()\n"
		"    _combined = ''\n"
		"    if _out:\n"
		"        _combined += _out\n"
		"    if _err:\n"
		"        _combined += '\\n=== STDERR ===\\n' + _err\n"
		"    if not _combined:\n"
		"        _combined = '(script produced no output)'\n"
		"    with open(_autonomix_output_path, 'w', encoding='utf-8') as _f:\n"
		"        _f.write(_combined)\n"
	), *OutputPath.Replace(TEXT("\\"), TEXT("\\\\")), *IndentScript(Script));

	// 7. Write the wrapped script to disk
	if (!FFileHelper::SaveStringToFile(WrappedScript, *ScriptPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
	{
		Result.Errors.Add(FString::Printf(TEXT("Failed to write temp script to: %s"), *ScriptPath));
		return Result;
	}

	// 8. Execute via IPythonScriptPlugin
	UE_LOG(LogAutonomix, Log, TEXT("PythonActions: Executing script %s (timeout: %ds)"), *ScriptPath, TimeoutSeconds);

	double StartTime = FPlatformTime::Seconds();

#if WITH_PYTHON
	IPythonScriptPlugin* PythonPlugin = IPythonScriptPlugin::Get();
	if (PythonPlugin)
	{
		// ExecPythonCommand runs synchronously on the game thread
		FString ExecCommand = FString::Printf(TEXT("exec(open(r'%s', encoding='utf-8').read())"), *ScriptPath);
		PythonPlugin->ExecPythonCommand(*ExecCommand);
	}
	else
	{
		Result.Errors.Add(TEXT("IPythonScriptPlugin::Get() returned null despite the module being loaded."));
		// Cleanup
		IFileManager::Get().Delete(*ScriptPath);
		return Result;
	}
#else
	Result.Errors.Add(TEXT("This build of Autonomix was compiled without Python support (WITH_PYTHON=0)."));
	IFileManager::Get().Delete(*ScriptPath);
	return Result;
#endif

	double ElapsedSeconds = FPlatformTime::Seconds() - StartTime;

	// 9. Read captured output
	FString CapturedOutput;
	if (FFileHelper::LoadFileToString(CapturedOutput, *OutputPath))
	{
		// Truncate very long output to prevent context window bloat
		const int32 MaxOutputChars = 50000;
		if (CapturedOutput.Len() > MaxOutputChars)
		{
			CapturedOutput = CapturedOutput.Left(MaxOutputChars)
				+ FString::Printf(TEXT("\n\n... [OUTPUT TRUNCATED — showing first %d of %d characters]"),
					MaxOutputChars, CapturedOutput.Len());
		}
	}
	else
	{
		CapturedOutput = TEXT("(no output captured — the script may have crashed before the output redirect initialized)");
	}

	// 10. Check for errors in output
	bool bHasErrors = CapturedOutput.Contains(TEXT("=== STDERR ==="))
		|| CapturedOutput.Contains(TEXT("ERROR:"))
		|| CapturedOutput.Contains(TEXT("Traceback"));

	// 11. Build result
	Result.bSuccess = !bHasErrors;
	Result.ExecutionTimeSeconds = static_cast<float>(ElapsedSeconds);
	Result.ResultMessage = FString::Printf(
		TEXT("=== Python Script Result (%.1fs) ===\n%s"),
		ElapsedSeconds, *CapturedOutput);

	if (bHasErrors)
	{
		Result.Warnings.Add(TEXT("Script produced error output. Check the result for details."));
	}

	// 12. Cleanup temp files
	IFileManager::Get().Delete(*ScriptPath);
	IFileManager::Get().Delete(*OutputPath);

	UE_LOG(LogAutonomix, Log, TEXT("PythonActions: Script completed in %.1fs (success: %s)"),
		ElapsedSeconds, Result.bSuccess ? TEXT("true") : TEXT("false"));

	return Result;
}

// ============================================================================
// Helpers
// ============================================================================

FString FAutonomixPythonActions::IndentScript(const FString& Script)
{
	// Indent each line by 4 spaces (to fit inside the try: block)
	TArray<FString> Lines;
	Script.ParseIntoArrayLines(Lines);

	FString Indented;
	for (const FString& Line : Lines)
	{
		Indented += TEXT("    ") + Line + TEXT("\n");
	}
	return Indented;
}

bool FAutonomixPythonActions::IsPythonPluginAvailable() const
{
#if WITH_PYTHON
	return IPythonScriptPlugin::Get() != nullptr;
#else
	return false;
#endif
}

FString FAutonomixPythonActions::GetScriptsDir() const
{
	return FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Autonomix"), TEXT("Scripts"));
}

void FAutonomixPythonActions::InitializeDenylist()
{
	// Block dangerous Python operations
	ScriptDenylist = {
		// System execution
		TEXT("os.system"),
		TEXT("os.popen"),
		TEXT("os.exec"),
		TEXT("subprocess"),
		TEXT("Popen"),
		// File destruction (allow os.path, os.listdir, etc.)
		TEXT("shutil.rmtree"),
		TEXT("os.rmdir"),
		TEXT("os.remove"),
		TEXT("os.unlink"),
		TEXT("os.rename"),
		// Network
		TEXT("urllib"),
		TEXT("requests"),
		TEXT("http.client"),
		TEXT("socket.connect"),
		TEXT("ftplib"),
		TEXT("smtplib"),
		// Code loading
		TEXT("__import__"),
		TEXT("importlib"),
		TEXT("compile("),
		TEXT("eval("),
		TEXT("exec("),       // We allow our wrapper exec, but block user scripts from nesting exec
		// Environment
		TEXT("os.environ"),
		TEXT("getenv"),
		TEXT("putenv"),
	};
}

bool FAutonomixPythonActions::ValidatePythonScript(const FString& Script, TArray<FString>& OutViolations) const
{
	bool bValid = true;

	for (const FString& Pattern : ScriptDenylist)
	{
		if (Script.Contains(Pattern, ESearchCase::CaseSensitive))
		{
			OutViolations.Add(FString::Printf(TEXT("Blocked pattern detected: '%s'"), *Pattern));
			bValid = false;
		}
	}

	// Block absolute path writes outside project
	// Allow writes to project dir but catch attempts to write to system dirs
	if (Script.Contains(TEXT("C:\\Windows"), ESearchCase::IgnoreCase)
		|| Script.Contains(TEXT("/usr/"), ESearchCase::CaseSensitive)
		|| Script.Contains(TEXT("/etc/"), ESearchCase::CaseSensitive)
		|| Script.Contains(TEXT("C:\\Program"), ESearchCase::IgnoreCase))
	{
		OutViolations.Add(TEXT("Script attempts to access system directories."));
		bValid = false;
	}

	return bValid;
}

#undef LOCTEXT_NAMESPACE
