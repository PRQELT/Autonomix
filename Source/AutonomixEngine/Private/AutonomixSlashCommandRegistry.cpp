// Copyright Autonomix. All Rights Reserved.

#include "AutonomixSlashCommandRegistry.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "Serialization/JsonSerializer.h"

FAutonomixSlashCommandRegistry::FAutonomixSlashCommandRegistry()
{
}

FAutonomixSlashCommandRegistry::~FAutonomixSlashCommandRegistry()
{
}

void FAutonomixSlashCommandRegistry::Initialize()
{
	RegisterBuiltinCommands();
	LoadCustomCommandsFromDisk();
}

void FAutonomixSlashCommandRegistry::RegisterBuiltinCommands()
{
	// ---- Actor Creation ----
	FAutonomixSlashCommand NewActor;
	NewActor.Name = TEXT("new-actor");
	NewActor.DisplayName = TEXT("New Actor");
	NewActor.Description = TEXT("Create a new C++ Actor class with Blueprint scaffold");
	NewActor.UsageHint = TEXT("/new-actor [ClassName]");
	NewActor.PromptTemplate =
		TEXT("Create a new Unreal Engine Actor class named '{{arg}}'. Please:\n"
		     "1. Create the C++ header file ({{arg}}.h) with UCLASS, GENERATED_BODY, BeginPlay, Tick\n"
		     "2. Create the C++ source file ({{arg}}.cpp) with implementations\n"
		     "3. Follow UE5 coding conventions\n"
		     "4. Add appropriate UPROPERTY and UFUNCTION macros for Blueprint exposure\n"
		     "5. Include a brief description comment at the top explaining the actor's purpose\n\n"
		     "Actor name: {{arg}}");
	NewActor.SuggestedMode = EAutonomixAgentMode::CppCode;
	NewActor.bAutoSwitchMode = true;
	Commands.Add(NewActor);

	// ---- Fix Errors ----
	FAutonomixSlashCommand FixErrors;
	FixErrors.Name = TEXT("fix-errors");
	FixErrors.DisplayName = TEXT("Fix Compile Errors");
	FixErrors.Description = TEXT("Analyze current compile errors and apply fixes");
	FixErrors.UsageHint = TEXT("/fix-errors");
	FixErrors.PromptTemplate =
		TEXT("Please analyze and fix the current compile errors in this Unreal Engine project.\n\n"
		     "Steps:\n"
		     "1. Read the error messages from the Output Log\n"
		     "2. Identify the root cause of each error\n"
		     "3. Apply the minimal changes to fix the errors\n"
		     "4. Verify the fixes don't introduce new issues\n\n"
		     "@errors");
	FixErrors.SuggestedMode = EAutonomixAgentMode::Debug;
	FixErrors.bAutoSwitchMode = true;
	Commands.Add(FixErrors);

	// ---- Create Material ----
	FAutonomixSlashCommand CreateMaterial;
	CreateMaterial.Name = TEXT("create-material");
	CreateMaterial.DisplayName = TEXT("Create Material");
	CreateMaterial.Description = TEXT("Create a new Unreal Engine material");
	CreateMaterial.UsageHint = TEXT("/create-material [MaterialName]");
	CreateMaterial.PromptTemplate =
		TEXT("Create a new Unreal Engine material named '{{arg}}'. Please:\n"
		     "1. Create the material asset in an appropriate Content directory\n"
		     "2. Set up the material nodes for the intended use case\n"
		     "3. Configure material properties (blend mode, shading model, etc.)\n"
		     "4. Create a Material Instance if needed for runtime variation\n\n"
		     "Material name: {{arg}}");
	CreateMaterial.SuggestedMode = EAutonomixAgentMode::Asset;
	CreateMaterial.bAutoSwitchMode = true;
	Commands.Add(CreateMaterial);

	// ---- Refactor ----
	FAutonomixSlashCommand Refactor;
	Refactor.Name = TEXT("refactor");
	Refactor.DisplayName = TEXT("Refactor File");
	Refactor.Description = TEXT("Analyze and refactor a C++ file");
	Refactor.UsageHint = TEXT("/refactor [FilePath]");
	Refactor.PromptTemplate =
		TEXT("Please analyze and refactor the file '{{arg}}'.\n\n"
		     "Focus on:\n"
		     "1. Code clarity and readability\n"
		     "2. UE5 best practices and conventions\n"
		     "3. Performance improvements\n"
		     "4. Proper use of UE types (FString, TArray, TMap, etc.)\n"
		     "5. Reducing unnecessary includes\n"
		     "6. Adding missing documentation comments\n\n"
		     "File: @/{{arg}}");
	Refactor.SuggestedMode = EAutonomixAgentMode::CppCode;
	Refactor.bAutoSwitchMode = true;
	Commands.Add(Refactor);

	// ---- Document ----
	FAutonomixSlashCommand Document;
	Document.Name = TEXT("document");
	Document.DisplayName = TEXT("Document Class");
	Document.Description = TEXT("Generate documentation comments for a C++ class");
	Document.UsageHint = TEXT("/document [ClassName or FilePath]");
	Document.PromptTemplate =
		TEXT("Please generate comprehensive documentation comments for '{{arg}}'.\n\n"
		     "Include:\n"
		     "1. Class-level Doxygen comment explaining purpose, usage, and design\n"
		     "2. /** */ comments for each UFUNCTION and UPROPERTY\n"
		     "3. Brief inline comments for non-obvious logic\n"
		     "4. Parameter and return value documentation\n\n"
		     "Target: {{arg}}");
	Document.SuggestedMode = EAutonomixAgentMode::Architect;
	Document.bAutoSwitchMode = false;
	Commands.Add(Document);

	// ---- Add Component ----
	FAutonomixSlashCommand AddComponent;
	AddComponent.Name = TEXT("add-component");
	AddComponent.DisplayName = TEXT("Add Component");
	AddComponent.Description = TEXT("Add a component to an actor");
	AddComponent.UsageHint = TEXT("/add-component [ActorClass] [ComponentType]");
	AddComponent.PromptTemplate =
		TEXT("Add a {{arg2}} component to the actor class '{{arg1}}'.\n\n"
		     "Steps:\n"
		     "1. Add the component declaration in the header (UPROPERTY)\n"
		     "2. Create and attach the component in the constructor\n"
		     "3. Configure default properties\n"
		     "4. Add any necessary event handlers\n\n"
		     "Actor: {{arg1}}, Component: {{arg2}}");
	AddComponent.SuggestedMode = EAutonomixAgentMode::CppCode;
	AddComponent.bAutoSwitchMode = true;
	Commands.Add(AddComponent);

	// ---- Setup Input ----
	FAutonomixSlashCommand SetupInput;
	SetupInput.Name = TEXT("setup-input");
	SetupInput.DisplayName = TEXT("Setup Input");
	SetupInput.Description = TEXT("Configure Enhanced Input action mapping");
	SetupInput.UsageHint = TEXT("/setup-input [ActionName]");
	SetupInput.PromptTemplate =
		TEXT("Set up Enhanced Input for the action '{{arg}}'.\n\n"
		     "Steps:\n"
		     "1. Create an Input Action asset for '{{arg}}'\n"
		     "2. Add the mapping to the Input Mapping Context\n"
		     "3. Bind the action in the appropriate character/pawn class\n"
		     "4. Implement the action handler function\n\n"
		     "Action name: {{arg}}");
	SetupInput.SuggestedMode = EAutonomixAgentMode::Blueprint;
	SetupInput.bAutoSwitchMode = false;
	Commands.Add(SetupInput);

	// ---- Create Interface ----
	FAutonomixSlashCommand CreateInterface;
	CreateInterface.Name = TEXT("create-interface");
	CreateInterface.DisplayName = TEXT("Create Interface");
	CreateInterface.Description = TEXT("Create a Blueprint interface with implementation");
	CreateInterface.UsageHint = TEXT("/create-interface [InterfaceName]");
	CreateInterface.PromptTemplate =
		TEXT("Create a Blueprint interface named '{{arg}}'.\n\n"
		     "Steps:\n"
		     "1. Create the interface asset (I{{arg}})\n"
		     "2. Define the interface functions\n"
		     "3. Show an example of implementing the interface on an actor\n"
		     "4. Show how to call the interface function with IsValid check\n\n"
		     "Interface name: {{arg}}");
	CreateInterface.SuggestedMode = EAutonomixAgentMode::Blueprint;
	CreateInterface.bAutoSwitchMode = false;
	Commands.Add(CreateInterface);

	// ---- Setup Replication ----
	FAutonomixSlashCommand SetupReplication;
	SetupReplication.Name = TEXT("setup-replication");
	SetupReplication.DisplayName = TEXT("Setup Replication");
	SetupReplication.Description = TEXT("Configure actor replication for multiplayer");
	SetupReplication.UsageHint = TEXT("/setup-replication [ActorClass]");
	SetupReplication.PromptTemplate =
		TEXT("Configure replication for the actor class '{{arg}}'.\n\n"
		     "Steps:\n"
		     "1. Enable bReplicates in the constructor\n"
		     "2. Add GetLifetimeReplicatedProps with appropriate DOREPLIFETIME macros\n"
		     "3. Mark properties for replication with UPROPERTY(Replicated) or ReplicatedUsing\n"
		     "4. Implement OnRep_ functions for properties with callbacks\n"
		     "5. Add server RPCs for player actions\n"
		     "6. Add NetMulticast RPCs for effects\n\n"
		     "Actor class: {{arg}}");
	SetupReplication.SuggestedMode = EAutonomixAgentMode::CppCode;
	SetupReplication.bAutoSwitchMode = true;
	Commands.Add(SetupReplication);
}

