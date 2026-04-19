// Copyright Autonomix. All Rights Reserved.

#include "BehaviorTree/AutonomixBehaviorTreeActions.h"
#include "AutonomixCoreModule.h"
#include "AutonomixSettings.h"
#include "BehaviorTree/BehaviorTree.h"
#include "BehaviorTree/BlackboardData.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Bool.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Int.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Float.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_String.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Name.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Vector.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Rotator.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Object.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Class.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Enum.h"
#include "BehaviorTree/BTCompositeNode.h"
#include "BehaviorTree/Composites/BTComposite_Selector.h"
#include "BehaviorTree/Composites/BTComposite_Sequence.h"
#include "BehaviorTree/Composites/BTComposite_SimpleParallel.h"
#include "BehaviorTree/Tasks/BTTask_Wait.h"
#include "BehaviorTree/Tasks/BTTask_MoveTo.h"
#include "BehaviorTree/Tasks/BTTask_RunBehavior.h"
#include "BehaviorTree/Decorators/BTDecorator_Blackboard.h"
#include "BehaviorTree/Decorators/BTDecorator_Cooldown.h"
#include "BehaviorTree/Services/BTService_DefaultFocus.h"
#include "NavigationSystem.h"
#include "NavMesh/NavMeshBoundsVolume.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "UObject/SavePackage.h"
#include "Editor.h"
#include "Engine/World.h"
#include "EngineUtils.h"

#define LOCTEXT_NAMESPACE "AutonomixBehaviorTreeActions"

// ============================================================================
// Lifecycle
// ============================================================================

FAutonomixBehaviorTreeActions::FAutonomixBehaviorTreeActions() {}
FAutonomixBehaviorTreeActions::~FAutonomixBehaviorTreeActions() {}

// ============================================================================
// IAutonomixActionExecutor Interface
// ============================================================================

FName FAutonomixBehaviorTreeActions::GetActionName() const { return FName(TEXT("BehaviorTree")); }
FText FAutonomixBehaviorTreeActions::GetDisplayName() const { return LOCTEXT("DisplayName", "Behavior Tree & AI"); }
EAutonomixActionCategory FAutonomixBehaviorTreeActions::GetCategory() const { return EAutonomixActionCategory::Blueprint; }
EAutonomixRiskLevel FAutonomixBehaviorTreeActions::GetDefaultRiskLevel() const { return EAutonomixRiskLevel::Medium; }
bool FAutonomixBehaviorTreeActions::CanUndo() const { return true; }
bool FAutonomixBehaviorTreeActions::UndoAction() { return GEditor && GEditor->UndoTransaction(); }

TArray<FString> FAutonomixBehaviorTreeActions::GetSupportedToolNames() const
{
	return {
		TEXT("create_blackboard"),
		TEXT("create_behavior_tree"),
		TEXT("inject_bt_nodes"),
		TEXT("configure_navmesh")
	};
}

bool FAutonomixBehaviorTreeActions::ValidateParams(const TSharedRef<FJsonObject>& Params, TArray<FString>& OutErrors) const
{
	return true;
}

FAutonomixActionPlan FAutonomixBehaviorTreeActions::PreviewAction(const TSharedRef<FJsonObject>& Params)
{
	FAutonomixActionPlan Plan;
	FString Action;
	Params->TryGetStringField(TEXT("action"), Action);
	if (Action.IsEmpty()) Params->TryGetStringField(TEXT("tool_name"), Action);

	FString AssetPath;
	Params->TryGetStringField(TEXT("asset_path"), AssetPath);

	if (Action == TEXT("create_blackboard"))
		Plan.Summary = FString::Printf(TEXT("Create Blackboard: %s"), *AssetPath);
	else if (Action == TEXT("create_behavior_tree"))
		Plan.Summary = FString::Printf(TEXT("Create Behavior Tree: %s"), *AssetPath);
	else if (Action == TEXT("inject_bt_nodes"))
		Plan.Summary = FString::Printf(TEXT("Inject nodes into BT: %s"), *AssetPath);
	else if (Action == TEXT("configure_navmesh"))
		Plan.Summary = TEXT("Configure NavMesh for current level");
	else
		Plan.Summary = TEXT("Behavior Tree operation");

	Plan.MaxRiskLevel = EAutonomixRiskLevel::Medium;
	FAutonomixAction A;
	A.Description = Plan.Summary;
	A.Category = EAutonomixActionCategory::Blueprint;
	A.RiskLevel = EAutonomixRiskLevel::Medium;
	Plan.Actions.Add(A);
	return Plan;
}

