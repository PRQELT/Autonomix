// Copyright Autonomix. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AutonomixErrorCodes.generated.h"

/**
 * Comprehensive error code system for Autonomix.
 * Ensures consistent error reporting, logging, and user feedback.
 * 
 * Error codes use compact uint8 values (0-255) grouped by category.
 * Values are sequential within each category block for readability.
 * Categories:
 * - 0-6:    General/Core errors
 * - 10-18:  Asset loading/management errors
 * - 20-26:  Validation errors
 * - 30-39:  File operation errors
 * - 40-53:  Blueprint-specific errors
 * - 60-65:  Material-specific errors
 * - 70-77:  C++ code generation errors
 * - 80-84:  Animation system errors
 * - 90-94:  Safety/Security errors
 * - 100-107: LLM/API errors
 * - 110-116: Context/Memory errors
 */

UENUM(BlueprintType)
enum class EAutonomixErrorCode : uint8
{
	// ========================================================================
	// GENERAL / CORE ERRORS (1-6)
	// ========================================================================

	/** Operation completed successfully */
	Success = 0 UMETA(DisplayName = "Success"),

	/** Unknown/unclassified error */
	UnknownError = 1 UMETA(DisplayName = "Unknown Error"),

	/** Operation was cancelled by user */
	Cancelled = 2 UMETA(DisplayName = "Cancelled"),

	/** Operation timed out */
	Timeout = 3 UMETA(DisplayName = "Timeout"),

	/** Operation not implemented */
	NotImplemented = 4 UMETA(DisplayName = "Not Implemented"),

	/** Invalid parameters provided */
	InvalidParameters = 5 UMETA(DisplayName = "Invalid Parameters"),

	/** Operation not permitted in current mode */
	OperationNotPermitted = 6 UMETA(DisplayName = "Operation Not Permitted"),

	// ========================================================================
	// ASSET LOADING ERRORS (10-18)
	// ========================================================================

	/** Asset not found at specified path */
	AssetNotFound = 10 UMETA(DisplayName = "Asset Not Found"),

	/** Asset path is invalid or malformed */
	InvalidAssetPath = 11 UMETA(DisplayName = "Invalid Asset Path"),

	/** Asset is of wrong type */
	AssetTypeMismatch = 12 UMETA(DisplayName = "Asset Type Mismatch"),

	/** Failed to load asset */
	AssetLoadFailed = 13 UMETA(DisplayName = "Asset Load Failed"),

	/** Asset is read-only and cannot be modified */
	AssetReadOnly = 14 UMETA(DisplayName = "Asset Read-Only"),

	/** Asset is already locked by another operation */
	AssetLocked = 15 UMETA(DisplayName = "Asset Locked"),

	/** Failed to create asset package */
	PackageCreationFailed = 16 UMETA(DisplayName = "Package Creation Failed"),

	/** Failed to save asset */
	AssetSaveFailed = 17 UMETA(DisplayName = "Asset Save Failed"),

	/** Asset is in use (e.g., open in editor) */
	AssetInUse = 18 UMETA(DisplayName = "Asset In Use"),

	// ========================================================================
	// VALIDATION ERRORS (20-26)
	// ========================================================================

	/** Required JSON field missing */
	MissingJsonField = 20 UMETA(DisplayName = "Missing JSON Field"),

	/** JSON field has wrong type */
	WrongJsonFieldType = 21 UMETA(DisplayName = "Wrong JSON Field Type"),

	/** Array field is empty when non-empty required */
	EmptyArrayField = 22 UMETA(DisplayName = "Empty Array Field"),

	/** String field is empty when non-empty required */
	EmptyStringField = 23 UMETA(DisplayName = "Empty String Field"),

	/** Numeric field out of valid range */
	NumericFieldOutOfRange = 24 UMETA(DisplayName = "Numeric Field Out Of Range"),

	/** Invalid file path provided */
	InvalidFilePath = 25 UMETA(DisplayName = "Invalid File Path"),