void FAutonomixSlashCommandRegistry::LoadCustomCommandsFromDisk()
{
	const FString CmdsDir = GetCommandsDirectory();
	if (!FPaths::DirectoryExists(CmdsDir)) return;

	TArray<FString> Files;
	IFileManager::Get().FindFiles(Files, *FPaths::Combine(CmdsDir, TEXT("*.json")), true, false);

	for (const FString& FileName : Files)
	{
		FString FilePath = FPaths::Combine(CmdsDir, FileName);
		FString Content;
		if (!FFileHelper::LoadFileToString(Content, *FilePath)) continue;

		TSharedPtr<FJsonObject> Root;
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Content);
		if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid()) continue;

		FAutonomixSlashCommand Cmd;
		Root->TryGetStringField(TEXT("name"), Cmd.Name);
		Root->TryGetStringField(TEXT("display_name"), Cmd.DisplayName);
		Root->TryGetStringField(TEXT("description"), Cmd.Description);
		Root->TryGetStringField(TEXT("usage_hint"), Cmd.UsageHint);
		Root->TryGetStringField(TEXT("prompt_template"), Cmd.PromptTemplate);

		if (!Cmd.Name.IsEmpty() && !Cmd.PromptTemplate.IsEmpty())
		{
			Commands.Add(Cmd);
		}
	}
}

