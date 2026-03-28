// Copyright Autonomix. All Rights Reserved.

#include "AutonomixToolSchemaRegistry.h"
#include "AutonomixCoreModule.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/PlatformFileManager.h"
#include "Interfaces/IPluginManager.h"
#include "Serialization/JsonSerializer.h"

FAutonomixToolSchemaRegistry::FAutonomixToolSchemaRegistry()
{
}

FAutonomixToolSchemaRegistry::~FAutonomixToolSchemaRegistry()
{
}

FString FAutonomixToolSchemaRegistry::GetSchemasDirectory() const
{
	FString PluginBaseDir = FPaths::Combine(FPaths::ProjectPluginsDir(), TEXT("Autonomix"));
	FString SchemasDir = FPaths::Combine(PluginBaseDir, TEXT("Resources"), TEXT("ToolSchemas"));

	if (!FPaths::DirectoryExists(SchemasDir))
	{
		PluginBaseDir = FPaths::Combine(FPaths::EnginePluginsDir(), TEXT("Autonomix"));
		SchemasDir = FPaths::Combine(PluginBaseDir, TEXT("Resources"), TEXT("ToolSchemas"));
	}

	return SchemasDir;
}

void FAutonomixToolSchemaRegistry::LoadAllSchemas()
{
	FString SchemasDir = GetSchemasDirectory();

	if (!FPaths::DirectoryExists(SchemasDir))
	{
		UE_LOG(LogAutonomix, Warning, TEXT("ToolSchemaRegistry: Schemas directory not found: %s"), *SchemasDir);
		return;
	}

	TArray<FString> SchemaFiles;
	IFileManager::Get().FindFiles(SchemaFiles, *FPaths::Combine(SchemasDir, TEXT("*.json")), true, false);

	for (const FString& FileName : SchemaFiles)
	{
		FString FullPath = FPaths::Combine(SchemasDir, FileName);
		LoadSchemaFile(FullPath);
	}

	UE_LOG(LogAutonomix, Log, TEXT("ToolSchemaRegistry: Loaded %d tool schemas from %d files."),
		ToolSchemas.Num(), SchemaFiles.Num());
}

bool FAutonomixToolSchemaRegistry::LoadSchemaFile(const FString& FilePath)
{
	FString FileContent;
	if (!FFileHelper::LoadFileToString(FileContent, *FilePath))
	{
		UE_LOG(LogAutonomix, Error, TEXT("ToolSchemaRegistry: Failed to read file: %s"), *FilePath);
		return false;
	}

	// Try parsing as a JSON object first (versioned envelope format)
	TSharedPtr<FJsonObject> RootObj;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(FileContent);
	if (FJsonSerializer::Deserialize(Reader, RootObj) && RootObj.IsValid())
	{
		// Check if this is a versioned schema envelope
		FString SchemaVersion;
		if (RootObj->TryGetStringField(TEXT("schema_version"), SchemaVersion))
		{
			// Versioned format: { "schema_version": "1.0.0", "domain": "...", "tools": [...] }
			FString Domain;
			RootObj->TryGetStringField(TEXT("domain"), Domain);

			const TArray<TSharedPtr<FJsonValue>>* ToolsArray = nullptr;
			if (RootObj->TryGetArrayField(TEXT("tools"), ToolsArray))
			{
				for (const TSharedPtr<FJsonValue>& ToolValue : *ToolsArray)
				{
					const TSharedPtr<FJsonObject>* ToolObj = nullptr;
					if (ToolValue->TryGetObject(ToolObj) && ToolObj->IsValid())
					{
						FString ToolName;
						if ((*ToolObj)->TryGetStringField(TEXT("name"), ToolName))
						{
							ToolSchemas.Add(ToolName, *ToolObj);
						}
					}
				}

				UE_LOG(LogAutonomix, Verbose, TEXT("ToolSchemaRegistry: Loaded %s v%s (%d tools)"),
					*Domain, *SchemaVersion, ToolsArray->Num());
				return true;
			}
		}

		// Single tool object (non-versioned, non-array)
		FString ToolName;
		if (RootObj->TryGetStringField(TEXT("name"), ToolName))
		{
			ToolSchemas.Add(ToolName, RootObj);
			return true;
		}
	}

	// Try parsing as a raw JSON array (legacy format: [tool, tool, ...])
	TArray<TSharedPtr<FJsonValue>> ToolsArray;
	Reader = TJsonReaderFactory<>::Create(FileContent);
	if (FJsonSerializer::Deserialize(Reader, ToolsArray))
	{
		for (const TSharedPtr<FJsonValue>& ToolValue : ToolsArray)
		{
			const TSharedPtr<FJsonObject>* ToolObj = nullptr;
			if (ToolValue->TryGetObject(ToolObj) && ToolObj->IsValid())
			{
				FString ToolName;
				if ((*ToolObj)->TryGetStringField(TEXT("name"), ToolName))
				{
					ToolSchemas.Add(ToolName, *ToolObj);
				}
			}
		}
		return true;
	}

	UE_LOG(LogAutonomix, Error, TEXT("ToolSchemaRegistry: Failed to parse schema file: %s"), *FilePath);
	return false;
}

