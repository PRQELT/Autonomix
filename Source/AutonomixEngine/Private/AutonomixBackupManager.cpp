// Copyright Autonomix. All Rights Reserved.

#include "AutonomixBackupManager.h"
#include "AutonomixCoreModule.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/PlatformFileManager.h"

FAutonomixBackupManager::FAutonomixBackupManager() {}
FAutonomixBackupManager::~FAutonomixBackupManager() {}

FString FAutonomixBackupManager::GetBackupDirectory() const
{
	return FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Autonomix"), TEXT("Backups"));
}

FString FAutonomixBackupManager::BackupFile(const FString& FilePath)
{
	FString BackupDir = FPaths::Combine(GetBackupDirectory(), FDateTime::UtcNow().ToString());
	FString FileName = FPaths::GetCleanFilename(FilePath);
	FString BackupPath = FPaths::Combine(BackupDir, FileName);
	IFileManager::Get().MakeDirectory(*BackupDir, true);
	IFileManager::Get().Copy(*BackupPath, *FilePath);
	UE_LOG(LogAutonomix, Log, TEXT("BackupManager: Backed up %s -> %s"), *FilePath, *BackupPath);
	return BackupPath;
}

TArray<FString> FAutonomixBackupManager::BackupFiles(const TArray<FString>& FilePaths)
{
	TArray<FString> BackupPaths;
	for (const FString& Path : FilePaths) { BackupPaths.Add(BackupFile(Path)); }
	return BackupPaths;
}

bool FAutonomixBackupManager::RestoreFile(const FString& BackupPath) { /* Stub */ return false; }
bool FAutonomixBackupManager::RestoreUndoGroup(const FString& UndoGroupName) { /* Stub */ return false; }
void FAutonomixBackupManager::PruneOldBackups() { /* Stub */ }