FString FAutonomixSlashCommandRegistry::GetCommandsDirectory()
{
	FString PluginBaseDir = FPaths::Combine(FPaths::ProjectPluginsDir(), TEXT("Autonomix"));
	return FPaths::Combine(PluginBaseDir, TEXT("Resources"), TEXT("SlashCommands"));
}

bool FAutonomixSlashCommandRegistry::IsSlashCommand(const FString& InputText) const
{
	FString Trimmed = InputText.TrimStart();
	return Trimmed.StartsWith(TEXT("/"));
}

bool FAutonomixSlashCommandRegistry::ExpandSlashCommand(
	const FString& InputText,
	FString& OutExpandedPrompt,
	EAutonomixAgentMode& OutSuggestedMode
) const
{
	FString Trimmed = InputText.TrimStart();
	if (!Trimmed.StartsWith(TEXT("/"))) return false;

	// Parse: /command-name [arg]
	FString CommandPart = Trimmed.Mid(1);  // Remove leading /
	FString CommandName, Arg;

	int32 SpaceIdx;
	if (CommandPart.FindChar(TEXT(' '), SpaceIdx))
	{
		CommandName = CommandPart.Left(SpaceIdx).ToLower();
		Arg = CommandPart.Mid(SpaceIdx + 1).TrimStartAndEnd();
	}
	else
	{
		CommandName = CommandPart.ToLower();
	}

	const FAutonomixSlashCommand* Cmd = GetCommand(CommandName);
	if (!Cmd) return false;

	// Expand template
	FString Expanded = Cmd->PromptTemplate;
	Expanded.ReplaceInline(TEXT("{{arg}}"), *Arg);

	// Handle {{arg1}} and {{arg2}} for two-argument commands
	TArray<FString> Args;
	Arg.ParseIntoArray(Args, TEXT(" "));
	if (Args.Num() >= 1) Expanded.ReplaceInline(TEXT("{{arg1}}"), *Args[0]);
	if (Args.Num() >= 2) Expanded.ReplaceInline(TEXT("{{arg2}}"), *Args[1]);

	OutExpandedPrompt = Expanded;
	OutSuggestedMode = Cmd->bAutoSwitchMode ? Cmd->SuggestedMode : EAutonomixAgentMode::General;
	return true;
}

TArray<FAutonomixSlashCommand> FAutonomixSlashCommandRegistry::GetSuggestions(const FString& Partial) const
{
	TArray<FAutonomixSlashCommand> Suggestions;
	FString SearchStr = Partial.TrimStart().ToLower();
	if (SearchStr.StartsWith(TEXT("/")))
	{
		SearchStr = SearchStr.Mid(1);
	}

	for (const FAutonomixSlashCommand& Cmd : Commands)
	{
		if (Cmd.Name.ToLower().StartsWith(SearchStr))
		{
			Suggestions.Add(Cmd);
		}
	}

	return Suggestions;
}

void FAutonomixSlashCommandRegistry::RegisterCommand(const FAutonomixSlashCommand& Command)
{
	// Replace if exists
	for (int32 i = 0; i < Commands.Num(); i++)
	{
		if (Commands[i].Name == Command.Name)
		{
			Commands[i] = Command;
			return;
		}
	}
	Commands.Add(Command);
}

const FAutonomixSlashCommand* FAutonomixSlashCommandRegistry::GetCommand(const FString& Name) const
{
	for (const FAutonomixSlashCommand& Cmd : Commands)
	{
		if (Cmd.Name.Equals(Name, ESearchCase::IgnoreCase))
		{
			return &Cmd;
		}
	}
	return nullptr;
}
