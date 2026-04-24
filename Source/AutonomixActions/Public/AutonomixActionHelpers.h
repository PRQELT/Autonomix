// Copyright Autonomix. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AutonomixTypes.h"
#include "Dom/JsonObject.h"
#include "AssetToolsModule.h"

class UObject;
class UAssetFactory;

/**
 * Shared utilities for all action handlers.
 * Eliminates code duplication across Animation, Blueprint, Material, GAS, BehaviorTree, etc.
 * Provides RAII patterns and templated helpers for common operations.
 */

// ============================================================================
// RAII Transaction Guard
// ============================================================================

/**
 * RAII wrapper for UE transaction and asset modification lifecycle.
 * Automatically:
 * - Starts FScopedTransaction
 * - Calls Modify() on the asset
 * - Marks package dirty on destruction
 * - Handles transaction rollback on error
 */
class FAutonomixTransactionGuard
{
public:
	/**
	 * Begin a scoped transaction for modifying an asset.
	 * @param InObject The UObject to modify (Blueprint, Material, etc.)
	 * @param InActionName Display name for the transaction (shown in Undo History)
	 */
	explicit FAutonomixTransactionGuard(UObject* InObject, const TCHAR* InActionName = TEXT("Autonomix Action"));
	
	/** Destructor marks the package dirty and completes the transaction */
	~FAutonomixTransactionGuard();
	
	/** Cancel this transaction (prevents MarkPackageDirty) */
	void Cancel();
	
	/** Commit this transaction explicitly (normally happens in destructor) */
	void Commit();

private:
	UObject* Object;
	bool bCancelled;
	bool bCommitted;
};

// ============================================================================
// Asset Loading Helpers
// ============================================================================

class FAutonomixAssetHelper
{
public:
	/**
	 * Load an asset of type T, with error reporting.
	 * @param AssetPath Full asset path (e.g., "/Game/Characters/SK_Hero")
	 * @param OutError Set if asset not found
	 * @return Loaded asset or nullptr
	 */
	template<typename T>
	static T* LoadAssetChecked(const FString& AssetPath, FString& OutError)
	{
		if (AssetPath.IsEmpty())
		{
			OutError = TEXT("Asset path is empty");
			return nullptr;
		}

		T* Asset = LoadObject<T>(nullptr, *AssetPath);
		if (!Asset)
		{
			OutError = FString::Printf(TEXT("Failed to load asset: %s"), *AssetPath);
			return nullptr;
		}

		return Asset;
	}

	/**
	 * Load an asset with automatic class suffix fallback.
	 * Tries the given path, then tries appending "_C" for Blueprint-generated classes.
	 * @param AssetPath Base asset path
	 * @param OutError Set if both attempts fail
	 * @return Loaded UClass or nullptr
	 */
	static UClass* LoadClassChecked(const FString& AssetPath, FString& OutError);

	/**
	 * Validate that an asset path is syntactically valid (looks like "/Game/...").
	 * @param AssetPath Path to validate
	 * @return true if path looks valid
	 */
	static bool IsValidAssetPath(const FString& AssetPath);
};

// ============================================================================
// File Operations
// ============================================================================

class FAutonomixFileHelper
{
public:
	/**
	 * Write content to a file, creating automatic backup of existing file.
	 * Backup is stored in: Saved/Autonomix/Backups/[timestamp]/[filename]
	 * @param FilePath Absolute file path to write to
	 * @param Content Content to write
	 * @param OutBackupPath If provided, filled with path to backup file (if one was created)
	 * @return true if write succeeded
	 */
	static bool WriteFileWithBackup(
		const FString& FilePath,
		const FString& Content,
		FString* OutBackupPath = nullptr
	);

	/**
	 * Read file content with error reporting.
	 * @param FilePath Absolute file path to read
	 * @param OutContent Populated with file content
	 * @param OutError Set if read fails
	 * @return true if read succeeded
	 */
	static bool ReadFileChecked(
		const FString& FilePath,
		FString& OutContent,
		FString& OutError
	);
};

// ============================================================================
// Package & Path Helpers
// ============================================================================

struct FAutonomixPackageInfo
{
	/** Long package path (e.g., "/Game/Characters/Hero") */
	FString LongPath;
	
	/** Short asset name (e.g., "Hero") */
	FString ShortName;

	explicit FAutonomixPackageInfo(const FString& FullAssetPath);
};

class FAutonomixPathHelper
{
public:
	/**
	 * Parse an asset path into long path and short name.
	 * @param AssetPath Full asset path
	 * @return FAutonomixPackageInfo with separated components
	 */
	static FAutonomixPackageInfo GetPackageInfo(const FString& AssetPath);

	/**
	 * Get the package that would contain the asset at this path.
	 * @param AssetPath Asset path to check
	 * @return UPackage or nullptr
	 */
	static UPackage* GetOrCreatePackage(const FString& AssetPath);
};

// ============================================================================
// Asset Creation Factory Pattern
// ============================================================================

/**
 * Template helper to create assets with common boilerplate.
 * Usage: FAutonomixAssetFactory::Create<UAnimBlueprint, UAnimBlueprintFactory>(
 *     "/Game/Animations/MyAnim",
 *     [](UAnimBlueprintFactory* Factory) { Factory->TargetSkeleton = Skeleton; },
 *     Result
 * );
 */
