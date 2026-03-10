// Copyright Autonomix. All Rights Reserved.

#include "AutonomixSkillsManager.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

FAutonomixSkillsManager::FAutonomixSkillsManager()
{
}

FAutonomixSkillsManager::~FAutonomixSkillsManager()
{
}

void FAutonomixSkillsManager::Initialize()
{
	EnsureBuiltinSkills();

	// Load global skills from Resources/Skills/*.md
	const FString SkillsDir = GetSkillsDirectory();
	if (FPaths::DirectoryExists(SkillsDir))
	{
		TArray<FString> Files;
		IFileManager::Get().FindFiles(Files, *FPaths::Combine(SkillsDir, TEXT("*.md")), true, false);
		for (const FString& FileName : Files)
		{
			LoadSkillFromFile(FPaths::Combine(SkillsDir, FileName));
		}
	}

	// Load mode-specific skills from Resources/Skills-[mode]/*.md
	// These skills are only available when in the matching mode
	static const TArray<FString> ModeNames = {
		TEXT("general"), TEXT("blueprint"), TEXT("cpp_code"),
		TEXT("architect"), TEXT("debug"), TEXT("asset"), TEXT("orchestrator")
	};

	FString PluginBaseDir = FPaths::Combine(FPaths::ProjectPluginsDir(), TEXT("Autonomix"));
	for (const FString& ModeName : ModeNames)
	{
		const FString ModeSkillsDir = FPaths::Combine(
			PluginBaseDir, TEXT("Resources"),
			FString::Printf(TEXT("Skills-%s"), *ModeName)
		);
		if (FPaths::DirectoryExists(ModeSkillsDir))
		{
			TArray<FString> ModeFiles;
			IFileManager::Get().FindFiles(ModeFiles, *FPaths::Combine(ModeSkillsDir, TEXT("*.md")), true, false);
			for (const FString& FileName : ModeFiles)
			{
				FAutonomixSkill Skill = ParseSkillDocument(
					FString(),  // will load from file
					FPaths::Combine(ModeSkillsDir, FileName)
				);
				FString Content;
				if (FFileHelper::LoadFileToString(Content, *FPaths::Combine(ModeSkillsDir, FileName)))
				{
					Skill = ParseSkillDocument(Content, FPaths::Combine(ModeSkillsDir, FileName));
					Skill.Tags.AddUnique(FString::Printf(TEXT("mode:%s"), *ModeName));
					if (!Skill.Name.IsEmpty())
					{
						Skills.Add(Skill);
					}
				}
			}
		}
	}

	// Load project-level skills from [ProjectDir]/.autonomix/skills/*.md
	// These override global skills for this specific project
	const FString ProjectSkillsDir = FPaths::Combine(
		FPaths::ProjectDir(), TEXT(".autonomix"), TEXT("skills")
	);
	if (FPaths::DirectoryExists(ProjectSkillsDir))
	{
		TArray<FString> ProjectFiles;
		IFileManager::Get().FindFiles(ProjectFiles, *FPaths::Combine(ProjectSkillsDir, TEXT("*.md")), true, false);
		for (const FString& FileName : ProjectFiles)
		{
			FString FilePath = FPaths::Combine(ProjectSkillsDir, FileName);
			FString Content;
			if (FFileHelper::LoadFileToString(Content, *FilePath))
			{
				FAutonomixSkill Skill = ParseSkillDocument(Content, FilePath);
				Skill.Tags.AddUnique(TEXT("source:project"));
				if (!Skill.Name.IsEmpty())
				{
					// Project skills override global skills with same name
					Skills.RemoveAll([&](const FAutonomixSkill& S) { return S.Name == Skill.Name; });
					Skills.Add(Skill);
				}
			}
		}
		UE_LOG(LogTemp, Log, TEXT("AutonomixSkillsManager: Loaded project skills from %s"), *ProjectSkillsDir);
	}

	UE_LOG(LogTemp, Log, TEXT("AutonomixSkillsManager: Loaded %d total skills."), Skills.Num());
}

FString FAutonomixSkillsManager::GetSkillsDirectory()
{
	FString PluginBaseDir = FPaths::Combine(FPaths::ProjectPluginsDir(), TEXT("Autonomix"));
	return FPaths::Combine(PluginBaseDir, TEXT("Resources"), TEXT("Skills"));
}

void FAutonomixSkillsManager::LoadSkillFromFile(const FString& FilePath)
{
	FString Content;
	if (!FFileHelper::LoadFileToString(Content, *FilePath)) return;

	FAutonomixSkill Skill = ParseSkillDocument(Content, FilePath);
	if (!Skill.Name.IsEmpty())
	{
		Skills.Add(Skill);
	}
}