	/** Directory does not exist */
	DirectoryNotFound = 26 UMETA(DisplayName = "Directory Not Found"),

	// ========================================================================
	// FILE OPERATION ERRORS (30-39)
	// ========================================================================

	/** Failed to read file */
	FileReadFailed = 30 UMETA(DisplayName = "File Read Failed"),

	/** Failed to write file */
	FileWriteFailed = 31 UMETA(DisplayName = "File Write Failed"),

	/** File does not exist */
	FileNotFound = 32 UMETA(DisplayName = "File Not Found"),

	/** File is read-only */
	FileReadOnly = 33 UMETA(DisplayName = "File Read-Only"),

	/** Failed to create directory */
	DirectoryCreationFailed = 34 UMETA(DisplayName = "Directory Creation Failed"),

	/** Failed to delete file/directory */
	DeletionFailed = 35 UMETA(DisplayName = "Deletion Failed"),

	/** Failed to copy file */
	FileCopyFailed = 36 UMETA(DisplayName = "File Copy Failed"),

	/** Failed to back up file */
	BackupFailed = 37 UMETA(DisplayName = "Backup Failed"),

	/** Insufficient disk space */
	InsufficientDiskSpace = 38 UMETA(DisplayName = "Insufficient Disk Space"),

	/** File path is too long */
	FilePathTooLong = 39 UMETA(DisplayName = "File Path Too Long"),

	// ========================================================================
	// BLUEPRINT-SPECIFIC ERRORS (40-53)
	// ========================================================================

	/** Blueprint parent class not found */
	BlueprintParentNotFound = 40 UMETA(DisplayName = "Blueprint Parent Not Found"),

	/** Failed to create Blueprint */
	BlueprintCreationFailed = 41 UMETA(DisplayName = "Blueprint Creation Failed"),

	/** Blueprint compilation failed */
	BlueprintCompilationFailed = 42 UMETA(DisplayName = "Blueprint Compilation Failed"),

	/** Failed to inject Blueprint nodes */
	BlueprintNodeInjectionFailed = 43 UMETA(DisplayName = "Blueprint Node Injection Failed"),

	/** Pin connection failed */
	PinConnectionFailed = 44 UMETA(DisplayName = "Pin Connection Failed"),

	/** Node not found in Blueprint */
	NodeNotFound = 45 UMETA(DisplayName = "Node Not Found"),

	/** Pin not found on node */
	PinNotFound = 46 UMETA(DisplayName = "Pin Not Found"),

	/** Component not found in Blueprint */
	ComponentNotFound = 47 UMETA(DisplayName = "Component Not Found"),

	/** Variable not found in Blueprint */
	VariableNotFound = 48 UMETA(DisplayName = "Variable Not Found"),

	/** Function not found in Blueprint */
	FunctionNotFound = 49 UMETA(DisplayName = "Function Not Found"),

	/** Graph not found in Blueprint */
	GraphNotFound = 50 UMETA(DisplayName = "Graph Not Found"),

	/** T3D format parsing failed */
	T3DParsingFailed = 51 UMETA(DisplayName = "T3D Parsing Failed"),

	/** GUID placeholder resolution failed */
	GUIDPlaceholderResolutionFailed = 52 UMETA(DisplayName = "GUID Placeholder Resolution Failed"),

	/** DAG layout algorithm failed */
	DAGLayoutFailed = 53 UMETA(DisplayName = "DAG Layout Failed"),

	// ========================================================================
	// MATERIAL-SPECIFIC ERRORS (60-65)
	// ========================================================================

	/** Failed to create Material */
	MaterialCreationFailed = 60 UMETA(DisplayName = "Material Creation Failed"),

	/** Material compilation failed */
	MaterialCompilationFailed = 61 UMETA(DisplayName = "Material Compilation Failed"),

	/** Texture not found for Material */
	TextureNotFound = 62 UMETA(DisplayName = "Texture Not Found"),

