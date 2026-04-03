// Copyright Autonomix. All Rights Reserved.

#include "Performance/AutonomixPerformanceActions.h"
#include "AutonomixCoreModule.h"
#include "Editor.h"
#include "Engine/Engine.h"
#include "HAL/PlatformMemory.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/IConsoleManager.h"
#include "Engine/RendererSettings.h"
#include "Scalability.h"
#include "GameFramework/GameUserSettings.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/AssetData.h"
#include "UObject/UnrealType.h"

FAutonomixPerformanceActions::FAutonomixPerformanceActions() {}
FAutonomixPerformanceActions::~FAutonomixPerformanceActions() {}
FName FAutonomixPerformanceActions::GetActionName() const { return FName(TEXT("Performance")); }
FText FAutonomixPerformanceActions::GetDisplayName() const { return FText::FromString(TEXT("Performance Actions")); }
EAutonomixActionCategory FAutonomixPerformanceActions::GetCategory() const { return EAutonomixActionCategory::Performance; }
EAutonomixRiskLevel FAutonomixPerformanceActions::GetDefaultRiskLevel() const { return EAutonomixRiskLevel::Low; }
bool FAutonomixPerformanceActions::CanUndo() const { return false; }
bool FAutonomixPerformanceActions::UndoAction() { return false; }

TArray<FString> FAutonomixPerformanceActions::GetSupportedToolNames() const
{
	return {
		// Original tools
		TEXT("get_memory_stats"),
		TEXT("get_performance_stats"),
		TEXT("run_stat_command"),
		TEXT("analyze_asset_sizes"),
		// New CVar tools
		TEXT("get_cvar"),
		TEXT("set_cvar"),
		TEXT("discover_cvars"),
		TEXT("execute_console_command"),
		// Profiling tools
		TEXT("start_csv_profiler"),
		TEXT("stop_csv_profiler"),
		TEXT("read_profiling_file"),
		// Scalability tools
		TEXT("get_scalability_settings"),
		TEXT("set_scalability_settings"),
		// Renderer settings tools
		TEXT("get_renderer_settings"),
		TEXT("set_renderer_setting")
	};
}

bool FAutonomixPerformanceActions::ValidateParams(const TSharedRef<FJsonObject>& Params, TArray<FString>& OutErrors) const { return true; }

FAutonomixActionPlan FAutonomixPerformanceActions::PreviewAction(const TSharedRef<FJsonObject>& Params)
{
	FAutonomixActionPlan Plan;
	Plan.Summary = TEXT("Performance profiling / optimization operation");
	FAutonomixAction Action;
	Action.Description = Plan.Summary;
	Action.Category = EAutonomixActionCategory::Performance;
	Action.RiskLevel = EAutonomixRiskLevel::Low;
	Plan.Actions.Add(Action);
	return Plan;
}

// ---------------------------------------------------------------------------
// Helper: get the editor world safely
// ---------------------------------------------------------------------------
static UWorld* GetEditorWorld()
{
	if (GEditor)
	{
		return GEditor->GetEditorWorldContext().World();
	}
	return nullptr;
}

// ---------------------------------------------------------------------------
// Helper: get Saved/Profiling directory
// ---------------------------------------------------------------------------
static FString GetProfilingDir()
{
	return FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Profiling"));
}