FAutonomixSkill FAutonomixSkillsManager::ParseSkillDocument(
	const FString& Content, const FString& FilePath) const
{
	FAutonomixSkill Skill;
	Skill.Content = Content;
	Skill.FilePath = FilePath;

	// Name from filename (without extension)
	Skill.Name = FPaths::GetBaseFilename(FilePath).ToLower();

	// Try to extract display name and description from first lines
	TArray<FString> Lines;
	Content.ParseIntoArrayLines(Lines, false);

	for (const FString& Line : Lines)
	{
		if (Line.StartsWith(TEXT("# Skill:")))
		{
			Skill.DisplayName = Line.Mid(9).TrimStartAndEnd();
		}
		else if (Line.StartsWith(TEXT("## Description")) && Skill.Description.IsEmpty())
		{
			// Description is next non-empty line
		}
		else if (!Skill.DisplayName.IsEmpty() && Skill.Description.IsEmpty()
			&& !Line.StartsWith(TEXT("#")) && !Line.IsEmpty())
		{
			Skill.Description = Line.TrimStartAndEnd();
		}

		if (!Skill.DisplayName.IsEmpty() && !Skill.Description.IsEmpty())
		{
			break;
		}
	}

	if (Skill.DisplayName.IsEmpty())
	{
		Skill.DisplayName = Skill.Name;
	}

	return Skill;
}

bool FAutonomixSkillsManager::GetSkillContent(
	const FString& SkillName,
	const FString& Args,
	FString& OutContent
) const
{
	for (const FAutonomixSkill& Skill : Skills)
	{
		if (Skill.Name.Equals(SkillName, ESearchCase::IgnoreCase))
		{
			OutContent = Skill.Content;
			OutContent.ReplaceInline(TEXT("{{arg}}"), *Args);

			// Handle multiple arguments
			TArray<FString> ArgList;
			Args.ParseIntoArray(ArgList, TEXT(" "));
			for (int32 i = 0; i < ArgList.Num(); i++)
			{
				const FString Placeholder = FString::Printf(TEXT("{{arg%d}}"), i + 1);
				OutContent.ReplaceInline(*Placeholder, *ArgList[i]);
			}

			return true;
		}
	}
	return false;
}

FString FAutonomixSkillsManager::HandleSkillToolCall(
	const FString& SkillName,
	const FString& Args
) const
{
	FString Content;
	if (!GetSkillContent(SkillName, Args, Content))
	{
		// Build a helpful error with available skill names
		TArray<FString> Names = GetSkillNames();
		return FString::Printf(
			TEXT("Error: Skill '%s' not found.\n\nAvailable skills:\n%s"),
			*SkillName,
			*FString::Join(Names, TEXT("\n"))
		);
	}

	return FString::Printf(
		TEXT("# Skill Loaded: %s\n\nArgs: %s\n\n---\n\n%s\n\n---\n\n"
		     "Follow the above skill instructions to complete the task."),
		*SkillName,
		Args.IsEmpty() ? TEXT("(none)") : *Args,
		*Content
	);
}

TArray<FString> FAutonomixSkillsManager::GetSkillNames() const
{
	TArray<FString> Names;
	for (const FAutonomixSkill& Skill : Skills)
	{
		Names.Add(Skill.Name);
	}
	return Names;
}

TArray<FAutonomixSkill> FAutonomixSkillsManager::GetSuggestions(const FString& Partial) const
{
	TArray<FAutonomixSkill> Suggestions;
	for (const FAutonomixSkill& Skill : Skills)
	{
		if (Skill.Name.StartsWith(Partial, ESearchCase::IgnoreCase))
		{
			Suggestions.Add(Skill);
		}
	}
	return Suggestions;
}

void FAutonomixSkillsManager::EnsureBuiltinSkills()
{
	const FString SkillsDir = GetSkillsDirectory();
	IFileManager::Get().MakeDirectory(*SkillsDir, true);

	WriteBuiltinSkill(TEXT("create-actor"), GetCreateActorSkillContent());
	WriteBuiltinSkill(TEXT("setup-input"), GetSetupInputSkillContent());
	WriteBuiltinSkill(TEXT("create-interface"), GetCreateInterfaceSkillContent());
	WriteBuiltinSkill(TEXT("add-component"), GetAddComponentSkillContent());
	WriteBuiltinSkill(TEXT("setup-replication"), GetSetupReplicationSkillContent());
}

void FAutonomixSkillsManager::WriteBuiltinSkill(const FString& Name, const FString& Content)
{
	const FString FilePath = FPaths::Combine(GetSkillsDirectory(), Name + TEXT(".md"));
	if (!FPaths::FileExists(FilePath))
	{
		FFileHelper::SaveStringToFile(Content, *FilePath);
	}
}

