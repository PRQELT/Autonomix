// Copyright Autonomix. All Rights Reserved.

#include "Cpp/AutonomixCppActions.h"
#include "AutonomixCoreModule.h"
#include "AutonomixSettings.h"

#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformFileManager.h"
#include "DesktopPlatformModule.h"
#include "Interfaces/IMainFrameModule.h"

#if WITH_LIVE_CODING
#include "ILiveCodingModule.h"
#endif

FAutonomixCppActions::FAutonomixCppActions() {}
FAutonomixCppActions::~FAutonomixCppActions() {}

FName FAutonomixCppActions::GetActionName() const { return FName(TEXT("Cpp")); }
FText FAutonomixCppActions::GetDisplayName() const { return FText::FromString(TEXT("C++ Generation Actions")); }
EAutonomixActionCategory FAutonomixCppActions::GetCategory() const { return EAutonomixActionCategory::Cpp; }
EAutonomixRiskLevel FAutonomixCppActions::GetDefaultRiskLevel() const { return EAutonomixRiskLevel::High; }
bool FAutonomixCppActions::CanUndo() const { return false; }
bool FAutonomixCppActions::UndoAction() { return false; }

TArray<FString> FAutonomixCppActions::GetSupportedToolNames() const
{
	return {
		TEXT("create_cpp_class"),
		TEXT("modify_cpp_file"),
		TEXT("trigger_compile"),
		TEXT("regenerate_project_files")
	};
}

bool FAutonomixCppActions::ValidateParams(const TSharedRef<FJsonObject>& Params, TArray<FString>& OutErrors) const
{
	// For code gen, validate that the code doesn't contain dangerous patterns
	FString HeaderCode, CppCode;
	if (Params->TryGetStringField(TEXT("header_code"), HeaderCode))
	{
		TArray<FString> Violations;
		if (!ValidateCodeSafety(HeaderCode, Violations))
		{
			for (const FString& V : Violations) OutErrors.Add(FString::Printf(TEXT("Header: %s"), *V));
			return false;
		}
	}
	if (Params->TryGetStringField(TEXT("cpp_code"), CppCode))
	{
		TArray<FString> Violations;
		if (!ValidateCodeSafety(CppCode, Violations))
		{
			for (const FString& V : Violations) OutErrors.Add(FString::Printf(TEXT("Cpp: %s"), *V));
			return false;
		}
	}
	return true;
}

FAutonomixActionPlan FAutonomixCppActions::PreviewAction(const TSharedRef<FJsonObject>& Params)
{
	FAutonomixActionPlan Plan;
	Plan.Summary = TEXT("C++ code generation (HIGH RISK — requires compilation)");
	Plan.MaxRiskLevel = EAutonomixRiskLevel::High;

	FAutonomixAction Action;
	Action.Description = Plan.Summary;
	Action.Category = EAutonomixActionCategory::Cpp;
	Action.RiskLevel = EAutonomixRiskLevel::High;

	FString ClassName;
	if (Params->TryGetStringField(TEXT("class_name"), ClassName))
	{
		FString HeaderPath = FPaths::Combine(FPaths::GameSourceDir(), FApp::GetProjectName(), TEXT("Public"), ClassName + TEXT(".h"));
		FString CppPath = FPaths::Combine(FPaths::GameSourceDir(), FApp::GetProjectName(), TEXT("Private"), ClassName + TEXT(".cpp"));
		Action.AffectedPaths.Add(HeaderPath);
		Action.AffectedPaths.Add(CppPath);
	}

	Plan.Actions.Add(Action);
	return Plan;
}

FAutonomixActionResult FAutonomixCppActions::ExecuteAction(const TSharedRef<FJsonObject>& Params)
{
	FAutonomixActionResult Result;
	Result.bSuccess = false;

	if (Params->HasField(TEXT("class_name")) && Params->HasField(TEXT("header_code")))
	{
		return ExecuteCreateCppClass(Params, Result);
	}
	else if (Params->HasField(TEXT("file_path")) && Params->HasField(TEXT("content")))
	{
		return ExecuteModifyCppFile(Params, Result);
	}
	else if (Params->HasField(TEXT("compile")) && Params->GetBoolField(TEXT("compile")))
	{
		return ExecuteTriggerCompile(Result);
	}
	else if (Params->HasField(TEXT("regenerate_project")))
	{
		return ExecuteRegenerateProjectFiles(Result);
	}

	Result.Errors.Add(TEXT("Could not determine C++ action from parameters."));
	return Result;
}

