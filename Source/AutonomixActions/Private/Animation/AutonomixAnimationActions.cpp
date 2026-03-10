// Copyright Autonomix. All Rights Reserved.

#include "Animation/AutonomixAnimationActions.h"
#include "AutonomixCoreModule.h"

// Animation runtime
#include "Animation/AnimBlueprint.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimMontage.h"
#include "Animation/Skeleton.h"

// Editor animation factories
#include "Factories/AnimBlueprintFactory.h"
#include "Factories/AnimMontageFactory.h"

// Asset management
#include "AssetToolsModule.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetImportTask.h"
#include "Factories/FbxFactory.h"
#include "Factories/FbxImportUI.h"
#include "Factories/FbxAnimSequenceImportData.h"
#include "FileHelpers.h"
#include "UObject/SavePackage.h"
#include "Misc/PackageName.h"

// Blueprint
#include "Engine/Blueprint.h"
#include "Engine/SCS_Node.h"
#include "Engine/SimpleConstructionScript.h"
#include "Components/SkeletalMeshComponent.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"

// JSON
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

// Misc
#include "ScopedTransaction.h"

// ============================================================================
// Statics / Lifecycle
// ============================================================================

FAutonomixAnimationActions::FAutonomixAnimationActions() {}
FAutonomixAnimationActions::~FAutonomixAnimationActions() {}

FName FAutonomixAnimationActions::GetActionName() const { return FName(TEXT("Animation")); }
FText FAutonomixAnimationActions::GetDisplayName() const { return FText::FromString(TEXT("Animation Actions")); }
EAutonomixActionCategory FAutonomixAnimationActions::GetCategory() const { return EAutonomixActionCategory::Blueprint; }
EAutonomixRiskLevel FAutonomixAnimationActions::GetDefaultRiskLevel() const { return EAutonomixRiskLevel::Medium; }
bool FAutonomixAnimationActions::CanUndo() const { return false; }
bool FAutonomixAnimationActions::UndoAction() { return false; }

TArray<FString> FAutonomixAnimationActions::GetSupportedToolNames() const
{
	return {
		TEXT("create_anim_blueprint"),
		TEXT("import_animation_fbx"),
		TEXT("assign_anim_blueprint"),
		TEXT("create_anim_montage"),
		TEXT("get_anim_info")
	};
}

bool FAutonomixAnimationActions::ValidateParams(const TSharedRef<FJsonObject>& Params, TArray<FString>& OutErrors) const
{
	if (!Params->HasField(TEXT("asset_path")))
	{
		OutErrors.Add(TEXT("Missing required field: asset_path"));
		return false;
	}
	return true;
}

// ============================================================================
// PreviewAction
// ============================================================================

FAutonomixActionPlan FAutonomixAnimationActions::PreviewAction(const TSharedRef<FJsonObject>& Params)
{
	FAutonomixActionPlan Plan;
	FString AssetPath;
	Params->TryGetStringField(TEXT("asset_path"), AssetPath);
	Plan.Summary = FString::Printf(TEXT("Animation operation at %s"), *AssetPath);

	FAutonomixAction Action;
	Action.Description = Plan.Summary;
	Action.Category = EAutonomixActionCategory::Blueprint;
	Action.RiskLevel = EAutonomixRiskLevel::Medium;
	Action.AffectedAssets.Add(AssetPath);
	Plan.Actions.Add(Action);
	Plan.MaxRiskLevel = EAutonomixRiskLevel::Medium;

	return Plan;
}

// ============================================================================
// ExecuteAction — Dispatch
// ============================================================================

FAutonomixActionResult FAutonomixAnimationActions::ExecuteAction(const TSharedRef<FJsonObject>& Params)
{
	FAutonomixActionResult Result;
	Result.bSuccess = false;

	FString ToolName;
	Params->TryGetStringField(TEXT("_tool_name"), ToolName);

	if (ToolName == TEXT("create_anim_blueprint"))  return ExecuteCreateAnimBlueprint(Params, Result);
	if (ToolName == TEXT("import_animation_fbx"))   return ExecuteImportAnimationFBX(Params, Result);
	if (ToolName == TEXT("assign_anim_blueprint"))  return ExecuteAssignAnimBlueprint(Params, Result);
	if (ToolName == TEXT("create_anim_montage"))    return ExecuteCreateAnimMontage(Params, Result);
	if (ToolName == TEXT("get_anim_info"))          return ExecuteGetAnimInfo(Params, Result);

	Result.Errors.Add(FString::Printf(TEXT("Unknown Animation tool: '%s'. Supported: create_anim_blueprint, import_animation_fbx, assign_anim_blueprint, create_anim_montage, get_anim_info"), *ToolName));
	return Result;
}

