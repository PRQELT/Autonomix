// Copyright Autonomix. All Rights Reserved.

#include "AutonomixUIModule.h"
#include "AutonomixCoreModule.h"
#include "AutonomixStyle.h"
#include "AutonomixCommands.h"
#include "Widgets/SAutonomixMainPanel.h"
#include "Widgets/Docking/SDockTab.h"
#include "Framework/Docking/TabManager.h"
#include "ToolMenus.h"

#define LOCTEXT_NAMESPACE "FAutonomixUIModule"

const FName FAutonomixUIModule::AutonomixTabName(TEXT("AutonomixPanel"));

void FAutonomixUIModule::StartupModule()
{
	FAutonomixStyle::Initialize();
	FAutonomixStyle::ReloadTextures();
	FAutonomixCommands::Register();

	RegisterTabSpawner();
	RegisterMenuExtensions();

	UE_LOG(LogAutonomix, Log, TEXT("AutonomixUI module started."));
}

void FAutonomixUIModule::ShutdownModule()
{
	UnregisterTabSpawner();

	FAutonomixCommands::Unregister();
	FAutonomixStyle::Shutdown();

	UE_LOG(LogAutonomix, Log, TEXT("AutonomixUI module shut down."));
}

FAutonomixUIModule& FAutonomixUIModule::Get()
{
	return FModuleManager::LoadModuleChecked<FAutonomixUIModule>("AutonomixUI");
}

bool FAutonomixUIModule::IsAvailable()
{
	return FModuleManager::Get().IsModuleLoaded("AutonomixUI");
}

void FAutonomixUIModule::RegisterTabSpawner()
{
	FGlobalTabmanager::Get()->RegisterNomadTabSpawner(
		AutonomixTabName,
		FOnSpawnTab::CreateRaw(this, &FAutonomixUIModule::OnSpawnAutonomixTab))
		.SetDisplayName(LOCTEXT("AutonomixTabTitle", "Autonomix"))
		.SetMenuType(ETabSpawnerMenuType::Hidden);
}

void FAutonomixUIModule::UnregisterTabSpawner()
{
	FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(AutonomixTabName);
}

void FAutonomixUIModule::RegisterMenuExtensions()
{
	UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateLambda([]()
	{
		UToolMenu* Menu = UToolMenus::Get()->ExtendMenu("LevelEditor.MainMenu.Window");
		if (Menu)
		{
			FToolMenuSection& Section = Menu->FindOrAddSection("WindowLayout");
			Section.AddEntry(FToolMenuEntry::InitMenuEntry(
				"OpenAutonomix",
				LOCTEXT("OpenAutonomixLabel", "Autonomix"),
				LOCTEXT("OpenAutonomixTooltip", "Open Autonomix AI Assistant"),
				FSlateIcon(),
				FUIAction(FExecuteAction::CreateLambda([]()
				{
					FGlobalTabmanager::Get()->TryInvokeTab(FName("AutonomixPanel"));
				}))
			));
		}
	}));
}

TSharedRef<SDockTab> FAutonomixUIModule::OnSpawnAutonomixTab(const FSpawnTabArgs& SpawnTabArgs)
{
	return SNew(SDockTab)
		.TabRole(ETabRole::NomadTab)
		[
			SNew(SAutonomixMainPanel)
		];
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FAutonomixUIModule, AutonomixUI)
