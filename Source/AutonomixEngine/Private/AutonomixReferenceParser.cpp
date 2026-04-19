// Copyright Autonomix. All Rights Reserved.

#include "AutonomixReferenceParser.h"
#include "AutonomixIgnoreController.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

#if WITH_EDITOR
#include "Editor.h"
#include "Engine/Selection.h"
#include "GameFramework/Actor.h"
#include "Engine/World.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "EngineUtils.h"
#endif

FAutonomixReferenceParser::FAutonomixReferenceParser()
{
}

FAutonomixReferenceParser::~FAutonomixReferenceParser()
{
}

void FAutonomixReferenceParser::SetIgnoreController(FAutonomixIgnoreController* InController)
{
	IgnoreController = InController;
}

// ============================================================================
// Core parse + resolve
// ============================================================================

FAutonomixParseReferencesResult FAutonomixReferenceParser::ParseAndResolve(const FString& InputText) const
{
	FAutonomixParseReferencesResult Result;
	Result.ProcessedText = InputText;

	// Find all @references in the text
	TArray<FString> Refs = ExtractReferences(InputText);

	for (const FString& Ref : Refs)
	{
		FAutonomixResolvedReference Resolved;

		if (Ref.StartsWith(TEXT("/")))
		{
			// File or folder path reference
			FString Path = Ref;
			if (Path.EndsWith(TEXT("/")))
			{
				Resolved = ResolveFolderReference(Path.LeftChop(1));
			}
			else
			{
				// Check if it's a directory
				FString AbsPath = ResolveAbsolutePath(Path);
				if (FPaths::DirectoryExists(AbsPath))
				{
					Resolved = ResolveFolderReference(Path);
				}
				else
				{
					Resolved = ResolveFileReference(Path);
				}
			}
		}
		else
		{
			// Special keyword reference (@errors, @selection, etc.)
			Resolved = ResolveSpecialReference(Ref);
		}

		if (Resolved.bSuccess)
		{
			Result.ResolvedReferences.Add(Resolved);
			Result.TotalEstimatedTokens += Resolved.EstimatedTokens;

			// Replace @ref in processed text with a brief note
			const FString Marker = TEXT("@") + Ref;
			const FString Replacement = FString::Printf(TEXT("[%s - see attached content]"), *Ref);
			Result.ProcessedText.ReplaceInline(*Marker, *Replacement);
		}
		else
		{
			// Replace with error note
			const FString Marker = TEXT("@") + Ref;
			const FString Replacement = FString::Printf(TEXT("[Could not resolve @%s: %s]"),
				*Ref, *Resolved.ErrorMessage);
			Result.ProcessedText.ReplaceInline(*Marker, *Replacement);
		}
	}

	return Result;
}

TArray<FString> FAutonomixReferenceParser::ExtractReferences(const FString& Text) const
{
	TArray<FString> Refs;

	// Find all @xxx patterns
	// Matches: @/path/to/file, @keyword, @asset:Name
	int32 SearchPos = 0;
	while (SearchPos < Text.Len())
	{
		int32 AtPos = Text.Find(TEXT("@"), ESearchCase::CaseSensitive, ESearchDir::FromStart, SearchPos);
		if (AtPos == INDEX_NONE) break;

		// Extract the reference token after @
		int32 EndPos = AtPos + 1;
		while (EndPos < Text.Len())
		{
			TCHAR Ch = Text[EndPos];
			// Reference ends at whitespace or certain punctuation (except / : . - _)
			if (FChar::IsWhitespace(Ch) || Ch == TEXT(',') || Ch == TEXT(')') ||
				Ch == TEXT(']') || Ch == TEXT('"') || Ch == TEXT('\''))
			{
				break;
			}
			EndPos++;
		}

		if (EndPos > AtPos + 1)
		{
			FString Ref = Text.Mid(AtPos + 1, EndPos - AtPos - 1);
			if (!Ref.IsEmpty())
			{
				Refs.AddUnique(Ref);
			}
		}

		SearchPos = AtPos + 1;
	}

	return Refs;
}

// ============================================================================
// File reference resolution
// ============================================================================