/** Maximum characters for a tool description in schemas sent to the LLM.
 *  Long descriptions waste tokens without improving tool selection.
 *  ~200 chars = ~50 tokens per tool. At 93 tools = ~4,650 tokens for descriptions.
 *  Without truncation: ~35,000 tokens. Savings: ~86%. */
static constexpr int32 MaxDescriptionChars = 200;

/** Truncate a description string to fit within the token budget */
static FString TruncateDescription(const FString& Desc)
{
	if (Desc.Len() <= MaxDescriptionChars)
	{
		return Desc;
	}
	// Truncate at a word boundary near the limit
	int32 CutPos = MaxDescriptionChars;
	while (CutPos > 0 && Desc[CutPos] != TEXT(' '))
	{
		CutPos--;
	}
	if (CutPos == 0) CutPos = MaxDescriptionChars;
	return Desc.Left(CutPos) + TEXT("...");
}

/** Create a lightweight clone of a schema with truncated description.
 *  Only clones when truncation is needed — returns original pointer otherwise. */
static TSharedPtr<FJsonObject> MakeTruncatedSchema(const TSharedPtr<FJsonObject>& Original)
{
	if (!Original.IsValid()) return Original;

	FString Desc;
	if (!Original->TryGetStringField(TEXT("description"), Desc) || Desc.Len() <= MaxDescriptionChars)
	{
		return Original;  // No truncation needed
	}

	// Shallow clone: copy all fields, replace only description
	TSharedPtr<FJsonObject> Clone = MakeShared<FJsonObject>();
	for (const auto& Pair : Original->Values)
	{
		Clone->SetField(Pair.Key, Pair.Value);
	}
	Clone->SetStringField(TEXT("description"), TruncateDescription(Desc));
	return Clone;
}

TArray<TSharedPtr<FJsonObject>> FAutonomixToolSchemaRegistry::GetAllSchemas() const
{
	TArray<TSharedPtr<FJsonObject>> Result;
	for (const auto& Pair : ToolSchemas)
	{
		Result.Add(MakeTruncatedSchema(Pair.Value));
	}
	return Result;
}

TArray<TSharedPtr<FJsonObject>> FAutonomixToolSchemaRegistry::GetSchemasByCategory(const FString& Category) const
{
	TArray<TSharedPtr<FJsonObject>> Result;
	for (const auto& Pair : ToolSchemas)
	{
		FString SchemaCategory;
		if (Pair.Value->TryGetStringField(TEXT("category"), SchemaCategory) && SchemaCategory == Category)
		{
			Result.Add(Pair.Value);
		}
	}
	return Result;
}

TSharedPtr<FJsonObject> FAutonomixToolSchemaRegistry::GetSchemaByName(const FString& ToolName) const
{
	const TSharedPtr<FJsonObject>* Found = ToolSchemas.Find(ToolName);
	return Found ? *Found : nullptr;
}