// ---------------------------------------------------------------------------
// ExecuteAction — dispatcher
// ---------------------------------------------------------------------------
FAutonomixActionResult FAutonomixPerformanceActions::ExecuteAction(const TSharedRef<FJsonObject>& Params)
{
	FAutonomixActionResult Result;
	Result.bSuccess = false;

	FString ToolName;
	if (!Params->TryGetStringField(TEXT("_tool_name"), ToolName))
	{
		// Legacy compat: read 'tool_name' (without underscore) or 'action' field
		if (!Params->TryGetStringField(TEXT("tool_name"), ToolName))
		{
			Params->TryGetStringField(TEXT("action"), ToolName);
		}
	}

	// -----------------------------------------------------------------------
	// LEGACY: map old action values → new tool names
	// -----------------------------------------------------------------------
	if (ToolName == TEXT("memory_stats"))   ToolName = TEXT("get_memory_stats");
	if (ToolName == TEXT("stat_command"))   ToolName = TEXT("run_stat_command");
	if (ToolName == TEXT("fps"))            ToolName = TEXT("get_performance_stats");

	// -----------------------------------------------------------------------
	// 1. get_performance_stats — FPS, frame time
	// -----------------------------------------------------------------------
	if (ToolName == TEXT("get_performance_stats"))
	{
		float DeltaTime = FApp::GetDeltaTime();
		float FPS = (DeltaTime > 0.0f) ? (1.0f / DeltaTime) : 0.0f;

		FString Report;
		Report += FString::Printf(TEXT("FPS: %.1f\n"), FPS);
		Report += FString::Printf(TEXT("Frame Time: %.2f ms\n"), DeltaTime * 1000.0f);

		if (GEngine)
		{
			Report += FString::Printf(TEXT("Fixed Tick Rate: %.1f\n"), GEngine->GetMaxFPS());
		}

		Result.bSuccess = true;
		Result.ResultMessage = Report;
		return Result;
	}

	// -----------------------------------------------------------------------
	// 2. get_memory_stats — platform memory
	// -----------------------------------------------------------------------
	if (ToolName == TEXT("get_memory_stats"))
	{
		FPlatformMemoryStats MemStats = FPlatformMemory::GetStats();
		FString Report;
		Report += FString::Printf(TEXT("Physical Memory Used:      %.2f MB\n"), MemStats.UsedPhysical / (1024.0 * 1024.0));
		Report += FString::Printf(TEXT("Physical Memory Available: %.2f MB\n"), MemStats.AvailablePhysical / (1024.0 * 1024.0));
		Report += FString::Printf(TEXT("Virtual Memory Used:       %.2f MB\n"), MemStats.UsedVirtual / (1024.0 * 1024.0));
		Report += FString::Printf(TEXT("Peak Physical Used:        %.2f MB\n"), MemStats.PeakUsedPhysical / (1024.0 * 1024.0));

		Result.bSuccess = true;
		Result.ResultMessage = Report;
		return Result;
	}

	// -----------------------------------------------------------------------
	// 3. run_stat_command — toggle a stat overlay (stat fps, stat unit, etc.)
	// -----------------------------------------------------------------------
	if (ToolName == TEXT("run_stat_command"))
	{
		FString Command;
		if (!Params->TryGetStringField(TEXT("command"), Command))
		{
			Result.Errors.Add(TEXT("Missing 'command' field for run_stat_command."));
			return Result;
		}

		UWorld* World = GetEditorWorld();
		if (GEngine && World)
		{
			GEngine->Exec(World, *FString::Printf(TEXT("stat %s"), *Command));
			Result.bSuccess = true;
			Result.ResultMessage = FString::Printf(TEXT("Executed: stat %s"), *Command);
		}
		else
		{
			Result.Errors.Add(TEXT("GEngine or editor world not available."));
		}
		return Result;
	}

	// -----------------------------------------------------------------------
	// 4. analyze_asset_sizes — iterate asset registry, report largest assets
	// -----------------------------------------------------------------------
	if (ToolName == TEXT("analyze_asset_sizes"))
	{
		FString FilterPath;
		Params->TryGetStringField(TEXT("path_filter"), FilterPath);

		int32 TopN = 20;
		Params->TryGetNumberField(TEXT("top_n"), TopN);

		FAssetRegistryModule& ARModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
		IAssetRegistry& AR = ARModule.Get();

		TArray<FAssetData> AllAssets;
		AR.GetAllAssets(AllAssets, true);

		// Filter by path if provided
		if (!FilterPath.IsEmpty())
		{
			AllAssets = AllAssets.FilterByPredicate([&FilterPath](const FAssetData& A)
			{
				return A.PackagePath.ToString().StartsWith(FilterPath);
			});
		}

		// Gather disk sizes
		TArray<TPair<int64, FString>> SizeList;
		for (const FAssetData& A : AllAssets)
		{
			FString PackageFilePath;
			if (FPackageName::TryConvertLongPackageNameToFilename(A.PackageName.ToString(), PackageFilePath, TEXT(".uasset")))
			{
				int64 FileSize = IFileManager::Get().FileSize(*PackageFilePath);
				if (FileSize > 0)
				{
					SizeList.Add(TPair<int64, FString>(FileSize, A.GetObjectPathString()));
				}
			}
		}

		// Sort descending
		SizeList.Sort([](const TPair<int64, FString>& A, const TPair<int64, FString>& B)
		{
			return A.Key > B.Key;
		});

		FString Report = FString::Printf(TEXT("Top %d assets by disk size:\n"), TopN);
		int32 Shown = 0;
		for (const TPair<int64, FString>& Entry : SizeList)
		{
			if (Shown >= TopN) break;
			Report += FString::Printf(TEXT("  %.2f MB  %s\n"), Entry.Key / (1024.0 * 1024.0), *Entry.Value);
			Shown++;
		}
		Report += FString::Printf(TEXT("\nTotal assets scanned: %d\n"), SizeList.Num());

		Result.bSuccess = true;
		Result.ResultMessage = Report;
		return Result;
	}

	// -----------------------------------------------------------------------
	// 5. get_cvar — read a single console variable
	// -----------------------------------------------------------------------
	if (ToolName == TEXT("get_cvar"))
	{
		FString CVarName;
		if (!Params->TryGetStringField(TEXT("name"), CVarName))
		{
			Result.Errors.Add(TEXT("Missing 'name' field for get_cvar."));
			return Result;
		}

		IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(*CVarName);
		if (!CVar)
		{
			Result.Errors.Add(FString::Printf(TEXT("CVar not found: %s"), *CVarName));
			return Result;
		}

		FString Report;
		Report += FString::Printf(TEXT("Name:  %s\n"), *CVarName);
		Report += FString::Printf(TEXT("Value: %s\n"), *CVar->GetString());
		Report += FString::Printf(TEXT("Help:  %s\n"), CVar->GetHelp() ? CVar->GetHelp() : TEXT("(no description)"));

		Result.bSuccess = true;
		Result.ResultMessage = Report;
		return Result;
	}

	// -----------------------------------------------------------------------
	// 6. set_cvar — set a console variable (transient, not saved to .ini)
	// -----------------------------------------------------------------------
	if (ToolName == TEXT("set_cvar"))
	{
		FString CVarName, CVarValue;
		if (!Params->TryGetStringField(TEXT("name"), CVarName) ||
			!Params->TryGetStringField(TEXT("value"), CVarValue))
		{
			Result.Errors.Add(TEXT("Missing 'name' or 'value' fields for set_cvar."));
			return Result;
		}

		IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(*CVarName);
		if (!CVar)
		{
			Result.Errors.Add(FString::Printf(TEXT("CVar not found: %s"), *CVarName));
			return Result;
		}

		FString OldValue = CVar->GetString();
		CVar->Set(*CVarValue, ECVF_SetByConsole);

		UE_LOG(LogAutonomix, Log, TEXT("PerformanceActions: CVar %s: %s -> %s"), *CVarName, *OldValue, *CVarValue);

		Result.bSuccess = true;
		Result.ResultMessage = FString::Printf(
			TEXT("Set %s = %s  (was: %s)\nNOTE: This is transient — use set_renderer_setting or write_config_value to make it persistent."),
			*CVarName, *CVarValue, *OldValue);
		return Result;
	}

	// -----------------------------------------------------------------------
	// 7. discover_cvars — enumerate CVars by prefix
	// -----------------------------------------------------------------------
	if (ToolName == TEXT("discover_cvars"))
	{
		FString Prefix = TEXT("r.");
		Params->TryGetStringField(TEXT("prefix"), Prefix);

		int32 MaxResults = 50;
		Params->TryGetNumberField(TEXT("max_results"), MaxResults);

		TArray<TPair<FString, FString>> Found;

		IConsoleManager::Get().ForEachConsoleObjectThatStartsWith(
			FConsoleObjectVisitor::CreateLambda([&Found, MaxResults](const TCHAR* Name, IConsoleObject* Obj)
			{
				if (Found.Num() >= MaxResults) return;

				IConsoleVariable* Var = Obj->AsVariable();
				if (Var)
				{
					FString Val = Var->GetString();
					Found.Add(TPair<FString, FString>(FString(Name), Val));
				}
			}),
			*Prefix
		);

		FString Report = FString::Printf(TEXT("CVars matching prefix '%s' (%d found, showing up to %d):\n\n"),
			*Prefix, Found.Num(), MaxResults);

		for (const TPair<FString, FString>& Entry : Found)
		{
			IConsoleVariable* Var = IConsoleManager::Get().FindConsoleVariable(*Entry.Key);
			const TCHAR* HelpText = Var ? Var->GetHelp() : nullptr;
			Report += FString::Printf(TEXT("  %s = %s\n"), *Entry.Key, *Entry.Value);
			if (HelpText && FCString::Strlen(HelpText) > 0)
			{
				Report += FString::Printf(TEXT("    -> %s\n"), HelpText);
			}
		}

		Result.bSuccess = true;
		Result.ResultMessage = Report;
		return Result;
	}

	// -----------------------------------------------------------------------
	// 8. execute_console_command — run any GEngine->Exec command
	// -----------------------------------------------------------------------
	if (ToolName == TEXT("execute_console_command"))
	{
		FString Command;
		if (!Params->TryGetStringField(TEXT("command"), Command))
		{
			Result.Errors.Add(TEXT("Missing 'command' field for execute_console_command."));
			return Result;
		}

		UWorld* World = GetEditorWorld();
		if (!GEngine)
		{
			Result.Errors.Add(TEXT("GEngine not available."));
			return Result;
		}

		bool bExecuted = GEngine->Exec(World, *Command);
		UE_LOG(LogAutonomix, Log, TEXT("PerformanceActions: Executed console command: %s (result=%d)"), *Command, (int)bExecuted);

		Result.bSuccess = true;
		Result.ResultMessage = FString::Printf(TEXT("Executed: %s"), *Command);
		return Result;
	}

	// -----------------------------------------------------------------------
	// 9. start_csv_profiler
	// -----------------------------------------------------------------------
	if (ToolName == TEXT("start_csv_profiler"))
	{
		UWorld* World = GetEditorWorld();
		if (GEngine)
		{
			GEngine->Exec(World, TEXT("csvprofile start"));
			Result.bSuccess = true;
			Result.ResultMessage = FString::Printf(
				TEXT("CSV profiler started. Output will be written to: %s\nUse stop_csv_profiler when done, then read_profiling_file to analyze."),
				*GetProfilingDir());
		}
		else
		{
			Result.Errors.Add(TEXT("GEngine not available."));
		}
		return Result;
	}

	// -----------------------------------------------------------------------
	// 10. stop_csv_profiler — stop and list generated files
	// -----------------------------------------------------------------------
	if (ToolName == TEXT("stop_csv_profiler"))
	{
		UWorld* World = GetEditorWorld();
		if (GEngine)
		{
			GEngine->Exec(World, TEXT("csvprofile stop"));

			// List CSV files in profiling dir
			TArray<FString> Files;
			IFileManager::Get().FindFiles(Files, *(GetProfilingDir() / TEXT("*.csv")), true, false);
			Files.Sort();

			FString Report = TEXT("CSV profiler stopped.\n\nAvailable profiling files:\n");
			for (const FString& F : Files)
			{
				Report += FString::Printf(TEXT("  %s\n"), *F);
			}
			if (Files.Num() == 0)
			{
				Report += TEXT("  (none found — profiling may not have produced output yet)\n");
			}

			Result.bSuccess = true;
			Result.ResultMessage = Report;
		}
		else
		{
			Result.Errors.Add(TEXT("GEngine not available."));
		}
		return Result;
	}

	// -----------------------------------------------------------------------
	// 11. read_profiling_file — read a CSV/log from Saved/Profiling
	// -----------------------------------------------------------------------
	if (ToolName == TEXT("read_profiling_file"))
	{
		FString FileName;
		if (!Params->TryGetStringField(TEXT("file_name"), FileName))
		{
			// List available files so the AI knows what to pick
			TArray<FString> CSVFiles, LogFiles;
			IFileManager::Get().FindFiles(CSVFiles, *(GetProfilingDir() / TEXT("*.csv")), true, false);
			IFileManager::Get().FindFiles(LogFiles, *(GetProfilingDir() / TEXT("*.log")), true, false);
			CSVFiles.Sort(); LogFiles.Sort();

			FString Report = FString::Printf(TEXT("Profiling directory: %s\n\nCSV files:\n"), *GetProfilingDir());
			for (const FString& F : CSVFiles) Report += FString::Printf(TEXT("  %s\n"), *F);
			Report += TEXT("\nLog files:\n");
			for (const FString& F : LogFiles) Report += FString::Printf(TEXT("  %s\n"), *F);
			if (CSVFiles.Num() == 0 && LogFiles.Num() == 0) Report += TEXT("  (none)\n");

			Result.bSuccess = true;
			Result.ResultMessage = Report;
			return Result;
		}

		// Absolute path or relative to profiling dir
		FString FullPath = FPaths::IsRelative(FileName) ? (GetProfilingDir() / FileName) : FileName;

		FString FileContent;
		if (!FFileHelper::LoadFileToString(FileContent, *FullPath))
		{
			Result.Errors.Add(FString::Printf(TEXT("Could not read file: %s"), *FullPath));
			return Result;
		}

		// Optionally truncate very large files to first N lines
		int32 MaxLines = 200;
		Params->TryGetNumberField(TEXT("max_lines"), MaxLines);

		TArray<FString> Lines;
		FileContent.ParseIntoArrayLines(Lines, false);
		FString Truncated;
		int32 Count = FMath::Min(MaxLines, Lines.Num());
		for (int32 i = 0; i < Count; i++)
		{
			Truncated += Lines[i] + TEXT("\n");
		}
		if (Lines.Num() > MaxLines)
		{
			Truncated += FString::Printf(TEXT("... (%d lines total, showing first %d)\n"), Lines.Num(), MaxLines);
		}

		Result.bSuccess = true;
		Result.ResultMessage = FString::Printf(TEXT("File: %s\n\n%s"), *FullPath, *Truncated);
		return Result;
	}

	// -----------------------------------------------------------------------
	// 12. get_scalability_settings — read quality levels
	// -----------------------------------------------------------------------
	if (ToolName == TEXT("get_scalability_settings"))
	{
		Scalability::FQualityLevels Levels = Scalability::GetQualityLevels();

		FString Report = TEXT("Current Scalability / Quality Settings:\n\n");
		Report += FString::Printf(TEXT("  ResolutionQuality:        %.0f%%\n"), Levels.ResolutionQuality);
		Report += FString::Printf(TEXT("  ViewDistanceQuality:      %d  (0=Low, 1=Med, 2=High, 3=Epic)\n"), Levels.ViewDistanceQuality);
		Report += FString::Printf(TEXT("  AntiAliasingQuality:      %d\n"), Levels.AntiAliasingQuality);
		Report += FString::Printf(TEXT("  ShadowQuality:            %d\n"), Levels.ShadowQuality);
		Report += FString::Printf(TEXT("  GlobalIlluminationQuality:%d\n"), Levels.GlobalIlluminationQuality);
		Report += FString::Printf(TEXT("  ReflectionQuality:        %d\n"), Levels.ReflectionQuality);
		Report += FString::Printf(TEXT("  PostProcessQuality:       %d\n"), Levels.PostProcessQuality);
		Report += FString::Printf(TEXT("  TextureQuality:           %d\n"), Levels.TextureQuality);
		Report += FString::Printf(TEXT("  EffectsQuality:           %d\n"), Levels.EffectsQuality);
		Report += FString::Printf(TEXT("  FoliageQuality:           %d\n"), Levels.FoliageQuality);
		Report += FString::Printf(TEXT("  ShadingQuality:           %d\n"), Levels.ShadingQuality);

		Result.bSuccess = true;
		Result.ResultMessage = Report;
		return Result;
	}

	// -----------------------------------------------------------------------
	// 13. set_scalability_settings — apply quality level overrides
	// -----------------------------------------------------------------------
	if (ToolName == TEXT("set_scalability_settings"))
	{
		Scalability::FQualityLevels Levels = Scalability::GetQualityLevels();

		double TempNumber = 0.0;

		if (Params->TryGetNumberField(TEXT("resolution_quality"), TempNumber))      Levels.ResolutionQuality      = (float)TempNumber;
		if (Params->TryGetNumberField(TEXT("view_distance_quality"), TempNumber))    Levels.ViewDistanceQuality    = (int32)TempNumber;
		if (Params->TryGetNumberField(TEXT("anti_aliasing_quality"), TempNumber))    Levels.AntiAliasingQuality    = (int32)TempNumber;
		if (Params->TryGetNumberField(TEXT("shadow_quality"), TempNumber))           Levels.ShadowQuality          = (int32)TempNumber;
		if (Params->TryGetNumberField(TEXT("global_illumination_quality"), TempNumber)) Levels.GlobalIlluminationQuality = (int32)TempNumber;
		if (Params->TryGetNumberField(TEXT("reflection_quality"), TempNumber))       Levels.ReflectionQuality      = (int32)TempNumber;
		if (Params->TryGetNumberField(TEXT("post_process_quality"), TempNumber))     Levels.PostProcessQuality     = (int32)TempNumber;
		if (Params->TryGetNumberField(TEXT("texture_quality"), TempNumber))          Levels.TextureQuality         = (int32)TempNumber;
		if (Params->TryGetNumberField(TEXT("effects_quality"), TempNumber))          Levels.EffectsQuality         = (int32)TempNumber;
		if (Params->TryGetNumberField(TEXT("foliage_quality"), TempNumber))          Levels.FoliageQuality         = (int32)TempNumber;
		if (Params->TryGetNumberField(TEXT("shading_quality"), TempNumber))          Levels.ShadingQuality         = (int32)TempNumber;

		Scalability::SetQualityLevels(Levels);

		// Optionally save to GameUserSettings
		bool bSave = false;
		Params->TryGetBoolField(TEXT("save_to_ini"), bSave);
		if (bSave)
		{
			UGameUserSettings* UserSettings = GEngine ? GEngine->GetGameUserSettings() : nullptr;
			if (UserSettings)
			{
				UserSettings->SetVisualEffectQuality(Levels.EffectsQuality);
				UserSettings->SetShadowQuality(Levels.ShadowQuality);
				UserSettings->SetAntiAliasingQuality(Levels.AntiAliasingQuality);
				UserSettings->SetTextureQuality(Levels.TextureQuality);
				UserSettings->SetPostProcessingQuality(Levels.PostProcessQuality);
				UserSettings->SetFoliageQuality(Levels.FoliageQuality);
				UserSettings->SetShadingQuality(Levels.ShadingQuality);
				UserSettings->ApplySettings(false);
				UserSettings->SaveSettings();
			}
		}

		UE_LOG(LogAutonomix, Log, TEXT("PerformanceActions: Scalability levels updated (save=%d)"), (int)bSave);

		Result.bSuccess = true;
		Result.ResultMessage = FString::Printf(
			TEXT("Scalability settings applied%s.\nUse get_scalability_settings to verify."),
			bSave ? TEXT(" and saved to GameUserSettings.ini") : TEXT(" (transient — set save_to_ini=true to persist)"));
		return Result;
	}

	// -----------------------------------------------------------------------
	// 14. get_renderer_settings — read URendererSettings properties
	// -----------------------------------------------------------------------
	if (ToolName == TEXT("get_renderer_settings"))
	{
		const URendererSettings* RendSettings = GetDefault<URendererSettings>();
		if (!RendSettings)
		{
			Result.Errors.Add(TEXT("Could not load URendererSettings."));
			return Result;
		}

		// Optional: filter by property name prefix
		FString Filter;
		Params->TryGetStringField(TEXT("filter"), Filter);

		UClass* Class = URendererSettings::StaticClass();
		FString Report = TEXT("URendererSettings (DefaultEngine.ini / [/Script/Engine.RendererSettings]):\n\n");

		for (TFieldIterator<FProperty> PropIt(Class); PropIt; ++PropIt)
		{
			FProperty* Prop = *PropIt;
			if (!(Prop->PropertyFlags & CPF_Config)) continue;

			FString PropName = Prop->GetName();
			if (!Filter.IsEmpty() && !PropName.Contains(Filter)) continue;

			FString ValueStr;
			const void* PropAddr = Prop->ContainerPtrToValuePtr<void>(RendSettings);
			Prop->ExportTextItem_Direct(ValueStr, PropAddr, nullptr, nullptr, PPF_None);

			Report += FString::Printf(TEXT("  %-50s = %s\n"), *PropName, *ValueStr);
		}

		Result.bSuccess = true;
		Result.ResultMessage = Report;
		return Result;
	}

	// -----------------------------------------------------------------------
	// 15. set_renderer_setting — set a URendererSettings property via reflection
	// -----------------------------------------------------------------------
	if (ToolName == TEXT("set_renderer_setting"))
	{
		FString PropName, Value;
		if (!Params->TryGetStringField(TEXT("property"), PropName) ||
			!Params->TryGetStringField(TEXT("value"), Value))
		{
			Result.Errors.Add(TEXT("Missing 'property' or 'value' fields for set_renderer_setting."));
			return Result;
		}

		URendererSettings* RendSettings = GetMutableDefault<URendererSettings>();
		if (!RendSettings)
		{
			Result.Errors.Add(TEXT("Could not get mutable URendererSettings."));
			return Result;
		}

		UClass* Class = URendererSettings::StaticClass();
		FProperty* Prop = Class->FindPropertyByName(FName(*PropName));
		if (!Prop)
		{
			// Try case-insensitive search
			for (TFieldIterator<FProperty> PropIt(Class); PropIt; ++PropIt)
			{
				if ((*PropIt)->GetName().Equals(PropName, ESearchCase::IgnoreCase))
				{
					Prop = *PropIt;
					break;
				}
			}
		}

		if (!Prop)
		{
			Result.Errors.Add(FString::Printf(
				TEXT("Property '%s' not found on URendererSettings. Use get_renderer_settings to list all available properties."),
				*PropName));
			return Result;
		}

		// Apply via reflection
		RendSettings->Modify();
		void* PropAddr = Prop->ContainerPtrToValuePtr<void>(RendSettings);
		Prop->ImportText_Direct(*Value, PropAddr, RendSettings, PPF_None);

		// Notify the editor UI and save to .ini
		FPropertyChangedEvent ChangedEvent(Prop);
		RendSettings->PostEditChangeProperty(ChangedEvent);
		RendSettings->SaveConfig();

		UE_LOG(LogAutonomix, Log, TEXT("PerformanceActions: URendererSettings.%s = %s (saved to DefaultEngine.ini)"),
			*PropName, *Value);

		Result.bSuccess = true;
		Result.ResultMessage = FString::Printf(
			TEXT("URendererSettings.%s = %s\nSaved to DefaultEngine.ini. Editor may require restart for some settings to take effect."),
			*PropName, *Value);
		Result.ModifiedPaths.Add(TEXT("Config/DefaultEngine.ini: [/Script/Engine.RendererSettings]"));
		return Result;
	}

	// -----------------------------------------------------------------------
	// Unknown tool
	// -----------------------------------------------------------------------
	Result.Errors.Add(FString::Printf(TEXT("Unknown performance tool: '%s'. Supported tools: get_performance_stats, get_memory_stats, run_stat_command, analyze_asset_sizes, get_cvar, set_cvar, discover_cvars, execute_console_command, start_csv_profiler, stop_csv_profiler, read_profiling_file, get_scalability_settings, set_scalability_settings, get_renderer_settings, set_renderer_setting"), *ToolName));
	return Result;
}
