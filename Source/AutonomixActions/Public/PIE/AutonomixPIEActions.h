// Copyright Autonomix. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AutonomixInterfaces.h"

/**
 * FAutonomixPIEActions
 *
 * Play-In-Editor automation tools:
 *   - start_pie_session: Launch PIE for runtime testing
 *   - simulate_input: Inject key presses during PIE
 *   - stop_pie_session: End the current PIE session
 *
 * Combined with read_message_log (from DiagnosticsActions), the AI can:
 *   1. Build a Blueprint
 *   2. Start PIE
 *   3. Simulate player input (move forward, press interact)
 *   4. Read the Output Log for Accessed None errors
 *   5. Stop PIE and fix the bugs
 *
 * SAFETY:
 *   - PIE is inherently risky (editor crash potential), so rated High risk
 *   - Requires Developer security mode
 *   - Input simulation is limited to keyboard/gamepad (no mouse movement)
 *   - PIE sessions auto-stop after a configurable timeout
 */
class AUTONOMIXACTIONS_API FAutonomixPIEActions : public IAutonomixActionExecutor
{
public:
	FAutonomixPIEActions();
	virtual ~FAutonomixPIEActions();

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
	/** Launch Play-In-Editor. */
	FAutonomixActionResult ExecuteStartPIE(const TSharedRef<FJsonObject>& Params, FAutonomixActionResult& Result);

	/** Inject keyboard input during a running PIE session. */
	FAutonomixActionResult ExecuteSimulateInput(const TSharedRef<FJsonObject>& Params, FAutonomixActionResult& Result);

	/** Stop the current PIE session. */
	FAutonomixActionResult ExecuteStopPIE(const TSharedRef<FJsonObject>& Params, FAutonomixActionResult& Result);

	/** Whether PIE is currently running. */
	static bool IsPIERunning();
};
