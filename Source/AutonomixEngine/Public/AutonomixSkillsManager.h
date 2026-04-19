// Copyright Autonomix. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/**
 * A single skill document loaded from disk.
 */
struct AUTONOMIXENGINE_API FAutonomixSkill
{
	/** Skill name (filename without extension), e.g. "create-actor" */
	FString Name;

	/** Human-readable display name */
	FString DisplayName;

	/** Short description shown in autocomplete */
	FString Description;

	/** Full markdown content of the skill document */
	FString Content;

	/** File path of the skill document */
	FString FilePath;

	/** Tags for categorization */
	TArray<FString> Tags;
};

/**
 * Manages loadable skill documents (step-by-step instruction guides for Claude).
 *
 * Ported/adapted from Roo Code's skill tool concept.
 *
 * WHAT ARE SKILLS:
 *   Skills are Markdown files stored in Resources/Skills/ that contain detailed,
 *   step-by-step instructions for accomplishing common UE development tasks.
 *   When Claude calls the skill tool (e.g., skill("create-actor", "PickupItem")),
 *   the skill document is injected as context, guiding Claude through the workflow.
 *
 * BUILT-IN SKILLS:
 *   create-actor    — Full workflow: C++ header → source → Blueprint → test
 *   setup-input     — Enhanced Input system configuration
 *   create-interface — Blueprint interface creation and usage
 *   add-component   — Add and configure a UE component
 *   setup-replication — Configure actor replication for multiplayer
 *   create-hud      — Create and configure a HUD class
 *   add-save-game   — Add SaveGame system to a project
 *   setup-ai        — Configure AI character with Behavior Tree
 *
 * CUSTOM SKILLS:
 *   Users can create their own .md skill files in Resources/Skills/
 *   and they will be loaded automatically.
 *
 * TOOL INVOCATION:
 *   Claude calls: skill("create-actor", "PickupItem")
 *   The skill content is injected into the conversation as system context.
 *
 * SKILL DOCUMENT FORMAT:
 *   # Skill: [Name]
 *   ## Description
 *   Brief description of what this skill accomplishes.
 *   ## Arguments
 *   - {{arg}}: Description of the argument
 *   ## Steps
 *   1. Step one...
 *   2. Step two...
 *   ## Example
 *   ...
 */
class AUTONOMIXENGINE_API FAutonomixSkillsManager
{
public:
	FAutonomixSkillsManager();
	~FAutonomixSkillsManager();

	/** Load all skills from Resources/Skills/ directory */
	void Initialize();

	/**
	 * Get the content of a skill by name, with {{arg}} replaced.
	 *
	 * @param SkillName   Skill name (e.g., "create-actor")
	 * @param Args        Optional arguments to substitute for {{arg}}
	 * @param OutContent  The skill content with argument substitution
	 * @return true if skill was found
	 */
	bool GetSkillContent(
		const FString& SkillName,
		const FString& Args,
		FString& OutContent
	) const;

	/**
	 * Handle the skill tool call from Claude.
	 * Returns the skill content formatted as a tool result.
	 *
	 * @param SkillName   Skill name
	 * @param Args        Optional args string
	 * @return Tool result string (skill content or error message)
	 */
	FString HandleSkillToolCall(const FString& SkillName, const FString& Args) const;

	/** Get all available skills */
	const TArray<FAutonomixSkill>& GetAllSkills() const { return Skills; }

	/** Get skill names for autocomplete */
	TArray<FString> GetSkillNames() const;

	/** Get autocomplete suggestions for a partial skill name */
	TArray<FAutonomixSkill> GetSuggestions(const FString& Partial) const;

	/** Get number of loaded skills */
	int32 GetSkillCount() const { return Skills.Num(); }

	/** Get skills directory path */
	static FString GetSkillsDirectory();

private:
	void LoadSkillFromFile(const FString& FilePath);
	FAutonomixSkill ParseSkillDocument(const FString& Content, const FString& FilePath) const;

	TArray<FAutonomixSkill> Skills;
};
