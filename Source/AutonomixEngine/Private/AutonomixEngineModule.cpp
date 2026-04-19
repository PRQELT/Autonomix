// Copyright Autonomix. All Rights Reserved.

#include "AutonomixEngineModule.h"
#include "AutonomixCoreModule.h"

#define LOCTEXT_NAMESPACE "FAutonomixEngineModule"

void FAutonomixEngineModule::StartupModule() { UE_LOG(LogAutonomix, Log, TEXT("AutonomixEngine module started.")); }
void FAutonomixEngineModule::ShutdownModule() { UE_LOG(LogAutonomix, Log, TEXT("AutonomixEngine module shut down.")); }
FAutonomixEngineModule& FAutonomixEngineModule::Get() { return FModuleManager::LoadModuleChecked<FAutonomixEngineModule>("AutonomixEngine"); }
bool FAutonomixEngineModule::IsAvailable() { return FModuleManager::Get().IsModuleLoaded("AutonomixEngine"); }

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FAutonomixEngineModule, AutonomixEngine)
