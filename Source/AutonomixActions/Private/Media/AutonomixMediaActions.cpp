// Copyright Autonomix. All Rights Reserved.

#include "Media/AutonomixMediaActions.h"
#include "AutonomixCoreModule.h"

FAutonomixMediaActions::FAutonomixMediaActions() {}
FAutonomixMediaActions::~FAutonomixMediaActions() {}
FName FAutonomixMediaActions::GetActionName() const { return FName(TEXT("Media")); }
FText FAutonomixMediaActions::GetDisplayName() const { return FText::FromString(TEXT("Media Actions")); }
EAutonomixActionCategory FAutonomixMediaActions::GetCategory() const { return EAutonomixActionCategory::Texture; }
EAutonomixRiskLevel FAutonomixMediaActions::GetDefaultRiskLevel() const { return EAutonomixRiskLevel::Medium; }
FAutonomixActionPlan FAutonomixMediaActions::PreviewAction(const TSharedRef<FJsonObject>& Params) { return FAutonomixActionPlan(); }
FAutonomixActionResult FAutonomixMediaActions::ExecuteAction(const TSharedRef<FJsonObject>& Params) { FAutonomixActionResult R; R.ResultMessage = TEXT("Stub: not yet implemented"); return R; }
bool FAutonomixMediaActions::CanUndo() const { return false; }
bool FAutonomixMediaActions::UndoAction() { return false; }
TArray<FString> FAutonomixMediaActions::GetSupportedToolNames() const { return TArray<FString>(); }
bool FAutonomixMediaActions::ValidateParams(const TSharedRef<FJsonObject>& Params, TArray<FString>& OutErrors) const { return true; }
