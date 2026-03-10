// Copyright Autonomix. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/**
 * A single saved checkpoint (git commit in the shadow repo).
 */
struct AUTONOMIXENGINE_API FAutonomixCheckpoint
{
	/** Git commit hash in the shadow repository */
	FString CommitHash;

	/** When the checkpoint was saved */
	FDateTime Timestamp;

	/** Human-readable description (e.g., "Before tool batch 3") */
	FString Description;

	/** Files modified up to and including this checkpoint */
	TArray<FString> ModifiedFiles;

	/** Agentic loop iteration this checkpoint was taken at */
	int32 LoopIteration = 0;

	FAutonomixCheckpoint() = default;
};

/**
 * Diff information between two checkpoints.
 */
struct AUTONOMIXENGINE_API FAutonomixCheckpointDiff
{
	/** Whether the diff was computed successfully */
	bool bSuccess = false;

	/** From commit hash */
	FString FromHash;

	/** To commit hash */
	FString ToHash;

	/** Files changed between the two commits */
	TArray<FString> ChangedFiles;

	/** Full unified diff text */
	FString DiffText;

	FString ErrorMessage;
};

/**
 * Git-based checkpoint system for Autonomix sessions.
 *
 * Creates a shadow git repository in Saved/Autonomix/Checkpoints/[SessionId]/
 * that mirrors the project's tracked file state. At each tool execution batch,
 * a git commit is created. This allows:
 *
 *   1. checkpointSave()    — commit current state to shadow repo
 *   2. checkpointRestore() — restore files to a previous checkpoint
 *   3. checkpointDiff()    — show what changed between any two checkpoints
 *
 * Ported and adapted from Roo Code's checkpoints/index.ts:
 *   - Uses shadow git repo (not the project's own git repo)
 *   - Supports 4 diff modes: checkpoint, from-init, to-current, full
 *   - Restore does NOT rewind conversation history (that's handled by SAutonomixMainPanel)
 *
 * REQUIREMENTS:
 *   - Git must be installed and on PATH
 *   - Project files must be writable
 *
 * SHADOW REPO LOCATION:
 *   [ProjectRoot]/Saved/Autonomix/Checkpoints/[SessionId]/
 *
 * FILES TRACKED:
 *   - Source/ (all .h and .cpp)
 *   - Config/ (all .ini)
 *   - Content/ (excludes .uasset/.umap binaries, tracks text assets only)
 *   - Limited to files <= 1MB (binary/huge files excluded)
 */
class AUTONOMIXENGINE_API FAutonomixCheckpointManager
{
public:
	FAutonomixCheckpointManager();
	~FAutonomixCheckpointManager();

	// Non-copyable
	FAutonomixCheckpointManager(const FAutonomixCheckpointManager&) = delete;
	FAutonomixCheckpointManager& operator=(const FAutonomixCheckpointManager&) = delete;

	// ---- Initialization ----

	/**
	 * Initialize the shadow git repository for this session.
	 * Creates the repo if it doesn't exist. Sets up .gitignore for binary files.
	 *
	 * @param InSessionId       Unique ID for this conversation session (used as repo folder name)
	 * @param InProjectRoot     Absolute path to the UE project root
	 * @return true if initialization succeeded
	 */
	bool Initialize(const FString& InSessionId, const FString& InProjectRoot);

	/** Whether the checkpoint system is initialized and ready */
	bool IsInitialized() const { return bIsInitialized; }

	// ---- Core operations ----

	/**
	 * Save current project state as a checkpoint.
	 * Copies tracked files to the shadow repo and creates a git commit.
	 *
	 * @param Description       Human-readable label (e.g., "Before tool batch 3")
	 * @param LoopIteration     Current agentic loop iteration
	 * @param OutCheckpoint     The saved checkpoint info
	 * @return true if checkpoint was saved successfully
	 */
	bool SaveCheckpoint(
		const FString& Description,
		int32 LoopIteration,
		FAutonomixCheckpoint& OutCheckpoint
	);

	/**
	 * Restore project files to the state at a given checkpoint.
	 * Copies files from the shadow repo back to the project.
	 * Does NOT touch the Autonomix conversation history.
	 *
	 * @param CommitHash    The git commit hash to restore to
	 * @param OutRestoredFiles  Files that were restored
	 * @return true if restore succeeded
	 */
	bool RestoreToCheckpoint(const FString& CommitHash, TArray<FString>& OutRestoredFiles);

	/**
	 * Compute a diff between two checkpoints.
	 *
	 * @param FromHash      Starting commit (older)
	 * @param ToHash        Ending commit (newer, empty = current working tree)
	 * @param DiffMode      "checkpoint" | "from-init" | "to-current" | "full"
	 * @return Diff info struct
	 */
	FAutonomixCheckpointDiff GetDiff(
		const FString& FromHash,
		const FString& ToHash,
		const FString& DiffMode = TEXT("checkpoint")
	);

	// ---- Query ----

	/** Get all checkpoints in this session, oldest first */
	TArray<FAutonomixCheckpoint> GetAllCheckpoints() const;

	/** Get the most recent checkpoint */
	const FAutonomixCheckpoint* GetLatestCheckpoint() const;

	/** Get a checkpoint by commit hash */
	const FAutonomixCheckpoint* GetCheckpointByHash(const FString& CommitHash) const;

	/** Get the initial (first) checkpoint hash */
	FString GetInitialCommitHash() const;

	/** Number of checkpoints saved this session */
	int32 GetCheckpointCount() const { return Checkpoints.Num(); }

	/** Get the shadow repo directory path */
	FString GetShadowRepoPath() const { return ShadowRepoPath; }

	// ---- Utilities ----

	/** Check if git is installed and available on PATH */
	static bool IsGitAvailable();

	/** Get the git version string (for diagnostics) */
	static FString GetGitVersion();

private:
	/**
	 * Run a git command in the shadow repo directory.
	 * @param Args      Git command arguments (e.g., "add -A" or "commit -m \"Message\"")
	 * @param Output    Stdout from git
	 * @param ErrOutput Stderr from git
	 * @return Exit code (0 = success)
	 */
	int32 RunGitCommand(const FString& Args, FString& Output, FString& ErrOutput) const;

	/** Copy tracked files from project to shadow repo */
	bool SyncFilesToShadowRepo(TArray<FString>& OutSyncedFiles);

	/** Copy files from shadow repo back to project (for restore) */
	bool SyncFilesFromShadowRepo(const FString& CommitHash, TArray<FString>& OutRestoredFiles);

	/** Get list of files to track (Source/, Config/, small text files) */
	TArray<FString> GatherTrackedFiles() const;

	/** Check if a file should be tracked by checkpoints */
	bool ShouldTrackFile(const FString& AbsolutePath) const;

	/** Build the shadow repo's .gitignore content */
	static FString GetShadowRepoGitignore();

	/** Initialize git config for the shadow repo (user name/email) */
	bool ConfigureGitUser();

	/** Get git executable path */
	static FString GetGitPath();

	FString SessionId;
	FString ProjectRoot;
	FString ShadowRepoPath;
	bool bIsInitialized = false;

	TArray<FAutonomixCheckpoint> Checkpoints;
	FString InitialCommitHash;
};
