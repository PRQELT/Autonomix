// Copyright Autonomix. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AutonomixInterfaces.h"

class AUTONOMIXACTIONS_API FAutonomixPerformanceActions : public IAutonomixActionExecutor
{
public:
    FAutonomixPerformanceActions();
    virtual ~FAutonomixPerformanceActions();

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
};
