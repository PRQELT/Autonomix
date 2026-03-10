// Copyright Autonomix. All Rights Reserved.

#include "AutonomixSafetyGate.h"
#include "AutonomixIgnoreController.h"
#include "AutonomixSettings.h"
#include "AutonomixCoreModule.h"
#include "Misc/Paths.h"

FAutonomixSafetyGate::FAutonomixSafetyGate()
{
	InitializeDefaults();
	InitializeDefaultProtectedPaths();
	LoadProtectedPathsFromSettings();
}

FAutonomixSafetyGate::~FAutonomixSafetyGate() {}

void FAutonomixSafetyGate::InitializeDefaults()
{
	AllowedWritePaths.Add(FPaths::ProjectContentDir());
	AllowedWritePaths.Add(FPaths::GameSourceDir());
	AllowedWritePaths.Add(FPaths::ProjectConfigDir());
	CodeDenylistPatterns.Add(TEXT("system("));
	CodeDenylistPatterns.Add(TEXT("exec("));
	CodeDenylistPatterns.Add(TEXT("ShellExecute"));
	CodeDenylistPatterns.Add(TEXT("DeleteFile"));
	CodeDenylistPatterns.Add(TEXT("RemoveDirectory"));
}

void FAutonomixSafetyGate::LoadProtectedPathsFromSettings()
{
	const UAutonomixDeveloperSettings* Settings = UAutonomixDeveloperSettings::Get();
	if (!Settings) return;

	if (Settings->bOverrideDefaultProtectedPaths)
	{
		// Replace built-in defaults entirely with user-supplied patterns.
		// CAUTION: this means core files like *.uplugin are no longer protected
		// unless the user explicitly lists them. Documented in the settings tooltip.
		ProtectedPaths.Empty();
		UE_LOG(LogAutonomix, Warning,
			TEXT("SafetyGate: bOverrideDefaultProtectedPaths=true — default protected paths replaced by user list."));
	}

	// Add all user-supplied patterns (AddUnique prevents duplicates)
	for (const FString& Pattern : Settings->AdditionalProtectedPaths)
	{
		if (!Pattern.IsEmpty())
		{
			ProtectedPaths.AddUnique(Pattern);
		}
	}

	UE_LOG(LogAutonomix, Log,
		TEXT("SafetyGate: %d total protected path patterns loaded (%d user-defined)."),
		ProtectedPaths.Num(), Settings->AdditionalProtectedPaths.Num());
}

void FAutonomixSafetyGate::InitializeDefaultProtectedPaths()
{
	// Plugin descriptor — breaking it kills the plugin
	ProtectedPaths.Add(TEXT("*.uplugin"));

	// Project descriptor — editing incorrectly can corrupt the project
	ProtectedPaths.Add(TEXT("*.uproject"));

	// Module build rules — incorrect edits prevent compilation
	ProtectedPaths.Add(TEXT("*.Build.cs"));
	ProtectedPaths.Add(TEXT("*.Target.cs"));

	// Core engine config — wrong edits can break the engine
	ProtectedPaths.Add(TEXT("Config/DefaultEngine.ini"));
	ProtectedPaths.Add(TEXT("Config/DefaultEditor.ini"));
	ProtectedPaths.Add(TEXT("Config/DefaultGame.ini"));

	// Source control metadata
	ProtectedPaths.Add(TEXT(".gitignore"));
	ProtectedPaths.Add(TEXT(".gitattributes"));
	ProtectedPaths.Add(TEXT(".p4ignore"));

	// Autonomix own config
	ProtectedPaths.Add(TEXT(".autonomixignore"));
}

EAutonomixRiskLevel FAutonomixSafetyGate::EvaluateRisk(const FAutonomixAction& Action) const
{
	// Check protected paths — always Critical for write operations
	for (const FString& Path : Action.AffectedPaths)
	{
		if (IsPathProtected(Path))
		{
			return EAutonomixRiskLevel::Critical;
		}
	}
	return Action.RiskLevel;
}

bool FAutonomixSafetyGate::IsActionAllowed(const FAutonomixAction& Action, FString& OutReason) const
{
	for (const FString& Path : Action.AffectedPaths)
	{
		// Check ignore controller first (file should not even be visible to AI)
		if (IsPathIgnoredByFilter(Path))
		{
			OutReason = FString::Printf(TEXT("Path is excluded by .autonomixignore: %s"), *Path);
			return false;
		}

		// Check protected paths (read-only for AI writes)
		if (IsPathProtected(Path))
		{
			OutReason = FString::Printf(
				TEXT("Path is protected (read-only for AI): %s\n"
				     "To allow AI modification of this file, remove it from the protected paths list in Autonomix settings."),
				*Path
			);
			return false;
		}

		if (!IsPathAllowed(Path))
		{
			OutReason = FString::Printf(TEXT("Path not in allowed write paths: %s"), *Path);
			return false;
		}
	}
	return true;
}

