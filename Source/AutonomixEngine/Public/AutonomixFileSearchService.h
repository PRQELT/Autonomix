// Copyright Autonomix. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/**
 * A single file search result match.
 */
struct AUTONOMIXENGINE_API FAutonomixSearchMatch
{
	/** Relative file path */
	FString FilePath;

	/** 1-based line number of the match */
	int32 LineNumber = 0;

	/** The matched line content */
	FString MatchLine;

	/** Context lines before the match (up to 2) */
	TArray<FString> ContextBefore;

	/** Context lines after the match (up to 2) */
	TArray<FString> ContextAfter;
};

/**
 * Result of a file content search operation.
 */
struct AUTONOMIXENGINE_API FAutonomixSearchResult
{
	/** Whether the search succeeded */
	bool bSuccess = false;

	/** All matches found */
	TArray<FAutonomixSearchMatch> Matches;

	/** Total number of files searched */
	int32 FilesSearched = 0;

	/** Total number of files with matches */
	int32 FilesWithMatches = 0;

	/** Error message if search failed */
	FString ErrorMessage;

	/**
	 * Format results in ripgrep style (pipe-bordered context blocks).
	 * Matches Roo Code's ripgrep output format for compatibility.
	 *
	 * Output format:
	 *   Source/MyActor.cpp
	 *   │----
	 *   │void AMyActor::BeginPlay()
	 *   │{
	 *   │    // TODO: fix this
	 *   │----
	 */
	FString FormatAsRipgrepOutput() const;
};

/**
 * File content search service using regex pattern matching.
 *
 * Ported from Roo Code's ripgrep service (`src/services/ripgrep/index.ts`).
 *
 * Roo Code uses the `rg` binary for fast regex searching. Autonomix implements
 * the same functionality using UE's native file I/O and a simple regex-like
 * pattern matcher. For very large projects, this can be upgraded to spawn
 * rg.exe via FPlatformProcess.
 *
 * SEARCH BEHAVIOR:
 *   - Searches all files matching filePattern (glob syntax) under directoryPath
 *   - Returns matches with 2 lines of context before/after
 *   - Skips binary files (.uasset, .pak, .bin, etc.)
 *   - Respects IgnoreController patterns
 *   - Case-insensitive by default
 *
 * BACKED BY:
 *   1. Native FRegexPattern (UE built-in, for simple patterns)
 *   2. FPlatformProcess + rg.exe (if ripgrep is installed, for complex patterns)
 *
 * USAGE (via the search_files tool):
 *   FAutonomixFileSearchService Svc;
 *   FAutonomixSearchResult R = Svc.SearchFiles(
 *       FPaths::ProjectDir(),
 *       TEXT("Source/"),
 *       TEXT("TODO.*Optimize"),
 *       TEXT("*.cpp")
 *   );
 *   FString Output = R.FormatAsRipgrepOutput();
 */
class AUTONOMIXENGINE_API FAutonomixFileSearchService
{
public:
	FAutonomixFileSearchService();

	/** Maximum number of matches to return (prevent huge outputs) */
	static constexpr int32 MaxMatches = 500;

	/** Maximum lines per file to read (skip very large files) */
	static constexpr int32 MaxFileSizeKB = 512;

	/** Context lines before/after each match */
	static constexpr int32 ContextLines = 2;

	/**
	 * Search files for a regex pattern.
	 *
	 * @param ProjectRoot     Absolute path to project root
	 * @param DirectoryPath   Directory to search (relative to ProjectRoot, or absolute)
	 * @param RegexPattern    Pattern to search for (UE FRegexPattern syntax)
	 * @param FilePattern     Glob pattern to filter files (e.g., "*.cpp", "*.h", "*.*")
	 * @param bCaseSensitive  Whether search is case-sensitive (default: false)
	 * @return Search results with all matches and formatted output
	 */
	FAutonomixSearchResult SearchFiles(
		const FString& ProjectRoot,
		const FString& DirectoryPath,
		const FString& RegexPattern,
		const FString& FilePattern = TEXT("*.*"),
		bool bCaseSensitive = false
	) const;

private:
	/** Check if a file extension is binary (skip binary files) */
	static bool IsBinaryExtension(const FString& Extension);

	/** Check if a glob pattern matches a filename */
	static bool MatchesGlob(const FString& FileName, const FString& GlobPattern);

	/** Search a single file for the pattern */
	bool SearchSingleFile(
		const FString& AbsFilePath,
		const FString& RelFilePath,
		const FString& RegexPattern,
		bool bCaseSensitive,
		TArray<FAutonomixSearchMatch>& OutMatches,
		int32& InOutMatchCount
	) const;
};
