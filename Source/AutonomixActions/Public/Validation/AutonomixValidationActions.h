// Copyright Autonomix. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AutonomixInterfaces.h"

/**
 * FAutonomixValidationActions
 *
 * Asset validation and testing tools:
 *   - validate_assets: Run UEditorValidatorSubsystem on specified assets
 *   - run_automation_tests: Execute Unreal Automation Tests by name/group
 *
 * These tools form the "verification ladder" that the AI uses to confirm
 * its work is correct beyond just "it compiles":
 *   Step 1: Compile Blueprint / Live Coding (existing tools)
 *   Step 2: validate_assets — official UE data validation
 *   Step 3: run_automation_tests — project + engine tests
 *   Step 4: Performance profiling (existing tools)
 *
 * Data Validation runs registered UEditorValidator instances, which check:
 *   - Missing/broken asset references
 *   - Invalid property values
 *   - Custom project validators (if registered)
 *   - Blueprint compilation status
 *
 * Automation Tests run the UE test framework, which can verify:
 *   - Functional tests (spawn actors, check behavior)
 *   - Unit tests (C++ logic)
 *   - Smoke tests (load maps, check for errors)
 */
class AUTONOMIXACTIONS_API FAutonomixValidationActions : public IAutonomixActionExecutor
{
public:
	FAutonomixValidationActions();
	virtual ~FAutonomixValidationActions();

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
	 * Run UEditorValidatorSubsystem on specified assets or all project assets.
	 *
	 * @param Params  JSON with optional "asset_paths" (array of content paths) or
	 *                "validate_all" (bool) to validate the entire project
	 */
	FAutonomixActionResult ExecuteValidateAssets(const TSharedRef<FJsonObject>& Params, FAutonomixActionResult& Result);

	/**
	 * Run Unreal Automation Tests matching a filter pattern.
	 *
	 * @param Params  JSON with "test_filter" (string pattern) and optional
	 *                "test_flags" (string: "Smoke", "Product", "Stress", etc.)
	 */
	FAutonomixActionResult ExecuteRunAutomationTests(const TSharedRef<FJsonObject>& Params, FAutonomixActionResult& Result);
};
