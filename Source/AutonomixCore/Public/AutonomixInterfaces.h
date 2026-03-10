// Copyright Autonomix. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AutonomixTypes.h"

/**
 * Interface for all action executors.
 * Each action executor handles a specific domain of UE operations
 * (Blueprints, Materials, C++, etc.)
 */
class IAutonomixActionExecutor
{
public:
	virtual ~IAutonomixActionExecutor() = default;

	/** Get the unique name of this action executor */
	virtual FName GetActionName() const = 0;

	/** Get the display name for UI */
	virtual FText GetDisplayName() const = 0;

	/** Get the category this executor belongs to */
	virtual EAutonomixActionCategory GetCategory() const = 0;

	/** Get the default risk level for this action type */
	virtual EAutonomixRiskLevel GetDefaultRiskLevel() const = 0;

	/**
	 * Preview the action without executing it.
	 * Returns a plan describing what will happen, including affected files/assets.
	 */
	virtual FAutonomixActionPlan PreviewAction(const TSharedRef<FJsonObject>& Params) = 0;

	/**
	 * Execute the action.
	 * Should be called only after the user has approved the plan.
	 */
	virtual FAutonomixActionResult ExecuteAction(const TSharedRef<FJsonObject>& Params) = 0;

	/** Whether this action can be undone */
	virtual bool CanUndo() const = 0;

	/** Undo the last executed action */
	virtual bool UndoAction() = 0;

	/** Get the list of tool names this executor handles */
	virtual TArray<FString> GetSupportedToolNames() const = 0;

	/** Validate input parameters before execution */
	virtual bool ValidateParams(const TSharedRef<FJsonObject>& Params, TArray<FString>& OutErrors) const = 0;
};

// Forward declaration for the typed downcast helper
class FAutonomixClaudeClient;

/**
 * Interface for the LLM client.
 * Abstracts the AI provider so different backends can be swapped.
 */
class IAutonomixLLMClient
{
public:
	IAutonomixLLMClient() = default;
	virtual ~IAutonomixLLMClient() = default;

	/**
	 * Safe typed downcast to FAutonomixClaudeClient (Anthropic provider).
	 * Returns nullptr for all non-Anthropic providers.
	 * Uses a virtual method instead of dynamic_cast because UE compiles with /GR- (RTTI disabled).
	 */
	virtual FAutonomixClaudeClient* AsClaudeClient() { return nullptr; }

	/** Send a message and receive a response asynchronously */
	virtual void SendMessage(
		const TArray<FAutonomixMessage>& ConversationHistory,
		const FString& SystemPrompt,
		const TArray<TSharedPtr<FJsonObject>>& ToolSchemas
	) = 0;

	/** Cancel the current in-flight request */
	virtual void CancelRequest() = 0;

	/** Whether a request is currently in-flight */
	virtual bool IsRequestInFlight() const = 0;

	/** Delegate: fired when streaming text arrives (real-time, token-by-token) */
	virtual FOnAutonomixStreamingText& OnStreamingText() = 0;

	/** Delegate: fired when a tool call is received */
	virtual FOnAutonomixToolCallReceived& OnToolCallReceived() = 0;

	/** Delegate: fired when the full message is received (stream complete) */
	virtual FOnAutonomixMessageAdded& OnMessageComplete() = 0;

	/** Delegate: fired when the request starts */
	virtual FOnAutonomixRequestStarted& OnRequestStarted() = 0;

	/** Delegate: fired when the request completes (success or failure) */
	virtual FOnAutonomixRequestCompleted& OnRequestCompleted() = 0;

	/** Delegate: fired when an HTTP error is received with user-friendly message */
	virtual FOnAutonomixErrorReceived& OnErrorReceived() = 0;

	/** Delegate: fired with token usage info from response */
	virtual FOnAutonomixTokenUsageUpdated& OnTokenUsageUpdated() = 0;
};

/**
 * Interface for the context gatherer.
 * Collects project state information to include in AI prompts.
 */
class IAutonomixContextGatherer
{
public:
	virtual ~IAutonomixContextGatherer() = default;

	/** Build a full project context snapshot */
	virtual FString BuildContextString() = 0;

	/** Get the file tree as a compressed string */
	virtual FString GetFileTreeString() = 0;

	/** Get a summary of all assets in the project */
	virtual FString GetAssetSummaryString() = 0;

	/** Get the current project settings as a JSON string */
	virtual FString GetSettingsSnapshotString() = 0;

	/** Get the class hierarchy for project classes */
	virtual FString GetClassHierarchyString() = 0;

	/** Estimate the token count of the context */
	virtual int32 EstimateTokenCount() const = 0;
};

/**
 * Interface for the safety gate.
 * Determines whether an action can proceed and at what confirmation level.
 */
class IAutonomixSafetyGate
{
public:
	virtual ~IAutonomixSafetyGate() = default;

	/** Evaluate the risk level of an action */
	virtual EAutonomixRiskLevel EvaluateRisk(const FAutonomixAction& Action) const = 0;

	/** Check if an action is allowed based on current safety policy */
	virtual bool IsActionAllowed(const FAutonomixAction& Action, FString& OutReason) const = 0;

	/** Check if a file path is within the allowed write paths */
	virtual bool IsPathAllowed(const FString& FilePath) const = 0;

	/** Validate generated C++ code against denylist patterns */
	virtual bool ValidateGeneratedCode(const FString& Code, TArray<FString>& OutViolations) const = 0;
};

/**
 * Interface for the backup manager.
 * Handles file backups and rollback operations.
 */
class IAutonomixBackupManager
{
public:
	virtual ~IAutonomixBackupManager() = default;

	/** Create a backup of a file before modification */
	virtual FString BackupFile(const FString& FilePath) = 0;

	/** Create backups of multiple files */
	virtual TArray<FString> BackupFiles(const TArray<FString>& FilePaths) = 0;

	/** Restore a file from its backup */
	virtual bool RestoreFile(const FString& BackupPath) = 0;

	/** Restore all files from a specific undo group */
	virtual bool RestoreUndoGroup(const FString& UndoGroupName) = 0;

	/** Get the backup directory path */
	virtual FString GetBackupDirectory() const = 0;

	/** Clean up old backups beyond the rolling window */
	virtual void PruneOldBackups() = 0;
};
