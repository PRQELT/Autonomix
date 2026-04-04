// Copyright Autonomix. All Rights Reserved.

#include "AutonomixCheckpointManager.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

// Maximum file size to track in shadow repo (1MB)
static constexpr int64 MaxTrackedFileSizeBytes = 1 * 1024 * 1024;

// Timeout for git operations in milliseconds
static constexpr float GitTimeoutSeconds = 30.0f;

FAutonomixCheckpointManager::FAutonomixCheckpointManager()
{
}

FAutonomixCheckpointManager::~FAutonomixCheckpointManager()
{
}

// ============================================================================
// Initialization
// ============================================================================

bool FAutonomixCheckpointManager::Initialize(const FString& InSessionId, const FString& InProjectRoot)
{
	if (!IsGitAvailable())
	{
		UE_LOG(LogTemp, Warning, TEXT("AutonomixCheckpointManager: Git is not available. Checkpoint system disabled."));
		return false;
	}

	SessionId = InSessionId;
	ProjectRoot = FPaths::ConvertRelativePathToFull(InProjectRoot);

	// Shadow repo lives in Saved/Autonomix/Checkpoints/[SessionId]/
	ShadowRepoPath = FPaths::Combine(
		ProjectRoot,
		TEXT("Saved"),
		TEXT("Autonomix"),
		TEXT("Checkpoints"),
		SessionId
	);

	// Create the shadow repo directory
	IFileManager::Get().MakeDirectory(*ShadowRepoPath, true);

	// Check if git repo already exists
	const FString GitDir = FPaths::Combine(ShadowRepoPath, TEXT(".git"));
	const bool bRepoExists = FPaths::DirectoryExists(GitDir);

	if (!bRepoExists)
	{
		// Initialize new git repo
		FString Output, ErrOutput;
		if (RunGitCommand(TEXT("init"), Output, ErrOutput) != 0)
		{
			UE_LOG(LogTemp, Error, TEXT("AutonomixCheckpointManager: Failed to init git repo at %s: %s"),
				*ShadowRepoPath, *ErrOutput);
			return false;
		}

		// Configure git user for this repo
		ConfigureGitUser();

		// Write .gitignore to exclude large binaries
		FString GitignorePath = FPaths::Combine(ShadowRepoPath, TEXT(".gitignore"));
		FFileHelper::SaveStringToFile(GetShadowRepoGitignore(), *GitignorePath);

		// Create initial empty commit
		RunGitCommand(TEXT("add .gitignore"), Output, ErrOutput);
		RunGitCommand(TEXT("commit -m \"Autonomix: initial commit\" --allow-empty"), Output, ErrOutput);

		// Save the initial commit hash
		RunGitCommand(TEXT("rev-parse HEAD"), InitialCommitHash, ErrOutput);
		InitialCommitHash.TrimStartAndEndInline();
	}
	else
	{
		// Load existing initial commit hash
		FString Output, ErrOutput;
		RunGitCommand(TEXT("rev-list --max-parents=0 HEAD"), Output, ErrOutput);
		InitialCommitHash = Output.TrimStartAndEnd();

		UE_LOG(LogTemp, Log, TEXT("AutonomixCheckpointManager: Resuming existing checkpoint session. Initial hash: %s"), *InitialCommitHash);
	}

	bIsInitialized = true;
	UE_LOG(LogTemp, Log, TEXT("AutonomixCheckpointManager: Initialized shadow repo at %s"), *ShadowRepoPath);
	return true;
}

// ============================================================================
// Save Checkpoint
// ============================================================================

