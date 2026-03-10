// Copyright Autonomix. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AutonomixTypes.h"

/**
 * A single slash command definition.
 */
struct AUTONOMIXENGINE_API FAutonomixSlashCommand
{
	/** Command name (without /) — e.g. "new-actor" */
	FString Name;

	/** Display label for UI */
	FString DisplayName;

	/** Short description shown in autocomplete popup */
	FString Description;

	/** Usage hint shown in autocomplete (e.g., "/new-actor [ClassName]") */
	FString UsageHint;

	/** Template prompt injected when command is invoked.
	 *  Use {{arg}} for user-supplied arguments. */
	FString PromptTemplate;

	/** Optional: specific agent mode to switch to when this command runs */
	EAutonomixAgentMode SuggestedMode = EAutonomixAgentMode::General;

	/** Whether this command switches to a specific mode automatically */
	bool bAutoSwitchMode = false;
};

/**
 * Registry for slash commands (predefined workflow shortcuts).
 *
 * Ported/adapted from Roo Code's run_slash_command concept.
 *
 * SUPPORTED COMMANDS:
 *   /new-actor [ClassName]       Create actor C++ class + Blueprint scaffold
 *   /fix-errors                  Analyze and fix current compile errors
 *   /create-material [Name]      Material creation workflow
 *   /refactor [FilePath]         Analyze and refactor a C++ file
 *   /document [ClassName]        Generate documentation comments
 *   /add-component [ActorClass] [ComponentType]  Add component to actor
 *   /setup-input [ActionName]    Configure Enhanced Input mapping
 *   /create-interface [Name]     Create Blueprint interface
 *   /setup-replication [ActorClass]  Configure actor replication
 *   /optimize                    Analyze and optimize the selection/current level
 *
 * SLASH COMMAND EXPANSION:
 *   When user types "/new-actor MyPickup" in the input area, the text is expanded to
 *   the PromptTemplate with {{arg}} = "MyPickup" before being sent to Claude.
 *   The command may also switch the agent mode.
 *
 * ADDING CUSTOM COMMANDS:
 *   Custom commands can be loaded from Resources/SlashCommands/*.json at startup.
 *   Each JSON file can define one or more commands using the same schema.
 */
class AUTONOMIXENGINE_API FAutonomixSlashCommandRegistry
{
public:
	FAutonomixSlashCommandRegistry();
	~FAutonomixSlashCommandRegistry();

	/** Initialize built-in commands + load custom commands from Resources/SlashCommands/ */
	void Initialize();

	/** Check if input text starts with a known slash command */
	bool IsSlashCommand(const FString& InputText) const;

	/**
	 * Expand a slash command input to a full prompt string.
	 * @param InputText  Raw user input (e.g., "/new-actor MyPickup")
	 * @param OutExpandedPrompt  Full prompt for Claude
	 * @param OutSuggestedMode  Suggested mode to switch to (or General if no switch needed)
	 * @return true if a command was found and expanded
	 */
	bool ExpandSlashCommand(
		const FString& InputText,
		FString& OutExpandedPrompt,
		EAutonomixAgentMode& OutSuggestedMode
	) const;

	/** Get autocomplete suggestions for a partial slash command (e.g., "/new") */
	TArray<FAutonomixSlashCommand> GetSuggestions(const FString& Partial) const;

	/** Get all registered commands */
	const TArray<FAutonomixSlashCommand>& GetAllCommands() const { return Commands; }

	/** Register a new command */
	void RegisterCommand(const FAutonomixSlashCommand& Command);

	/** Get a command by name */
	const FAutonomixSlashCommand* GetCommand(const FString& Name) const;

private:
	TArray<FAutonomixSlashCommand> Commands;

	void RegisterBuiltinCommands();
	void LoadCustomCommandsFromDisk();
	static FString GetCommandsDirectory();
};
