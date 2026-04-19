// Copyright Autonomix. All Rights Reserved.

#include "AutonomixEnvironmentDetails.h"
#include "AutonomixFileContextTracker.h"
#include "AutonomixIgnoreController.h"
#include "Misc/Paths.h"

// UE Editor includes (editor-only)
#if WITH_EDITOR
#include "Editor.h"
#include "EditorSubsystem.h"
#include "LevelEditor.h"
#include "Engine/Selection.h"
#include "Engine/World.h"
#include "Engine/Level.h"
#include "GameFramework/Actor.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "Logging/MessageLog.h"
#include "Logging/TokenizedMessage.h"
#include "Framework/Notifications/NotificationManager.h"
#include "EngineUtils.h"
#endif

FAutonomixEnvironmentDetails::FAutonomixEnvironmentDetails()
{
}

FAutonomixEnvironmentDetails::~FAutonomixEnvironmentDetails()
{
}

void FAutonomixEnvironmentDetails::SetFileContextTracker(FAutonomixFileContextTracker* InTracker)
{
	FileContextTracker = InTracker;
}

void FAutonomixEnvironmentDetails::SetIgnoreController(FAutonomixIgnoreController* InIgnoreController)
{
	IgnoreController = InIgnoreController;
}

FString FAutonomixEnvironmentDetails::Build(
	float ContextUsagePercent,
	const TArray<FAutonomixTodoItem>& Todos,
	const FString& TabTitle,
	int32 LoopIteration
) const
{
	FString Details;

	// Stale files section — most critical, goes first
	FString StaleSection = GetStaleFilesSection();
	if (!StaleSection.IsEmpty())
	{
		Details += StaleSection + TEXT("\n\n");
	}

	// Session info
	FString SessionSection = GetSessionInfoSection(TabTitle, LoopIteration);
	if (!SessionSection.IsEmpty())
	{
		Details += SessionSection + TEXT("\n\n");
	}

	// Currently open files
	FString OpenFilesSection = GetOpenFilesSection();
	if (!OpenFilesSection.IsEmpty())
	{
		Details += OpenFilesSection + TEXT("\n\n");
	}

	// Active level
	if (bIncludeActiveLevel)
	{
		FString LevelSection = GetActiveLevelSection();
		if (!LevelSection.IsEmpty())
		{
			Details += LevelSection + TEXT("\n\n");
		}
	}

	// Selected actors
	if (bIncludeSelectedActors)
	{
		FString ActorsSection = GetSelectedActorsSection();
		if (!ActorsSection.IsEmpty())
		{
			Details += ActorsSection + TEXT("\n\n");
		}
	}

	// Compile errors
	if (bIncludeCompileErrors)
	{
		FString ErrorsSection = GetCompileErrorsSection();
		if (!ErrorsSection.IsEmpty())
		{
			Details += ErrorsSection + TEXT("\n\n");
		}
	}

	// Context window usage
	if (bIncludeContextWindowUsage && ContextUsagePercent > 0.0f)
	{
		FString ContextSection = GetContextWindowSection(ContextUsagePercent);
		if (!ContextSection.IsEmpty())
		{
			Details += ContextSection + TEXT("\n\n");
		}
	}

	// Todo summary
	if (bIncludeTodoSummary && Todos.Num() > 0)
	{
		FString TodoSection = GetTodoSummarySection(Todos);
		if (!TodoSection.IsEmpty())
		{
			Details += TodoSection + TEXT("\n\n");
		}
	}

	// Trim trailing newlines
	Details.TrimEndInline();
	return Details;
}

FString FAutonomixEnvironmentDetails::GetOpenFilesSection() const
{
	TArray<FString> OpenFiles = GatherOpenFiles();
	if (OpenFiles.Num() == 0)
	{
		return FString();
	}

	FString Section = TEXT("# Currently Open Files\n");
	for (const FString& File : OpenFiles)
	{
		Section += FString::Printf(TEXT("%s\n"), *File);
	}
	return Section;
}

FString FAutonomixEnvironmentDetails::GetActiveLevelSection() const
{
	FString LevelName = GetActiveLevelName();
	if (LevelName.IsEmpty())
	{
		return FString();
	}

	FString ActorCountStr;

#if WITH_EDITOR
	if (GEditor && GEditor->GetEditorWorldContext().World())
	{
		UWorld* World = GEditor->GetEditorWorldContext().World();
		int32 ActorCount = 0;
		for (TActorIterator<AActor> It(World); It; ++It) { ActorCount++; }
		ActorCountStr = FString::Printf(TEXT(" (%d actors)"), ActorCount);
	}
#endif

	return FString::Printf(TEXT("# Active Level\n%s%s"), *LevelName, *ActorCountStr);
}

