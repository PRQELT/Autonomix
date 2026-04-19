// Copyright Autonomix. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AutonomixInterfaces.h"

class UWorld;

class AUTONOMIXACTIONS_API FAutonomixLevelActions : public IAutonomixActionExecutor
{
public:
    FAutonomixLevelActions();
    virtual ~FAutonomixLevelActions();

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
    FAutonomixActionResult ExecuteSpawnActor(const TSharedRef<FJsonObject>& Params, UWorld* World);
    FAutonomixActionResult ExecutePlaceLight(const TSharedRef<FJsonObject>& Params, UWorld* World);
    FAutonomixActionResult ExecuteModifyWorldSettings(const TSharedRef<FJsonObject>& Params, UWorld* World);
};
