// Copyright Autonomix. All Rights Reserved.

#include "Validation/AutonomixValidationActions.h"
#include "AutonomixCoreModule.h"
#include "AutonomixSettings.h"
#include "Editor.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "EditorValidatorSubsystem.h"
#include "Misc/AutomationTest.h"

#define LOCTEXT_NAMESPACE "AutonomixValidationActions"

// ============================================================================
// Lifecycle
// ============================================================================

FAutonomixValidationActions::FAutonomixValidationActions() {}
FAutonomixValidationActions::~FAutonomixValidationActions() {}

// ============================================================================
// IAutonomixActionExecutor Interface
// ============================================================================

FName FAutonomixValidationActions::GetActionName() const { return FName(TEXT("Validation")); }
FText FAutonomixValidationActions::GetDisplayName() const { return LOCTEXT("DisplayName", "Validation & Testing"); }
EAutonomixActionCategory FAutonomixValidationActions::GetCategory() const { return EAutonomixActionCategory::General; }
EAutonomixRiskLevel FAutonomixValidationActions::GetDefaultRiskLevel() const { return EAutonomixRiskLevel::Low; }
bool FAutonomixValidationActions::CanUndo() const { return false; }
bool FAutonomixValidationActions::UndoAction() { return false; }

TArray<FString> FAutonomixValidationActions::GetSupportedToolNames() const
{
	return {
		TEXT("validate_assets"),
		TEXT("run_automation_tests")
	};
}

bool FAutonomixValidationActions::ValidateParams(const TSharedRef<FJsonObject>& Params, TArray<FString>& OutErrors) const
{
	return true;
}

FAutonomixActionPlan FAutonomixValidationActions::PreviewAction(const TSharedRef<FJsonObject>& Params)
{
	FAutonomixActionPlan Plan;
	FString Action;
	Params->TryGetStringField(TEXT("action"), Action);
	if (Action.IsEmpty()) Params->TryGetStringField(TEXT("tool_name"), Action);

	if (Action == TEXT("validate_assets"))
		Plan.Summary = TEXT("Run UE Data Validation on assets (read-only)");
	else if (Action == TEXT("run_automation_tests"))
		Plan.Summary = TEXT("Run Unreal Automation Tests");
	else
		Plan.Summary = TEXT("Validation operation");

	Plan.MaxRiskLevel = EAutonomixRiskLevel::Low;
	FAutonomixAction A;
	A.Description = Plan.Summary;
	A.Category = EAutonomixActionCategory::General;
	A.RiskLevel = EAutonomixRiskLevel::Low;
	Plan.Actions.Add(A);
	return Plan;
}

FAutonomixActionResult FAutonomixValidationActions::ExecuteAction(const TSharedRef<FJsonObject>& Params)
{
	FAutonomixActionResult Result;
	Result.bSuccess = false;

	FString Action;
	if (!Params->TryGetStringField(TEXT("action"), Action) || Action.IsEmpty())
		Params->TryGetStringField(TEXT("tool_name"), Action);

	if (Action == TEXT("validate_assets"))
		return ExecuteValidateAssets(Params, Result);
	else if (Action == TEXT("run_automation_tests"))
		return ExecuteRunAutomationTests(Params, Result);

	// Infer
	if (Params->HasField(TEXT("asset_paths")) || Params->HasField(TEXT("validate_all")))
		return ExecuteValidateAssets(Params, Result);
	if (Params->HasField(TEXT("test_filter")))
		return ExecuteRunAutomationTests(Params, Result);

	Result.Errors.Add(TEXT("Could not determine validation action. Use validate_assets or run_automation_tests."));
	return Result;
}

// ============================================================================
// validate_assets
// ============================================================================

FAutonomixActionResult FAutonomixValidationActions::ExecuteValidateAssets(
	const TSharedRef<FJsonObject>& Params, FAutonomixActionResult& Result)
{
	// Get the EditorValidatorSubsystem
	if (!GEditor)
	{
		Result.Errors.Add(TEXT("GEditor not available."));
		return Result;
	}

	UEditorValidatorSubsystem* ValidatorSubsystem = GEditor->GetEditorSubsystem<UEditorValidatorSubsystem>();
	if (!ValidatorSubsystem)
	{
		Result.Errors.Add(TEXT("UEditorValidatorSubsystem is not available. It may not be enabled in this engine version."));
		return Result;
	}

	// Collect asset paths to validate
	TArray<FAssetData> AssetsToValidate;
	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();

	bool bValidateAll = false;
	Params->TryGetBoolField(TEXT("validate_all"), bValidateAll);

	const TArray<TSharedPtr<FJsonValue>>* AssetPathsArray;
	if (Params->TryGetArrayField(TEXT("asset_paths"), AssetPathsArray))
	{
		for (const TSharedPtr<FJsonValue>& Val : *AssetPathsArray)
		{
			FString Path;
			if (Val->TryGetString(Path))
			{
				FAssetData AssetData = AssetRegistry.GetAssetByObjectPath(FSoftObjectPath(Path));
				if (AssetData.IsValid())
				{
					AssetsToValidate.Add(AssetData);
				}
				else
				{
					Result.Warnings.Add(FString::Printf(TEXT("Asset not found in registry: '%s'"), *Path));
				}
			}
		}
	}
	else if (bValidateAll)
	{
		// Get all project assets (limit to /Game/ to avoid engine content)
		TArray<FAssetData> AllAssets;
		AssetRegistry.GetAllAssets(AllAssets, true);
		for (const FAssetData& Asset : AllAssets)
		{
			if (Asset.GetObjectPathString().StartsWith(TEXT("/Game/")))
			{
				AssetsToValidate.Add(Asset);
			}
		}
	}
	else
	{
		Result.Errors.Add(TEXT("Specify 'asset_paths' (array of content paths) or 'validate_all': true."));
		return Result;
	}

	if (AssetsToValidate.Num() == 0)
	{
		Result.bSuccess = true;
		Result.ResultMessage = TEXT("No assets to validate (all specified paths were empty or not found).");
		return Result;
	}

	// Run validation
	UE_LOG(LogAutonomix, Log, TEXT("ValidationActions: Validating %d assets..."), AssetsToValidate.Num());

	FValidateAssetsSettings ValidationSettings;
	ValidationSettings.bSkipExcludedDirectories = true;
	ValidationSettings.bShowIfNoFailures = false;

	FValidateAssetsResults ValidationResults;
	int32 NumValidated = ValidatorSubsystem->ValidateAssetsWithSettings(AssetsToValidate, ValidationSettings, ValidationResults);

	// Build result report
	FString Report = FString::Printf(TEXT("=== Asset Validation Results ===\nAssets checked: %d\n"), NumValidated);

	int32 NumValid = ValidationResults.NumChecked - ValidationResults.NumUnableToValidate - ValidationResults.NumRequested;
	int32 NumInvalid = ValidationResults.NumChecked - NumValid - ValidationResults.NumUnableToValidate;

	Report += FString::Printf(TEXT("  Checked: %d\n"), ValidationResults.NumChecked);
	Report += FString::Printf(TEXT("  Requested: %d\n"), ValidationResults.NumRequested);
	Report += FString::Printf(TEXT("  Unable to validate: %d\n"), ValidationResults.NumUnableToValidate);

	Report += TEXT("\nCheck the Message Log (Window > Developer Tools > Message Log) for detailed validation results.\n");
	Report += TEXT("Use read_message_log with category_filter='LogContentValidation' to see details.\n");

	// In UE 5.4, detailed error/warning counts aren't exposed on the results struct.
	// Use the Message Log to get specifics.
	Result.bSuccess = true;
	Result.ResultMessage = Report;

	return Result;
}

