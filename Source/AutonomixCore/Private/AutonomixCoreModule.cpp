// Copyright Autonomix. All Rights Reserved.
//fixed a few things here and there, but overall this is the same as the original code. I just added some logging to the startup and shutdown of the module.
#include "AutonomixCoreModule.h"

DEFINE_LOG_CATEGORY(LogAutonomix);

#define LOCTEXT_NAMESPACE "FAutonomixCoreModule"

void FAutonomixCoreModule::StartupModule()
{
	UE_LOG(LogAutonomix, Log, TEXT("AutonomixCore module started."));
}

void FAutonomixCoreModule::ShutdownModule()
{
	UE_LOG(LogAutonomix, Log, TEXT("AutonomixCore module shut down."));
}

FAutonomixCoreModule& FAutonomixCoreModule::Get()
{
	return FModuleManager::LoadModuleChecked<FAutonomixCoreModule>("AutonomixCore");
}

bool FAutonomixCoreModule::IsAvailable()
{
	return FModuleManager::Get().IsModuleLoaded("AutonomixCore");
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FAutonomixCoreModule, AutonomixCore)
