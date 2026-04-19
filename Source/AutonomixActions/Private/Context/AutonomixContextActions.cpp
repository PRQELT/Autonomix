// Copyright Autonomix. All Rights Reserved.

#include "Context/AutonomixContextActions.h"
#include "AutonomixCoreModule.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "HAL/PlatformFileManager.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"

/** Directories that should never be listed (matches ContextGatherer exclusions) */
static const TArray<FString> ContextDenyDirs = {
	TEXT("Saved"), TEXT("Intermediate"), TEXT("Binaries"),
	TEXT("DerivedDataCache"), TEXT("DDC"), TEXT(".git"), TEXT(".svn")
};

FAutonomixContextActions::FAutonomixContextActions() {}
FAutonomixContextActions::~FAutonomixContextActions() {}

FName FAutonomixContextActions::GetActionName() const { return FName(TEXT("Context")); }
FText FAutonomixContextActions::GetDisplayName() const { return FText::FromString(TEXT("Context Exploration")); }
EAutonomixActionCategory FAutonomixContextActions::GetCategory() const { return EAutonomixActionCategory::General; }
EAutonomixRiskLevel FAutonomixContextActions::GetDefaultRiskLevel() const { return EAutonomixRiskLevel::Low; }
bool FAutonomixContextActions::CanUndo() const { return false; }
bool FAutonomixContextActions::UndoAction() { return false; }

TArray<FString> FAutonomixContextActions::GetSupportedToolNames() const
{
	return {
		TEXT("list_directory"),
		TEXT("search_assets"),
		TEXT("read_file_snippet")
	};
}

bool FAutonomixContextActions::ValidateParams(const TSharedRef<FJsonObject>& Params, TArray<FString>& OutErrors) const
{
	return true;
}

FAutonomixActionPlan FAutonomixContextActions::PreviewAction(const TSharedRef<FJsonObject>& Params)
{
	FAutonomixActionPlan Plan;
	Plan.Summary = TEXT("Context exploration (read-only)");
	FAutonomixAction Action;
	Action.Description = Plan.Summary;
	Action.Category = EAutonomixActionCategory::General;
	Action.RiskLevel = EAutonomixRiskLevel::Low;
	Plan.Actions.Add(Action);
	return Plan;
}

FAutonomixActionResult FAutonomixContextActions::ExecuteAction(const TSharedRef<FJsonObject>& Params)
{
	FAutonomixActionResult Result;
	Result.bSuccess = false;

	// Determine action from "action" field, or fall back to "tool_name" (injected by the client)
	// Some providers (DeepSeek) may not include const-defined schema fields in their arguments.
	FString Action;
	if (!Params->TryGetStringField(TEXT("action"), Action) || Action.IsEmpty())
	{
		Params->TryGetStringField(TEXT("tool_name"), Action);
	}

	if (!Action.IsEmpty())
	{
		if (Action == TEXT("list_directory"))
			return ExecuteListDirectory(Params, Result);
		else if (Action == TEXT("search_assets"))
			return ExecuteSearchAssets(Params, Result);
		else if (Action == TEXT("read_file_snippet"))
			return ExecuteReadFileSnippet(Params, Result);
	}

	// Try to infer from params
	if (Params->HasField(TEXT("directory")))
		return ExecuteListDirectory(Params, Result);
	else if (Params->HasField(TEXT("query")) || Params->HasField(TEXT("class_filter")))
		return ExecuteSearchAssets(Params, Result);
	else if (Params->HasField(TEXT("file_path")))
		return ExecuteReadFileSnippet(Params, Result);

	Result.Errors.Add(TEXT("Could not determine context action. Use 'action' field or provide 'directory'/'query'/'file_path'."));
	return Result;
}

bool FAutonomixContextActions::IsPathSafe(const FString& RelativePath) const
{
	// Block path traversal attacks
	if (RelativePath.Contains(TEXT("..")) || RelativePath.Contains(TEXT("~")))
	{
		return false;
	}

	// Block denied directories
	FString FirstSegment = RelativePath;
	int32 SlashPos;
	if (FirstSegment.FindChar(TEXT('/'), SlashPos))
	{
		FirstSegment = FirstSegment.Left(SlashPos);
	}
	if (FirstSegment.FindChar(TEXT('\\'), SlashPos))
	{
		FirstSegment = FirstSegment.Left(SlashPos);
	}

	for (const FString& Deny : ContextDenyDirs)
	{
		if (FirstSegment.Equals(Deny, ESearchCase::IgnoreCase))
		{
			return false;
		}
	}

	return true;
}

