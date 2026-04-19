// Copyright Autonomix. All Rights Reserved.

#include "AutonomixContextGatherer.h"
#include "AutonomixCoreModule.h"
#include "AutonomixSettings.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "Misc/EngineVersion.h"
#include "HAL/PlatformFileManager.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "UObject/UObjectIterator.h"
#include "Engine/World.h"
#include "Editor.h"

FAutonomixContextGatherer::FAutonomixContextGatherer()
	: LastCaptureTime(FDateTime::MinValue())
{
}

FAutonomixContextGatherer::~FAutonomixContextGatherer()
{
}

// ============================================================================
// Directory Visitor with hard-ignore exclusions (Gemini fix)
// ============================================================================

/** Directories that must NEVER be iterated — they can freeze the editor and blow tokens */
static const TArray<FString> HardIgnoreDirs = {
	TEXT("Saved"),
	TEXT("Intermediate"),
	TEXT("Binaries"),
	TEXT("DerivedDataCache"),
	TEXT("DDC"),
	TEXT(".git"),
	TEXT(".svn"),
	TEXT("__ExternalActors__"),
	TEXT("__ExternalObjects__")
};

static bool ShouldIgnoreDirectory(const FString& DirPath)
{
	FString DirName = FPaths::GetCleanFilename(DirPath);
	for (const FString& Ignored : HardIgnoreDirs)
	{
		if (DirName.Equals(Ignored, ESearchCase::IgnoreCase))
		{
			return true;
		}
	}
	return false;
}

class FAutonomixDirectoryVisitor : public IPlatformFile::FDirectoryVisitor
{
public:
	TArray<FAutonomixFileEntry>& Entries;
	FString BasePath;
	TSet<FString> AllowedExtensions;
	int32 MaxEntries;

	FAutonomixDirectoryVisitor(TArray<FAutonomixFileEntry>& InEntries, const FString& InBasePath,
		const TSet<FString>& InAllowedExts, int32 InMaxEntries = 2000)
		: Entries(InEntries), BasePath(InBasePath), AllowedExtensions(InAllowedExts), MaxEntries(InMaxEntries) {}

	virtual bool Visit(const TCHAR* FilenameOrDirectory, bool bIsDirectory) override
	{
		// Hard cap on entries to prevent token explosion
		if (Entries.Num() >= MaxEntries) return false;

		FString FullPath = FString(FilenameOrDirectory);
		FString RelativePath = FullPath;
		FPaths::MakePathRelativeTo(RelativePath, *BasePath);

		if (bIsDirectory)
		{
			// CRITICAL FIX (Gemini): Hard-ignore Saved/Intermediate/Binaries/DerivedDataCache
			// Iterating Intermediate alone can freeze the editor and blow past 200K tokens
			if (ShouldIgnoreDirectory(FullPath))
			{
				return true; // Continue but skip this directory's contents
			}

			FAutonomixFileEntry Entry;
			Entry.Path = RelativePath;
			Entry.bIsDirectory = true;
			Entries.Add(Entry);
		}
		else
		{
			FString Extension = FPaths::GetExtension(FullPath).ToLower();
			if (AllowedExtensions.Num() == 0 || AllowedExtensions.Contains(Extension))
			{
				FAutonomixFileEntry Entry;
				Entry.Path = RelativePath;
				Entry.Extension = Extension;
				Entry.bIsDirectory = false;
				Entry.FileSize = IFileManager::Get().FileSize(*FullPath);
				Entries.Add(Entry);
			}
		}
		return true; // Continue iteration
	}
};

// ============================================================================
// Public API
// ============================================================================

FString FAutonomixContextGatherer::BuildContextString()
{
	FAutonomixProjectContext Ctx = BuildProjectContext();

	// CRITICAL FIX (ChatGPT): Enforce token cap on context string
	// For 1000+ asset projects this could exceed 200k quickly
	const UAutonomixDeveloperSettings* Settings = UAutonomixDeveloperSettings::Get();
	int32 TokenBudget = Settings ? Settings->ContextTokenBudget : 30000;

	FString FullContext = Ctx.ToContextString();
	int32 EstTokens = FullContext.Len() / 4; // ~4 chars per token

	if (EstTokens > TokenBudget)
	{
		// Truncate to fit budget with a clear truncation notice
		int32 MaxChars = TokenBudget * 4;
		FullContext = FullContext.Left(MaxChars);
		FullContext += TEXT("\n\n[CONTEXT TRUNCATED — project has more files/assets than token budget allows. Consider enabling Context-as-Tools for large projects.]");

		UE_LOG(LogAutonomix, Warning, TEXT("ContextGatherer: Context truncated from ~%d to ~%d tokens (budget: %d)"),
			EstTokens, TokenBudget, TokenBudget);
	}

	return FullContext;
}

FString FAutonomixContextGatherer::GetFileTreeString()
{
	FAutonomixProjectContext Ctx = BuildProjectContext();
	FString Result = TEXT("=== Content Directory ===\n");
	for (const FAutonomixFileEntry& Entry : Ctx.ContentTree)
	{
		if (Entry.bIsDirectory)
			Result += FString::Printf(TEXT("[DIR] %s\n"), *Entry.Path);
		else
			Result += FString::Printf(TEXT("  %s (%lld bytes)\n"), *Entry.Path, Entry.FileSize);
	}

	Result += TEXT("\n=== Source Directory ===\n");
	for (const FAutonomixFileEntry& Entry : Ctx.SourceTree)
	{
		if (Entry.bIsDirectory)
			Result += FString::Printf(TEXT("[DIR] %s\n"), *Entry.Path);
		else
			Result += FString::Printf(TEXT("  %s\n"), *Entry.Path);
	}

	return Result;
}

