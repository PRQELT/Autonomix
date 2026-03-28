// Copyright Autonomix. All Rights Reserved.

#include "Sequencer/AutonomixSequencerActions.h"
#include "AutonomixCoreModule.h"
#include "AutonomixSettings.h"
#include "LevelSequence.h"
#include "LevelSequenceActor.h"
#include "MovieScene.h"
#include "MovieScenePossessable.h"
#include "MovieSceneSpawnable.h"
#include "MovieSceneSection.h"
#include "MovieSceneTrack.h"
#include "Tracks/MovieScene3DTransformTrack.h"
#include "Tracks/MovieSceneFloatTrack.h"
#include "Tracks/MovieSceneCameraCutTrack.h"
#include "Tracks/MovieSceneAudioTrack.h"
#include "Sections/MovieScene3DTransformSection.h"
#include "Sections/MovieSceneFloatSection.h"
#include "Channels/MovieSceneChannelProxy.h"
#include "Channels/MovieSceneFloatChannel.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "Editor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Misc/FrameRate.h"

#define LOCTEXT_NAMESPACE "AutonomixSequencerActions"

// ============================================================================
// Lifecycle
// ============================================================================

FAutonomixSequencerActions::FAutonomixSequencerActions() {}
FAutonomixSequencerActions::~FAutonomixSequencerActions() {}

// ============================================================================
// IAutonomixActionExecutor Interface
// ============================================================================

FName FAutonomixSequencerActions::GetActionName() const { return FName(TEXT("Sequencer")); }
FText FAutonomixSequencerActions::GetDisplayName() const { return LOCTEXT("DisplayName", "Sequencer & Cinematics"); }
EAutonomixActionCategory FAutonomixSequencerActions::GetCategory() const { return EAutonomixActionCategory::Level; }
EAutonomixRiskLevel FAutonomixSequencerActions::GetDefaultRiskLevel() const { return EAutonomixRiskLevel::Medium; }
bool FAutonomixSequencerActions::CanUndo() const { return true; }
bool FAutonomixSequencerActions::UndoAction() { return GEditor && GEditor->UndoTransaction(); }

TArray<FString> FAutonomixSequencerActions::GetSupportedToolNames() const
{
	return {
		TEXT("create_level_sequence"),
		TEXT("add_sequencer_track"),
		TEXT("add_sequencer_keyframe")
	};
}

bool FAutonomixSequencerActions::ValidateParams(const TSharedRef<FJsonObject>& Params, TArray<FString>& OutErrors) const
{
	return true;
}

FAutonomixActionPlan FAutonomixSequencerActions::PreviewAction(const TSharedRef<FJsonObject>& Params)
{
	FAutonomixActionPlan Plan;
	FString Action;
	Params->TryGetStringField(TEXT("action"), Action);
	if (Action.IsEmpty()) Params->TryGetStringField(TEXT("tool_name"), Action);

	FString AssetPath;
	Params->TryGetStringField(TEXT("asset_path"), AssetPath);

	if (Action == TEXT("create_level_sequence"))
		Plan.Summary = FString::Printf(TEXT("Create Level Sequence: %s"), *AssetPath);
	else if (Action == TEXT("add_sequencer_track"))
		Plan.Summary = FString::Printf(TEXT("Add track to sequence: %s"), *AssetPath);
	else if (Action == TEXT("add_sequencer_keyframe"))
		Plan.Summary = TEXT("Add keyframe to sequencer track");
	else
		Plan.Summary = TEXT("Sequencer operation");

	Plan.MaxRiskLevel = EAutonomixRiskLevel::Medium;
	FAutonomixAction A;
	A.Description = Plan.Summary;
	A.Category = EAutonomixActionCategory::Level;
	A.RiskLevel = EAutonomixRiskLevel::Medium;
	Plan.Actions.Add(A);
	return Plan;
}

FAutonomixActionResult FAutonomixSequencerActions::ExecuteAction(const TSharedRef<FJsonObject>& Params)
{
	FAutonomixActionResult Result;
	Result.bSuccess = false;

	FString Action;
	if (!Params->TryGetStringField(TEXT("action"), Action) || Action.IsEmpty())
		Params->TryGetStringField(TEXT("tool_name"), Action);

	if (Action == TEXT("create_level_sequence"))
		return ExecuteCreateLevelSequence(Params, Result);
	else if (Action == TEXT("add_sequencer_track"))
		return ExecuteAddSequencerTrack(Params, Result);
	else if (Action == TEXT("add_sequencer_keyframe"))
		return ExecuteAddSequencerKeyframe(Params, Result);

	Result.Errors.Add(TEXT("Unknown Sequencer action. Use create_level_sequence, add_sequencer_track, or add_sequencer_keyframe."));
	return Result;
}

