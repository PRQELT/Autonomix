// Copyright Autonomix. All Rights Reserved.

#include "AutonomixLLMModule.h"
#include "AutonomixCoreModule.h"

#define LOCTEXT_NAMESPACE "FAutonomixLLMModule"

void FAutonomixLLMModule::StartupModule()
{
	UE_LOG(LogAutonomix, Log, TEXT("AutonomixLLM module started."));
}

void FAutonomixLLMModule::ShutdownModule()
{
	UE_LOG(LogAutonomix, Log, TEXT("AutonomixLLM module shut down."));
}

FAutonomixLLMModule& FAutonomixLLMModule::Get()
{
	return FModuleManager::LoadModuleChecked<FAutonomixLLMModule>("AutonomixLLM");
}

bool FAutonomixLLMModule::IsAvailable()
{
	return FModuleManager::Get().IsModuleLoaded("AutonomixLLM");
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FAutonomixLLMModule, AutonomixLLM)