FAutonomixActionResult FAutonomixBehaviorTreeActions::ExecuteAction(const TSharedRef<FJsonObject>& Params)
{
	FAutonomixActionResult Result;
	Result.bSuccess = false;

	FString Action;
	if (!Params->TryGetStringField(TEXT("action"), Action) || Action.IsEmpty())
		Params->TryGetStringField(TEXT("tool_name"), Action);

	if (Action == TEXT("create_blackboard"))
		return ExecuteCreateBlackboard(Params, Result);
	else if (Action == TEXT("create_behavior_tree"))
		return ExecuteCreateBehaviorTree(Params, Result);
	else if (Action == TEXT("inject_bt_nodes"))
		return ExecuteInjectBTNodes(Params, Result);
	else if (Action == TEXT("configure_navmesh"))
		return ExecuteConfigureNavMesh(Params, Result);

	Result.Errors.Add(TEXT("Unknown BT action. Use create_blackboard, create_behavior_tree, inject_bt_nodes, or configure_navmesh."));
	return Result;
}

// ============================================================================
// create_blackboard
// ============================================================================

FAutonomixActionResult FAutonomixBehaviorTreeActions::ExecuteCreateBlackboard(
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
	UObject* NewAsset = AssetTools.CreateAsset(AssetName, PackagePath, UBlackboardData::StaticClass(), nullptr);

	if (!NewAsset)
	{
		Result.Errors.Add(FString::Printf(TEXT("Failed to create Blackboard at '%s'."), *AssetPath));
		return Result;
	}

	UBlackboardData* Blackboard = Cast<UBlackboardData>(NewAsset);

	// Add keys from JSON array
	const TArray<TSharedPtr<FJsonValue>>* KeysArray;
	if (Params->TryGetArrayField(TEXT("keys"), KeysArray))
	{
		for (const TSharedPtr<FJsonValue>& KeyVal : *KeysArray)
		{
			const TSharedPtr<FJsonObject>* KeyObjPtr;
			if (!KeyVal->TryGetObject(KeyObjPtr)) continue;
			const TSharedPtr<FJsonObject>& KeyObj = *KeyObjPtr;

			FString KeyName, KeyType;
			KeyObj->TryGetStringField(TEXT("name"), KeyName);
			KeyObj->TryGetStringField(TEXT("type"), KeyType);

			if (KeyName.IsEmpty() || KeyType.IsEmpty()) continue;

			FBlackboardEntry Entry;
			Entry.EntryName = FName(*KeyName);

			// Map type string to UBlackboardKeyType subclass
			KeyType = KeyType.ToLower();
			if (KeyType == TEXT("bool"))
				Entry.KeyType = NewObject<UBlackboardKeyType_Bool>(Blackboard);
			else if (KeyType == TEXT("int") || KeyType == TEXT("int32"))
				Entry.KeyType = NewObject<UBlackboardKeyType_Int>(Blackboard);
			else if (KeyType == TEXT("float"))
				Entry.KeyType = NewObject<UBlackboardKeyType_Float>(Blackboard);
			else if (KeyType == TEXT("string"))
				Entry.KeyType = NewObject<UBlackboardKeyType_String>(Blackboard);
			else if (KeyType == TEXT("name"))
				Entry.KeyType = NewObject<UBlackboardKeyType_Name>(Blackboard);
			else if (KeyType == TEXT("vector"))
				Entry.KeyType = NewObject<UBlackboardKeyType_Vector>(Blackboard);
			else if (KeyType == TEXT("rotator"))
				Entry.KeyType = NewObject<UBlackboardKeyType_Rotator>(Blackboard);
			else if (KeyType == TEXT("object") || KeyType == TEXT("actor"))
				Entry.KeyType = NewObject<UBlackboardKeyType_Object>(Blackboard);
			else if (KeyType == TEXT("class"))
				Entry.KeyType = NewObject<UBlackboardKeyType_Class>(Blackboard);
			else if (KeyType == TEXT("enum"))
				Entry.KeyType = NewObject<UBlackboardKeyType_Enum>(Blackboard);
			else
			{
				Result.Warnings.Add(FString::Printf(TEXT("Unknown key type '%s' for key '%s', defaulting to Object."), *KeyType, *KeyName));
				Entry.KeyType = NewObject<UBlackboardKeyType_Object>(Blackboard);
			}

			Blackboard->Keys.Add(Entry);
		}
	}

	Blackboard->MarkPackageDirty();

	Result.bSuccess = true;
	Result.ModifiedAssets.Add(AssetPath);
	Result.ResultMessage = FString::Printf(
		TEXT("Created Blackboard '%s' with %d keys."), *AssetPath, Blackboard->Keys.Num());
	return Result;
}

// ============================================================================
// create_behavior_tree
// ============================================================================