bool FAutonomixCheckpointManager::SaveCheckpoint(
	const FString& Description,
	int32 LoopIteration,
	FAutonomixCheckpoint& OutCheckpoint
)
{
	if (!bIsInitialized)
	{
		return false;
	}

	// Copy tracked files from project to shadow repo
	TArray<FString> SyncedFiles;
	if (!SyncFilesToShadowRepo(SyncedFiles))
	{
		UE_LOG(LogTemp, Warning, TEXT("AutonomixCheckpointManager: Failed to sync files to shadow repo."));
		return false;
	}

	if (SyncedFiles.Num() == 0)
	{
		UE_LOG(LogTemp, Verbose, TEXT("AutonomixCheckpointManager: No files changed — skipping checkpoint."));
		return false;
	}

	// Stage all changes
	FString Output, ErrOutput;
	int32 AddResult = RunGitCommand(TEXT("add -A"), Output, ErrOutput);
	if (AddResult != 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("AutonomixCheckpointManager: git add failed (exit %d): %s"), AddResult, *ErrOutput);
		return false;
	}

	// Check if there are actually staged changes
	// git diff --cached --quiet returns 0 if no staged changes, 1 if there are changes
	int32 DiffResult = RunGitCommand(TEXT("diff --cached --quiet"), Output, ErrOutput);
	if (DiffResult == 0)
	{
		UE_LOG(LogTemp, Verbose, TEXT("AutonomixCheckpointManager: No staged changes for checkpoint '%s' — skipping."), *Description);
		return false;
	}

	// Create commit
	const FString CommitMsg = FString::Printf(
		TEXT("Autonomix: %s (loop %d)"),
		*Description,
		LoopIteration
	);
	const FString CommitArg = FString::Printf(TEXT("commit -m \"%s\""), *CommitMsg);
	int32 CommitResult = RunGitCommand(CommitArg, Output, ErrOutput);
	if (CommitResult != 0)
	{
		// May fail if nothing to commit — not an error (check both Output and ErrOutput since
		// git may write this message to stdout rather than stderr)
		if (Output.Contains(TEXT("nothing to commit")) || ErrOutput.Contains(TEXT("nothing to commit")))
		{
			UE_LOG(LogTemp, Verbose, TEXT("AutonomixCheckpointManager: Nothing to commit for checkpoint '%s'."), *Description);
			return false;
		}
		UE_LOG(LogTemp, Warning, TEXT("AutonomixCheckpointManager: Commit failed (exit %d): %s"), CommitResult, *ErrOutput);
		return false;
	}

	// Get the new commit hash
	FString CommitHash;
	RunGitCommand(TEXT("rev-parse HEAD"), CommitHash, ErrOutput);
	CommitHash.TrimStartAndEndInline();

	// Build checkpoint record
	OutCheckpoint.CommitHash = CommitHash;
	OutCheckpoint.Timestamp = FDateTime::UtcNow();
	OutCheckpoint.Description = Description;
	OutCheckpoint.ModifiedFiles = SyncedFiles;
	OutCheckpoint.LoopIteration = LoopIteration;

	Checkpoints.Add(OutCheckpoint);

	UE_LOG(LogTemp, Log, TEXT("AutonomixCheckpointManager: Saved checkpoint '%s' -> %s (%d files)"),
		*Description, *CommitHash.Left(8), SyncedFiles.Num());

	return true;
}

// ============================================================================
// Restore Checkpoint
// ============================================================================

bool FAutonomixCheckpointManager::RestoreToCheckpoint(
	const FString& CommitHash,
	TArray<FString>& OutRestoredFiles
)
{
	if (!bIsInitialized)
	{
		return false;
	}

	return SyncFilesFromShadowRepo(CommitHash, OutRestoredFiles);
}

// ============================================================================
// Diff
// ============================================================================