bool FAutonomixToolSchemaRegistry::IsToolRegistered(const FString& ToolName) const
{
	return ToolSchemas.Contains(ToolName);
}

TArray<FString> FAutonomixToolSchemaRegistry::GetAllToolNames() const
{
	TArray<FString> Names;
	ToolSchemas.GetKeys(Names);
	return Names;
}

void FAutonomixToolSchemaRegistry::SetToolEnabled(const FString& ToolName, bool bEnabled)
{
	if (bEnabled)
	{
		DisabledTools.Remove(ToolName);
	}
	else
	{
		DisabledTools.Add(ToolName);
	}
}

bool FAutonomixToolSchemaRegistry::IsToolEnabled(const FString& ToolName) const
{
	return !DisabledTools.Contains(ToolName);
}

TArray<TSharedPtr<FJsonObject>> FAutonomixToolSchemaRegistry::GetEnabledSchemas() const
{
	TArray<TSharedPtr<FJsonObject>> Result;
	for (const auto& Pair : ToolSchemas)
	{
		if (!DisabledTools.Contains(Pair.Key))
		{
			Result.Add(MakeTruncatedSchema(Pair.Value));
		}
	}
	return Result;
}

void FAutonomixToolSchemaRegistry::SyncWithRegisteredTools(const TArray<FString>& RegisteredToolNames)
{
	// Build a set for O(1) lookups
	TSet<FString> RegisteredSet;
	for (const FString& Name : RegisteredToolNames)
	{
		RegisteredSet.Add(Name);
	}

	// Meta-tools are always available even without an executor (they are handled
	// by the main panel directly, not through the ActionRouter)
	const TArray<FString>& AlwaysAvailable = GetAlwaysAvailableTools();

	int32 DisabledCount = 0;
	for (const auto& Pair : ToolSchemas)
	{
		const FString& ToolName = Pair.Key;

		// Skip meta-tools — they're handled outside the executor pipeline
		if (AlwaysAvailable.Contains(ToolName))
		{
			continue;
		}

		if (!RegisteredSet.Contains(ToolName))
		{
			// This schema has no executor registered — disable it so the LLM won't see it
			DisabledTools.Add(ToolName);
			++DisabledCount;
		}
		else
		{
			// Executor exists — make sure it's not disabled by a previous sync
			DisabledTools.Remove(ToolName);
		}
	}

	if (DisabledCount > 0)
	{
		UE_LOG(LogAutonomix, Log, TEXT("ToolSchemaRegistry: SyncWithRegisteredTools disabled %d schemas with no registered executor."), DisabledCount);
	}
}

// ============================================================================
// Mode-based filtering (v2.0)
// ============================================================================

const TArray<FString>& FAutonomixToolSchemaRegistry::GetAlwaysAvailableTools()
{
	static TArray<FString> Always = {
		TEXT("update_todo_list"),
		TEXT("switch_mode"),
		TEXT("attempt_completion"),
		TEXT("ask_followup_question"),
		TEXT("new_task"),
	};
	return Always;
}