// ============================================================================
// run_automation_tests
// ============================================================================

FAutonomixActionResult FAutonomixValidationActions::ExecuteRunAutomationTests(
	const TSharedRef<FJsonObject>& Params, FAutonomixActionResult& Result)
{
	FString TestFilter;
	if (!Params->TryGetStringField(TEXT("test_filter"), TestFilter))
	{
		TestFilter = TEXT("Project."); // Default: run project tests only
	}

	FString TestFlagsStr;
	Params->TryGetStringField(TEXT("test_flags"), TestFlagsStr);

	// Get the automation framework
	FAutomationTestFramework& Framework = FAutomationTestFramework::Get();

	// Find matching tests
	TArray<FAutomationTestInfo> TestInfos;
	Framework.GetValidTestNames(TestInfos);

	TArray<FString> MatchingTests;
	for (const FAutomationTestInfo& TestInfo : TestInfos)
	{
		FString TestName = TestInfo.GetDisplayName();
		if (TestName.Contains(TestFilter, ESearchCase::IgnoreCase) ||
			TestInfo.GetTestName().Contains(TestFilter, ESearchCase::IgnoreCase))
		{
			MatchingTests.Add(TestInfo.GetTestName());
		}
	}

	if (MatchingTests.Num() == 0)
	{
		Result.bSuccess = true;
		Result.ResultMessage = FString::Printf(
			TEXT("No automation tests matching filter '%s'. Available test count: %d. "
				 "Try broader filters like 'Project.', 'Engine.', or 'Functional.' "
				 "To list all tests, use test_filter='*'."),
			*TestFilter, TestInfos.Num());
		return Result;
	}

	// List matching tests (cap display at 50)
	FString Report = FString::Printf(TEXT("=== Automation Tests matching '%s' ===\n"), *TestFilter);
	Report += FString::Printf(TEXT("Found %d matching tests:\n"), MatchingTests.Num());

	int32 DisplayCount = FMath::Min(MatchingTests.Num(), 50);
	for (int32 i = 0; i < DisplayCount; ++i)
	{
		Report += FString::Printf(TEXT("  %d. %s\n"), i + 1, *MatchingTests[i]);
	}
	if (MatchingTests.Num() > 50)
	{
		Report += FString::Printf(TEXT("  ... and %d more\n"), MatchingTests.Num() - 50);
	}

	// Run tests if requested (not just listing)
	bool bRunTests = true;
	Params->TryGetBoolField(TEXT("list_only"), bRunTests);
	if (Params->HasField(TEXT("list_only")))
	{
		Params->TryGetBoolField(TEXT("list_only"), bRunTests);
		bRunTests = !bRunTests; // list_only = true → don't run
	}

	if (bRunTests)
	{
		// Execute tests via console command — the most reliable cross-version method
		for (const FString& TestName : MatchingTests)
		{
			GEngine->Exec(nullptr, *FString::Printf(TEXT("Automation RunTest %s"), *TestName));
		}

		// Note: Automation tests run asynchronously over multiple frames.
		// We can't get results synchronously. Instead, tell the AI to check
		// the Automation tab or use read_message_log after a delay.
		Report += FString::Printf(TEXT("\nQueued %d tests for execution. Tests run asynchronously over multiple frames.\n"), MatchingTests.Num());
		Report += TEXT("Results will appear in:\n");
		Report += TEXT("  1. Window > Developer Tools > Session Frontend > Automation tab\n");
		Report += TEXT("  2. Output Log (use read_message_log with category_filter='LogAutomationTest')\n");
		Report += TEXT("\nCall read_message_log after a few seconds to check test results.\n");
	}

	Result.bSuccess = true;
	Result.ResultMessage = Report;
	return Result;
}

#undef LOCTEXT_NAMESPACE