// ============================================================================
// ExecuteCreateAnimBlueprint
// ============================================================================

FAutonomixActionResult FAutonomixAnimationActions::ExecuteCreateAnimBlueprint(const TSharedRef<FJsonObject>& Params, FAutonomixActionResult& Result)
{
	FString AssetPath    = Params->GetStringField(TEXT("asset_path"));
	FString SkeletonPath = Params->GetStringField(TEXT("skeleton_path"));

	USkeleton* Skeleton = LoadObject<USkeleton>(nullptr, *SkeletonPath);
	if (!Skeleton)
	{
		Result.Errors.Add(FString::Printf(TEXT("Skeleton not found: '%s'. Ensure the skeleton asset exists (search_assets can help locate it)."), *SkeletonPath));
		return Result;
	}

	FString PackagePath = FPackageName::GetLongPackagePath(AssetPath);
	FString AssetName   = FPackageName::GetShortName(AssetPath);

	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();

	UAnimBlueprintFactory* Factory = NewObject<UAnimBlueprintFactory>();
	Factory->TargetSkeleton = Skeleton;
	Factory->ParentClass = UAnimInstance::StaticClass();

	// Allow parent class override
	FString ParentClassName;
	if (Params->TryGetStringField(TEXT("parent_class"), ParentClassName) && !ParentClassName.IsEmpty())
	{
		UClass* ParentClass = FindFirstObject<UClass>(*ParentClassName, EFindFirstObjectOptions::None);
		if (ParentClass && ParentClass->IsChildOf(UAnimInstance::StaticClass()))
			Factory->ParentClass = ParentClass;
		else
			Result.Warnings.Add(FString::Printf(TEXT("Parent class '%s' not found or not an AnimInstance subclass — using default UAnimInstance."), *ParentClassName));
	}

	UObject* NewAsset = AssetTools.CreateAsset(AssetName, PackagePath, UAnimBlueprint::StaticClass(), Factory);
	UAnimBlueprint* NewAnimBP = Cast<UAnimBlueprint>(NewAsset);

	if (!NewAnimBP)
	{
		Result.Errors.Add(FString::Printf(TEXT("Failed to create Animation Blueprint at '%s'."), *AssetPath));
		return Result;
	}

	// Save
	UPackage* Package = NewAnimBP->GetOutermost();
	Package->MarkPackageDirty();
	FString PackageFilename;
	if (FPackageName::TryConvertLongPackageNameToFilename(Package->GetName(), PackageFilename, FPackageName::GetAssetPackageExtension()))
	{
		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Standalone;
		UPackage::SavePackage(Package, NewAnimBP, *PackageFilename, SaveArgs);
	}
	FAssetRegistryModule::AssetCreated(NewAnimBP);

	UE_LOG(LogAutonomix, Log, TEXT("AnimationActions: Created AnimBP '%s' targeting skeleton '%s'"), *AssetPath, *SkeletonPath);

	Result.bSuccess = true;
	Result.ResultMessage = FString::Printf(
		TEXT("Created Animation Blueprint '%s' targeting skeleton '%s'.\n"
		     "Architecture note: The AnimGraph (blend nodes, state machines) must be authored in the editor UI. "
		     "The EventGraph (variable caching from the owning Pawn) can be wired via inject_blueprint_nodes_t3d. "
		     "Assign to a character via assign_anim_blueprint."),
		*AssetName, *SkeletonPath);
	Result.ModifiedAssets.Add(AssetPath);
	return Result;
}

// ============================================================================
// ExecuteImportAnimationFBX
// ============================================================================