// ============================================================================
// create_level_sequence
// ============================================================================

FAutonomixActionResult FAutonomixSequencerActions::ExecuteCreateLevelSequence(
	const TSharedRef<FJsonObject>& Params, FAutonomixActionResult& Result)
{
	FString AssetPath;
	if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
	{
		Result.Errors.Add(TEXT("Missing required field: 'asset_path'"));
		return Result;
	}

	FString PackagePath = FPackageName::GetLongPackagePath(AssetPath);
	FString AssetName = FPackageName::GetLongPackageAssetName(AssetPath);

	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
	UObject* NewAsset = AssetTools.CreateAsset(AssetName, PackagePath, ULevelSequence::StaticClass(), nullptr);

	if (!NewAsset)
	{
		Result.Errors.Add(FString::Printf(TEXT("Failed to create Level Sequence at '%s'."), *AssetPath));
		return Result;
	}

	ULevelSequence* Sequence = Cast<ULevelSequence>(NewAsset);

	// Set duration if specified
	float DurationSeconds = 5.0f;
	Params->TryGetNumberField(TEXT("duration_seconds"), DurationSeconds);

	UMovieScene* MovieScene = Sequence->GetMovieScene();
	if (MovieScene)
	{
		FFrameRate TickResolution = MovieScene->GetTickResolution();
		FFrameNumber EndFrame = (DurationSeconds * TickResolution).FloorToFrame();
		MovieScene->SetPlaybackRange(FFrameNumber(0), EndFrame.Value);
	}

	// Optionally spawn a LevelSequenceActor in the world
	bool bSpawnInWorld = false;
	Params->TryGetBoolField(TEXT("spawn_in_world"), bSpawnInWorld);

	FString SpawnInfo;
	if (bSpawnInWorld)
	{
		UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
		if (World)
		{
			FActorSpawnParameters SpawnParams;
			SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

			ALevelSequenceActor* SeqActor = World->SpawnActor<ALevelSequenceActor>(
				ALevelSequenceActor::StaticClass(), FTransform::Identity, SpawnParams);

			if (SeqActor)
			{
				SeqActor->SetSequence(Sequence);
				SeqActor->SetActorLabel(AssetName);
				SpawnInfo = FString::Printf(TEXT(" Spawned LevelSequenceActor '%s' in the current level."), *AssetName);
			}
		}
	}

	Sequence->MarkPackageDirty();

	Result.bSuccess = true;
	Result.ModifiedAssets.Add(AssetPath);
	Result.ResultMessage = FString::Printf(
		TEXT("Created Level Sequence '%s' (%.1fs duration).%s "
			 "Use add_sequencer_track to add actor bindings, camera cuts, or audio tracks."),
		*AssetPath, DurationSeconds, *SpawnInfo);
	return Result;
}

// ============================================================================
// add_sequencer_track
// ============================================================================