FAutonomixCheckpointDiff FAutonomixCheckpointManager::GetDiff(
	const FString& FromHash,
	const FString& ToHash,
	const FString& DiffMode
)
{
	FAutonomixCheckpointDiff Result;

	if (!bIsInitialized)
	{
		Result.bSuccess = false;
		Result.ErrorMessage = TEXT("Checkpoint system not initialized.");
		return Result;
	}

	FString ActualFrom = FromHash;
	FString ActualTo = ToHash;

	// Handle diff modes
	if (DiffMode == TEXT("from-init"))
	{
		ActualFrom = InitialCommitHash;
		ActualTo = ToHash.IsEmpty() ? TEXT("HEAD") : ToHash;
	}
	else if (DiffMode == TEXT("to-current"))
	{
		ActualFrom = FromHash;
		ActualTo = TEXT("HEAD");
	}
	else if (DiffMode == TEXT("full"))
	{
		ActualFrom = InitialCommitHash;
		ActualTo = TEXT("HEAD");
	}
	else // "checkpoint" (default)
	{
		if (ActualTo.IsEmpty()) ActualTo = TEXT("HEAD");
	}

	Result.FromHash = ActualFrom;
	Result.ToHash = ActualTo;

	FString Output, ErrOutput;

	// Get list of changed files
	const FString FilesArg = FString::Printf(
		TEXT("diff --name-only %s %s"),
		*ActualFrom,
		*ActualTo
	);
	if (RunGitCommand(*FilesArg, Output, ErrOutput) != 0)
	{
		Result.bSuccess = false;
		Result.ErrorMessage = FString::Printf(TEXT("git diff --name-only failed: %s"), *ErrOutput);
		return Result;
	}

	Output.ParseIntoArrayLines(Result.ChangedFiles, false);

	// Get unified diff
	const FString DiffArg = FString::Printf(
		TEXT("diff --unified=3 %s %s"),
		*ActualFrom,
		*ActualTo
	);
	if (RunGitCommand(*DiffArg, Result.DiffText, ErrOutput) != 0)
	{
		// Non-zero exit from diff is normal when there are differences
		// Only fail if ErrOutput has actual errors
		if (!ErrOutput.IsEmpty() && !ErrOutput.Contains(TEXT("warning:")))
		{
			Result.bSuccess = false;
			Result.ErrorMessage = FString::Printf(TEXT("git diff failed: %s"), *ErrOutput);
			return Result;
		}
	}

	Result.bSuccess = true;
	return Result;
}

// ============================================================================
// Query
// ============================================================================

TArray<FAutonomixCheckpoint> FAutonomixCheckpointManager::GetAllCheckpoints() const
{
	return Checkpoints;
}

const FAutonomixCheckpoint* FAutonomixCheckpointManager::GetLatestCheckpoint() const
{
	return Checkpoints.Num() > 0 ? &Checkpoints.Last() : nullptr;
}

const FAutonomixCheckpoint* FAutonomixCheckpointManager::GetCheckpointByHash(const FString& CommitHash) const
{
	for (const FAutonomixCheckpoint& CP : Checkpoints)
	{
		if (CP.CommitHash == CommitHash)
		{
			return &CP;
		}
	}
	return nullptr;
}

FString FAutonomixCheckpointManager::GetInitialCommitHash() const
{
	return InitialCommitHash;
}

// ============================================================================
// Git utilities
// ============================================================================

int32 FAutonomixCheckpointManager::RunGitCommand(
	const FString& Args,
	FString& Output,
	FString& ErrOutput
) const
{
	const FString GitPath = GetGitPath();
	Output.Empty();
	ErrOutput.Empty();

	void* PipeRead = nullptr;
	void* PipeWrite = nullptr;

	FPlatformProcess::CreatePipe(PipeRead, PipeWrite);

	FProcHandle Process = FPlatformProcess::CreateProc(
		*GitPath,
		*Args,
		false,   // bLaunchDetached
		true,    // bLaunchHidden
		true,    // bLaunchReallyHidden
		nullptr, // OutProcessID
		0,       // PriorityModifier
		*ShadowRepoPath,  // OptionalWorkingDirectory
		PipeWrite,        // PipeWriteChild (stdout)
		nullptr           // PipeReadChild (stdin)
	);

	if (!Process.IsValid())
	{
		FPlatformProcess::ClosePipe(PipeRead, PipeWrite);
		ErrOutput = TEXT("Failed to create git process");
		return -1;
	}

	// Read stdout with timeout
	const double StartTime = FPlatformTime::Seconds();
	while (FPlatformProcess::IsProcRunning(Process))
	{
		Output += FPlatformProcess::ReadPipe(PipeRead);

		if (FPlatformTime::Seconds() - StartTime > GitTimeoutSeconds)
		{
			FPlatformProcess::TerminateProc(Process);
			ErrOutput = TEXT("Git operation timed out");
			FPlatformProcess::ClosePipe(PipeRead, PipeWrite);
			FPlatformProcess::CloseProc(Process);
			return -1;
		}

		FPlatformProcess::Sleep(0.01f);
	}

	// Read any remaining output
	Output += FPlatformProcess::ReadPipe(PipeRead);

	int32 ReturnCode = 0;
	FPlatformProcess::GetProcReturnCode(Process, &ReturnCode);
	FPlatformProcess::ClosePipe(PipeRead, PipeWrite);
	FPlatformProcess::CloseProc(Process);

	// NOTE: UE's CreateProc only captures stdout via PipeWrite. Git writes errors
	// to stderr which is NOT captured separately. When the process fails with a
	// non-zero exit code and ErrOutput is empty, populate it from stdout (which
	// sometimes contains the error message) or provide a descriptive fallback.
	if (ReturnCode != 0 && ErrOutput.IsEmpty())
	{
		if (!Output.IsEmpty())
		{
			ErrOutput = Output.TrimStartAndEnd();
		}
		else
		{
			ErrOutput = FString::Printf(
				TEXT("git %s exited with code %d but produced no output (stderr not captured)"),
				*Args, ReturnCode);
		}
	}

	return ReturnCode;
}