TArray<FString> FAutonomixToolSchemaRegistry::GetAllowedToolNamesForMode(EAutonomixAgentMode Mode)
{
	// These are substring/prefix patterns — a tool is allowed if its name CONTAINS one of these
	switch (Mode)
	{
	case EAutonomixAgentMode::Orchestrator:
		// Orchestrator: read tools + new_task + switch_mode (no direct write tools)
		return {
			TEXT("read_file"),
			TEXT("get_file"),
			TEXT("list_files"),
			TEXT("search_files"),
			TEXT("get_asset"),
			TEXT("find_asset"),
			TEXT("get_class"),
			TEXT("get_context"),
		};

	case EAutonomixAgentMode::General:
		// All tools
		return {};  // Empty = allow all

	case EAutonomixAgentMode::Blueprint:
		return {
			TEXT("blueprint"),
			TEXT("read_file"),
			TEXT("get_file"),
			TEXT("list_files"),
			TEXT("search_files"),
			TEXT("get_asset"),
			TEXT("find_asset"),
		};

	case EAutonomixAgentMode::CppCode:
		return {
			TEXT("read_file"),
			TEXT("get_file"),
			TEXT("list_files"),
			TEXT("search_files"),
			TEXT("write_file"),
			TEXT("create_file"),
			TEXT("apply_diff"),
			TEXT("edit_file"),
			TEXT("cpp"),
			TEXT("build"),
			TEXT("compile"),
			TEXT("generate"),
		};

	case EAutonomixAgentMode::Architect:
		// Read-only: no write/execute tools
		return {
			TEXT("read_file"),
			TEXT("get_file"),
			TEXT("list_files"),
			TEXT("search_files"),
			TEXT("get_asset"),
			TEXT("find_asset"),
			TEXT("get_class"),
			TEXT("get_context"),
		};

	case EAutonomixAgentMode::Debug:
		return {
			TEXT("read_file"),
			TEXT("get_file"),
			TEXT("list_files"),
			TEXT("search_files"),
			TEXT("execute"),
			TEXT("run"),
			TEXT("build"),
			TEXT("compile"),
			TEXT("get_log"),
			TEXT("get_error"),
		};

	case EAutonomixAgentMode::Asset:
		return {
			TEXT("read_file"),
			TEXT("get_file"),
			TEXT("list_files"),
			TEXT("search_files"),
			TEXT("material"),
			TEXT("texture"),
			TEXT("mesh"),
			TEXT("audio"),
			TEXT("animation"),
			TEXT("asset"),
			TEXT("import"),
		};
	}

	return {};
}

TSet<FString> FAutonomixToolSchemaRegistry::GetAllowedCategoriesForMode(EAutonomixAgentMode Mode) const
{
	switch (Mode)
	{
	case EAutonomixAgentMode::General:
		return {};  // All categories

	case EAutonomixAgentMode::Blueprint:
		return { TEXT("Blueprint"), TEXT("General"), TEXT("FileSystem") };

	case EAutonomixAgentMode::CppCode:
		return { TEXT("Cpp"), TEXT("Build"), TEXT("General"), TEXT("FileSystem") };

	case EAutonomixAgentMode::Architect:
		return { TEXT("General"), TEXT("FileSystem") };  // Read-only subset

	case EAutonomixAgentMode::Debug:
		return { TEXT("Build"), TEXT("General"), TEXT("FileSystem") };

	case EAutonomixAgentMode::Asset:
		return { TEXT("Material"), TEXT("Mesh"), TEXT("Texture"), TEXT("Audio"), TEXT("Animation"), TEXT("FileSystem"), TEXT("General") };

	case EAutonomixAgentMode::Orchestrator:
		return { TEXT("General"), TEXT("FileSystem") };  // Read-only — delegates work via new_task
	}

	return {};
}

TArray<TSharedPtr<FJsonObject>> FAutonomixToolSchemaRegistry::GetSchemasForMode(EAutonomixAgentMode Mode) const
{
	// General mode = all enabled schemas
	if (Mode == EAutonomixAgentMode::General)
	{
		return GetEnabledSchemas();
	}

	const TArray<FString>& AlwaysAvailable = GetAlwaysAvailableTools();
	const TArray<FString> AllowedPatterns = GetAllowedToolNamesForMode(Mode);

	TArray<TSharedPtr<FJsonObject>> Result;

	for (const auto& Pair : ToolSchemas)
	{
		if (DisabledTools.Contains(Pair.Key)) continue;

		const FString& ToolName = Pair.Key;

		// Always-available tools are included in every mode
		if (AlwaysAvailable.Contains(ToolName))
		{
			Result.Add(MakeTruncatedSchema(Pair.Value));
			continue;
		}

		// Check if tool name matches any of the mode's allowed patterns
		bool bAllowed = false;
		for (const FString& Pattern : AllowedPatterns)
		{
			if (ToolName.Contains(Pattern, ESearchCase::IgnoreCase))
			{
				bAllowed = true;
				break;
			}
		}

		if (bAllowed)
		{
			Result.Add(MakeTruncatedSchema(Pair.Value));
		}
	}

	return Result;
}

