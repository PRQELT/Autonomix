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