FAutonomixActionResult FAutonomixSequencerActions::ExecuteAddSequencerTrack(
	const TSharedRef<FJsonObject>& Params, FAutonomixActionResult& Result)
{
	FString AssetPath;
	if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
	{
		Result.Errors.Add(TEXT("Missing required field: 'asset_path'"));
		return Result;
	}

	FString TrackType;
	if (!Params->TryGetStringField(TEXT("track_type"), TrackType))
	{
		Result.Errors.Add(TEXT("Missing required field: 'track_type' (Transform, Float, CameraCut, Audio)"));
		return Result;
	}

	ULevelSequence* Sequence = LoadObject<ULevelSequence>(nullptr, *AssetPath);
	if (!Sequence)
	{
		Result.Errors.Add(FString::Printf(TEXT("Level Sequence not found at '%s'."), *AssetPath));
		return Result;
	}

	UMovieScene* MovieScene = Sequence->GetMovieScene();
	if (!MovieScene)
	{
		Result.Errors.Add(TEXT("Level Sequence has no MovieScene."));
		return Result;
	}

	TrackType = TrackType.ToLower();
	FString Report;

	if (TrackType == TEXT("transform") || TrackType == TEXT("3dtransform"))
	{
		// Need an actor binding
		FString ActorLabel;
		Params->TryGetStringField(TEXT("actor_label"), ActorLabel);

		if (ActorLabel.IsEmpty())
		{
			Result.Errors.Add(TEXT("Transform track requires 'actor_label' — the label of the actor to bind."));
			return Result;
		}

		// Find the actor in the world
		UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
		AActor* TargetActor = nullptr;
		if (World)
		{
			for (TActorIterator<AActor> It(World); It; ++It)
			{
				if (It->GetActorLabel() == ActorLabel)
				{
					TargetActor = *It;
					break;
				}
			}
		}

		if (!TargetActor)
		{
			Result.Errors.Add(FString::Printf(TEXT("Actor with label '%s' not found in the current level."), *ActorLabel));
			return Result;
		}

		// Create or find the binding
		FGuid BindingGuid = MovieScene->AddPossessable(ActorLabel, TargetActor->GetClass());
		Sequence->BindPossessableObject(BindingGuid, *TargetActor, TargetActor->GetWorld());

		// Add 3D Transform track
		UMovieScene3DTransformTrack* TransformTrack = MovieScene->AddTrack<UMovieScene3DTransformTrack>(BindingGuid);
		if (TransformTrack)
		{
			UMovieScene3DTransformSection* Section = Cast<UMovieScene3DTransformSection>(
				TransformTrack->CreateNewSection());
			if (Section)
			{
				Section->SetRange(MovieScene->GetPlaybackRange());
				TransformTrack->AddSection(*Section);
			}
			Report = FString::Printf(TEXT("Added 3D Transform track bound to actor '%s'."), *ActorLabel);
		}
	}
	else if (TrackType == TEXT("cameraccut") || TrackType == TEXT("camera_cut") || TrackType == TEXT("camera"))
	{
		UMovieSceneCameraCutTrack* CameraTrack = MovieScene->AddTrack<UMovieSceneCameraCutTrack>();
		if (CameraTrack)
		{
			Report = TEXT("Added Camera Cut master track.");
		}
		else
		{
			Report = TEXT("Camera Cut track may already exist (only one allowed per sequence).");
		}
	}
	else if (TrackType == TEXT("audio"))
	{
		UMovieSceneAudioTrack* AudioTrack = MovieScene->AddTrack<UMovieSceneAudioTrack>();
		if (AudioTrack)
		{
			Report = TEXT("Added Audio master track.");
		}
	}
	else
	{
		Result.Errors.Add(FString::Printf(TEXT("Unknown track type '%s'. Supported: Transform, CameraCut, Audio."), *TrackType));
		return Result;
	}

	Sequence->MarkPackageDirty();

	Result.bSuccess = true;
	Result.ModifiedAssets.Add(AssetPath);
	Result.ResultMessage = Report;
	return Result;
}

// ============================================================================
// add_sequencer_keyframe
// ============================================================================

