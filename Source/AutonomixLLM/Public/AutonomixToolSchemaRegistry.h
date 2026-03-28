// Copyright Autonomix. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "AutonomixTypes.h"

/**
 * Loads, stores, and serves Claude tool definition JSON schemas.
 * Schemas are loaded from Resources/ToolSchemas/*.json at startup.
 *
 * v2.0: Added mode-filtered schema access (GetSchemasForMode).
 * Each EAutonomixAgentMode restricts the available tool set:
 *   - General:    all tools
 *   - Blueprint:  blueprint + read tools
 *   - CppCode:    cpp + build + read tools
 *   - Architect:  read tools only (no write)
 *   - Debug:      read + execute tools
 *   - Asset:      asset + read tools
 *
 * Tools always available regardless of mode (meta-tools):
 *   - update_todo_list, switch_mode, attempt_completion, ask_followup_question
 */
class AUTONOMIXLLM_API FAutonomixToolSchemaRegistry
{
public:
	FAutonomixToolSchemaRegistry();
	~FAutonomixToolSchemaRegistry();

	/** Load all tool schemas from the Resources/ToolSchemas/ directory */
	void LoadAllSchemas();

	/** Load a single schema file */
	bool LoadSchemaFile(const FString& FilePath);

	/** Get all tool schemas as a JSON array for the Claude API tools parameter */
	TArray<TSharedPtr<FJsonObject>> GetAllSchemas() const;

	/** Get schemas for a specific action category */
	TArray<TSharedPtr<FJsonObject>> GetSchemasByCategory(const FString& Category) const;

	/** Get a single tool schema by tool name */
	TSharedPtr<FJsonObject> GetSchemaByName(const FString& ToolName) const;

	/** Check if a tool name is registered */
	bool IsToolRegistered(const FString& ToolName) const;

	/** Get the total number of registered tools */
	int32 GetToolCount() const { return ToolSchemas.Num(); }

	/** Get all registered tool names */
	TArray<FString> GetAllToolNames() const;

	/** Enable or disable a specific tool */
	void SetToolEnabled(const FString& ToolName, bool bEnabled);

	/** Check if a tool is enabled */
	bool IsToolEnabled(const FString& ToolName) const;

	/** Get only enabled tool schemas */
	TArray<TSharedPtr<FJsonObject>> GetEnabledSchemas() const;

	/**
	 * Sync schemas with actually-registered tool executors.
	 * Any schema whose tool name has no registered executor (and is not a meta-tool)
	 * will be disabled. This prevents the LLM from calling tools that have no
	 * backend executor (e.g. python tools disabled in settings).
	 *
	 * @param RegisteredToolNames  Tool names from ActionRouter->GetRegisteredToolNames()
	 */
	void SyncWithRegisteredTools(const TArray<FString>& RegisteredToolNames);

	// ---- Mode-based filtering (v2.0) ----

	/**
	 * Get tool schemas filtered to those available in the given agent mode.
	 * Always includes meta-tools (update_todo_list, switch_mode, attempt_completion, ask_followup_question).
	 * Respects the global enabled/disabled state per tool.
	 *
	 * @param Mode  The active agent mode
	 * @return Filtered array of tool schemas
	 */
	TArray<TSharedPtr<FJsonObject>> GetSchemasForMode(EAutonomixAgentMode Mode) const;

	/**
	 * Get a minimal set of essential tool schemas for local providers (Ollama, LM Studio).
	 * Local models have limited context windows (8K-64K) and can't handle 90+ tool schemas.
	 * Without this, models respond with "I can't create files" because the tool schema
	 * payload alone exceeds their reasoning capacity.
	 * Returns ~15 core tools (file ops, context, meta-tools) with truncated descriptions.
	 * Token cost: ~750 tokens (vs ~5,000+ for full set).
	 */
	TArray<TSharedPtr<FJsonObject>> GetEssentialSchemas() const;

	/**
	 * Get the set of tool categories allowed in a given mode.
	 * Used to pre-filter tool execution in the ActionRouter.
	 */
	TSet<FString> GetAllowedCategoriesForMode(EAutonomixAgentMode Mode) const;

	/**
	 * Get the role definition string for a given mode.
	 * Injected into the system prompt when a mode is active.
	 */
	static FString GetModeRoleDefinition(EAutonomixAgentMode Mode);

	/**
	 * Get the display name for a mode (for UI labels).
	 */
	static FString GetModeDisplayName(EAutonomixAgentMode Mode);

	/**
	 * Get the "when to use this mode" hint for switch_mode dialog.
	 */
	static FString GetModeWhenToUse(EAutonomixAgentMode Mode);

	/**
	 * Tool names that are ALWAYS available in every mode (meta-tools).
	 * These are never restricted by mode filtering.
	 */
	static const TArray<FString>& GetAlwaysAvailableTools();

private:
	/** Map of tool name -> JSON schema object */
	TMap<FString, TSharedPtr<FJsonObject>> ToolSchemas;

	/** Set of disabled tool names */
	TSet<FString> DisabledTools;

	/** The base path for tool schema JSON files */
	FString GetSchemasDirectory() const;

	/** Get tool name prefix/suffix categories allowed for a given mode */
	static TArray<FString> GetAllowedToolNamesForMode(EAutonomixAgentMode Mode);
};