FString FAutonomixToolSchemaRegistry::GetModeRoleDefinition(EAutonomixAgentMode Mode)
{
	switch (Mode)
	{
	case EAutonomixAgentMode::General:
		return TEXT(
			"You are Autonomix, an AI assistant for Unreal Engine development. "
			"You have access to all available tools and can help with any aspect of UE development: "
			"C++ code, Blueprints, materials, levels, assets, builds, and project configuration."
		);

	case EAutonomixAgentMode::Blueprint:
		return TEXT(
			"You are Autonomix in Blueprint mode. Your role is to design and implement Blueprint logic. "
			"You can read any file and create/modify Blueprints. "
			"You do NOT have access to C++ write tools in this mode — use switch_mode to change to C++ Code mode if needed. "
			"Focus on visual scripting, event graphs, and Blueprint communication patterns."
		);

	case EAutonomixAgentMode::CppCode:
		return TEXT(
			"You are Autonomix in C++ Code mode. Your role is to write, modify, and compile Unreal Engine C++ code. "
			"You can read and write source files, apply diffs, and run builds. "
			"You do NOT have access to Blueprint or asset tools in this mode. "
			"Follow UE coding conventions: UCLASS, UPROPERTY, UFUNCTION macros; header/source split; "
			"FString/TArray/TMap over std:: types; check() and ensure() over assert()."
		);

	case EAutonomixAgentMode::Architect:
		return TEXT(
			"You are Autonomix in Architect mode. Your role is to analyze, plan, and explain — NOT to make changes. "
			"You have READ-ONLY access to the codebase. You cannot write, create, or modify any files. "
			"Use this mode to: review code, design systems, identify issues, plan refactoring, "
			"explain architecture, and generate recommendations. "
			"When you have a plan ready, use switch_mode to hand off to the appropriate implementation mode."
		);

	case EAutonomixAgentMode::Debug:
		return TEXT(
			"You are Autonomix in Debug mode. Your role is to diagnose and fix issues. "
			"You can read files, run builds, and execute diagnostic commands. "
			"You do NOT have access to create or modify source files directly — "
			"use switch_mode to C++ Code or Blueprint mode to apply fixes. "
			"Focus on: reading error messages, tracing call stacks, understanding crash reports, "
			"and identifying the root cause before proposing solutions."
		);

	case EAutonomixAgentMode::Asset:
		return TEXT(
			"You are Autonomix in Asset mode. Your role is to work with Unreal Engine assets: "
			"materials, textures, meshes, audio, and animations. "
			"You do NOT have access to C++ or Blueprint code tools in this mode. "
			"Focus on asset creation, modification, organization, and import workflows."
		);

	case EAutonomixAgentMode::Orchestrator:
		return TEXT(
			"You are Autonomix in Orchestrator mode. Your role is to coordinate complex, "
			"multi-step Unreal Engine projects by breaking them into focused sub-tasks "
			"and delegating each to the appropriate specialist mode.\n\n"
			"You have access to READ tools and the new_task tool. You do NOT have direct write access.\n\n"
			"Your workflow:\n"
			"1. Analyze the user's request and identify all sub-tasks\n"
			"2. Use new_task to delegate C++ work to cpp_code mode\n"
			"3. Use new_task to delegate Blueprint work to blueprint mode\n"
			"4. Use new_task to delegate asset work to asset mode\n"
			"5. Use new_task to delegate debugging to debug mode\n"
			"6. Monitor progress and coordinate between sub-tasks\n"
			"7. Use attempt_completion when all sub-tasks are complete\n\n"
			"Think of yourself as a technical project manager who delegates implementation "
			"to specialist agents while maintaining overall project coherence."
		);
	}

	return TEXT("You are Autonomix, an AI assistant for Unreal Engine development.");
}