// ============================================================================
// list_directory: List files and folders in a project-relative path
// ============================================================================

FAutonomixActionResult FAutonomixContextActions::ExecuteListDirectory(const TSharedRef<FJsonObject>& Params, FAutonomixActionResult& Result)
{
	FString RelativeDir;
	if (!Params->TryGetStringField(TEXT("directory"), RelativeDir))
	{
		RelativeDir = TEXT(""); // Root
	}

	if (!IsPathSafe(RelativeDir))
	{
		Result.Errors.Add(FString::Printf(TEXT("Path '%s' is not allowed (blocked directory or path traversal)."), *RelativeDir));
		return Result;
	}

	FString FullPath = FPaths::Combine(FPaths::ProjectDir(), RelativeDir);
	if (!FPaths::DirectoryExists(FullPath))
	{
		Result.Errors.Add(FString::Printf(TEXT("Directory not found: %s"), *RelativeDir));
		return Result;
	}

	// List top-level contents (non-recursive)
	TArray<FString> Directories;
	TArray<FString> Files;
	IFileManager::Get().FindFiles(Files, *FPaths::Combine(FullPath, TEXT("*")), true, false);
	IFileManager::Get().FindFiles(Directories, *FPaths::Combine(FullPath, TEXT("*")), false, true);

	FString Output = FString::Printf(TEXT("=== Directory: %s ===\n"), RelativeDir.IsEmpty() ? TEXT("(project root)") : *RelativeDir);

	// Filter and list directories
	for (const FString& Dir : Directories)
	{
		bool bDenied = false;
		for (const FString& Deny : ContextDenyDirs)
		{
			if (Dir.Equals(Deny, ESearchCase::IgnoreCase)) { bDenied = true; break; }
		}
		if (!bDenied)
		{
			Output += FString::Printf(TEXT("[DIR] %s/\n"), *Dir);
		}
	}

	// List files (cap at 200 entries)
	int32 FileCount = FMath::Min(Files.Num(), 200);
	for (int32 i = 0; i < FileCount; ++i)
	{
		int64 FileSize = IFileManager::Get().FileSize(*FPaths::Combine(FullPath, Files[i]));
		Output += FString::Printf(TEXT("  %s (%lld bytes)\n"), *Files[i], FileSize);
	}

	if (Files.Num() > 200)
	{
		Output += FString::Printf(TEXT("\n... and %d more files (showing first 200)\n"), Files.Num() - 200);
	}

	Output += FString::Printf(TEXT("\nTotal: %d directories, %d files\n"), Directories.Num(), Files.Num());

	Result.bSuccess = true;
	Result.ResultMessage = Output;
	return Result;
}

// ============================================================================
// search_assets: Search the asset registry by class, name, or path
// ============================================================================

FAutonomixActionResult FAutonomixContextActions::ExecuteSearchAssets(const TSharedRef<FJsonObject>& Params, FAutonomixActionResult& Result)
{
	FString Query, ClassFilter, PathFilter;
	Params->TryGetStringField(TEXT("query"), Query);
	Params->TryGetStringField(TEXT("class_filter"), ClassFilter);
	Params->TryGetStringField(TEXT("path_filter"), PathFilter);

	int32 MaxResults = 50;
	Params->TryGetNumberField(TEXT("max_results"), MaxResults);
	MaxResults = FMath::Clamp(MaxResults, 1, 200);

	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();

	TArray<FAssetData> AllAssets;
	AssetRegistry.GetAllAssets(AllAssets, true);

	FString Output = TEXT("=== Asset Search Results ===\n");
	int32 MatchCount = 0;

	for (const FAssetData& Asset : AllAssets)
	{
		if (MatchCount >= MaxResults) break;

		FString AssetPath = Asset.GetObjectPathString();
		FString AssetName = Asset.AssetName.ToString();
		FString AssetClass = Asset.AssetClassPath.GetAssetName().ToString();

		// Only project assets
		if (!AssetPath.StartsWith(TEXT("/Game/"))) continue;

		// Apply filters
		if (!ClassFilter.IsEmpty() && !AssetClass.Contains(ClassFilter, ESearchCase::IgnoreCase))
			continue;
		if (!PathFilter.IsEmpty() && !AssetPath.Contains(PathFilter, ESearchCase::IgnoreCase))
			continue;
		if (!Query.IsEmpty() && !AssetName.Contains(Query, ESearchCase::IgnoreCase) && !AssetPath.Contains(Query, ESearchCase::IgnoreCase))
			continue;

		Output += FString::Printf(TEXT("  %s [%s] — %s\n"), *AssetName, *AssetClass, *AssetPath);
		MatchCount++;
	}

	if (MatchCount == 0)
	{
		Output += TEXT("  (no matching assets found)\n");
	}
	else
	{
		Output += FString::Printf(TEXT("\nFound %d matches"), MatchCount);
		if (MatchCount >= MaxResults)
		{
			Output += FString::Printf(TEXT(" (capped at %d, use filters to narrow)"), MaxResults);
		}
		Output += TEXT("\n");
	}

	Result.bSuccess = true;
	Result.ResultMessage = Output;
	return Result;
}