FAutonomixActionResult FAutonomixAnimationActions::ExecuteImportAnimationFBX(const TSharedRef<FJsonObject>& Params, FAutonomixActionResult& Result)
{
	FString SourceFile   = Params->GetStringField(TEXT("source_file"));
	FString SkeletonPath = Params->GetStringField(TEXT("skeleton_path"));
	FString DestPath     = TEXT("/Game/Animations");
	Params->TryGetStringField(TEXT("destination_path"), DestPath);

	if (!FPaths::FileExists(SourceFile))
	{
		Result.Errors.Add(FString::Printf(TEXT("FBX file not found on disk: '%s'. Provide the absolute file path."), *SourceFile));
		return Result;
	}

	USkeleton* Skeleton = LoadObject<USkeleton>(nullptr, *SkeletonPath);
	if (!Skeleton)
	{
		Result.Errors.Add(FString::Printf(TEXT("Skeleton not found: '%s'."), *SkeletonPath));
		return Result;
	}

	// Build import task
	UFbxFactory* FbxFactory = NewObject<UFbxFactory>();
	FbxFactory->ImportUI->bImportMesh = false;
	FbxFactory->ImportUI->bImportAnimations = true;
	FbxFactory->ImportUI->bImportTextures = false;
	FbxFactory->ImportUI->AnimSequenceImportData->bImportCustomAttribute = true;
	FbxFactory->ImportUI->Skeleton = Skeleton;

	UAssetImportTask* Task = NewObject<UAssetImportTask>();
	Task->Filename        = SourceFile;
	Task->DestinationPath = DestPath;
	Task->bAutomated      = true;
	Task->bReplaceExisting = true;
	Task->bSave           = true;
	Task->Factory         = FbxFactory;

	FString AssetName;
	if (Params->TryGetStringField(TEXT("asset_name"), AssetName) && !AssetName.IsEmpty())
		Task->DestinationName = AssetName;

	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
	AssetTools.ImportAssetTasks({Task});

	if (Task->ImportedObjectPaths.Num() == 0)
	{
		Result.Errors.Add(FString::Printf(TEXT("FBX animation import failed — no assets created. Check: file is valid FBX with animation data, skeleton bone names match the source rig. Source: '%s'"), *SourceFile));
		return Result;
	}

	Result.bSuccess = true;
	Result.ResultMessage = FString::Printf(TEXT("Imported animation FBX '%s' to '%s'. Created assets:\n%s"),
		*FPaths::GetCleanFilename(SourceFile), *DestPath,
		*FString::Join(Task->ImportedObjectPaths, TEXT("\n")));

	for (const FString& Path : Task->ImportedObjectPaths)
		Result.ModifiedAssets.Add(Path);

	return Result;
}

// ============================================================================
// ExecuteAssignAnimBlueprint
// ============================================================================

FAutonomixActionResult FAutonomixAnimationActions::ExecuteAssignAnimBlueprint(const TSharedRef<FJsonObject>& Params, FAutonomixActionResult& Result)
{
	FString AssetPath      = Params->GetStringField(TEXT("asset_path"));
	FString ComponentName  = Params->GetStringField(TEXT("component_name"));
	FString AnimBPPath     = Params->GetStringField(TEXT("anim_blueprint_path"));

	UBlueprint* Blueprint = LoadObject<UBlueprint>(nullptr, *AssetPath);
	if (!Blueprint)
	{
		Result.Errors.Add(FString::Printf(TEXT("Blueprint not found: '%s'"), *AssetPath));
		return Result;
	}

	// Find the SCS node for the SkeletalMeshComponent
	USCS_Node* TargetNode = nullptr;
	if (Blueprint->SimpleConstructionScript)
	{
		for (USCS_Node* Node : Blueprint->SimpleConstructionScript->GetAllNodes())
		{
			if (Node && Node->GetVariableName().ToString() == ComponentName)
			{
				TargetNode = Node;
				break;
			}
		}
	}

	if (!TargetNode)
	{
		Result.Errors.Add(FString::Printf(TEXT("Component '%s' not found in Blueprint SCS. Use get_blueprint_info to list available components."), *ComponentName));
		return Result;
	}

	USkeletalMeshComponent* SKC = Cast<USkeletalMeshComponent>(TargetNode->ComponentTemplate);
	if (!SKC)
	{
		Result.Errors.Add(FString::Printf(TEXT("Component '%s' is not a SkeletalMeshComponent — cannot assign an AnimBlueprint."), *ComponentName));
		return Result;
	}

	// Load the AnimBlueprint generated class
	UClass* AnimBPClass = LoadObject<UClass>(nullptr, *AnimBPPath);
	if (!AnimBPClass)
	{
		// Try appending _C for the generated class
		FString GeneratedClassPath = AnimBPPath;
		if (!GeneratedClassPath.EndsWith(TEXT("_C")))
			GeneratedClassPath += TEXT("_C");
		AnimBPClass = LoadObject<UClass>(nullptr, *GeneratedClassPath);
	}

	if (!AnimBPClass || !AnimBPClass->IsChildOf(UAnimInstance::StaticClass()))
	{
		Result.Errors.Add(FString::Printf(TEXT("Animation Blueprint class not found at '%s'. Ensure it ends with '_C' (the generated class). Example: /Game/Animations/ABP_Char.ABP_Char_C"), *AnimBPPath));
		return Result;
	}

	SKC->Modify();
	SKC->AnimClass = AnimBPClass;

	FKismetEditorUtilities::CompileBlueprint(Blueprint, EBlueprintCompileOptions::SkipGarbageCollection);
	Blueprint->GetOutermost()->MarkPackageDirty();

	Result.bSuccess = true;
	Result.ResultMessage = FString::Printf(TEXT("Assigned AnimBlueprint '%s' to component '%s' in '%s'."), *AnimBPPath, *ComponentName, *AssetPath);
	Result.ModifiedAssets.Add(AssetPath);
	return Result;
}

