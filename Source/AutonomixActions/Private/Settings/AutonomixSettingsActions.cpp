// Copyright Autonomix. All Rights Reserved.

#include "Settings/AutonomixSettingsActions.h"
#include "AutonomixCoreModule.h"
#include "AutonomixSettings.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "UObject/UnrealType.h"
#include "ISettingsModule.h"
#include "ScopedTransaction.h"

FAutonomixSettingsActions::FAutonomixSettingsActions() {}
FAutonomixSettingsActions::~FAutonomixSettingsActions() {}
FName FAutonomixSettingsActions::GetActionName() const { return FName(TEXT("Settings")); }
FText FAutonomixSettingsActions::GetDisplayName() const { return FText::FromString(TEXT("Settings Actions")); }
EAutonomixActionCategory FAutonomixSettingsActions::GetCategory() const { return EAutonomixActionCategory::Settings; }
EAutonomixRiskLevel FAutonomixSettingsActions::GetDefaultRiskLevel() const { return EAutonomixRiskLevel::High; }
bool FAutonomixSettingsActions::CanUndo() const { return false; }
bool FAutonomixSettingsActions::UndoAction() { return false; }

TArray<FString> FAutonomixSettingsActions::GetSupportedToolNames() const
{
	// CRITICAL: These must match the tool names in settings_tools.json exactly.
	// Previously mismatched (read_ini_setting vs read_config_value) causing
	// "No executor registered for tool: write_config_value"
	return {
		TEXT("read_config_value"),
		TEXT("write_config_value")
	};
}

bool FAutonomixSettingsActions::ValidateParams(const TSharedRef<FJsonObject>& Params, TArray<FString>& OutErrors) const { return true; }

FAutonomixActionPlan FAutonomixSettingsActions::PreviewAction(const TSharedRef<FJsonObject>& Params)
{
	FAutonomixActionPlan Plan;
	Plan.Summary = TEXT("Settings modification (HIGH RISK)");
	Plan.MaxRiskLevel = EAutonomixRiskLevel::High;
	FAutonomixAction Action;
	Action.Description = Plan.Summary;
	Action.Category = EAutonomixActionCategory::Settings;
	Action.RiskLevel = EAutonomixRiskLevel::High;
	Plan.Actions.Add(Action);
	return Plan;
}

FAutonomixActionResult FAutonomixSettingsActions::ExecuteAction(const TSharedRef<FJsonObject>& Params)
{
	FScopedTransaction Transaction(FText::FromString(TEXT("Autonomix Settings Action")));

	FAutonomixActionResult Result;
	Result.bSuccess = false;

	// Dispatch by tool name — the ActionRouter passes tool name via "tool_name" field
	// or we detect from required fields. The schema uses config_file/section/key layout.
	FString ConfigFile, Section, Key, Value;
	Params->TryGetStringField(TEXT("config_file"), ConfigFile);
	Params->TryGetStringField(TEXT("section"), Section);
	Params->TryGetStringField(TEXT("key"), Key);
	Params->TryGetStringField(TEXT("value"), Value);

	// Detect read vs write: if "value" is present, it's a write; otherwise a read.
	bool bIsWrite = Params->HasField(TEXT("value"));

	if (Section.IsEmpty() || Key.IsEmpty())
	{
		Result.Errors.Add(TEXT("Missing required fields: 'section' and 'key' are required."));
		return Result;
	}

	if (!bIsWrite)
	{
		// READ CONFIG VALUE
		// Determine which ini to read from
		FString TargetIni = GEngineIni;
		if (ConfigFile.Contains(TEXT("Game"))) TargetIni = GGameIni;
		else if (ConfigFile.Contains(TEXT("Input"))) TargetIni = GInputIni;
		else if (ConfigFile.Contains(TEXT("Editor"))) TargetIni = GEditorIni;

		FString ReadValue;
		if (GConfig->GetString(*Section, *Key, ReadValue, TargetIni))
		{
			Result.bSuccess = true;
			Result.ResultMessage = FString::Printf(TEXT("[%s] %s = %s (from %s)"), *Section, *Key, *ReadValue, *ConfigFile);
		}
		else
		{
			// Return success=true with a "not found" message instead of a hard error.
			// "Not found" is valid diagnostic information, not a tool execution failure.
			// The AI uses this to determine that a key/section doesn't exist and adjusts its approach.
			Result.bSuccess = true;
			Result.ResultMessage = FString::Printf(TEXT("Setting not found: [%s] %s in %s. The key does not exist in this config file."), *Section, *Key, *ConfigFile);
		}
	}
	else
	{
		// WRITE CONFIG VALUE

		// CRITICAL FIX (ChatGPT): INI write restrictions in Advanced mode
		const UAutonomixDeveloperSettings* Settings = UAutonomixDeveloperSettings::Get();
		if (Settings && !Settings->IsIniSectionAllowed(Section))
		{
			Result.Errors.Add(FString::Printf(
				TEXT("INI write to section '%s' is blocked in Advanced security mode. Only /Script/Autonomix.* sections are writable. Switch to Developer mode for full access."),
				*Section));
			return Result;
		}

		// Determine which ini to target
		FString TargetIni = GEngineIni;
		if (ConfigFile.Contains(TEXT("Game"))) TargetIni = GGameIni;
		else if (ConfigFile.Contains(TEXT("Input"))) TargetIni = GInputIni;
		else if (ConfigFile.Contains(TEXT("Editor"))) TargetIni = GEditorIni;

		// CRITICAL FIX (Gemini): Use Reflection + PostEditChangeProperty instead of raw GConfig
		bool bUsedReflection = false;

		if (FModuleManager::Get().IsModuleLoaded(TEXT("Settings")))
		{
			FString ClassName = FPackageName::ObjectPathToObjectName(Section);
			UClass* SettingsClass = FindFirstObject<UClass>(*ClassName, EFindFirstObjectOptions::None);

			if (SettingsClass)
			{
				UObject* SettingsObject = SettingsClass->GetDefaultObject();
				if (SettingsObject)
				{
					FProperty* Prop = SettingsClass->FindPropertyByName(FName(*Key));
					if (Prop)
					{
						SettingsObject->Modify();
						void* PropAddr = Prop->ContainerPtrToValuePtr<void>(SettingsObject);
						Prop->ImportText_Direct(*Value, PropAddr, SettingsObject, PPF_None);
						FPropertyChangedEvent ChangedEvent(Prop);
						SettingsObject->PostEditChangeProperty(ChangedEvent);
						SettingsObject->SaveConfig();
						bUsedReflection = true;

						UE_LOG(LogAutonomix, Log, TEXT("SettingsActions: Set %s.%s = %s via Reflection+PostEditChangeProperty"),
							*Section, *Key, *Value);
					}
				}
			}
		}

		// Fallback: use GConfig directly if reflection didn't work
		if (!bUsedReflection)
		{
			GConfig->SetString(*Section, *Key, *Value, TargetIni);
			GConfig->Flush(false, TargetIni);

			UE_LOG(LogAutonomix, Log, TEXT("SettingsActions: Set [%s] %s = %s via GConfig (no live UI update)"),
				*Section, *Key, *Value);
		}

		Result.bSuccess = true;
		Result.ResultMessage = FString::Printf(TEXT("Set [%s] %s = %s in %s%s"),
			*Section, *Key, *Value, *ConfigFile,
			bUsedReflection ? TEXT(" (live UI updated)") : TEXT(" (ini only, restart may be needed)"));
		Result.ModifiedPaths.Add(FString::Printf(TEXT("%s: [%s] %s"), *ConfigFile, *Section, *Key));
	}

	return Result;
}