FString FAutonomixToolSchemaRegistry::GetModeDisplayName(EAutonomixAgentMode Mode)
{
	switch (Mode)
	{
	case EAutonomixAgentMode::General:      return TEXT("General");
	case EAutonomixAgentMode::Blueprint:    return TEXT("Blueprint");
	case EAutonomixAgentMode::CppCode:      return TEXT("C++ Code");
	case EAutonomixAgentMode::Architect:    return TEXT("Architect");
	case EAutonomixAgentMode::Debug:        return TEXT("Debug");
	case EAutonomixAgentMode::Asset:        return TEXT("Asset");
	case EAutonomixAgentMode::Orchestrator: return TEXT("Orchestrator");
	}
	return TEXT("General");
}

TArray<TSharedPtr<FJsonObject>> FAutonomixToolSchemaRegistry::GetEssentialSchemas() const
{
	// =========================================================================
	// Essential tool set for local providers (Ollama, LM Studio).
	//
	// Local models with 8K-64K context windows cannot handle 90+ tool schemas.
	// The full schema set alone = ~5,000 tokens (post-truncation); with project
	// context and system prompt, this easily exceeds the model's capacity.
	//
	// Symptoms when overloaded:
	//   - "I currently do not have the capability to create files"
	//   - "I can't access your files or personal data"
	//   - Model ignores tools entirely and responds with plain text
	//
	// This method returns only the ~15 core tools needed for useful work.
	// Token cost: ~750 tokens (vs ~5,000 for full set, ~35K for untruncated).
	//
	// The tools selected mirror Roo Code's core tool set:
	//   - File system: read_file, write_file, apply_diff, list_directory, search_files
	//   - Context: search_assets, get_blueprint_info
	//   - Meta: attempt_completion, ask_followup_question, update_todo_list, switch_mode, new_task
	// =========================================================================
	static const TSet<FString> EssentialToolNames = {
		// File operations (core agentic workflow)
		TEXT("read_file"),
		TEXT("write_file"),
		TEXT("apply_diff"),
		TEXT("list_directory"),
		TEXT("search_files"),

		// Asset/context queries
		TEXT("search_assets"),
		TEXT("get_blueprint_info"),
		TEXT("get_project_context"),

		// Blueprint basics (most common UE task)
		TEXT("inject_blueprint_nodes_t3d"),
		TEXT("connect_blueprint_pins"),

		// Meta-tools (always needed)
		TEXT("attempt_completion"),
		TEXT("ask_followup_question"),
		TEXT("update_todo_list"),
		TEXT("switch_mode"),
		TEXT("new_task"),
	};

	TArray<TSharedPtr<FJsonObject>> Result;
	for (const auto& Pair : ToolSchemas)
	{
		if (DisabledTools.Contains(Pair.Key)) continue;

		if (EssentialToolNames.Contains(Pair.Key))
		{
			Result.Add(MakeTruncatedSchema(Pair.Value));
		}
	}

	UE_LOG(LogAutonomix, Log, TEXT("ToolSchemaRegistry: GetEssentialSchemas() returned %d tools (from %d total)."),
		Result.Num(), ToolSchemas.Num());

	return Result;
}

FString FAutonomixToolSchemaRegistry::GetModeWhenToUse(EAutonomixAgentMode Mode)
{
	switch (Mode)
	{
	case EAutonomixAgentMode::General:
		return TEXT("Use for mixed tasks spanning multiple domains.");
	case EAutonomixAgentMode::Blueprint:
		return TEXT("Use when working exclusively with Blueprint visual scripting.");
	case EAutonomixAgentMode::CppCode:
		return TEXT("Use when implementing or modifying C++ source code.");
	case EAutonomixAgentMode::Architect:
		return TEXT("Use for planning, code review, and design without making changes.");
	case EAutonomixAgentMode::Debug:
		return TEXT("Use when diagnosing compile errors, crashes, or unexpected behavior.");
	case EAutonomixAgentMode::Asset:
		return TEXT("Use when working with materials, meshes, textures, or other assets.");
	case EAutonomixAgentMode::Orchestrator:
		return TEXT("Use for complex multi-step projects requiring coordination across multiple domains.");
	}
	return FString();
}