FAutonomixActionResult FAutonomixCppActions::ExecuteCreateCppClass(const TSharedRef<FJsonObject>& Params, FAutonomixActionResult& Result)
{
	FString ClassName = Params->GetStringField(TEXT("class_name"));
	FString HeaderCode = Params->GetStringField(TEXT("header_code"));
	FString CppCode;
	Params->TryGetStringField(TEXT("cpp_code"), CppCode);

	// Validate code safety
	TArray<FString> Violations;
	if (!ValidateCodeSafety(HeaderCode, Violations) || (!CppCode.IsEmpty() && !ValidateCodeSafety(CppCode, Violations)))
	{
		Result.Errors.Add(TEXT("Generated code failed safety validation:"));
		Result.Errors.Append(Violations);
		return Result;
	}

	// CRITICAL: Detect structural header changes that Live Coding CANNOT handle.
	// Live Coding is great for .cpp implementation changes, but CANNOT handle:
	// - New UCLASS / USTRUCT / UENUM macros in headers
	// - New UPROPERTY / UFUNCTION declarations
	// - Changes to the UObject reflection system
	// Attempting to Live Code these will crash or fail silently.
	bool bHasStructuralHeaderChanges = false;
	if (HeaderCode.Contains(TEXT("UCLASS(")) ||
		HeaderCode.Contains(TEXT("USTRUCT(")) ||
		HeaderCode.Contains(TEXT("UENUM(")) ||
		HeaderCode.Contains(TEXT("GENERATED_BODY()")) ||
		HeaderCode.Contains(TEXT("GENERATED_UCLASS_BODY()")) ||
		HeaderCode.Contains(TEXT("GENERATED_USTRUCT_BODY()")))
	{
		bHasStructuralHeaderChanges = true;
		Result.Warnings.Add(TEXT("⚠ STRUCTURAL HEADER CHANGE DETECTED: This file contains UCLASS/USTRUCT/UENUM macros. "
			"Live Coding CANNOT compile these changes. You MUST close and restart the Unreal Editor, "
			"then compile from your IDE (Visual Studio / Rider) for these changes to take effect."));
	}

	// Determine target paths
	FString ModuleName = FApp::GetProjectName();
	FString PublicDir = FPaths::Combine(FPaths::GameSourceDir(), ModuleName, TEXT("Public"));
	FString PrivateDir = FPaths::Combine(FPaths::GameSourceDir(), ModuleName, TEXT("Private"));

	// Allow custom subdirectory
	FString SubDir;
	if (Params->TryGetStringField(TEXT("subdirectory"), SubDir))
	{
		PublicDir = FPaths::Combine(PublicDir, SubDir);
		PrivateDir = FPaths::Combine(PrivateDir, SubDir);
	}

	FString HeaderPath = FPaths::Combine(PublicDir, ClassName + TEXT(".h"));
	FString CppPath = FPaths::Combine(PrivateDir, ClassName + TEXT(".cpp"));

	// Ensure directories exist
	IFileManager::Get().MakeDirectory(*PublicDir, true);
	IFileManager::Get().MakeDirectory(*PrivateDir, true);

	// Write header
	if (!WriteFileWithBackup(HeaderPath, HeaderCode))
	{
		Result.Errors.Add(FString::Printf(TEXT("Failed to write header: %s"), *HeaderPath));
		return Result;
	}
	Result.ModifiedPaths.Add(HeaderPath);

	// Write cpp (if provided)
	if (!CppCode.IsEmpty())
	{
		if (!WriteFileWithBackup(CppPath, CppCode))
		{
			Result.Errors.Add(FString::Printf(TEXT("Failed to write cpp: %s"), *CppPath));
			return Result;
		}
		Result.ModifiedPaths.Add(CppPath);
	}

	UE_LOG(LogAutonomix, Log, TEXT("CppActions: Created class %s at %s"), *ClassName, *HeaderPath);

	// Auto-trigger compilation if requested — but BLOCK if structural header changes detected
	bool bAutoCompile = false;
	Params->TryGetBoolField(TEXT("auto_compile"), bAutoCompile);
	if (bAutoCompile)
	{
		if (bHasStructuralHeaderChanges)
		{
			Result.Warnings.Add(TEXT("Auto-compile SKIPPED: Structural header changes (UCLASS/USTRUCT/UENUM) detected. "
				"Live Coding cannot handle these. Restart the editor and compile from your IDE."));
		}
		else
		{
			FAutonomixActionResult CompileResult;
			ExecuteTriggerCompile(CompileResult);
			if (!CompileResult.bSuccess)
			{
				Result.Warnings.Add(TEXT("Code written but compilation failed. Check build output."));
				Result.Warnings.Append(CompileResult.Errors);
			}
		}
	}

	Result.bSuccess = true;
	Result.ResultMessage = FString::Printf(TEXT("Created C++ class '%s' (%s, %s)"),
		*ClassName, *HeaderPath, CppCode.IsEmpty() ? TEXT("header only") : *CppPath);
	return Result;
}