// ============================================================================
// read_file_snippet: Read a section of a source file with line range
// ============================================================================

FAutonomixActionResult FAutonomixContextActions::ExecuteReadFileSnippet(const TSharedRef<FJsonObject>& Params, FAutonomixActionResult& Result)
{
	FString FilePath;
	if (!Params->TryGetStringField(TEXT("file_path"), FilePath))
	{
		Result.Errors.Add(TEXT("Missing required field: file_path"));
		return Result;
	}

	if (!IsPathSafe(FilePath))
	{
		Result.Errors.Add(FString::Printf(TEXT("Path '%s' is not allowed."), *FilePath));
		return Result;
	}

	// Resolve relative to project dir
	FString FullPath = FPaths::Combine(FPaths::ProjectDir(), FilePath);
	if (!FPaths::FileExists(FullPath))
	{
		// Try Source dir
		FullPath = FPaths::Combine(FPaths::GameSourceDir(), FilePath);
		if (!FPaths::FileExists(FullPath))
		{
			Result.Errors.Add(FString::Printf(TEXT("File not found: %s"), *FilePath));
			return Result;
		}
	}

	// Only allow reading text-like files
	FString Ext = FPaths::GetExtension(FullPath).ToLower();
	static const TSet<FString> AllowedExts = { TEXT("h"), TEXT("cpp"), TEXT("cs"), TEXT("ini"), TEXT("json"), TEXT("txt"), TEXT("xml"), TEXT("yaml"), TEXT("md"), TEXT("py"), TEXT("uplugin"), TEXT("uproject") };
	if (!AllowedExts.Contains(Ext))
	{
		Result.Errors.Add(FString::Printf(TEXT("Cannot read binary file type: .%s"), *Ext));
		return Result;
	}

	FString FileContent;
	if (!FFileHelper::LoadFileToString(FileContent, *FullPath))
	{
		Result.Errors.Add(FString::Printf(TEXT("Failed to read file: %s"), *FilePath));
		return Result;
	}

	// Parse optional line range
	int32 StartLine = 1, EndLine = 0;
	Params->TryGetNumberField(TEXT("start_line"), StartLine);
	Params->TryGetNumberField(TEXT("end_line"), EndLine);
	StartLine = FMath::Max(1, StartLine);

	// Split into lines
	TArray<FString> Lines;
	FileContent.ParseIntoArrayLines(Lines);

	if (EndLine <= 0) EndLine = FMath::Min(StartLine + 99, Lines.Num()); // Default: 100 lines
	EndLine = FMath::Min(EndLine, Lines.Num());
	EndLine = FMath::Min(EndLine, StartLine + 499); // Hard cap: 500 lines per read

	FString Output = FString::Printf(TEXT("=== %s (lines %d-%d of %d) ===\n"), *FilePath, StartLine, EndLine, Lines.Num());

	for (int32 i = StartLine - 1; i < EndLine; ++i)
	{
		Output += FString::Printf(TEXT("%4d | %s\n"), i + 1, *Lines[i]);
	}

	if (EndLine < Lines.Num())
	{
		Output += FString::Printf(TEXT("\n... %d more lines. Use start_line=%d to continue reading.\n"), Lines.Num() - EndLine, EndLine + 1);
	}

	Result.bSuccess = true;
	Result.ResultMessage = Output;
	return Result;
}