	/** Material graph node not found */
	MaterialNodeNotFound = 63 UMETA(DisplayName = "Material Node Not Found"),

	/** Material parameter not found */
	MaterialParameterNotFound = 64 UMETA(DisplayName = "Material Parameter Not Found"),

	/** Failed to set material parameter */
	MaterialParameterSetFailed = 65 UMETA(DisplayName = "Material Parameter Set Failed"),

	// ========================================================================
	// C++ CODE GENERATION ERRORS (70-77)
	// ========================================================================

	/** Failed to create C++ class files */
	CppClassCreationFailed = 70 UMETA(DisplayName = "C++ Class Creation Failed"),

	/** Generated C++ code failed to compile */
	CppCompilationFailed = 71 UMETA(DisplayName = "C++ Compilation Failed"),

	/** Failed to apply C++ code diff */
	CppDiffApplyFailed = 72 UMETA(DisplayName = "C++ Diff Apply Failed"),

	/** C++ code contains unsafe patterns */
	CppCodeUnsafe = 73 UMETA(DisplayName = "C++ Code Unsafe"),

	/** Failed to parse existing C++ file */
	CppParsingFailed = 74 UMETA(DisplayName = "C++ Parsing Failed"),

	/** Header file not found */
	HeaderFileNotFound = 75 UMETA(DisplayName = "Header File Not Found"),

	/** Source file not found */
	SourceFileNotFound = 76 UMETA(DisplayName = "Source File Not Found"),

	/** Build.cs file not found */
	BuildCsFileNotFound = 77 UMETA(DisplayName = "Build.cs File Not Found"),

	// ========================================================================
	// ANIMATION SYSTEM ERRORS (80-84)
	// ========================================================================

	/** Failed to create Animation Blueprint */
	AnimBlueprintCreationFailed = 80 UMETA(DisplayName = "Anim Blueprint Creation Failed"),

	/** Animation compilation failed */
	AnimCompilationFailed = 81 UMETA(DisplayName = "Anim Compilation Failed"),

	/** Skeleton not found for Animation Blueprint */
	SkeletonNotFound = 82 UMETA(DisplayName = "Skeleton Not Found"),

	/** Animation sequence not found */
	AnimSequenceNotFound = 83 UMETA(DisplayName = "Anim Sequence Not Found"),

	/** Montage not found */
	MontageNotFound = 84 UMETA(DisplayName = "Montage Not Found"),

	// ========================================================================
	// SAFETY & SECURITY ERRORS (90-94)
	// ========================================================================

	/** Operation blocked by safety gate */
	SafetyGateRejected = 90 UMETA(DisplayName = "Safety Gate Rejected"),

	/** File is protected and cannot be modified */
	ProtectedFileModification = 91 UMETA(DisplayName = "Protected File Modification"),

	/** Operation exceeds safety limits */
	SafetyLimitExceeded = 92 UMETA(DisplayName = "Safety Limit Exceeded"),

	/** Insufficient approval level */
	InsufficientApprovalLevel = 93 UMETA(DisplayName = "Insufficient Approval Level"),

	/** File pattern matches ignore list */
	FileIgnored = 94 UMETA(DisplayName = "File Ignored"),

	// ========================================================================
	// LLM / API ERRORS (100-107)
	// ========================================================================

	/** API key is missing or invalid */
	MissingAPIKey = 100 UMETA(DisplayName = "Missing API Key"),

	/** API request failed */
	APIRequestFailed = 101 UMETA(DisplayName = "API Request Failed"),

	/** API returned error response */
	APIErrorResponse = 102 UMETA(DisplayName = "API Error Response"),

	/** API rate limit exceeded */
	APIRateLimitExceeded = 103 UMETA(DisplayName = "API Rate Limit Exceeded"),

	/** API response parsing failed */
	APIResponseParsingFailed = 104 UMETA(DisplayName = "API Response Parsing Failed"),