bool FAutonomixCheckpointManager::SyncFilesToShadowRepo(TArray<FString>& OutSyncedFiles)
{
	OutSyncedFiles.Empty();
	TArray<FString> TrackedFiles = GatherTrackedFiles();

	for (const FString& AbsPath : TrackedFiles)
	{
		// Compute relative path within project
		FString RelPath = AbsPath;
		FPaths::MakePathRelativeTo(RelPath, *(ProjectRoot + TEXT("/")));

		// Destination in shadow repo
		const FString DestPath = FPaths::Combine(ShadowRepoPath, RelPath);

		// Ensure destination directory exists
		IFileManager::Get().MakeDirectory(*FPaths::GetPath(DestPath), true);

		// Copy file
		if (IFileManager::Get().Copy(*DestPath, *AbsPath) == COPY_OK)
		{
			OutSyncedFiles.Add(RelPath);
		}
	}

	return true;
}

bool FAutonomixCheckpointManager::SyncFilesFromShadowRepo(
	const FString& CommitHash,
	TArray<FString>& OutRestoredFiles
)
{
	OutRestoredFiles.Empty();

	FString Output, ErrOutput;

	// Get list of files at this commit
	const FString FilesArg = FString::Printf(TEXT("show --name-only --pretty=\"\" %s"), *CommitHash);
	if (RunGitCommand(*FilesArg, Output, ErrOutput) != 0)
	{
		UE_LOG(LogTemp, Error, TEXT("AutonomixCheckpointManager: Failed to list files at %s: %s"), *CommitHash, *ErrOutput);
		return false;
	}

	TArray<FString> FilesAtCommit;
	Output.ParseIntoArrayLines(FilesAtCommit, false);

	for (const FString& RelPath : FilesAtCommit)
	{
		if (RelPath.IsEmpty()) continue;

		// Extract file content from git object
		const FString ExtractArg = FString::Printf(TEXT("show %s:%s"), *CommitHash, *RelPath);
		FString FileContent, ExtractErr;
		if (RunGitCommand(*ExtractArg, FileContent, ExtractErr) == 0)
		{
			const FString DestAbsPath = FPaths::Combine(ProjectRoot, RelPath);
			IFileManager::Get().MakeDirectory(*FPaths::GetPath(DestAbsPath), true);

			if (FFileHelper::SaveStringToFile(FileContent, *DestAbsPath))
			{
				OutRestoredFiles.Add(RelPath);
			}
		}
	}

	UE_LOG(LogTemp, Log, TEXT("AutonomixCheckpointManager: Restored %d files from checkpoint %s"),
		OutRestoredFiles.Num(), *CommitHash.Left(8));

	return OutRestoredFiles.Num() > 0;
}

TArray<FString> FAutonomixCheckpointManager::GatherTrackedFiles() const
{
	TArray<FString> Result;

	// Track Source/ (C++ headers and source)
	TArray<FString> SourceFiles;
	IFileManager::Get().FindFilesRecursive(SourceFiles,
		*FPaths::Combine(ProjectRoot, TEXT("Source")),
		TEXT("*.*"), true, false);

	for (const FString& File : SourceFiles)
	{
		if (ShouldTrackFile(File))
		{
			Result.Add(File);
		}
	}

	// Track Config/ (ini files)
	TArray<FString> ConfigFiles;
	IFileManager::Get().FindFiles(ConfigFiles,
		*FPaths::Combine(ProjectRoot, TEXT("Config"), TEXT("*.ini")),
		true, false);

	for (const FString& FileName : ConfigFiles)
	{
		FString FullPath = FPaths::Combine(ProjectRoot, TEXT("Config"), FileName);
		if (ShouldTrackFile(FullPath))
		{
			Result.Add(FullPath);
		}
	}

	return Result;
}