FAutonomixResolvedReference FAutonomixReferenceParser::ResolveFileReference(const FString& RelativePath) const
{
	FAutonomixResolvedReference Result;
	Result.OriginalRef = RelativePath;
	Result.Type = EAutonomixReferenceType::File;

	// Check ignore controller
	if (IgnoreController && IgnoreController->IsPathIgnored(RelativePath))
	{
		Result.bSuccess = false;
		Result.ErrorMessage = FString::Printf(TEXT("File '%s' is excluded by .autonomixignore"), *RelativePath);
		return Result;
	}

	const FString AbsPath = ResolveAbsolutePath(RelativePath);

	if (!FPaths::FileExists(AbsPath))
	{
		Result.bSuccess = false;
		Result.ErrorMessage = FString::Printf(TEXT("File not found: %s"), *RelativePath);
		return Result;
	}

	// Check binary file
	if (IsBinaryFile(AbsPath))
	{
		Result.bSuccess = false;
		Result.ErrorMessage = FString::Printf(
			TEXT("File '%s' is binary. Binary assets cannot be included as text context."), *RelativePath);
		return Result;
	}

	// Check file size
	int64 FileSize = IFileManager::Get().FileSize(*AbsPath);
	if (FileSize > MaxFileContentBytes)
	{
		// Include file info but not content
		Result.bSuccess = true;
		Result.Content = FString::Printf(
			TEXT("# File: %s\n**Note:** File is too large to include (%.1f KB > 512 KB limit). "
			     "Summary only:\n- Path: %s\n- Size: %.1f KB\n- Extension: %s"),
			*RelativePath,
			(float)FileSize / 1024.0f,
			*RelativePath,
			(float)FileSize / 1024.0f,
			*FPaths::GetExtension(RelativePath)
		);
		Result.EstimatedTokens = 50;
		return Result;
	}

	// Read file content
	FString FileContent;
	if (!FFileHelper::LoadFileToString(FileContent, *AbsPath))
	{
		Result.bSuccess = false;
		Result.ErrorMessage = FString::Printf(TEXT("Failed to read file: %s"), *RelativePath);
		return Result;
	}

	// Format with code block
	FString Ext = FPaths::GetExtension(RelativePath).ToLower();
	FString LangHint;
	if (Ext == TEXT("cpp") || Ext == TEXT("h")) LangHint = TEXT("cpp");
	else if (Ext == TEXT("cs")) LangHint = TEXT("csharp");
	else if (Ext == TEXT("ini")) LangHint = TEXT("ini");
	else if (Ext == TEXT("json")) LangHint = TEXT("json");
	else LangHint = Ext;

	Result.bSuccess = true;
	Result.Content = FString::Printf(
		TEXT("# File: %s\n```%s\n%s\n```"),
		*RelativePath,
		*LangHint,
		*FileContent
	);
	Result.EstimatedTokens = FileContent.Len() / 4;  // rough estimate
	return Result;
}

FAutonomixResolvedReference FAutonomixReferenceParser::ResolveFolderReference(const FString& RelativePath) const
{
	FAutonomixResolvedReference Result;
	Result.OriginalRef = RelativePath;
	Result.Type = EAutonomixReferenceType::Folder;

	const FString AbsPath = ResolveAbsolutePath(RelativePath);

	if (!FPaths::DirectoryExists(AbsPath))
	{
		Result.bSuccess = false;
		Result.ErrorMessage = FString::Printf(TEXT("Directory not found: %s"), *RelativePath);
		return Result;
	}

	Result.bSuccess = true;
	Result.Content = FString::Printf(TEXT("# Directory: %s\n%s"),
		*RelativePath, *BuildFolderSummary(AbsPath, RelativePath));
	Result.EstimatedTokens = 200;
	return Result;
}

// ============================================================================
// Special keyword resolution
// ============================================================================