// ============================================================================
// ExecuteCreateAnimMontage
// ============================================================================

FAutonomixActionResult FAutonomixAnimationActions::ExecuteCreateAnimMontage(const TSharedRef<FJsonObject>& Params, FAutonomixActionResult& Result)
{
	FString AssetPath    = Params->GetStringField(TEXT("asset_path"));
	FString SkeletonPath = Params->GetStringField(TEXT("skeleton_path"));

	const TArray<TSharedPtr<FJsonValue>>* SequencesArray = nullptr;
	if (!Params->TryGetArrayField(TEXT("sequences"), SequencesArray) || SequencesArray->Num() == 0)
	{
		Result.Errors.Add(TEXT("Missing required field: 'sequences' (array of AnimSequence content paths)."));
		return Result;
	}

	USkeleton* Skeleton = LoadObject<USkeleton>(nullptr, *SkeletonPath);
	if (!Skeleton)
	{
		Result.Errors.Add(FString::Printf(TEXT("Skeleton not found: '%s'"), *SkeletonPath));
		return Result;
	}

	FString PackagePath = FPackageName::GetLongPackagePath(AssetPath);
	FString AssetName   = FPackageName::GetShortName(AssetPath);

	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();

	UAnimMontageFactory* Factory = NewObject<UAnimMontageFactory>();
	Factory->TargetSkeleton = Skeleton;

	// Set the first sequence as the source
	FString FirstSeqPath;
	if ((*SequencesArray)[0]->TryGetString(FirstSeqPath))
	{
		UAnimSequence* FirstSeq = LoadObject<UAnimSequence>(nullptr, *FirstSeqPath);
		if (FirstSeq)
			Factory->SourceAnimation = FirstSeq;
		else
			Result.Warnings.Add(FString::Printf(TEXT("First AnimSequence not found: '%s' — montage created with default settings."), *FirstSeqPath));
	}

	UObject* NewAsset = AssetTools.CreateAsset(AssetName, PackagePath, UAnimMontage::StaticClass(), Factory);
	UAnimMontage* NewMontage = Cast<UAnimMontage>(NewAsset);

	if (!NewMontage)
	{
		Result.Errors.Add(FString::Printf(TEXT("Failed to create AnimMontage at '%s'."), *AssetPath));
		return Result;
	}

	// Save
	UPackage* Package = NewMontage->GetOutermost();
	Package->MarkPackageDirty();
	FString PackageFilename;
	if (FPackageName::TryConvertLongPackageNameToFilename(Package->GetName(), PackageFilename, FPackageName::GetAssetPackageExtension()))
	{
		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Standalone;
		UPackage::SavePackage(Package, NewMontage, *PackageFilename, SaveArgs);
	}
	FAssetRegistryModule::AssetCreated(NewMontage);

	Result.bSuccess = true;
	Result.ResultMessage = FString::Printf(TEXT("Created AnimMontage '%s' targeting skeleton '%s'. Use PlayMontage in Blueprint T3D to play it at runtime."), *AssetName, *SkeletonPath);
	Result.ModifiedAssets.Add(AssetPath);
	return Result;
}

