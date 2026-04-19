// Copyright Autonomix. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AutonomixInterfaces.h"

/**
 * FAutonomixDataTableActions
 *
 * Provides tools for creating and populating DataTable assets:
 *   - create_data_table: Create a DataTable from an existing row struct
 *   - import_json_to_datatable: Populate a DataTable from inline JSON data
 *
 * DataTables are used extensively in UE for game balancing, item databases,
 * weapon stats, dialogue, level configuration, and more.
 *
 * The AI can:
 *   - Create DataTables targeting any FTableRowBase-derived struct
 *   - Generate balanced game data (e.g., RPG weapon progression curves)
 *   - Import pre-computed JSON data directly into rows
 *   - Modify existing DataTable rows
 */
class AUTONOMIXACTIONS_API FAutonomixDataTableActions : public IAutonomixActionExecutor
{
public:
	FAutonomixDataTableActions();
	virtual ~FAutonomixDataTableActions();

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
	/** Create a new DataTable asset with a specified row struct. */
	FAutonomixActionResult ExecuteCreateDataTable(const TSharedRef<FJsonObject>& Params, FAutonomixActionResult& Result);

	/** Import JSON data into an existing or new DataTable. */
	FAutonomixActionResult ExecuteImportJsonToDataTable(const TSharedRef<FJsonObject>& Params, FAutonomixActionResult& Result);
};
