// Copyright Autonomix. All Rights Reserved.

#include "AutonomixActionHelpers.h"
#include "AutonomixCore/Public/AutonomixTypes.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "AssetToolsModule.h"
#include "AssetRegistryModule.h"

DEFINE_LOG_CATEGORY(LogAutonomixActions);

// ============================================================================
// FAutonomixTransactionGuard
// ============================================================================

FAutonomixTransactionGuard::FAutonomixTransactionGuard(UObject* InObject, const TCHAR* InActionName)
	: Object(InObject)
	, bCancelled(false)
	, bCommitted(false)
{
	if (Object)
	{
		// Start the transaction
		GEditor->BeginTransaction(FText::FromString(FString(InActionName)));
		
		// Mark the object as modified
		Object->Modify();
		
		UE_LOG(LogAutonomixActions, Verbose, TEXT("Transaction started: %s for %s"), InActionName, *Object->GetName());
	}
}

FAutonomixTransactionGuard::~FAutonomixTransactionGuard()
{
	if (!bCommitted && !bCancelled && Object)
	{
		// Mark the package dirty
		Object->GetOutermost()->MarkPackageDirty();
		
		// End and commit the transaction
		GEditor->EndTransaction();
		
		UE_LOG(LogAutonomixActions, Verbose, TEXT("Transaction committed for %s"), *Object->GetName());
	}
	else if (bCancelled)
	{
		GEditor->CancelTransaction();
		UE_LOG(LogAutonomixActions, Warning, TEXT("Transaction cancelled"));
	}
}

void FAutonomixTransactionGuard::Cancel()
{
	bCancelled = true;
}

void FAutonomixTransactionGuard::Commit()
{
	if (!bCancelled && Object)
	{
		Object->GetOutermost()->MarkPackageDirty();
		GEditor->EndTransaction();
		bCommitted = true;
	}
}

// ============================================================================
// FAutonomixAssetHelper
// ============================================================================

UClass* FAutonomixAssetHelper::LoadClassChecked(const FString& AssetPath, FString& OutError)
{
	if (AssetPath.IsEmpty())
	{
		OutError = TEXT("Asset path is empty");
		return nullptr;
	}

	// Try direct path first
	UClass* LoadedClass = LoadObject<UClass>(nullptr, *AssetPath);
	if (LoadedClass)
	{
		return LoadedClass;
	}

	// Try with "_C" suffix (Blueprint-generated class)
	FString ClassPath = AssetPath;
	if (!ClassPath.EndsWith(TEXT("_C")))
	{
		ClassPath += TEXT("_C");
		LoadedClass = LoadObject<UClass>(nullptr, *ClassPath);
		if (LoadedClass)
		{
			return LoadedClass;
		}
	}

	OutError = FString::Printf(TEXT("Failed to load class: %s (tried both %s and %s_C)"), *AssetPath, *AssetPath, *AssetPath);
	return nullptr;
}

bool FAutonomixAssetHelper::IsValidAssetPath(const FString& AssetPath)
{
	// Asset paths should start with /
	if (!AssetPath.StartsWith(TEXT("/")))
	{
		return false;
	}

	// Should have a reasonable minimum length
	if (AssetPath.Len() < 5) // At least "/Game/X"
	{
		return false;
	}

	// Shouldn't have suspicious patterns
	if (AssetPath.Contains(TEXT("..")) || AssetPath.Contains(TEXT("\\\\")) || AssetPath.Contains(TEXT("///")))
	{
		return false;
	}

	return true;
}

// ============================================================================
// FAutonomixFileHelper
// ============================================================================