bool FAutonomixCheckpointManager::ShouldTrackFile(const FString& AbsolutePath) const
{
	// Skip binary asset files
	FString Ext = FPaths::GetExtension(AbsolutePath).ToLower();
	static TArray<FString> SkipExtensions = {
		TEXT("uasset"), TEXT("umap"), TEXT("pak"), TEXT("pdb"),
		TEXT("exe"), TEXT("dll"), TEXT("lib"), TEXT("obj"),
		TEXT("bin"), TEXT("sig"), TEXT("ucas"), TEXT("utoc")
	};

	if (SkipExtensions.Contains(Ext))
	{
		return false;
	}

	// Skip files over size limit
	int64 FileSize = IFileManager::Get().FileSize(*AbsolutePath);
	if (FileSize < 0 || FileSize > MaxTrackedFileSizeBytes)
	{
		return false;
	}

	return true;
}

FString FAutonomixCheckpointManager::GetShadowRepoGitignore()
{
	return
		TEXT("# Autonomix shadow repo - binary files excluded\n")
		TEXT("*.uasset\n")
		TEXT("*.umap\n")
		TEXT("*.pak\n")
		TEXT("*.pdb\n")
		TEXT("*.exe\n")
		TEXT("*.dll\n")
		TEXT("*.lib\n")
		TEXT("*.obj\n")
		TEXT("*.bin\n");
}

bool FAutonomixCheckpointManager::ConfigureGitUser()
{
	FString Output, ErrOutput;
	RunGitCommand(TEXT("config user.name \"Autonomix\""), Output, ErrOutput);
	RunGitCommand(TEXT("config user.email \"autonomix@local\""), Output, ErrOutput);
	return true;
}

FString FAutonomixCheckpointManager::GetGitPath()
{
#if PLATFORM_WINDOWS
	// Try common git locations on Windows
	TArray<FString> Candidates = {
		TEXT("git.exe"),
		TEXT("C:\\Program Files\\Git\\cmd\\git.exe"),
		TEXT("C:\\Program Files (x86)\\Git\\cmd\\git.exe"),
	};
	for (const FString& Candidate : Candidates)
	{
		if (FPaths::FileExists(Candidate) || Candidate == TEXT("git.exe"))
		{
			return Candidate;
		}
	}
	return TEXT("git.exe");
#else
	return TEXT("/usr/bin/git");
#endif
}

bool FAutonomixCheckpointManager::IsGitAvailable()
{
	// Try to run "git --version"
	void* PipeRead = nullptr;
	void* PipeWrite = nullptr;
	FPlatformProcess::CreatePipe(PipeRead, PipeWrite);

	FProcHandle Process = FPlatformProcess::CreateProc(
		*GetGitPath(),
		TEXT("--version"),
		false, true, true,
		nullptr, 0,
		nullptr, PipeWrite
	);

	if (!Process.IsValid())
	{
		FPlatformProcess::ClosePipe(PipeRead, PipeWrite);
		return false;
	}

	FPlatformProcess::WaitForProc(Process);

	int32 ReturnCode = 0;
	FPlatformProcess::GetProcReturnCode(Process, &ReturnCode);
	FPlatformProcess::ClosePipe(PipeRead, PipeWrite);
	FPlatformProcess::CloseProc(Process);

	return ReturnCode == 0;
}

FString FAutonomixCheckpointManager::GetGitVersion()
{
	void* PipeRead = nullptr;
	void* PipeWrite = nullptr;
	FPlatformProcess::CreatePipe(PipeRead, PipeWrite);

	FProcHandle Process = FPlatformProcess::CreateProc(
		*GetGitPath(),
		TEXT("--version"),
		false, true, true,
		nullptr, 0,
		nullptr, PipeWrite
	);

	FString Output;
	if (Process.IsValid())
	{
		FPlatformProcess::WaitForProc(Process);
		Output = FPlatformProcess::ReadPipe(PipeRead);
		FPlatformProcess::ClosePipe(PipeRead, PipeWrite);
		FPlatformProcess::CloseProc(Process);
	}

	return Output.TrimStartAndEnd();
}