const TCHAR* FAutonomixSkillsManager::GetCreateActorSkillContent()
{
	return
		TEXT("# Skill: Create Actor\n")
		TEXT("## Description\n")
		TEXT("Create a complete Unreal Engine Actor with C++ class and optional Blueprint child.\n")
		TEXT("## Arguments\n")
		TEXT("- {{arg}}: The class name for the actor (e.g., APickupItem)\n")
		TEXT("## Steps\n")
		TEXT("1. **Create the header file** (`Source/[Project]/{{arg}}.h`):\n")
		TEXT("   - Include UCLASS macro with appropriate specifiers\n")
		TEXT("   - Include GENERATED_BODY()\n")
		TEXT("   - Declare BeginPlay() and Tick() override\n")
		TEXT("   - Add root component (USceneComponent or USphereComponent)\n")
		TEXT("   - Add any mesh/collision components as needed\n\n")
		TEXT("2. **Create the source file** (`Source/[Project]/{{arg}}.cpp`):\n")
		TEXT("   - Implement constructor with CreateDefaultSubobject for components\n")
		TEXT("   - Set bReplicates if multiplayer is needed\n")
		TEXT("   - Implement BeginPlay and Tick\n\n")
		TEXT("3. **Verify includes** are correct and there are no circular dependencies\n\n")
		TEXT("4. **Build** the project to verify the new class compiles\n\n")
		TEXT("## Example\n")
		TEXT("```cpp\n")
		TEXT("// AMyPickup.h\n")
		TEXT("UCLASS()\n")
		TEXT("class MYPROJECT_API AMyPickup : public AActor\n")
		TEXT("{\n")
		TEXT("    GENERATED_BODY()\n")
		TEXT("public:\n")
		TEXT("    AMyPickup();\n")
		TEXT("protected:\n")
		TEXT("    virtual void BeginPlay() override;\n")
		TEXT("private:\n")
		TEXT("    UPROPERTY(VisibleAnywhere)\n")
		TEXT("    UStaticMeshComponent* MeshComponent;\n")
		TEXT("};\n")
		TEXT("```\n");
}

const TCHAR* FAutonomixSkillsManager::GetSetupInputSkillContent()
{
	return
		TEXT("# Skill: Setup Enhanced Input\n")
		TEXT("## Description\n")
		TEXT("Configure the Enhanced Input system with Input Actions and Mapping Contexts.\n")
		TEXT("## Arguments\n")
		TEXT("- {{arg}}: The action name to set up (e.g., Jump, Sprint, Interact)\n")
		TEXT("## Steps\n")
		TEXT("1. **Create Input Action** asset in Content/Input/Actions/IA_{{arg}}.uasset\n\n")
		TEXT("2. **Add to Input Mapping Context** (create IMC_Default if it doesn't exist)\n\n")
		TEXT("3. **In the character/pawn header**, add:\n")
		TEXT("   ```cpp\n")
		TEXT("   UPROPERTY(EditDefaultsOnly, Category=\"Input\")\n")
		TEXT("   TObjectPtr<UInputAction> {{arg}}Action;\n")
		TEXT("   void Handle{{arg}}(const FInputActionValue& Value);\n")
		TEXT("   ```\n\n")
		TEXT("4. **In BeginPlay**, bind the action:\n")
		TEXT("   ```cpp\n")
		TEXT("   if (UEnhancedInputComponent* EIC = Cast<UEnhancedInputComponent>(InputComponent))\n")
		TEXT("       EIC->BindAction({{arg}}Action, ETriggerEvent::Triggered, this, &AMyChar::Handle{{arg}});\n")
		TEXT("   ```\n\n")
		TEXT("5. **Implement the handler function**\n\n")
		TEXT("## Notes\n")
		TEXT("- Include `EnhancedInput/Public/EnhancedInputComponent.h`\n")
		TEXT("- The project must have the EnhancedInput plugin enabled\n");
}