FString FAutonomixEnvironmentDetails::GetSelectedActorsSection() const
{
	TArray<FString> SelectedActors = GatherSelectedActors();
	if (SelectedActors.Num() == 0)
	{
		return FString();
	}

	FString Section = TEXT("# Selected Actors in Viewport\n");
	for (const FString& Actor : SelectedActors)
	{
		Section += FString::Printf(TEXT("- %s\n"), *Actor);
	}
	return Section;
}

FString FAutonomixEnvironmentDetails::GetCompileErrorsSection() const
{
	TArray<FString> Errors = GatherCompileErrors();
	if (Errors.Num() == 0)
	{
		return FString();
	}

	FString Section = FString::Printf(TEXT("# Recent Compile Errors (%d)\n"), Errors.Num());
	int32 Count = 0;
	for (const FString& Error : Errors)
	{
		if (Count >= MaxCompileErrors) break;
		Section += FString::Printf(TEXT("- %s\n"), *Error);
		Count++;
	}

	if (Errors.Num() > MaxCompileErrors)
	{
		Section += FString::Printf(TEXT("... and %d more errors\n"), Errors.Num() - MaxCompileErrors);
	}

	return Section;
}

FString FAutonomixEnvironmentDetails::GetContextWindowSection(float UsagePercent) const
{
	FString Status;
	FString Advice;

	if (UsagePercent >= 90.0f)
	{
		Status = TEXT("⚠️ CRITICAL");
		Advice = TEXT(
			"\n⚠️ CRITICAL: Context window is almost full! You MUST:"
			"\n  1. Immediately wrap up your current step and call attempt_completion with partial progress."
			"\n  2. The system will auto-condense the conversation to free space."
			"\n  3. Do NOT start any new multi-step operations — there is not enough room."
			"\n  4. Keep responses extremely concise. Omit explanations."
		);
	}
	else if (UsagePercent >= 75.0f)
	{
		Status = TEXT("⚠️ High");
		Advice = TEXT(
			"\nNOTE: Context window is filling up. The system will auto-condense at ~80%."
			"\n  - Be concise in your responses. Avoid verbose explanations."
			"\n  - Prioritize completing the current task. Call attempt_completion soon."
			"\n  - If the task needs more steps, finish what you can and summarize remaining work in attempt_completion."
		);
	}
	else if (UsagePercent >= 50.0f)
	{
		Status = TEXT("Moderate");
		Advice = TEXT(
			"\nContext is at moderate usage. Keep responses focused and avoid unnecessary verbosity."
		);
	}
	else
	{
		Status = TEXT("Normal");
	}

	return FString::Printf(
		TEXT("# Context Window\n%.1f%% used (%s)%s"),
		UsagePercent,
		*Status,
		*Advice
	);
}

FString FAutonomixEnvironmentDetails::GetTodoSummarySection(const TArray<FAutonomixTodoItem>& Todos) const
{
	if (Todos.Num() == 0)
	{
		return FString();
	}

	int32 Completed = 0;
	for (const FAutonomixTodoItem& Todo : Todos)
	{
		if (Todo.Status == EAutonomixTodoStatus::Completed) Completed++;
	}

	// Roo-Code format: show ALL todos (including completed) so AI has full picture
	// This matches Roo Code's reminder.ts format with [ ], [-], [x] markers
	FString Section = FString::Printf(
		TEXT("# Current Reminders\n")
		TEXT("Todo list (%d/%d completed):\n"),
		Completed,
		Todos.Num()
	);

	for (const FAutonomixTodoItem& Todo : Todos)
	{
		const TCHAR* Marker;
		switch (Todo.Status)
		{
		case EAutonomixTodoStatus::Completed:  Marker = TEXT("[x]"); break;
		case EAutonomixTodoStatus::InProgress: Marker = TEXT("[-]"); break;
		default:                               Marker = TEXT("[ ]"); break;
		}
		Section += FString::Printf(TEXT("%s %s\n"), Marker, *Todo.Content);
	}

	return Section;
}

