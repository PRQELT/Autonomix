// Copyright Autonomix. All Rights Reserved.

#include "AutonomixEditorContextCapture.h"
#include "AutonomixCoreModule.h"
#include "Editor.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "Engine/Selection.h"
#include "GameFramework/Actor.h"
#include "LevelEditor.h"
#include "ContentBrowserModule.h"
#include "IContentBrowserSingleton.h"
#include "Subsystems/AssetEditorSubsystem.h"

FAutonomixEditorContextCapture::FAutonomixEditorContextCapture() {}
FAutonomixEditorContextCapture::~FAutonomixEditorContextCapture() {}

FAutonomixEditorContext FAutonomixEditorContextCapture::CaptureContext()
{
	FAutonomixEditorContext Ctx;

	if (!GEditor) return Ctx;

	// ---- Active Level ----
	UWorld* World = GEditor->GetEditorWorldContext().World();
	if (World)
	{
		Ctx.ActiveLevelName = World->GetMapName();

		// Count actors
		int32 Count = 0;
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			Count++;
		}
		Ctx.ActorCount = Count;
	}

	// ---- Selected Actors ----
	USelection* SelectedActors = GEditor->GetSelectedActors();
	if (SelectedActors && SelectedActors->Num() > 0)
	{
		TArray<FString> SelectedNames;
		for (int32 i = 0; i < SelectedActors->Num(); ++i)
		{
			UObject* Obj = SelectedActors->GetSelectedObject(i);
			if (AActor* Actor = Cast<AActor>(Obj))
			{
				SelectedNames.Add(FString::Printf(TEXT("%s (%s)"),
					*Actor->GetActorLabel(), *Actor->GetClass()->GetName()));
			}
		}
		Ctx.SelectedActorsSummary = FString::Join(SelectedNames, TEXT(", "));
	}

	// ---- Selected Assets in Content Browser ----
	if (FModuleManager::Get().IsModuleLoaded(TEXT("ContentBrowser")))
	{
		FContentBrowserModule& CBModule = FModuleManager::LoadModuleChecked<FContentBrowserModule>(TEXT("ContentBrowser"));
		TArray<FAssetData> SelectedAssets;
		CBModule.Get().GetSelectedAssets(SelectedAssets);

		if (SelectedAssets.Num() > 0)
		{
			TArray<FString> AssetNames;
			for (const FAssetData& Asset : SelectedAssets)
			{
				AssetNames.Add(FString::Printf(TEXT("%s (%s)"),
					*Asset.AssetName.ToString(),
					*Asset.AssetClassPath.GetAssetName().ToString()));
			}
			Ctx.SelectedAssetsSummary = FString::Join(AssetNames, TEXT(", "));
		}
	}

	// ---- Open Asset Editors ----
	if (GEditor)
	{
		UAssetEditorSubsystem* AssetEditorSubsystem = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
		if (AssetEditorSubsystem)
		{
			TArray<UObject*> EditedAssets = AssetEditorSubsystem->GetAllEditedAssets();
			for (UObject* Obj : EditedAssets)
			{
				if (Obj)
				{
					Ctx.OpenEditors.Add(FString::Printf(TEXT("%s (%s)"),
						*Obj->GetName(), *Obj->GetClass()->GetName()));
				}
			}
		}
	}

	// ---- Viewport Camera ----
	if (GEditor && GEditor->GetActiveViewport())
	{
		FEditorViewportClient* ViewportClient = static_cast<FEditorViewportClient*>(GEditor->GetActiveViewport()->GetClient());
		if (ViewportClient)
		{
			FVector CamLoc = ViewportClient->GetViewLocation();
			FRotator CamRot = ViewportClient->GetViewRotation();
			Ctx.ViewportCameraInfo = FString::Printf(TEXT("Location: %s, Rotation: %s"),
				*CamLoc.ToString(), *CamRot.ToString());
		}
	}

	return Ctx;
}

FString FAutonomixEditorContextCapture::BuildContextString()
{
	FAutonomixEditorContext Ctx = CaptureContext();
	return Ctx.ToContextString();
}