class FAutonomixAssetFactory
{
public:
	/**
	 * Create an asset with a factory and configuration callback.
	 * @param AssetPath Full asset path where to create it
	 * @param AssetClass The UClass of the asset to create
	 * @param FactoryClass The UFactory class to use
	 * @param ConfigureFactory Callback to configure the factory before asset creation
	 * @param Result Output parameter for success/failure details
	 * @return Created asset or nullptr
	 */
	template<typename AssetType, typename FactoryType>
	static AssetType* Create(
		const FString& AssetPath,
		UClass* AssetClass,
		TFunction<void(FactoryType*)> ConfigureFactory,
		FAutonomixActionResult& Result
	)
	{
		FAutonomixPackageInfo PackageInfo(AssetPath);

		// Validate path
		if (!FAutonomixAssetHelper::IsValidAssetPath(AssetPath))
		{
			Result.Errors.Add(FString::Printf(TEXT("Invalid asset path: %s"), *AssetPath));
			return nullptr;
		}

		// Get or create package
		UPackage* Package = FAutonomixPathHelper::GetOrCreatePackage(PackageInfo.LongPath);
		if (!Package)
		{
			Result.Errors.Add(FString::Printf(TEXT("Failed to create package for: %s"), *PackageInfo.LongPath));
			return nullptr;
		}

		// Create factory and configure
		FactoryType* Factory = NewObject<FactoryType>();
		if (!Factory)
		{
			Result.Errors.Add(FString::Printf(TEXT("Failed to instantiate factory for asset type")));
			return nullptr;
		}

		ConfigureFactory(Factory);

		// Create asset
		FAssetToolsModule& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools");
		UObject* NewAsset = AssetTools.Get().CreateAsset(
			PackageInfo.ShortName,
			PackageInfo.LongPath,
			AssetClass,
			Factory
		);

		if (!NewAsset)
		{
			Result.Errors.Add(FString::Printf(TEXT("AssetTools failed to create asset: %s"), *AssetPath));
			return nullptr;
		}

		Result.CreatedAssets.Add(AssetPath);
		return Cast<AssetType>(NewAsset);
	}
};

// ============================================================================
// Validation Helpers
// ============================================================================

class FAutonomixValidationHelper
{
public:
	/**
	 * Check if JSON object has required field with specific type.
	 * @param JsonObject Object to check
	 * @param FieldName Name of required field
	 * @param FieldType Expected type (e.g., EJson::String, EJson::Object)
	 * @param OutError Populated if field missing or wrong type
	 * @return true if field exists and has correct type
	 */
	static bool ValidateJsonField(
		const TSharedRef<FJsonObject>& JsonObject,
		const TCHAR* FieldName,
		EJson FieldType,
		FString& OutError
	);

	/**
	 * Validate an asset path exists and is of the correct type.
	 * @param AssetPath Path to validate
	 * @param ExpectedAssetClass Expected UClass (e.g., UBlueprint::StaticClass())
	 * @param OutError Populated if asset not found or wrong type
	 * @return true if asset exists and is correct type
	 */
	static bool ValidateAssetExists(
		const FString& AssetPath,
		UClass* ExpectedAssetClass,
		FString& OutError
	);

	/**
	 * Validate list of asset paths.
	 * @param AssetPaths Array of paths to validate
	 * @param OutErrors Populated with errors for any invalid paths
	 * @return true if all assets are valid
	 */
	static bool ValidateAssetArray(
		const TArray<FString>& AssetPaths,
		TArray<FString>& OutErrors
	);
};

// ============================================================================
// Error Result Helpers
// ============================================================================

class FAutonomixResultHelper
{
public:
	/**
	 * Create a failure result with a printf-formatted error message.
	 * @param Format Printf format string
	 * @param ... Printf arguments
	 * @return FAutonomixActionResult with bSuccess=false
	 */
	static FAutonomixActionResult MakeError(const TCHAR* Format, ...);

	/**
	 * Create a success result with a printf-formatted message.
	 * @param Format Printf format string
	 * @param ... Printf arguments
	 * @return FAutonomixActionResult with bSuccess=true
	 */
	static FAutonomixActionResult MakeSuccess(const TCHAR* Format, ...);

	/**
	 * Add multiple errors at once from a TArray<FString>.
	 * @param Result Result to add errors to
	 * @param Errors Array of error messages to add
	 */
	static void AddErrors(FAutonomixActionResult& Result, const TArray<FString>& Errors);
};

// ============================================================================
// Logging Helpers
// ============================================================================

DECLARE_LOG_CATEGORY_EXTERN(LogAutonomixActions, Log, All);

/**
 * Scoped logger for action execution.
 * Logs action start/end with timing and result.
 */
class FAutonomixActionLogger
{
public:
	explicit FAutonomixActionLogger(const TCHAR* ActionName);
	~FAutonomixActionLogger();

	void LogSuccess(const TCHAR* Message, ...);
	void LogWarning(const TCHAR* Message, ...);
	void LogError(const TCHAR* Message, ...);

private:
	FString ActionName;
	double StartTime;
};

#define AUTONOMIX_LOG_ACTION(ActionName) FAutonomixActionLogger _ActionLogger(ActionName)
