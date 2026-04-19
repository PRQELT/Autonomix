// Copyright Autonomix. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AutonomixInterfaces.h"

class AUTONOMIXACTIONS_API FAutonomixCppActions : public IAutonomixActionExecutor
{
public:
    FAutonomixCppActions();
    virtual ~FAutonomixCppActions();

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
    FAutonomixActionResult ExecuteCreateCppClass(const TSharedRef<FJsonObject>& Params, FAutonomixActionResult& Result);
    FAutonomixActionResult ExecuteModifyCppFile(const TSharedRef<FJsonObject>& Params, FAutonomixActionResult& Result);
    FAutonomixActionResult ExecuteTriggerCompile(FAutonomixActionResult& Result);
    FAutonomixActionResult ExecuteRegenerateProjectFiles(FAutonomixActionResult& Result);

    /** Validate generated code for dangerous patterns */
    bool ValidateCodeSafety(const FString& Code, TArray<FString>& OutViolations) const;

    /** Write a file to disk with backup */
    bool WriteFileWithBackup(const FString& FilePath, const FString& Content);
};