	/** API timeout */
	APITimeout = 105 UMETA(DisplayName = "API Timeout"),

	/** LLM provider not configured */
	LLMProviderNotConfigured = 106 UMETA(DisplayName = "LLM Provider Not Configured"),

	/** Model not available */
	ModelNotAvailable = 107 UMETA(DisplayName = "Model Not Available"),

	// ========================================================================
	// CONTEXT / MEMORY ERRORS (110-116)
	// ========================================================================

	/** Context window exceeded */
	ContextWindowExceeded = 110 UMETA(DisplayName = "Context Window Exceeded"),

	/** Token limit exceeded */
	TokenLimitExceeded = 111 UMETA(DisplayName = "Token Limit Exceeded"),

	/** Failed to condense context */
	ContextCondensationFailed = 112 UMETA(DisplayName = "Context Condensation Failed"),

	/** Insufficient memory for operation */
	InsufficientMemory = 113 UMETA(DisplayName = "Insufficient Memory"),

	/** Checkpoint not found */
	CheckpointNotFound = 114 UMETA(DisplayName = "Checkpoint Not Found"),

	/** Checkpoint restore failed */
	CheckpointRestoreFailed = 115 UMETA(DisplayName = "Checkpoint Restore Failed"),

	/** Checkpoint corruption detected */
	CheckpointCorrupted = 116 UMETA(DisplayName = "Checkpoint Corrupted")
};

/**
 * Mapping from error codes to user-friendly messages and recovery hints.
 */
namespace AutonomixErrorMessages
{
	/**
	 * Get user-friendly error message for an error code.
	 * @param ErrorCode The error code
	 * @return Human-readable error description
	 */
	FORCEINLINE FString GetErrorMessage(EAutonomixErrorCode ErrorCode)
	{
		switch (ErrorCode)
		{
			// General
			case EAutonomixErrorCode::Success:
				return TEXT("Operation completed successfully");
			case EAutonomixErrorCode::UnknownError:
				return TEXT("An unknown error occurred");
			case EAutonomixErrorCode::Cancelled:
				return TEXT("Operation was cancelled");
			case EAutonomixErrorCode::Timeout:
				return TEXT("Operation timed out");
			case EAutonomixErrorCode::InvalidParameters:
				return TEXT("Invalid parameters provided");

			// Asset loading
			case EAutonomixErrorCode::AssetNotFound:
				return TEXT("Asset not found at specified path");
			case EAutonomixErrorCode::InvalidAssetPath:
				return TEXT("Asset path is invalid or malformed");
			case EAutonomixErrorCode::AssetTypeMismatch:
				return TEXT("Asset is of unexpected type");
			case EAutonomixErrorCode::AssetReadOnly:
				return TEXT("Asset is read-only and cannot be modified");

			// Validation
			case EAutonomixErrorCode::MissingJsonField:
				return TEXT("Required JSON field is missing");
			case EAutonomixErrorCode::WrongJsonFieldType:
				return TEXT("JSON field has wrong type");
			case EAutonomixErrorCode::InvalidFilePath:
				return TEXT("File path is invalid");

			// File operations
			case EAutonomixErrorCode::FileNotFound:
				return TEXT("File does not exist");
			case EAutonomixErrorCode::FileReadFailed:
				return TEXT("Failed to read file (check permissions)");
			case EAutonomixErrorCode::FileWriteFailed:
				return TEXT("Failed to write file (check disk space and permissions)");
			case EAutonomixErrorCode::InsufficientDiskSpace:
				return TEXT("Insufficient disk space for operation");

			// Blueprint
			case EAutonomixErrorCode::BlueprintParentNotFound:
				return TEXT("Blueprint parent class not found (verify parent class path)");
			case EAutonomixErrorCode::BlueprintCompilationFailed:
				return TEXT("Blueprint failed to compile (check logs for details)");
			case EAutonomixErrorCode::PinConnectionFailed:
				return TEXT("Failed to connect Blueprint pins (verify pin types match)");
			case EAutonomixErrorCode::T3DParsingFailed:
				return TEXT("T3D format parsing failed (Blueprint text format corrupted)");

			// Safety
			case EAutonomixErrorCode::SafetyGateRejected:
				return TEXT("Operation blocked by safety gate (check permissions)");
			case EAutonomixErrorCode::ProtectedFileModification:
				return TEXT("Cannot modify protected file (system files are read-only)");

			// LLM
			case EAutonomixErrorCode::MissingAPIKey:
				return TEXT("API key is missing (configure provider settings)");
			case EAutonomixErrorCode::APIRateLimitExceeded:
				return TEXT("API rate limit exceeded (wait before retrying)");
			case EAutonomixErrorCode::APITimeout:
				return TEXT("API request timed out (network issue?)");

			// Context
			case EAutonomixErrorCode::ContextWindowExceeded:
				return TEXT("Context window exceeded (try condensing conversation)");
			case EAutonomixErrorCode::TokenLimitExceeded:
				return TEXT("Token limit exceeded for this provider");

			default:
				return FString::Printf(TEXT("Error code %d"), (int32)ErrorCode);
		}
	}