bool FAutonomixFileHelper::WriteFileWithBackup(
	const FString& FilePath,
	const FString& Content,
	FString* OutBackupPath
)
{
	// Create backup if file exists
	if (FPaths::FileExists(FilePath))
	{
		FString BackupDir = FPaths::Combine(
			FPaths::ProjectSavedDir(),
			TEXT("Autonomix"),
			TEXT("Backups"),
			FDateTime::UtcNow().ToString(TEXT("%Y%m%d_%H%M%S"))
		);

		if (!IFileManager::Get().MakeDirectory(*BackupDir, true))
		{
			UE_LOG(LogAutonomixActions, Warning, TEXT("Failed to create backup directory: %s"), *BackupDir);
			return false;
		}

		FString BackupPath = FPaths::Combine(BackupDir, FPaths::GetCleanFilename(FilePath));
		if (IFileManager::Get().Copy(*BackupPath, *FilePath) == COPY_OK)
		{
			UE_LOG(LogAutonomixActions, Log, TEXT("File backed up: %s -> %s"), *FilePath, *BackupPath);
			if (OutBackupPath)
			{
				*OutBackupPath = BackupPath;
			}
		}
		else
		{
			UE_LOG(LogAutonomixActions, Warning, TEXT("Failed to backup file: %s"), *FilePath);
		}
	}

	// Write the new content
	bool bSuccess = FFileHelper::SaveStringToFile(
		Content,
		*FilePath,
		FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM
	);

	if (bSuccess)
	{
		UE_LOG(LogAutonomixActions, Log, TEXT("File written: %s (%d bytes)"), *FilePath, Content.Len());
	}
	else
	{
		UE_LOG(LogAutonomixActions, Error, TEXT("Failed to write file: %s"), *FilePath);
	}

	return bSuccess;
}

bool FAutonomixFileHelper::ReadFileChecked(
	const FString& FilePath,
	FString& OutContent,
	FString& OutError
)
{
	if (!FPaths::FileExists(FilePath))
	{
		OutError = FString::Printf(TEXT("File does not exist: %s"), *FilePath);
		return false;
	}

	if (!FFileHelper::LoadFileToString(OutContent, *FilePath))
	{
		OutError = FString::Printf(TEXT("Failed to read file: %s"), *FilePath);
		return false;
	}

	return true;
}

// ============================================================================
// FAutonomixPackageInfo
// ============================================================================

FAutonomixPackageInfo::FAutonomixPackageInfo(const FString& FullAssetPath)
	: LongPath(FPackageName::GetLongPackagePath(FullAssetPath))
	, ShortName(FPackageName::GetShortName(FullAssetPath))
{
}

// ============================================================================
// FAutonomixPathHelper
// ============================================================================

FAutonomixPackageInfo FAutonomixPathHelper::GetPackageInfo(const FString& AssetPath)
{
	return FAutonomixPackageInfo(AssetPath);
}

UPackage* FAutonomixPathHelper::GetOrCreatePackage(const FString& AssetPath)
{
	FString PackagePath = FPackageName::GetLongPackagePath(AssetPath);
	
	// Try to find existing package
	UPackage* Package = FindPackage(nullptr, *PackagePath);
	if (Package)
	{
		return Package;
	}

	// Create new package
	Package = CreatePackage(*PackagePath);
	if (Package)
	{
		Package->MarkAsFullyLoaded();
		UE_LOG(LogAutonomixActions, Log, TEXT("Created new package: %s"), *PackagePath);
		return Package;
	}

	UE_LOG(LogAutonomixActions, Error, TEXT("Failed to create package: %s"), *PackagePath);
	return nullptr;
}

// ============================================================================
// FAutonomixValidationHelper
// ============================================================================

bool FAutonomixValidationHelper::ValidateJsonField(
	const TSharedRef<FJsonObject>& JsonObject,
	const TCHAR* FieldName,
	EJson FieldType,
	FString& OutError
)
{
	if (!JsonObject->HasField(FieldName))
	{
		OutError = FString::Printf(TEXT("Missing required field: %s"), FieldName);
		return false;
	}

	if (JsonObject->GetField(FieldName)->Type != FieldType)
	{
		OutError = FString::Printf(TEXT("Field '%s' has wrong type. Expected: %d, Got: %d"), 
			FieldName, (int32)FieldType, (int32)JsonObject->GetField(FieldName)->Type);
		return false;
	}

	return true;
}