FString FAutonomixEnvironmentDetails::GetStaleFilesSection() const
{
	if (!FileContextTracker || !FileContextTracker->HasStaleFiles())
	{
		return FString();
	}

	// Build the warning — FileContextTracker clears stale flags when warning is built
	// We use const_cast here because BuildStaleContextWarning() clears state
	// (intentionally side-effectful, same as Roo Code design)
	return const_cast<FAutonomixFileContextTracker*>(FileContextTracker)->BuildStaleContextWarning();
}

FString FAutonomixEnvironmentDetails::GetSessionInfoSection(const FString& TabTitle, int32 LoopIteration) const
{
	const FDateTime Now = FDateTime::Now();
	FString TimeStr = FString::Printf(
		TEXT("%04d-%02d-%02d %02d:%02d"),
		Now.GetYear(), Now.GetMonth(), Now.GetDay(),
		Now.GetHour(), Now.GetMinute()
	);

	FString Section = FString::Printf(TEXT("# Current Time\n%s"), *TimeStr);

	if (!TabTitle.IsEmpty())
	{
		Section += FString::Printf(TEXT("\n\n# Active Conversation\n%s"), *TabTitle);
	}

	if (LoopIteration > 0)
	{
		Section += FString::Printf(TEXT(" (loop iteration %d)"), LoopIteration);
	}

	return Section;
}

bool FAutonomixEnvironmentDetails::IsPathAllowed(const FString& Path) const
{
	if (!IgnoreController)
	{
		return true;
	}
	return !IgnoreController->IsPathIgnored(Path);
}

TArray<FString> FAutonomixEnvironmentDetails::GatherCompileErrors() const
{
	TArray<FString> Errors;

#if WITH_EDITOR
	// Query the compiler message log for recent errors
	FMessageLog CompileLog(TEXT("BlueprintLog"));
	// Also check compiler errors log
	FMessageLog CompilerLog(TEXT("Compiler"));

	// Get messages from the compiler log
	// Note: FMessageLog doesn't expose a direct GetMessages() — we read from
	// the output log filtering for error messages
	// This is a simplified version; a full implementation would use IMessageLogListing
	// For now we just note that this section is ready for extension
#endif

	return Errors;
}

TArray<FString> FAutonomixEnvironmentDetails::GatherOpenFiles() const
{
	TArray<FString> Files;

#if WITH_EDITOR
	if (!GEditor) return Files;

	const FString ProjectDir = FPaths::ProjectDir();

	// Get open asset editors
	UAssetEditorSubsystem* AssetEditorSubsystem = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
	if (AssetEditorSubsystem)
	{
		TArray<UObject*> EditedAssets = AssetEditorSubsystem->GetAllEditedAssets();
		int32 Count = 0;
		for (UObject* Asset : EditedAssets)
		{
			if (!Asset) continue;
			if (Count >= MaxOpenTabs) break;

			FString PackagePath = Asset->GetOutermost()->GetName();
			// Convert package path to relative filesystem path
			FString RelPath = PackagePath.Replace(TEXT("/Game/"), TEXT("Content/"));
			RelPath += TEXT(".uasset");

			if (IsPathAllowed(RelPath))
			{
				Files.Add(RelPath);
				Count++;
			}
		}
	}

	// Also check the level editor for source files open in the text editor
	// (This requires querying the source code editor module)
#endif

	return Files;
}

TArray<FString> FAutonomixEnvironmentDetails::GatherSelectedActors() const
{
	TArray<FString> ActorNames;

#if WITH_EDITOR
	if (!GEditor) return ActorNames;

	USelection* Selection = GEditor->GetSelectedActors();
	if (!Selection) return ActorNames;

	for (FSelectionIterator It(*Selection); It; ++It)
	{
		AActor* Actor = Cast<AActor>(*It);
		if (Actor)
		{
			FString ActorInfo = FString::Printf(
				TEXT("%s (%s)"),
				*Actor->GetActorLabel(),
				*Actor->GetClass()->GetName()
			);
			ActorNames.Add(ActorInfo);
		}
	}
#endif

	return ActorNames;
}

FString FAutonomixEnvironmentDetails::GetActiveLevelName() const
{
#if WITH_EDITOR
	if (GEditor && GEditor->GetEditorWorldContext().World())
	{
		UWorld* World = GEditor->GetEditorWorldContext().World();
		if (World->GetCurrentLevel())
		{
			FString LevelPath = World->GetCurrentLevel()->GetOutermost()->GetName();
			// Convert to relative: /Game/Maps/MyLevel -> Content/Maps/MyLevel
			return LevelPath.Replace(TEXT("/Game/"), TEXT("Content/"));
		}
	}
#endif
	return FString();
}
