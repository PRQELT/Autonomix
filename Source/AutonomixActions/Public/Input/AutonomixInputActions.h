// Copyright Autonomix. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AutonomixInterfaces.h"

/**
 * Enhanced Input actions for Autonomix.
 *
 * Provides programmatic creation and modification of Enhanced Input assets:
 * - create_input_action: Create a UInputAction data asset (IA_*)
 * - create_input_mapping_context: Create a UInputMappingContext data asset (IMC_*)
 * - add_input_mapping: Add a key→action binding to an existing IMC (with optional modifiers/triggers)
 *
 * These tools eliminate the need for manual editor interaction when configuring
 * the Enhanced Input System, upholding the zero-manual-steps protocol.
 */
class AUTONOMIXACTIONS_API FAutonomixInputActions : public IAutonomixActionExecutor
{
public:
	FAutonomixInputActions();
	virtual ~FAutonomixInputActions();

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
	FAutonomixActionResult ExecuteCreateInputAction(const TSharedRef<FJsonObject>& Params);
	FAutonomixActionResult ExecuteCreateInputMappingContext(const TSharedRef<FJsonObject>& Params);
	FAutonomixActionResult ExecuteAddInputMapping(const TSharedRef<FJsonObject>& Params);
};