FAutonomixActionResult FAutonomixCppActions::ExecuteModifyCppFile(const TSharedRef<FJsonObject>& Params, FAutonomixActionResult& Result)
{
	FString FilePath = Params->GetStringField(TEXT("file_path"));
	FString Content = Params->GetStringField(TEXT("content"));

	// Validate the path is within project source
	if (!FilePath.StartsWith(FPaths::GameSourceDir()) && !FilePath.StartsWith(FPaths::ProjectDir()))
	{
		Result.Errors.Add(FString::Printf(TEXT("Path not allowed: %s (must be within project source)"), *FilePath));
		return Result;
	}

	// Validate code safety
	TArray<FString> Violations;
	if (!ValidateCodeSafety(Content, Violations))
	{
		Result.Errors.Add(TEXT("Code failed safety validation:"));
		Result.Errors.Append(Violations);
		return Result;
	}

	if (!WriteFileWithBackup(FilePath, Content))
	{
		Result.Errors.Add(FString::Printf(TEXT("Failed to write file: %s"), *FilePath));
		return Result;
	}

	Result.bSuccess = true;
	Result.ResultMessage = FString::Printf(TEXT("Modified file: %s"), *FilePath);
	Result.ModifiedPaths.Add(FilePath);
	return Result;
}

FAutonomixActionResult FAutonomixCppActions::ExecuteTriggerCompile(FAutonomixActionResult& Result)
{
#if WITH_LIVE_CODING
	ILiveCodingModule* LiveCoding = FModuleManager::GetModulePtr<ILiveCodingModule>(LIVE_CODING_MODULE_NAME);
	if (LiveCoding && LiveCoding->IsEnabledForSession())
	{
		UE_LOG(LogAutonomix, Log, TEXT("CppActions: Triggering Live Coding compilation..."));
		LiveCoding->Compile();
		Result.bSuccess = true;
		Result.ResultMessage = TEXT("Live Coding compilation triggered. Check editor status bar for progress.");
		return Result;
	}
#endif

	Result.bSuccess = false;
	Result.Warnings.Add(TEXT("Live Coding not available. Please compile manually from your IDE or enable Live Coding in Editor Preferences."));
	Result.ResultMessage = TEXT("Live Coding not available — manual compilation required.");
	return Result;
}

FAutonomixActionResult FAutonomixCppActions::ExecuteRegenerateProjectFiles(FAutonomixActionResult& Result)
{
	FString ProjectPath = FPaths::GetProjectFilePath();

	IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();
	if (DesktopPlatform)
	{
		bool bSuccess = DesktopPlatform->GenerateProjectFiles(
			FPaths::RootDir(),
			ProjectPath,
			GWarn
		);

		if (bSuccess)
		{
			Result.bSuccess = true;
			Result.ResultMessage = TEXT("Project files regenerated successfully.");
		}
		else
		{
			Result.Errors.Add(TEXT("Failed to regenerate project files."));
		}
	}
	else
	{
		Result.Errors.Add(TEXT("Desktop platform module not available."));
	}

	return Result;
}

bool FAutonomixCppActions::ValidateCodeSafety(const FString& Code, TArray<FString>& OutViolations) const
{
	static const TArray<FString> DenyPatterns = {
		TEXT("system("),
		TEXT("exec("),
		TEXT("popen("),
		TEXT("ShellExecute"),
		TEXT("CreateProcess"),
		TEXT("WinExec"),
		TEXT("DeleteFileA"),
		TEXT("DeleteFileW"),
		TEXT("RemoveDirectoryA"),
		TEXT("RemoveDirectoryW"),
		TEXT("FPlatformProcess::CreateProc"),  // Only allowed via our own BuildActions
		TEXT("#include <windows.h>"),
		TEXT("#include <stdlib.h>"),
		TEXT("__asm"),
		TEXT("asm("),
		TEXT("__declspec(dllexport)"),
	};

	bool bSafe = true;
	for (const FString& Pattern : DenyPatterns)
	{
		if (Code.Contains(Pattern))
		{
			OutViolations.Add(FString::Printf(TEXT("Denied pattern: '%s'"), *Pattern));
			bSafe = false;
		}
	}

	return bSafe;
}

bool FAutonomixCppActions::WriteFileWithBackup(const FString& FilePath, const FString& Content)
{
	// Backup existing file if it exists
	if (FPaths::FileExists(FilePath))
	{
		FString BackupDir = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Autonomix"), TEXT("Backups"),
			FDateTime::UtcNow().ToString(TEXT("%Y%m%d_%H%M%S")));
		IFileManager::Get().MakeDirectory(*BackupDir, true);

		FString BackupPath = FPaths::Combine(BackupDir, FPaths::GetCleanFilename(FilePath));
		IFileManager::Get().Copy(*BackupPath, *FilePath);
		UE_LOG(LogAutonomix, Log, TEXT("CppActions: Backed up %s -> %s"), *FilePath, *BackupPath);
	}

	return FFileHelper::SaveStringToFile(Content, *FilePath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
}