bool FAutonomixValidationHelper::ValidateAssetExists(
	const FString& AssetPath,
	UClass* ExpectedAssetClass,
	FString& OutError
)
{
	UObject* Asset = LoadObject<UObject>(nullptr, *AssetPath);
	if (!Asset)
	{
		OutError = FString::Printf(TEXT("Asset not found: %s"), *AssetPath);
		return false;
	}

	if (ExpectedAssetClass && !Asset->IsA(ExpectedAssetClass))
	{
		OutError = FString::Printf(TEXT("Asset '%s' is not of type %s"), *AssetPath, *ExpectedAssetClass->GetName());
		return false;
	}

	return true;
}

bool FAutonomixValidationHelper::ValidateAssetArray(
	const TArray<FString>& AssetPaths,
	TArray<FString>& OutErrors
)
{
	bool bAllValid = true;

	for (const FString& AssetPath : AssetPaths)
	{
		if (!FAutonomixAssetHelper::IsValidAssetPath(AssetPath))
		{
			OutErrors.Add(FString::Printf(TEXT("Invalid asset path: %s"), *AssetPath));
			bAllValid = false;
		}
	}

	return bAllValid;
}

// ============================================================================
// FAutonomixResultHelper
// ============================================================================

FAutonomixActionResult FAutonomixResultHelper::MakeError(const TCHAR* Format, ...)
{
	TCHAR Buffer[4096];
	va_list Args;
	va_start(Args, Format);
	FCString::GetVarArgs(Buffer, UE_ARRAY_COUNT(Buffer), Format, Args);
	va_end(Args);

	FAutonomixActionResult Result;
	Result.bSuccess = false;
	Result.Errors.Add(FString(Buffer));
	return Result;
}

FAutonomixActionResult FAutonomixResultHelper::MakeSuccess(const TCHAR* Format, ...)
{
	TCHAR Buffer[4096];
	va_list Args;
	va_start(Args, Format);
	FCString::GetVarArgs(Buffer, UE_ARRAY_COUNT(Buffer), Format, Args);
	va_end(Args);

	FAutonomixActionResult Result;
	Result.bSuccess = true;
	Result.ResultMessage = FString(Buffer);
	return Result;
}

void FAutonomixResultHelper::AddErrors(FAutonomixActionResult& Result, const TArray<FString>& Errors)
{
	Result.Errors.Append(Errors);
	if (!Result.Errors.IsEmpty())
	{
		Result.bSuccess = false;
	}
}

// ============================================================================
// FAutonomixActionLogger
// ============================================================================

FAutonomixActionLogger::FAutonomixActionLogger(const TCHAR* InActionName)
	: ActionName(InActionName)
	, StartTime(FPlatformTime::Seconds())
{
	UE_LOG(LogAutonomixActions, Log, TEXT(">>> Starting action: %s"), *ActionName);
}

FAutonomixActionLogger::~FAutonomixActionLogger()
{
	double ElapsedTime = FPlatformTime::Seconds() - StartTime;
	UE_LOG(LogAutonomixActions, Log, TEXT("<<< Completed action: %s (%.3f sec)"), *ActionName, ElapsedTime);
}

void FAutonomixActionLogger::LogSuccess(const TCHAR* Message, ...)
{
	TCHAR Buffer[4096];
	va_list Args;
	va_start(Args, Message);
	FCString::GetVarArgs(Buffer, UE_ARRAY_COUNT(Buffer), Message, Args);
	va_end(Args);

	UE_LOG(LogAutonomixActions, Log, TEXT("[%s] SUCCESS: %s"), *ActionName, Buffer);
}

void FAutonomixActionLogger::LogWarning(const TCHAR* Message, ...)
{
	TCHAR Buffer[4096];
	va_list Args;
	va_start(Args, Message);
	FCString::GetVarArgs(Buffer, UE_ARRAY_COUNT(Buffer), Message, Args);
	va_end(Args);

	UE_LOG(LogAutonomixActions, Warning, TEXT("[%s] WARNING: %s"), *ActionName, Buffer);
}

void FAutonomixActionLogger::LogError(const TCHAR* Message, ...)
{
	TCHAR Buffer[4096];
	va_list Args;
	va_start(Args, Message);
	FCString::GetVarArgs(Buffer, UE_ARRAY_COUNT(Buffer), Message, Args);
	va_end(Args);

	UE_LOG(LogAutonomixActions, Error, TEXT("[%s] ERROR: %s"), *ActionName, Buffer);
}