// ============================================================================
// ExecuteGetAnimInfo
// ============================================================================

FAutonomixActionResult FAutonomixAnimationActions::ExecuteGetAnimInfo(const TSharedRef<FJsonObject>& Params, FAutonomixActionResult& Result)
{
	FString AssetPath = Params->GetStringField(TEXT("asset_path"));

	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("queried_asset"), AssetPath);

	// Try loading as Skeleton first
	USkeleton* Skeleton = LoadObject<USkeleton>(nullptr, *AssetPath);
	if (!Skeleton)
	{
		// Try loading as AnimBlueprint to get its skeleton
		UAnimBlueprint* AnimBP = LoadObject<UAnimBlueprint>(nullptr, *AssetPath);
		if (AnimBP && AnimBP->TargetSkeleton)
			Skeleton = AnimBP->TargetSkeleton;
	}

	if (!Skeleton)
	{
		Result.Errors.Add(FString::Printf(TEXT("Could not load a Skeleton or AnimBlueprint from '%s'."), *AssetPath));
		return Result;
	}

	Root->SetStringField(TEXT("skeleton_name"), Skeleton->GetName());
	Root->SetStringField(TEXT("skeleton_path"), Skeleton->GetPathName());

	// Query asset registry for compatible AnimSequences
	IAssetRegistry& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();

	TArray<FAssetData> AnimAssets;
	FARFilter Filter;
	Filter.ClassPaths.Add(UAnimSequence::StaticClass()->GetClassPathName());
	Filter.bRecursivePaths = true;
	Filter.PackagePaths.Add(FName(TEXT("/Game")));
	AssetRegistry.GetAssets(Filter, AnimAssets);

	TArray<TSharedPtr<FJsonValue>> SequencesArray;
	for (const FAssetData& AnimAsset : AnimAssets)
	{
		// Check if tagged to our skeleton
		FAssetTagValueRef SkeletonTag = AnimAsset.TagsAndValues.FindTag(FName(TEXT("Skeleton")));
		if (SkeletonTag.IsSet() && SkeletonTag.GetValue().Contains(Skeleton->GetName()))
		{
			if (true)
			{
				TSharedPtr<FJsonObject> SeqObj = MakeShared<FJsonObject>();
				SeqObj->SetStringField(TEXT("name"), AnimAsset.AssetName.ToString());
				SeqObj->SetStringField(TEXT("path"), AnimAsset.GetObjectPathString());
				SequencesArray.Add(MakeShared<FJsonValueObject>(SeqObj));
			}
		}
	}
	Root->SetArrayField(TEXT("compatible_sequences"), SequencesArray);

	// Query Montages
	TArray<FAssetData> MontageAssets;
	FARFilter MontageFilter;
	MontageFilter.ClassPaths.Add(UAnimMontage::StaticClass()->GetClassPathName());
	MontageFilter.bRecursivePaths = true;
	MontageFilter.PackagePaths.Add(FName(TEXT("/Game")));
	AssetRegistry.GetAssets(MontageFilter, MontageAssets);

	TArray<TSharedPtr<FJsonValue>> MontagesArray;
	for (const FAssetData& MontageAsset : MontageAssets)
	{
		FAssetTagValueRef SkeletonTag = MontageAsset.TagsAndValues.FindTag(FName(TEXT("Skeleton")));
		if (SkeletonTag.IsSet() && SkeletonTag.GetValue().Contains(Skeleton->GetName()))
		{
			TSharedPtr<FJsonObject> MObj = MakeShared<FJsonObject>();
			MObj->SetStringField(TEXT("name"), MontageAsset.AssetName.ToString());
			MObj->SetStringField(TEXT("path"), MontageAsset.GetObjectPathString());
			MontagesArray.Add(MakeShared<FJsonValueObject>(MObj));
		}
	}
	Root->SetArrayField(TEXT("compatible_montages"), MontagesArray);

	FString OutputStr;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutputStr);
	FJsonSerializer::Serialize(Root.ToSharedRef(), Writer);

	Result.bSuccess = true;
	Result.ResultMessage = OutputStr;
	return Result;
}