	/**
	 * Get recovery hint for an error code.
	 * @param ErrorCode The error code
	 * @return Actionable recovery suggestion
	 */
	FORCEINLINE FString GetRecoveryHint(EAutonomixErrorCode ErrorCode)
	{
		switch (ErrorCode)
		{
			case EAutonomixErrorCode::AssetNotFound:
				return TEXT("Check the asset path is correct. Use Content Browser to verify the asset exists.");
			
			case EAutonomixErrorCode::BlueprintCompilationFailed:
				return TEXT("Check the Blueprint compiler output for syntax errors. Verify all node pins are connected correctly.");
			
			case EAutonomixErrorCode::FileWriteFailed:
				return TEXT("Verify you have write permissions to the directory and sufficient disk space.");
			
			case EAutonomixErrorCode::SafetyGateRejected:
				return TEXT("This file is protected. Switch to 'Developer' security mode if you need to modify it.");
			
			case EAutonomixErrorCode::MissingAPIKey:
				return TEXT("Go to Project Settings > Autonomix and configure your API key for the selected provider.");
			
			case EAutonomixErrorCode::ContextWindowExceeded:
				return TEXT("Your conversation is too long. Use the 'Condense' button to summarize old messages.");
			
			case EAutonomixErrorCode::APIRateLimitExceeded:
				return TEXT("You've hit the API rate limit. Wait a few minutes before trying again, or reduce your request frequency.");
			
			default:
				return TEXT("Check the logs for more details. If the problem persists, contact support.");
		}
	}

	/**
	 * Get severity level for error code (0=info, 1=warning, 2=error, 3=critical)
	 */
	FORCEINLINE int32 GetSeverity(EAutonomixErrorCode ErrorCode)
	{
		if (ErrorCode == EAutonomixErrorCode::Success)
			return 0;
		
		if (ErrorCode >= EAutonomixErrorCode::SafetyGateRejected && ErrorCode <= EAutonomixErrorCode::FileIgnored)  // Safety errors (90-94)
			return 3;
		
		return 2;  // Most others are errors
	}
}

/**
 * Helper to create structured error results with error codes.
 */
struct FAutonomixErrorInfo
{
	EAutonomixErrorCode Code;
	FString Message;
	FString Details;
	FString RecoveryHint;
	FString StackTrace;

	FAutonomixErrorInfo(EAutonomixErrorCode InCode = EAutonomixErrorCode::UnknownError)
		: Code(InCode)
		, Message(AutonomixErrorMessages::GetErrorMessage(InCode))
		, RecoveryHint(AutonomixErrorMessages::GetRecoveryHint(InCode))
	{
	}

	explicit operator bool() const { return Code != EAutonomixErrorCode::Success; }
};