FAutonomixResolvedReference FAutonomixReferenceParser::ResolveSpecialReference(const FString& Keyword) const
{
	FAutonomixResolvedReference Result;
	Result.OriginalRef = Keyword;

	const FString KeyLower = Keyword.ToLower();

	if (KeyLower == TEXT("errors") || KeyLower == TEXT("compileerrors"))
	{
		Result.Type = EAutonomixReferenceType::CompileErrors;
		FString Errors = GatherCompileErrors();
		if (Errors.IsEmpty())
		{
			Result.Content = TEXT("# Compile Errors\nNo recent compile errors found.");
		}
		else
		{
			Result.Content = FString::Printf(TEXT("# Compile Errors\n%s"), *Errors);
		}
		Result.bSuccess = true;
		Result.EstimatedTokens = Result.Content.Len() / 4;
	}
	else if (KeyLower == TEXT("selection") || KeyLower == TEXT("selected"))
	{
		Result.Type = EAutonomixReferenceType::Selection;
		FString Selection = GatherSelectionSummary();
		Result.Content = FString::Printf(TEXT("# Selected Actors\n%s"),
			Selection.IsEmpty() ? TEXT("No actors currently selected.") : *Selection);
		Result.bSuccess = true;
		Result.EstimatedTokens = Result.Content.Len() / 4;
	}
	else if (KeyLower == TEXT("level") || KeyLower == TEXT("activelevel"))
	{
		Result.Type = EAutonomixReferenceType::Level;
		FString Level = GatherLevelInfo();
		Result.Content = FString::Printf(TEXT("# Active Level\n%s"),
			Level.IsEmpty() ? TEXT("No level currently open.") : *Level);
		Result.bSuccess = true;
		Result.EstimatedTokens = Result.Content.Len() / 4;
	}
	else if (KeyLower == TEXT("buildlog") || KeyLower == TEXT("build"))
	{
		Result.Type = EAutonomixReferenceType::BuildLog;
		FString Log = GatherBuildLog();
		Result.Content = FString::Printf(TEXT("# Last Build Log\n%s"),
			Log.IsEmpty() ? TEXT("No recent build log available.") : *Log);
		Result.bSuccess = true;
		Result.EstimatedTokens = Result.Content.Len() / 4;
	}
	else if (KeyLower.StartsWith(TEXT("asset:")))
	{
		Result.Type = EAutonomixReferenceType::BlueprintAsset;
		const FString AssetName = Keyword.Mid(6);  // after "asset:"
		Result.Content = FString::Printf(
			TEXT("# Asset: %s\nTo see Blueprint details, use the Blueprint editor to inspect '%s'."),
			*AssetName, *AssetName);
		Result.bSuccess = true;
		Result.EstimatedTokens = 100;
	}
	else
	{
		Result.bSuccess = false;
		Result.ErrorMessage = FString::Printf(
			TEXT("Unknown reference keyword '%s'. Valid keywords: errors, selection, level, buildlog, asset:Name"),
			*Keyword);
	}

	return Result;
}

// ============================================================================
// Autocomplete
// ============================================================================

TArray<FString> FAutonomixReferenceParser::GetAutocompleteSuggestions(const FString& Partial) const
{
	TArray<FString> Suggestions;

	// Special keywords
	static TArray<FString> Keywords = {
		TEXT("errors"), TEXT("selection"), TEXT("level"), TEXT("buildlog"),
		TEXT("project"), TEXT("asset:")
	};

	for (const FString& KW : Keywords)
	{
		if (KW.StartsWith(Partial, ESearchCase::IgnoreCase))
		{
			Suggestions.Add(KW);
		}
	}

	// Path completions
	if (Partial.StartsWith(TEXT("/")))
	{
		FString DirPart = FPaths::GetPath(Partial);
		FString AbsDirPart = ResolveAbsolutePath(DirPart.IsEmpty() ? TEXT("/") : DirPart);

		TArray<FString> Files;
		IFileManager::Get().FindFiles(Files, *FPaths::Combine(AbsDirPart, TEXT("*")), true, true);

		int32 Count = 0;
		for (const FString& File : Files)
		{
			if (Count++ > 20) break;

			FString Suggestion = DirPart + TEXT("/") + File;
			if (Suggestion.StartsWith(Partial, ESearchCase::IgnoreCase))
			{
				Suggestions.Add(Suggestion);
			}
		}
	}

	return Suggestions;
}

// ============================================================================
// Helpers
// ============================================================================

EAutonomixReferenceType FAutonomixReferenceParser::ClassifyReference(const FString& Ref)
{
	if (Ref.StartsWith(TEXT("/")))
	{
		return EAutonomixReferenceType::File;
	}
	const FString Lower = Ref.ToLower();
	if (Lower == TEXT("errors")) return EAutonomixReferenceType::CompileErrors;
	if (Lower == TEXT("selection")) return EAutonomixReferenceType::Selection;
	if (Lower == TEXT("level")) return EAutonomixReferenceType::Level;
	if (Lower == TEXT("buildlog")) return EAutonomixReferenceType::BuildLog;
	if (Lower.StartsWith(TEXT("asset:"))) return EAutonomixReferenceType::BlueprintAsset;
	return EAutonomixReferenceType::File;
}

bool FAutonomixReferenceParser::IsBinaryFile(const FString& AbsolutePath)
{
	FString Ext = FPaths::GetExtension(AbsolutePath).ToLower();
	static TArray<FString> BinaryExts = {
		TEXT("uasset"), TEXT("umap"), TEXT("pak"), TEXT("pdb"),
		TEXT("exe"), TEXT("dll"), TEXT("lib"), TEXT("obj"),
		TEXT("png"), TEXT("jpg"), TEXT("jpeg"), TEXT("bmp"),
		TEXT("tga"), TEXT("dds"), TEXT("wav"), TEXT("mp3"),
		TEXT("fbx"), TEXT("bin"), TEXT("ttf"), TEXT("otf")
	};
	return BinaryExts.Contains(Ext);
}

