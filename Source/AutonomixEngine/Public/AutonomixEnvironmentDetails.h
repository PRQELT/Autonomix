// Copyright Autonomix. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AutonomixTypes.h"

class FAutonomixFileContextTracker;
class FAutonomixIgnoreController;

/**
 * Snapshot of dynamic editor state, assembled fresh at each agentic loop iteration.
 * 
 * Key architectural insight (ported from Roo Code):
 *   Roo Code splits context into TWO layers:
 *     1. SYSTEM PROMPT   — built once at session start, static codebase overview
 *     2. ENVIRONMENT DETAILS — rebuilt on every API call, captures current editor state
 *
 *   Autonomix currently puts EVERYTHING into BuildSystemPrompt() which grows stale
 *   within a single conversation. This class implements the second layer.
 *
 * Usage in SAutonomixMainPanel::ContinueAgenticLoop():
 *   FString EnvDetails = EnvironmentDetails->Build();
 *   // Append to the user message (or last tool_result message) before sending to Claude
 *   // The Claude API will see fresh state at every loop iteration.
 *
 * The environment details string is formatted as markdown sections using the
 * same pattern as Roo Code's getEnvironmentDetails():
 *   # Currently Open Files
 *   # Active Level
 *   # Selected Actors
 *   # Recent Compile Errors
 *   # Context Window Usage
 *   # Active Todo List
 *   # Stale File Warnings (from FileContextTracker)
 */
class AUTONOMIXENGINE_API FAutonomixEnvironmentDetails
{
public:
	FAutonomixEnvironmentDetails();
	~FAutonomixEnvironmentDetails();

	// ---- Dependencies (set before calling Build()) ----

	/**
	 * Attach a FileContextTracker to include stale file warnings.
	 * Does NOT take ownership.
	 */
	void SetFileContextTracker(FAutonomixFileContextTracker* InTracker);

	/**
	 * Attach an IgnoreController to filter paths from environment output.
	 * Does NOT take ownership.
	 */
	void SetIgnoreController(FAutonomixIgnoreController* InIgnoreController);

	// ---- Configuration ----

	/** Maximum number of open editor tabs to list (default: 20) */
	int32 MaxOpenTabs = 20;

	/** Maximum number of compile errors to include (default: 10) */
	int32 MaxCompileErrors = 10;

	/** Whether to include selected actors section */
	bool bIncludeSelectedActors = true;

	/** Whether to include compile errors from the Output Log */
	bool bIncludeCompileErrors = true;

	/** Whether to include the active level name */
	bool bIncludeActiveLevel = true;

	/** Whether to include context window usage % */
	bool bIncludeContextWindowUsage = true;

	/** Whether to include todo list summary */
	bool bIncludeTodoSummary = true;

	// ---- Build ----

	/**
	 * Build the full environment details string.
	 * Call this immediately before each API request.
	 *
	 * @param ContextUsagePercent  Current context window usage (0-100)
	 * @param Todos                Current todo list items
	 * @param TabTitle             Current conversation tab title
	 * @param LoopIteration        Current agentic loop iteration number
	 * @return  Formatted markdown string to append to the user message
	 */
	FString Build(
		float ContextUsagePercent = 0.0f,
		const TArray<FAutonomixTodoItem>& Todos = {},
		const FString& TabTitle = FString(),
		int32 LoopIteration = 0
	) const;

	// ---- Individual sections (public for testing / override) ----

	/** Currently open/visible files in the editor (filtered by IgnoreController) */
	FString GetOpenFilesSection() const;

	/** Currently active level name and actor count */
	FString GetActiveLevelSection() const;

	/** Selected actors in the viewport */
	FString GetSelectedActorsSection() const;

	/** Recent compile errors from the Message Log */
	FString GetCompileErrorsSection() const;

	/** Context window usage percentage and token counts */
	FString GetContextWindowSection(float UsagePercent) const;

	/** Abbreviated todo list (pending + in-progress items) */
	FString GetTodoSummarySection(const TArray<FAutonomixTodoItem>& Todos) const;

	/** Stale file warnings from FileContextTracker */
	FString GetStaleFilesSection() const;

	/** Current time + active conversation info */
	FString GetSessionInfoSection(const FString& TabTitle, int32 LoopIteration) const;

private:
	FAutonomixFileContextTracker* FileContextTracker = nullptr;
	FAutonomixIgnoreController* IgnoreController = nullptr;

	/** Filter path through IgnoreController (returns empty string if ignored) */
	bool IsPathAllowed(const FString& Path) const;

	/** Get recent compile errors from UE's message log */
	TArray<FString> GatherCompileErrors() const;

	/** Get currently open asset editors and text files */
	TArray<FString> GatherOpenFiles() const;

	/** Get selected actors from the editor viewport */
	TArray<FString> GatherSelectedActors() const;

	/** Get the currently active level name */
	FString GetActiveLevelName() const;
};