bool FAutonomixSafetyGate::IsPathAllowed(const FString& FilePath) const
{
	// Canonicalize the path to prevent traversal attacks (e.g., "Content/../../../etc/passwd")
	// FPaths::NormalizeFilename handles forward/backward slash normalization.
	// FPaths::CollapseRelativeDirectories removes ".." segments.
	FString NormalizedPath = FilePath;
	FPaths::NormalizeFilename(NormalizedPath);
	FPaths::CollapseRelativeDirectories(NormalizedPath);
	FPaths::RemoveDuplicateSlashes(NormalizedPath);

	for (const FString& Allowed : AllowedWritePaths)
	{
		// Canonicalize the allowed path too for a safe comparison
		FString NormalizedAllowed = Allowed;
		FPaths::NormalizeFilename(NormalizedAllowed);
		FPaths::CollapseRelativeDirectories(NormalizedAllowed);

		// Ensure we check a full directory boundary — path must start with Allowed/ or be exact
		if (NormalizedPath.StartsWith(NormalizedAllowed))
		{
			// Additional check: the character after the allowed prefix must be a separator
			// or the paths are identical, preventing partial-name prefix matches.
			if (NormalizedPath.Len() == NormalizedAllowed.Len() ||
				NormalizedPath[NormalizedAllowed.Len()] == TEXT('/') ||
				NormalizedPath[NormalizedAllowed.Len()] == TEXT('\\'))
			{
				return true;
			}
		}
	}
	return false;
}

bool FAutonomixSafetyGate::IsPathProtected(const FString& FilePath) const
{
	const FString FileName = FPaths::GetCleanFilename(FilePath);
	const FString NormPath = FilePath.Replace(TEXT("\\"), TEXT("/"));

	for (const FString& Pattern : ProtectedPaths)
	{
		if (MatchesGlob(FileName, Pattern) || MatchesGlob(NormPath, Pattern))
		{
			return true;
		}
	}
	return false;
}

void FAutonomixSafetyGate::AddProtectedPath(const FString& PathOrPattern)
{
	ProtectedPaths.AddUnique(PathOrPattern);
}

void FAutonomixSafetyGate::RemoveProtectedPath(const FString& PathOrPattern)
{
	ProtectedPaths.Remove(PathOrPattern);
}

void FAutonomixSafetyGate::SetIgnoreController(FAutonomixIgnoreController* InController)
{
	IgnoreController = InController;
}

bool FAutonomixSafetyGate::IsPathIgnoredByFilter(const FString& RelativePath) const
{
	if (!IgnoreController)
	{
		return false;
	}
	return IgnoreController->IsPathIgnored(RelativePath);
}

bool FAutonomixSafetyGate::ValidateGeneratedCode(const FString& Code, TArray<FString>& OutViolations) const
{
	bool bValid = true;
	for (const FString& Pattern : CodeDenylistPatterns)
	{
		if (Code.Contains(Pattern))
		{
			OutViolations.Add(FString::Printf(TEXT("Denied pattern found: %s"), *Pattern));
			bValid = false;
		}
	}
	return bValid;
}

bool FAutonomixSafetyGate::MatchesGlob(const FString& Path, const FString& Pattern) const
{
	// Simple glob: support * (matches anything except /) and ** (matches everything)
	if (Pattern == TEXT("*") || Pattern == TEXT("**"))
	{
		return true;
	}

	// Exact match
	if (Path.Equals(Pattern, ESearchCase::IgnoreCase))
	{
		return true;
	}

	// Suffix pattern like "*.uplugin"
	if (Pattern.StartsWith(TEXT("*.")))
	{
		const FString Ext = Pattern.Mid(1); // ".uplugin"
		return Path.EndsWith(Ext, ESearchCase::IgnoreCase);
	}

	// Prefix pattern like "Config/*"
	if (Pattern.EndsWith(TEXT("*")))
	{
		const FString Prefix = Pattern.LeftChop(1);
		return Path.StartsWith(Prefix, ESearchCase::IgnoreCase);
	}

	// Contains pattern like "*Default*.ini"
	if (Pattern.Contains(TEXT("*")))
	{
		// Split on * and check sequential presence
		TArray<FString> Parts;
		Pattern.ParseIntoArray(Parts, TEXT("*"), true);
		FString Remaining = Path;
		for (const FString& Part : Parts)
		{
			int32 Idx = Remaining.Find(Part, ESearchCase::IgnoreCase);
			if (Idx == INDEX_NONE) return false;
			Remaining = Remaining.Mid(Idx + Part.Len());
		}
		return true;
	}

	return false;
}