FString FAutonomixReferenceParser::ResolveAbsolutePath(const FString& RelativePath) const
{
	FString Path = RelativePath;
	// Remove leading /
	if (Path.StartsWith(TEXT("/")))
	{
		Path = Path.Mid(1);
	}
	return FPaths::Combine(ProjectRoot, Path);
}

FString FAutonomixReferenceParser::BuildFolderSummary(const FString& AbsDir, const FString& RelDir) const
{
	TArray<FString> Files;
	IFileManager::Get().FindFilesRecursive(Files, *AbsDir, TEXT("*.*"), true, false);

	if (Files.Num() == 0)
	{
		return TEXT("(empty directory)");
	}

	FString Summary;
	int32 Count = 0;
	for (const FString& File : Files)
	{
		if (Count++ >= MaxFolderListingFiles)
		{
			Summary += FString::Printf(TEXT("\n... and %d more files"), Files.Num() - MaxFolderListingFiles);
			break;
		}

		FString RelPath = File;
		FPaths::MakePathRelativeTo(RelPath, *(ProjectRoot + TEXT("/")));

		if (IgnoreController && IgnoreController->IsPathIgnored(RelPath))
		{
			continue;
		}

		int64 FileSize = IFileManager::Get().FileSize(*File);
		Summary += FString::Printf(TEXT("- %s (%lld bytes)\n"), *RelPath, FileSize);
	}

	return Summary;
}

FString FAutonomixReferenceParser::GatherCompileErrors() const
{
	// This would query UE's MessageLog in a full implementation
	// For now, return a note about where to find errors
	return FString();
}

FString FAutonomixReferenceParser::GatherSelectionSummary() const
{
	FString Summary;

#if WITH_EDITOR
	if (!GEditor) return Summary;

	USelection* Selection = GEditor->GetSelectedActors();
	if (!Selection || Selection->Num() == 0) return Summary;

	Summary += FString::Printf(TEXT("(%d actors selected)\n"), Selection->Num());
	for (FSelectionIterator It(*Selection); It; ++It)
	{
		AActor* Actor = Cast<AActor>(*It);
		if (Actor)
		{
			FVector Loc = Actor->GetActorLocation();
			Summary += FString::Printf(
				TEXT("- %s (%s) at (%.1f, %.1f, %.1f)\n"),
				*Actor->GetActorLabel(),
				*Actor->GetClass()->GetName(),
				Loc.X, Loc.Y, Loc.Z
			);
		}
	}
#endif

	return Summary;
}

FString FAutonomixReferenceParser::GatherLevelInfo() const
{
#if WITH_EDITOR
	if (!GEditor) return FString();

	UWorld* World = GEditor->GetEditorWorldContext().World();
	if (!World) return FString();

	FString LevelPath = TEXT("(unknown)");
	if (World->GetCurrentLevel())
	{
		LevelPath = World->GetCurrentLevel()->GetOutermost()->GetName();
	}

	int32 ActorCount = 0;
	for (TActorIterator<AActor> It(World); It; ++It) { ActorCount++; }

	return FString::Printf(TEXT("Level: %s\nActor count: %d"), *LevelPath, ActorCount);
#else
	return FString();
#endif
}

FString FAutonomixReferenceParser::GatherBuildLog() const
{
	// Look for recent build logs in Saved/Logs/
	const FString LogDir = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Logs"));
	TArray<FString> LogFiles;
	IFileManager::Get().FindFiles(LogFiles, *FPaths::Combine(LogDir, TEXT("*.log")), true, false);

	if (LogFiles.Num() == 0) return FString();

	// Get the most recent log file
	FString MostRecentLog;
	FDateTime MostRecentTime = FDateTime::MinValue();
	for (const FString& LogFile : LogFiles)
	{
		FString FullPath = FPaths::Combine(LogDir, LogFile);
		FDateTime ModTime = IFileManager::Get().GetTimeStamp(*FullPath);
		if (ModTime > MostRecentTime)
		{
			MostRecentTime = ModTime;
			MostRecentLog = FullPath;
		}
	}

	if (MostRecentLog.IsEmpty()) return FString();

	// Read last 100 lines
	FString Content;
	FFileHelper::LoadFileToString(Content, *MostRecentLog);

	TArray<FString> Lines;
	Content.ParseIntoArrayLines(Lines, false);

	int32 StartLine = FMath::Max(0, Lines.Num() - 100);
	TArray<FString> LastLines(Lines.GetData() + StartLine, Lines.Num() - StartLine);
	return FString::Join(LastLines, TEXT("\n"));
}
