// Copyright Autonomix. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AutonomixInterfaces.h"

class FAutonomixIgnoreController;

/**
 * Safety gate that evaluates risk, path access, and code safety.
 *
 * v2.0: Added protected files system (read-only for AI) and
 * IgnoreController integration for .autonomixignore enforcement.
 *
 * Protected paths differ from AllowedWritePaths:
 *   - AllowedWritePaths: whitelist — only these paths are writable
 *   - ProtectedPaths: explicit read-only list for critical files the AI
 *     may see but must never modify (*.uplugin, *.uproject, *.Build.cs, etc.)
 *
 * Any path in ProtectedPaths returns EAutonomixRiskLevel::Critical for write
 * operations regardless of SecurityMode.
 */
class AUTONOMIXENGINE_API FAutonomixSafetyGate : public IAutonomixSafetyGate
{
public:
	FAutonomixSafetyGate();
	virtual ~FAutonomixSafetyGate();

	// IAutonomixSafetyGate interface
	virtual EAutonomixRiskLevel EvaluateRisk(const FAutonomixAction& Action) const override;
	virtual bool IsActionAllowed(const FAutonomixAction& Action, FString& OutReason) const override;
	virtual bool IsPathAllowed(const FString& FilePath) const override;
	virtual bool ValidateGeneratedCode(const FString& Code, TArray<FString>& OutViolations) const override;

	// ---- Protected Files (read-only for AI) ----

	/**
	 * Check if a path is protected (AI may read but must never write).
	 * Protected paths produce EAutonomixRiskLevel::Critical for write operations.
	 * @param FilePath  Absolute or relative path to check
	 * @return true if the file is protected
	 */
	bool IsPathProtected(const FString& FilePath) const;

	/**
	 * Add a path to the protected list.
	 * Supports glob patterns: "*.uplugin", "*.Build.cs", "Config/Default*.ini"
	 */
	void AddProtectedPath(const FString& PathOrPattern);

	/** Remove a path from the protected list */
	void RemoveProtectedPath(const FString& PathOrPattern);

	/** Get all protected path patterns */
	const TArray<FString>& GetProtectedPaths() const { return ProtectedPaths; }

	// ---- IgnoreController integration ----

	/**
	 * Attach an AutonomixIgnoreController instance.
	 * When set, IsPathIgnoredByFilter() will use it to block access.
	 * Does NOT take ownership — caller owns the controller.
	 */
	void SetIgnoreController(FAutonomixIgnoreController* InController);

	/**
	 * Check if a path is blocked by the .autonomixignore file.
	 * Returns false if no IgnoreController is attached.
	 */
	bool IsPathIgnoredByFilter(const FString& RelativePath) const;

private:
	TArray<FString> AllowedWritePaths;
	TArray<FString> CodeDenylistPatterns;

	/** Paths/patterns that are explicitly read-only for AI writes */
	TArray<FString> ProtectedPaths;

	/** Pointer to optional ignore controller (not owned) */
	FAutonomixIgnoreController* IgnoreController = nullptr;

	void InitializeDefaults();
	void InitializeDefaultProtectedPaths();

	/**
	 * Loads AdditionalProtectedPaths and bOverrideDefaultProtectedPaths from
	 * UAutonomixDeveloperSettings. Called at construction so user-configured
	 * patterns take effect immediately.
	 */
	void LoadProtectedPathsFromSettings();

	/** Match a path against a glob pattern */
	bool MatchesGlob(const FString& Path, const FString& Pattern) const;
};
