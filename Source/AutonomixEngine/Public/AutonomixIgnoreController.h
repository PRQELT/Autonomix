// Copyright Autonomix. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/**
 * Controls AI access to files using gitignore-style pattern matching.
 *
 * Reads a ".autonomixignore" file from the project root. Any file or directory
 * matching the patterns is hidden from the AI:
 *   - Excluded from file tree context (AutonomixContextGatherer)
 *   - Blocked from read/write tool operations (AutonomixSafetyGate)
 *   - Filtered from @reference resolution
 *
 * Ported and adapted from Roo Code's RooIgnoreController.ts:
 *   - Uses gitignore glob syntax (*, **, ?, [abc], prefix/)
 *   - Live file watcher reloads patterns when .autonomixignore changes
 *   - Always ignores the .autonomixignore file itself
 *
 * Default patterns (written on first run if no file exists):
 *   *.uasset, *.umap, *.pak, Binaries/, Intermediate/, DerivedDataCache/
 *
 * Usage:
 *   FAutonomixIgnoreController Ctrl;
 *   Ctrl.Initialize(FPaths::ProjectDir());
 *   if (!Ctrl.IsPathIgnored(TEXT("Source/MyActor.h"))) { ... }
 */
class AUTONOMIXENGINE_API FAutonomixIgnoreController
{
public:
	FAutonomixIgnoreController();
	~FAutonomixIgnoreController();

	// Non-copyable — owns file watcher handle
	FAutonomixIgnoreController(const FAutonomixIgnoreController&) = delete;
	FAutonomixIgnoreController& operator=(const FAutonomixIgnoreController&) = delete;

	/**
	 * Initialize the controller. Loads .autonomixignore from the project root.
	 * Creates the file with sensible defaults if it doesn't exist yet.
	 * Sets up a file system watcher to auto-reload on changes.
	 *
	 * @param InProjectDirectory  Absolute path to the project root (FPaths::ProjectDir())
	 * @return true if initialization succeeded
	 */
	bool Initialize(const FString& InProjectDirectory);

	/**
	 * Check whether a relative path is ignored.
	 * The path should be relative to the project root (same convention as FPaths::MakeRelativeTo).
	 *
	 * @param RelativePath  e.g. "Source/MyActor.cpp" or "Content/Blueprints/BP_Hero.uasset"
	 * @return true if AI access to this path is blocked
	 */
	bool IsPathIgnored(const FString& RelativePath) const;

	/**
	 * Filter an array of relative paths, returning only those NOT ignored.
	 * Preserves original order.
	 *
	 * @param Paths  Array of project-relative paths
	 * @return Filtered array with ignored paths removed
	 */
	TArray<FString> FilterPaths(const TArray<FString>& Paths) const;

	/** Get raw content of the .autonomixignore file (for display in settings UI) */
	FString GetIgnoreFileContent() const { return RawContent; }

	/** Get the absolute path to the .autonomixignore file */
	FString GetIgnoreFilePath() const;

	/** Whether the ignore file was found and loaded successfully */
	bool IsLoaded() const { return bIsLoaded; }

	/** Number of active ignore patterns */
	int32 GetPatternCount() const { return IgnorePatterns.Num(); }

	/** Force a reload of the ignore file (called automatically by watcher) */
	void Reload();

	/** Default file content written on first initialization */
	static const TCHAR* GetDefaultIgnoreContent();

private:
	/** Load and parse the .autonomixignore file */
	void LoadIgnoreFile();

	/** Write default .autonomixignore if the file doesn't exist */
	void CreateDefaultIgnoreFile();

	/** Parse raw text content into patterns (strips comments + empty lines) */
	void ParsePatterns(const FString& Content);

	/**
	 * Match a single relative path against a single pattern.
	 * Supports: * (any chars except /), ** (any chars including /), ? (one char),
	 * prefix/ (directory match), leading ! (negation -- not yet implemented).
	 */
	bool MatchesPattern(const FString& RelativePath, const FString& Pattern) const;

	/** Normalize a path: forward slashes, no leading ./ */
	static FString NormalizePath(const FString& Path);

	FString ProjectDirectory;
	TArray<FString> IgnorePatterns;
	FString RawContent;
	bool bIsLoaded = false;

	// File watcher — reloads patterns when .autonomixignore changes on disk
	FDelegateHandle WatcherHandle;
	bool bWatcherRegistered = false;
};