FAutonomixActionResult FAutonomixSequencerActions::ExecuteAddSequencerKeyframe(
	const TSharedRef<FJsonObject>& Params, FAutonomixActionResult& Result)
{
	FString AssetPath;
	if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
	{
		Result.Errors.Add(TEXT("Missing required field: 'asset_path'"));
		return Result;
	}

	float TimeSeconds = 0.0f;
	if (!Params->TryGetNumberField(TEXT("time"), TimeSeconds))
	{
		Result.Errors.Add(TEXT("Missing required field: 'time' (in seconds)"));
		return Result;
	}

	ULevelSequence* Sequence = LoadObject<ULevelSequence>(nullptr, *AssetPath);
	if (!Sequence)
	{
		Result.Errors.Add(FString::Printf(TEXT("Level Sequence not found at '%s'."), *AssetPath));
		return Result;
	}

	UMovieScene* MovieScene = Sequence->GetMovieScene();
	if (!MovieScene)
	{
		Result.Errors.Add(TEXT("Level Sequence has no MovieScene."));
		return Result;
	}

	FString TrackType;
	Params->TryGetStringField(TEXT("track_type"), TrackType);
	TrackType = TrackType.ToLower();

	FFrameRate TickResolution = MovieScene->GetTickResolution();
	FFrameNumber KeyFrame = (TimeSeconds * TickResolution).FloorToFrame();

	FString Report;
	int32 KeysAdded = 0;

	if (TrackType == TEXT("transform") || TrackType == TEXT("3dtransform"))
	{
		// Find the transform track by actor label
		FString ActorLabel;
		Params->TryGetStringField(TEXT("actor_label"), ActorLabel);

		// Parse transform values
		FVector Location(0, 0, 0);
		FRotator Rotation(0, 0, 0);
		FVector Scale(1, 1, 1);

		const TSharedPtr<FJsonObject>* LocObj;
		if (Params->TryGetObjectField(TEXT("location"), LocObj))
		{
			(*LocObj)->TryGetNumberField(TEXT("x"), Location.X);
			(*LocObj)->TryGetNumberField(TEXT("y"), Location.Y);
			(*LocObj)->TryGetNumberField(TEXT("z"), Location.Z);
		}

		const TSharedPtr<FJsonObject>* RotObj;
		if (Params->TryGetObjectField(TEXT("rotation"), RotObj))
		{
			(*RotObj)->TryGetNumberField(TEXT("pitch"), Rotation.Pitch);
			(*RotObj)->TryGetNumberField(TEXT("yaw"), Rotation.Yaw);
			(*RotObj)->TryGetNumberField(TEXT("roll"), Rotation.Roll);
		}

		const TSharedPtr<FJsonObject>* ScaleObj;
		if (Params->TryGetObjectField(TEXT("scale"), ScaleObj))
		{
			(*ScaleObj)->TryGetNumberField(TEXT("x"), Scale.X);
			(*ScaleObj)->TryGetNumberField(TEXT("y"), Scale.Y);
			(*ScaleObj)->TryGetNumberField(TEXT("z"), Scale.Z);
		}

		const TArray<FMovieSceneBinding>& Bindings = MovieScene->GetBindings();
		for (const FMovieSceneBinding& Binding : Bindings)
		{
			if (!ActorLabel.IsEmpty())
			{
				FString BindingName;
				if (FMovieScenePossessable* Possessable = MovieScene->FindPossessable(Binding.GetObjectGuid()))
				{
					BindingName = Possessable->GetName();
				}
				else if (FMovieSceneSpawnable* Spawnable = MovieScene->FindSpawnable(Binding.GetObjectGuid()))
				{
					BindingName = Spawnable->GetName();
				}

				if (BindingName != ActorLabel)
				{
					continue;
				}
			}

			for (UMovieSceneTrack* Track : Binding.GetTracks())
			{
				UMovieScene3DTransformTrack* TransformTrack = Cast<UMovieScene3DTransformTrack>(Track);
				if (!TransformTrack) continue;

				for (UMovieSceneSection* Section : TransformTrack->GetAllSections())
				{
					UMovieScene3DTransformSection* TransformSection = Cast<UMovieScene3DTransformSection>(Section);
					if (!TransformSection) continue;

					// Get the channel proxy to access individual channels
					TArrayView<FMovieSceneFloatChannel*> FloatChannels = TransformSection->GetChannelProxy().GetChannels<FMovieSceneFloatChannel>();

					// Channels: 0=TX, 1=TY, 2=TZ, 3=RX, 4=RY, 5=RZ, 6=SX, 7=SY, 8=SZ
					if (FloatChannels.Num() >= 9)
					{
						FloatChannels[0]->AddLinearKey(KeyFrame, Location.X);
						FloatChannels[1]->AddLinearKey(KeyFrame, Location.Y);
						FloatChannels[2]->AddLinearKey(KeyFrame, Location.Z);
						FloatChannels[3]->AddLinearKey(KeyFrame, Rotation.Roll);
						FloatChannels[4]->AddLinearKey(KeyFrame, Rotation.Pitch);
						FloatChannels[5]->AddLinearKey(KeyFrame, Rotation.Yaw);
						FloatChannels[6]->AddLinearKey(KeyFrame, Scale.X);
						FloatChannels[7]->AddLinearKey(KeyFrame, Scale.Y);
						FloatChannels[8]->AddLinearKey(KeyFrame, Scale.Z);
						KeysAdded = 9;
						Report = FString::Printf(TEXT("Added transform keyframe at %.2fs: Loc(%s) Rot(%s) Scale(%s)"),
							TimeSeconds, *Location.ToString(), *Rotation.ToString(), *Scale.ToString());
					}
				}
			}
		}
	}

	if (KeysAdded == 0 && Report.IsEmpty())
	{
		Result.Errors.Add(TEXT("No keyframes were added. Check that the track exists and the actor_label matches."));
		return Result;
	}

	Sequence->MarkPackageDirty();

	Result.bSuccess = true;
	Result.ModifiedAssets.Add(AssetPath);
	Result.ResultMessage = Report;
	return Result;
}

#undef LOCTEXT_NAMESPACE