const TCHAR* FAutonomixSkillsManager::GetCreateInterfaceSkillContent()
{
	return
		TEXT("# Skill: Create Blueprint Interface\n")
		TEXT("## Description\n")
		TEXT("Create a Blueprint interface with C++ backing for interactable objects.\n")
		TEXT("## Arguments\n")
		TEXT("- {{arg}}: Interface name (without 'I' prefix, e.g., Interactable)\n")
		TEXT("## Steps\n")
		TEXT("1. **Create header** `I{{arg}}.h`:\n")
		TEXT("   ```cpp\n")
		TEXT("   UINTERFACE(MinimalAPI)\n")
		TEXT("   class U{{arg}} : public UInterface { GENERATED_BODY() };\n")
		TEXT("   class I{{arg}} {\n")
		TEXT("       GENERATED_BODY()\n")
		TEXT("   public:\n")
		TEXT("       UFUNCTION(BlueprintNativeEvent)\n")
		TEXT("       void Interact(AActor* Interactor);\n")
		TEXT("   };\n")
		TEXT("   ```\n\n")
		TEXT("2. **Implement on an actor**:\n")
		TEXT("   ```cpp\n")
		TEXT("   class AMyActor : public AActor, public I{{arg}} {\n")
		TEXT("       virtual void Interact_Implementation(AActor* Interactor) override;\n")
		TEXT("   };\n")
		TEXT("   ```\n\n")
		TEXT("3. **Call via interface**:\n")
		TEXT("   ```cpp\n")
		TEXT("   if (I{{arg}}* Iface = Cast<I{{arg}}>(TargetActor))\n")
		TEXT("       Iface->Execute_Interact(TargetActor, this);\n")
		TEXT("   ```\n");
}

const TCHAR* FAutonomixSkillsManager::GetAddComponentSkillContent()
{
	return
		TEXT("# Skill: Add Component to Actor\n")
		TEXT("## Description\n")
		TEXT("Add and configure a UE component on an existing actor class.\n")
		TEXT("## Arguments\n")
		TEXT("- {{arg1}}: Actor class name\n")
		TEXT("- {{arg2}}: Component type (e.g., UStaticMeshComponent, UAudioComponent)\n")
		TEXT("## Steps\n")
		TEXT("1. **In header**, declare the component:\n")
		TEXT("   ```cpp\n")
		TEXT("   UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category=\"Components\")\n")
		TEXT("   TObjectPtr<{{arg2}}> MyComponent;\n")
		TEXT("   ```\n\n")
		TEXT("2. **In constructor**, create and attach:\n")
		TEXT("   ```cpp\n")
		TEXT("   MyComponent = CreateDefaultSubobject<{{arg2}}>(TEXT(\"MyComponent\"));\n")
		TEXT("   MyComponent->SetupAttachment(RootComponent);\n")
		TEXT("   ```\n\n")
		TEXT("3. **Configure default properties** as needed\n\n")
		TEXT("4. **Add required include** for the component type\n\n")
		TEXT("## Notes\n")
		TEXT("- Use TObjectPtr<> instead of raw pointers for GC safety\n")
		TEXT("- VisibleAnywhere shows in viewport, EditAnywhere allows editing\n");
}

const TCHAR* FAutonomixSkillsManager::GetSetupReplicationSkillContent()
{
	return
		TEXT("# Skill: Setup Actor Replication\n")
		TEXT("## Description\n")
		TEXT("Configure an actor class for network replication in multiplayer UE games.\n")
		TEXT("## Arguments\n")
		TEXT("- {{arg}}: Actor class to configure replication for\n")
		TEXT("## Steps\n")
		TEXT("1. **Constructor**: Enable replication\n")
		TEXT("   ```cpp\n")
		TEXT("   bReplicates = true;\n")
		TEXT("   SetReplicateMovement(true);\n")
		TEXT("   ```\n\n")
		TEXT("2. **Add GetLifetimeReplicatedProps**:\n")
		TEXT("   ```cpp\n")
		TEXT("   void {{arg}}::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const\n")
		TEXT("   {\n")
		TEXT("       Super::GetLifetimeReplicatedProps(OutLifetimeProps);\n")
		TEXT("       DOREPLIFETIME({{arg}}, Health);\n")
		TEXT("       DOREPLIFETIME_CONDITION({{arg}}, bIsVisible, COND_OwnerOnly);\n")
		TEXT("   }\n")
		TEXT("   ```\n\n")
		TEXT("3. **Mark properties for replication**:\n")
		TEXT("   ```cpp\n")
		TEXT("   UPROPERTY(Replicated)\n")
		TEXT("   float Health;\n")
		TEXT("   UPROPERTY(ReplicatedUsing=OnRep_Health)\n")
		TEXT("   float Health;\n")
		TEXT("   UFUNCTION()\n")
		TEXT("   void OnRep_Health();\n")
		TEXT("   ```\n\n")
		TEXT("4. **Server RPCs** for player actions:\n")
		TEXT("   ```cpp\n")
		TEXT("   UFUNCTION(Server, Reliable)\n")
		TEXT("   void ServerDoAction();\n")
		TEXT("   ```\n\n")
		TEXT("5. **Required include**: `#include \"Net/UnrealNetwork.h\"`\n");
}
