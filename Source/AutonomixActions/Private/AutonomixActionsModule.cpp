// Copyright Autonomix. All Rights Reserved.

#include "AutonomixActionsModule.h"
#include "AutonomixCoreModule.h"

#define LOCTEXT_NAMESPACE "FAutonomixActionsModule"

void FAutonomixActionsModule::StartupModule() { UE_LOG(LogAutonomix, Log, TEXT("AutonomixActions module started.")); }
void FAutonomixActionsModule::ShutdownModule() { UE_LOG(LogAutonomix, Log, TEXT("AutonomixActions module shut down.")); }
FAutonomixActionsModule& FAutonomixActionsModule::Get() { return FModuleManager::LoadModuleChecked<FAutonomixActionsModule>("AutonomixActions"); }
bool FAutonomixActionsModule::IsAvailable() { return FModuleManager::Get().IsModuleLoaded("AutonomixActions"); }

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FAutonomixActionsModule, AutonomixActions)
