// Copyright Autonomix. All Rights Reserved.

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