FAutonomixActionResult FAutonomixBehaviorTreeActions::ExecuteCreateBehaviorTree(
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
	UObject* NewAsset = AssetTools.CreateAsset(AssetName, PackagePath, UBehaviorTree::StaticClass(), nullptr);

	if (!NewAsset)
	{
		Result.Errors.Add(FString::Printf(TEXT("Failed to create Behavior Tree at '%s'."), *AssetPath));
		return Result;
	}

	UBehaviorTree* BT = Cast<UBehaviorTree>(NewAsset);

	// Assign blackboard if specified
	FString BlackboardPath;
	if (Params->TryGetStringField(TEXT("blackboard_asset"), BlackboardPath))
	{
		UBlackboardData* BB = LoadObject<UBlackboardData>(nullptr, *BlackboardPath);
		if (BB)
		{
			BT->BlackboardAsset = BB;
		}
		else
		{
			Result.Warnings.Add(FString::Printf(TEXT("Blackboard '%s' not found. BT created without Blackboard assignment."), *BlackboardPath));
		}
	}

	BT->MarkPackageDirty();

	Result.bSuccess = true;
	Result.ModifiedAssets.Add(AssetPath);
	Result.ResultMessage = FString::Printf(
		TEXT("Created Behavior Tree '%s'%s. Use inject_bt_nodes to add Composites, Tasks, Decorators, and Services."),
		*AssetPath,
		BT->BlackboardAsset ? *FString::Printf(TEXT(" with Blackboard '%s'"), *BlackboardPath) : TEXT(""));
	return Result;
}

// ============================================================================
// inject_bt_nodes
// ============================================================================

FAutonomixActionResult FAutonomixBehaviorTreeActions::ExecuteInjectBTNodes(
	const TSharedRef<FJsonObject>& Params, FAutonomixActionResult& Result)
{
	FString AssetPath;
	if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
	{
		Result.Errors.Add(TEXT("Missing required field: 'asset_path'"));
		return Result;
	}

	UBehaviorTree* BT = LoadObject<UBehaviorTree>(nullptr, *AssetPath);
	if (!BT)
	{
		Result.Errors.Add(FString::Printf(TEXT("Behavior Tree not found at '%s'."), *AssetPath));
		return Result;
	}

	// Parse nodes array
	const TArray<TSharedPtr<FJsonValue>>* NodesArray;
	if (!Params->TryGetArrayField(TEXT("nodes"), NodesArray))
	{
		Result.Errors.Add(TEXT("Missing required field: 'nodes' — array of node definitions"));
		return Result;
	}

	int32 NodesAdded = 0;
	FString Report;

	// For now, we support a flat structure: root composite + children
	// More complex hierarchies can be built via multiple inject_bt_nodes calls
	for (const TSharedPtr<FJsonValue>& NodeVal : *NodesArray)
	{
		const TSharedPtr<FJsonObject>* NodeObjPtr;
		if (!NodeVal->TryGetObject(NodeObjPtr)) continue;
		const TSharedPtr<FJsonObject>& NodeObj = *NodeObjPtr;

		FString NodeType;
		NodeObj->TryGetStringField(TEXT("type"), NodeType);
		NodeType = NodeType.ToLower();

		FString NodeName;
		NodeObj->TryGetStringField(TEXT("name"), NodeName);

		if (NodeType == TEXT("selector") || NodeType == TEXT("sequence") || NodeType == TEXT("parallel"))
		{
			// Create composite node
			UBTCompositeNode* Composite = nullptr;

			if (NodeType == TEXT("selector"))
				Composite = NewObject<UBTComposite_Selector>(BT);
			else if (NodeType == TEXT("sequence"))
				Composite = NewObject<UBTComposite_Sequence>(BT);
			else if (NodeType == TEXT("parallel"))
				Composite = NewObject<UBTComposite_SimpleParallel>(BT);

			if (Composite)
			{
				Composite->NodeName = NodeName.IsEmpty() ? NodeType : NodeName;

				// If this is the first composite and BT has no root, set as root
				if (!BT->RootNode)
				{
					BT->RootNode = Composite;
					Report += FString::Printf(TEXT("Set root node: %s (%s)\n"), *Composite->NodeName, *NodeType);
				}
				else
				{
					// Add as child of root
					FBTCompositeChild Child;
					Child.ChildComposite = Composite;
					BT->RootNode->Children.Add(Child);
					Report += FString::Printf(TEXT("Added child composite: %s (%s)\n"), *Composite->NodeName, *NodeType);
				}
				NodesAdded++;
			}
		}
		else if (NodeType == TEXT("wait"))
		{
			UBTTask_Wait* WaitTask = NewObject<UBTTask_Wait>(BT);
			float WaitTime = 5.0f;
			NodeObj->TryGetNumberField(TEXT("wait_time"), WaitTime);
			WaitTask->WaitTime = WaitTime;
			WaitTask->NodeName = NodeName.IsEmpty() ? TEXT("Wait") : NodeName;

			if (BT->RootNode)
			{
				FBTCompositeChild Child;
				Child.ChildTask = WaitTask;
				BT->RootNode->Children.Add(Child);
				Report += FString::Printf(TEXT("Added Wait task: %.1fs\n"), WaitTime);
				NodesAdded++;
			}
		}
		else if (NodeType == TEXT("moveto") || NodeType == TEXT("move_to"))
		{
			UBTTask_MoveTo* MoveTask = NewObject<UBTTask_MoveTo>(BT);
			MoveTask->NodeName = NodeName.IsEmpty() ? TEXT("MoveTo") : NodeName;
			float AcceptRadius = 50.0f;
			NodeObj->TryGetNumberField(TEXT("acceptable_radius"), AcceptRadius);
			MoveTask->AcceptableRadius = AcceptRadius;

			if (BT->RootNode)
			{
				FBTCompositeChild Child;
				Child.ChildTask = MoveTask;
				BT->RootNode->Children.Add(Child);
				Report += FString::Printf(TEXT("Added MoveTo task (radius: %.0f)\n"), AcceptRadius);
				NodesAdded++;
			}
		}
		else
		{
			Report += FString::Printf(TEXT("Skipped unknown node type: '%s'\n"), *NodeType);
		}
	}

	BT->MarkPackageDirty();

	Result.bSuccess = NodesAdded > 0;
	Result.ModifiedAssets.Add(AssetPath);
	Result.ResultMessage = FString::Printf(
		TEXT("Injected %d nodes into BT '%s':\n%s"),
		NodesAdded, *AssetPath, *Report);

	if (NodesAdded == 0)
	{
		Result.Errors.Add(TEXT("No nodes were added. Check node type names. Supported: selector, sequence, parallel, wait, moveto"));
	}

	return Result;
}