FString FAutonomixContextGatherer::GetAssetSummaryString()
{
	FAutonomixProjectContext Ctx = BuildProjectContext();
	FString Result = TEXT("=== Asset Summary ===\n");
	Result += FString::Printf(TEXT("Total assets: %d\n"), Ctx.Assets.Num());

	for (const auto& Pair : Ctx.AssetCountsByClass)
	{
		Result += FString::Printf(TEXT("  %s: %d\n"), *Pair.Key, Pair.Value);
	}

	return Result;
}

FString FAutonomixContextGatherer::GetSettingsSnapshotString()
{
	return TEXT("=== Settings Snapshot ===\n(Will be populated in Phase 13)");
}

FString FAutonomixContextGatherer::GetClassHierarchyString()
{
	FString Result = TEXT("=== Project Class Hierarchy ===\n");

	FString ProjectModuleName = FApp::GetProjectName();

	for (TObjectIterator<UClass> It; It; ++It)
	{
		UClass* Class = *It;
		if (!Class || !Class->GetPackage()) continue;

		FString PackageName = Class->GetPackage()->GetName();

		if (PackageName.StartsWith(TEXT("/Game")) || PackageName.Contains(ProjectModuleName))
		{
			FString ParentName = Class->GetSuperClass() ? Class->GetSuperClass()->GetName() : TEXT("None");
			Result += FString::Printf(TEXT("  %s : %s\n"), *Class->GetName(), *ParentName);
		}
	}

	return Result;
}

int32 FAutonomixContextGatherer::EstimateTokenCount() const
{
	return CachedContext.EstimateTokenCount();
}

FAutonomixProjectContext FAutonomixContextGatherer::BuildProjectContext()
{
	// Check cache validity
	FDateTime Now = FDateTime::UtcNow();
	if ((Now - LastCaptureTime).GetTotalSeconds() < CacheValiditySeconds)
	{
		return CachedContext;
	}

	FAutonomixProjectContext Ctx;
	Ctx.CaptureTimestamp = Now;

	// ---- Project Info ----
	Ctx.ProjectName = FApp::GetProjectName();
	Ctx.EngineVersion = FEngineVersion::Current().ToString();
	Ctx.ProjectRootPath = FPaths::ProjectDir();

	// ---- Current Level ----
	if (GEditor && GEditor->GetEditorWorldContext().World())
	{
		UWorld* World = GEditor->GetEditorWorldContext().World();
		Ctx.CurrentLevelPath = World->GetMapName();
	}

	// ---- File Trees (with hard-ignore directories) ----
	{
		TSet<FString> ContentExts;
		ContentExts.Add(TEXT("uasset"));
		ContentExts.Add(TEXT("umap"));
		FAutonomixDirectoryVisitor ContentVisitor(Ctx.ContentTree, FPaths::ProjectContentDir(), ContentExts);
		IFileManager::Get().IterateDirectoryRecursively(*FPaths::ProjectContentDir(), ContentVisitor);
	}
	{
		TSet<FString> SourceExts;
		SourceExts.Add(TEXT("h"));
		SourceExts.Add(TEXT("cpp"));
		SourceExts.Add(TEXT("cs"));
		FAutonomixDirectoryVisitor SourceVisitor(Ctx.SourceTree, FPaths::GameSourceDir(), SourceExts);
		if (FPaths::DirectoryExists(FPaths::GameSourceDir()))
		{
			IFileManager::Get().IterateDirectoryRecursively(*FPaths::GameSourceDir(), SourceVisitor);
		}
	}
	{
		TSet<FString> ConfigExts;
		ConfigExts.Add(TEXT("ini"));
		FAutonomixDirectoryVisitor ConfigVisitor(Ctx.ConfigTree, FPaths::ProjectConfigDir(), ConfigExts);
		IFileManager::Get().IterateDirectoryRecursively(*FPaths::ProjectConfigDir(), ConfigVisitor);
	}

	// ---- Asset Registry ----
	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();

	TArray<FAssetData> AllAssets;
	AssetRegistry.GetAllAssets(AllAssets, true);

	for (const FAssetData& AssetData : AllAssets)
	{
		FString AssetPath = AssetData.GetObjectPathString();
		if (!AssetPath.StartsWith(TEXT("/Game/"))) continue;

		FAutonomixAssetEntry Entry;
		Entry.AssetPath = AssetPath;
		Entry.AssetName = AssetData.AssetName.ToString();
		Entry.AssetClass = AssetData.AssetClassPath.GetAssetName().ToString();
		Ctx.Assets.Add(Entry);

		int32& Count = Ctx.AssetCountsByClass.FindOrAdd(Entry.AssetClass);
		Count++;
	}

	// ---- Modules ----
	TArray<FString> ModuleDirs;
	if (FPaths::DirectoryExists(FPaths::GameSourceDir()))
	{
		IFileManager::Get().FindFiles(ModuleDirs, *FPaths::GameSourceDir(), false, true);
		for (const FString& Dir : ModuleDirs)
		{
			Ctx.ProjectModules.Add(Dir);
		}
	}

	// Cache result
	CachedContext = Ctx;
	LastCaptureTime = Now;

	UE_LOG(LogAutonomix, Log, TEXT("ContextGatherer: Built context — %d content files, %d source files, %d assets, %d modules"),
		Ctx.ContentTree.Num(), Ctx.SourceTree.Num(), Ctx.Assets.Num(), Ctx.ProjectModules.Num());

	return Ctx;
}
