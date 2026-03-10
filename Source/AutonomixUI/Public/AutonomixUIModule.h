// Copyright Autonomix. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

class FAutonomixUIModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	static FAutonomixUIModule& Get();
	static bool IsAvailable();

private:
	/** Register the Nomad tab spawner for the main Autonomix panel */
	void RegisterTabSpawner();

	/** Unregister the tab spawner */
	void UnregisterTabSpawner();

	/** Register toolbar/menu extensions */
	void RegisterMenuExtensions();

	/** Callback to spawn the main panel tab */
	TSharedRef<class SDockTab> OnSpawnAutonomixTab(const class FSpawnTabArgs& SpawnTabArgs);

	/** The tab identifier for the main panel */
	static const FName AutonomixTabName;
};
