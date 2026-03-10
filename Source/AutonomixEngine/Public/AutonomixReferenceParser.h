// Copyright Autonomix. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/**
 * Type of resolved reference content.
 */
enum class EAutonomixReferenceType : uint8
{
	/** Full file contents */
	File,
	/** Directory listing and file summaries */
	Folder,
	/** Fetched web page content */
	Url,
	/** Compile errors and warnings from UE's message log */
	CompileErrors,
	/** Selected actors in the viewport */
	Selection,
	/** Currently active level info */
	Level,
	/** Last build output */
	BuildLog,
	/** Blueprint class summary */
	BlueprintAsset,
	/** UE project structure summary */
	ProjectSummary,
};

/**
 * A resolved @reference content block ready to inject into a message.
 */
struct AUTONOMIXENGINE_API FAutonomixResolvedReference
{
	/** Original reference string (e.g. "@/Source/MyActor.cpp") */
	FString OriginalRef;

	/** Type of content */
	EAutonomixReferenceType Type = EAutonomixReferenceType::File;

	/** Resolved content text (may be multi-line) */
	FString Content;

	/** Whether resolution succeeded */
	bool bSuccess = false;

	/** Error message if resolution failed */
	FString ErrorMessage;

	/** Approximate token count for context budget management */
	int32 EstimatedTokens = 0;
};

/**
 * Result of parsing an input text for @references.
 */
struct AUTONOMIXENGINE_API FAutonomixParseReferencesResult
{
	/** Input text with @reference markers replaced by inline descriptions */
	FString ProcessedText;

	/** Resolved content blocks to prepend to the message */
	TArray<FAutonomixResolvedReference> ResolvedReferences;

	/** Total estimated token cost of all resolved references */
	int32 TotalEstimatedTokens = 0;
};

class FAutonomixIgnoreController;

/**
 * Parses @reference syntax in user input and resolves each reference to file/asset content.
 *
 * Ported and adapted from Roo Code's mentions/index.ts.
 *
 * SUPPORTED REFERENCE SYNTAX:
 *   @/Source/MyActor.cpp        → full file contents
 *   @/Content/Blueprints/       → folder listing + file summaries
 *   @errors                     → current compile errors
 *   @selection                  → selected actors in viewport
 *   @level                      → currently active level info
 *   @buildlog                   → last build output
 *   @asset:BP_MyCharacter       → Blueprint class summary
 *   @project                    → project structure overview
 *
 * USAGE IN SAutonomixMainPanel::OnPromptSubmitted():
 *   FAutonomixParseReferencesResult Parsed = ReferenceParser->ParseAndResolve(UserInput);
 *   // Inject Parsed.ProcessedText as the user message
 *   // Prepend each Parsed.ResolvedReference.Content before the message
 *
 * BINARY FILE HANDLING:
 *   Binary files (.uasset, .umap, etc.) are detected and excluded with a note.
 *   Files > 512KB are summarized (file info only, no content).
 */
class AUTONOMIXENGINE_API FAutonomixReferenceParser
{
public:
	FAutonomixReferenceParser();
	~FAutonomixReferenceParser();

	/** Maximum file size to include full contents (512KB) */
	static constexpr int64 MaxFileContentBytes = 512 * 1024;

	/** Maximum number of files to list in folder summaries */
	static constexpr int32 MaxFolderListingFiles = 50;

	// ---- Configuration ----

	/** Project root directory for resolving relative paths */
	FString ProjectRoot;

	/** Attach ignore controller to filter excluded paths */
	void SetIgnoreController(FAutonomixIgnoreController* InController);

	// ---- Core API ----

	/**
	 * Parse input text for @references, resolve them, and return processed result.
	 * The processed text has @references replaced with inline notes.
	 * Content blocks contain the actual resolved content to prepend to the message.
	 *
	 * @param InputText   Raw user input (may contain @references)
	 * @return Parsed result with processed text + resolved content blocks
	 */
	FAutonomixParseReferencesResult ParseAndResolve(const FString& InputText) const;

	/**
	 * Get autocomplete suggestions for a partial @reference string.
	 * Used by the input area for @-completion popup.
	 *
	 * @param Partial    The partial reference after '@' (e.g., "/Source/My" or "sel")
	 * @return           List of matching completions
	 */
	TArray<FString> GetAutocompleteSuggestions(const FString& Partial) const;

	// ---- Resolution (public for testing) ----

	FAutonomixResolvedReference ResolveFileReference(const FString& RelativePath) const;
	FAutonomixResolvedReference ResolveFolderReference(const FString& RelativePath) const;
	FAutonomixResolvedReference ResolveSpecialReference(const FString& Keyword) const;

private:
	FAutonomixIgnoreController* IgnoreController = nullptr;

	/** Extract all @references from text, preserving their positions */
	TArray<FString> ExtractReferences(const FString& Text) const;

	/** Determine reference type from the reference string */
	static EAutonomixReferenceType ClassifyReference(const FString& Ref);

	/** Check if a file is binary (should not be included as text) */
	static bool IsBinaryFile(const FString& AbsolutePath);

	/** Get the absolute path for a project-relative reference */
	FString ResolveAbsolutePath(const FString& RelativePath) const;

	/** Build file tree string for a directory */
	FString BuildFolderSummary(const FString& AbsDir, const FString& RelDir) const;

	/** Gather compile errors from UE message log */
	FString GatherCompileErrors() const;

	/** Get selected actors summary from viewport */
	FString GatherSelectionSummary() const;

	/** Get active level info */
	FString GatherLevelInfo() const;

	/** Get last build log content */
	FString GatherBuildLog() const;
};