// ============================================================================
// configure_navmesh
// ============================================================================

FAutonomixActionResult FAutonomixBehaviorTreeActions::ExecuteConfigureNavMesh(
	const TSharedRef<FJsonObject>& Params, FAutonomixActionResult& Result)
{
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World)
	{
		Result.Errors.Add(TEXT("No editor world available."));
		return Result;
	}

	// Check if NavMeshBoundsVolume already exists
	bool bVolumeExists = false;
	for (TActorIterator<ANavMeshBoundsVolume> It(World); It; ++It)
	{
		bVolumeExists = true;
		break;
	}

	FString Report;

	if (!bVolumeExists)
	{
		// Parse bounds (default: large volume covering most gameplay areas)
		FVector Location(0.0f, 0.0f, 0.0f);
		FVector Scale(50.0f, 50.0f, 10.0f);  // Each unit = 200 UU, so 50 = 10000 UU = ~100m

		const TSharedPtr<FJsonObject>* LocationObj;
		if (Params->TryGetObjectField(TEXT("location"), LocationObj))
		{
			(*LocationObj)->TryGetNumberField(TEXT("x"), Location.X);
			(*LocationObj)->TryGetNumberField(TEXT("y"), Location.Y);
			(*LocationObj)->TryGetNumberField(TEXT("z"), Location.Z);
		}

		const TSharedPtr<FJsonObject>* ScaleObj;
		if (Params->TryGetObjectField(TEXT("scale"), ScaleObj))
		{
			(*ScaleObj)->TryGetNumberField(TEXT("x"), Scale.X);
			(*ScaleObj)->TryGetNumberField(TEXT("y"), Scale.Y);
			(*ScaleObj)->TryGetNumberField(TEXT("z"), Scale.Z);
		}

		// Spawn the NavMeshBoundsVolume
		FActorSpawnParameters SpawnParams;
		SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

		ANavMeshBoundsVolume* NavVolume = World->SpawnActor<ANavMeshBoundsVolume>(
			ANavMeshBoundsVolume::StaticClass(),
			FTransform(FRotator::ZeroRotator, Location, Scale),
			SpawnParams);

		if (NavVolume)
		{
			Report += FString::Printf(TEXT("Spawned NavMeshBoundsVolume at (%s) with scale (%s)\n"),
				*Location.ToString(), *Scale.ToString());
		}
		else
		{
			Result.Errors.Add(TEXT("Failed to spawn NavMeshBoundsVolume."));
			return Result;
		}
	}
	else
	{
		Report += TEXT("NavMeshBoundsVolume already exists in the level.\n");
	}

	// Trigger NavMesh rebuild
	bool bRebuild = true;
	Params->TryGetBoolField(TEXT("rebuild"), bRebuild);

	if (bRebuild)
	{
		UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
		if (NavSys)
		{
			NavSys->Build();
			Report += TEXT("NavMesh rebuild triggered.\n");
		}
		else
		{
			Report += TEXT("WARNING: NavigationSystemV1 not found. Ensure NavigationSystem is enabled in Project Settings.\n");
		}
	}

	Result.bSuccess = true;
	Result.ResultMessage = Report;
	return Result;
}

#undef LOCTEXT_NAMESPACE
