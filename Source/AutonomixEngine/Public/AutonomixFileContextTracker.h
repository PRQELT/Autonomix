// Copyright Autonomix. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/**
 * State of a file being tracked in the current session.
 */
struct AUTONOMIXENGINE_API FAutonomixTrackedFile
{
	/** Project-relative path (e.g. "Source/MyActor.cpp") */
	FString RelativePath;

	/** When the AI last read or wrote this file */
	FDateTime LastReadByAI;

	/** When the user last modified this file externally (0 = not modified) */
	FDateTime LastModifiedByUser;

	/** When Autonomix last wrote this file (used to prevent false stale detection) */
	FDateTime LastEditedByAutonomix;

	/** True if the AI's cached copy is likely stale (user edited since last read) */
	bool bIsStale = false;

	FAutonomixTrackedFile() = default;
	explicit FAutonomixTrackedFile(const FString& InPath)
		: RelativePath(InPath)
		, LastReadByAI(FDateTime::MinValue())
		, LastModifiedByUser(FDateTime::MinValue())
		, LastEditedByAutonomix(FDateTime::MinValue())
	{}
};

/**
 * Tracks which files the AI has read/written during a session.
 *
 * When a user modifies a file externally (via the code editor, Live Coding,
 * or any external tool) while the AI has a cached copy, this tracker detects
 * the discrepancy and injects a "stale context" warning into the next API call.
 *
 * This prevents diff-application failures that occur when Claude tries to apply
 * an edit to file content it read several turns ago.
 *
 * Ported and adapted from Roo Code's FileContextTracker.ts.
 *
 * Key behaviors:
 *   - Call OnFileReadByAI() whenever the AI reads a file via any read tool
 *   - Call OnFileEditedByAutonomix() after Autonomix writes a file (prevents false positives)
 *   - Call BuildStaleContextWarning() before each API call to get warning text to prepend
 *   - The warning is auto-cleared after retrieval (so it doesn't repeat every turn)
 *
 * Usage in SAutonomixMainPanel::ContinueAgenticLoop():
 *   FString StaleWarning = FileContextTracker->BuildStaleContextWarning();
 *   if (!StaleWarning.IsEmpty()) {
 *       // Prepend to the tool_result or user message being sent
 *   }
 */
class AUTONOMIXENGINE_API FAutonomixFileContextTracker
{
public:
	FAutonomixFileContextTracker();
	~FAutonomixFileContextTracker();

	// Non-copyable — owns file watcher handles
	FAutonomixFileContextTracker(const FAutonomixFileContextTracker&) = delete;
	FAutonomixFileContextTracker& operator=(const FAutonomixFileContextTracker&) = delete;

	/**
	 * Initialize the tracker for a session.
	 * @param InProjectDirectory  Absolute path to project root
	 */
	void Initialize(const FString& InProjectDirectory);

	/**
	 * Record that the AI read a file (via read_file, get_file_contents, or any read tool).
	 * Sets up a file system watcher for this file.
	 * @param RelativePath  Project-relative path
	 */
	void OnFileReadByAI(const FString& RelativePath);

	/**
	 * Record that Autonomix wrote/modified a file.
	 * Suppresses the stale detection for this file for the next external change.
	 * @param RelativePath  Project-relative path
	 */
	void OnFileEditedByAutonomix(const FString& RelativePath);

	/**
	 * Get the list of files that have become stale since last check.
	 * Clears the stale flags on all returned files.
	 * @return Array of relative paths for stale files
	 */
	TArray<FString> GetAndClearStaleFiles();

	/**
	 * Build a warning string to prepend to the next API message.
	 * Returns empty string if no files are stale.
	 * Clears stale flags on all reported files.
	 *
	 * Example output:
	 *   "NOTE: The following files have been modified externally since you last read them.
	 *    Re-read them before making edits to avoid applying changes to outdated content:
	 *    - Source/MyActor.cpp (modified 12 seconds ago)"
	 */
	FString BuildStaleContextWarning();

	/** Check if any file is currently stale (without clearing) */
	bool HasStaleFiles() const;

	/** Get count of currently tracked files */
	int32 GetTrackedFileCount() const { return TrackedFiles.Num(); }

	/**
	 * Get all currently tracked file paths (relative to project root).
	 * Used by AutonomixCodeStructureParser to generate folded context.
	 * @return Array of project-relative paths for all files the AI has read
	 */
	TArray<FString> GetTrackedPaths() const;

	/** Clear all tracking state (call on new conversation) */
	void Reset();

private:
	/** Called by file system watcher when a file changes on disk */
	void OnFileChangedExternally(const FString& AbsolutePath);

	/** Set up a UE directory watcher for a file's parent directory */
	void RegisterFileWatcher(const FString& RelativePath);

	/** Convert relative path to absolute */
	FString ToAbsolute(const FString& RelativePath) const;

	/** Convert absolute path to relative */
	FString ToRelative(const FString& AbsolutePath) const;

	FString ProjectDirectory;

	/** Map from relative path to tracking state */
	TMap<FString, FAutonomixTrackedFile> TrackedFiles;

	/** Files recently written by Autonomix — prevents false positives from our own writes */
	TSet<FString> RecentlyEditedByAutonomix;

	/** Per-directory watcher handles (we watch at directory level, filter by filename) */
	TMap<FString, FDelegateHandle> DirectoryWatcherHandles;
	bool bWatchersRegistered = false;
};
