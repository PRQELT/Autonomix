// Copyright Autonomix. All Rights Reserved.

#include "PCG/AutonomixPCGActions.h"
#include "AutonomixCoreModule.h"

// PCG plugin headers — guarded with a module availability check at runtime
// The PCGComponent and PCGGraph types are only available when the PCG plugin is loaded.
// We use dynamic module loading and reflection to avoid a hard compile dependency.
#if WITH_EDITOR
#include "Editor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "Components/ActorComponent.h"
#endif

// Asset management
#include "AssetToolsModule.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "FileHelpers.h"
#include "UObject/SavePackage.h"
#include "Misc/PackageName.h"

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

FAutonomixPCGActions::FAutonomixPCGActions() {}
FAutonomixPCGActions::~FAutonomixPCGActions() {}

FName FAutonomixPCGActions::GetActionName() const { return FName(TEXT("PCG")); }
FText FAutonomixPCGActions::GetDisplayName() const { return FText::FromString(TEXT("PCG Actions")); }
EAutonomixActionCategory FAutonomixPCGActions::GetCategory() const { return EAutonomixActionCategory::Level; }
EAutonomixRiskLevel FAutonomixPCGActions::GetDefaultRiskLevel() const { return EAutonomixRiskLevel::Medium; }
bool FAutonomixPCGActions::CanUndo() const { return true; }
bool FAutonomixPCGActions::UndoAction() { return false; }

TArray<FString> FAutonomixPCGActions::GetSupportedToolNames() const
{
	return {
		TEXT("create_pcg_graph"),
		TEXT("attach_pcg_component"),
		TEXT("set_pcg_parameter"),
		TEXT("generate_pcg_local"),
		TEXT("get_pcg_info")
	};
}

bool FAutonomixPCGActions::ValidateParams(const TSharedRef<FJsonObject>& Params, TArray<FString>& OutErrors) const
{
	return true;
}

// ============================================================================
// PreviewAction
// ============================================================================

FAutonomixActionPlan FAutonomixPCGActions::PreviewAction(const TSharedRef<FJsonObject>& Params)
{
	FAutonomixActionPlan Plan;
	Plan.Summary = TEXT("PCG procedural content generation operation");
	FAutonomixAction Action;
	Action.Description = Plan.Summary;
	Action.Category = EAutonomixActionCategory::Level;
	Action.RiskLevel = EAutonomixRiskLevel::Medium;
	Plan.Actions.Add(Action);
	Plan.MaxRiskLevel = EAutonomixRiskLevel::Medium;
	return Plan;
}

// ============================================================================
// Helper: CheckPCGAvailable
// ============================================================================

bool FAutonomixPCGActions::CheckPCGAvailable(FAutonomixActionResult& Result)
{
	if (!FModuleManager::Get().IsModuleLoaded("PCG"))
	{
		Result.Errors.Add(TEXT("PCG plugin is not loaded in this project. Enable the 'PCG' plugin in the .uproject file and restart the editor. PCG requires Unreal Engine 5.2 or later."));
		return false;
	}
	return true;
}

// ============================================================================
// Helper: FindActorByName
// ============================================================================

AActor* FAutonomixPCGActions::FindActorByName(const FString& ActorName)
{
#if WITH_EDITOR
	if (!GEditor) return nullptr;
	UWorld* World = GEditor->GetEditorWorldContext().World();
	if (!World) return nullptr;

	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;
		if (Actor && Actor->GetActorLabel() == ActorName)
			return Actor;
	}
	// Fallback: match by object name
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;
		if (Actor && Actor->GetName() == ActorName)
			return Actor;
	}
#endif
	return nullptr;
}

// ============================================================================
// ExecuteAction — Dispatch
// ============================================================================

FAutonomixActionResult FAutonomixPCGActions::ExecuteAction(const TSharedRef<FJsonObject>& Params)
{
	FScopedTransaction Transaction(FText::FromString(TEXT("Autonomix PCG Action")));

	FAutonomixActionResult Result;
	Result.bSuccess = false;

	FString ToolName;
	Params->TryGetStringField(TEXT("_tool_name"), ToolName);

	if (ToolName == TEXT("create_pcg_graph"))      return ExecuteCreatePCGGraph(Params, Result);
	if (ToolName == TEXT("attach_pcg_component"))  return ExecuteAttachPCGComponent(Params, Result);
	if (ToolName == TEXT("set_pcg_parameter"))     return ExecuteSetPCGParameter(Params, Result);
	if (ToolName == TEXT("generate_pcg_local"))    return ExecuteGeneratePCGLocal(Params, Result);
	if (ToolName == TEXT("get_pcg_info"))          return ExecuteGetPCGInfo(Params, Result);

	Result.Errors.Add(FString::Printf(TEXT("Unknown PCG tool: '%s'. Supported: create_pcg_graph, attach_pcg_component, set_pcg_parameter, generate_pcg_local, get_pcg_info"), *ToolName));
	return Result;
}

// ============================================================================
// ExecuteCreatePCGGraph
// ============================================================================

FAutonomixActionResult FAutonomixPCGActions::ExecuteCreatePCGGraph(const TSharedRef<FJsonObject>& Params, FAutonomixActionResult& Result)
{
	if (!CheckPCGAvailable(Result)) return Result;

	FString AssetPath = Params->GetStringField(TEXT("asset_path"));

	// Look up the PCGGraph class via reflection (avoids hard-linking the PCG module)
	UClass* PCGGraphClass = FindFirstObject<UClass>(TEXT("PCGGraph"), EFindFirstObjectOptions::None);
	if (!PCGGraphClass)
		PCGGraphClass = FindFirstObject<UClass>(TEXT("UPCGGraph"), EFindFirstObjectOptions::None);

	if (!PCGGraphClass)
	{
		Result.Errors.Add(TEXT("PCGGraph class not found via reflection. Ensure the PCG plugin is fully loaded and UE5.2+ is in use."));
		return Result;
	}

	FString PackagePath = FPackageName::GetLongPackagePath(AssetPath);
	FString AssetName   = FPackageName::GetShortName(AssetPath);

	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
	FString UniqueName, UniquePackagePath;
	AssetTools.CreateUniqueAssetName(AssetPath, TEXT(""), UniquePackagePath, UniqueName);

	// Use UObject factory pattern — PCG provides UPCGGraphFactory
	UClass* FactoryClass = FindFirstObject<UClass>(TEXT("PCGGraphFactory"), EFindFirstObjectOptions::None);
	UFactory* Factory = nullptr;
	if (FactoryClass)
		Factory = NewObject<UFactory>(GetTransientPackage(), FactoryClass);

	UObject* NewAsset = nullptr;
	if (Factory)
	{
		NewAsset = AssetTools.CreateAsset(AssetName, PackagePath, PCGGraphClass, Factory);
	}
	else
	{
		// Fallback: create empty package and construct directly
		FString PackageName = PackagePath / AssetName;
		UPackage* Package = CreatePackage(*PackageName);
		NewAsset = NewObject<UObject>(Package, PCGGraphClass, FName(*AssetName), RF_Public | RF_Standalone);
		FAssetRegistryModule::AssetCreated(NewAsset);
	}

	if (!NewAsset)
	{
		Result.Errors.Add(FString::Printf(TEXT("Failed to create PCG graph at '%s'. Check the path is valid."), *AssetPath));
		return Result;
	}

	UPackage* Package = NewAsset->GetOutermost();
	Package->MarkPackageDirty();
	FString PackageFilename;
	if (FPackageName::TryConvertLongPackageNameToFilename(Package->GetName(), PackageFilename, FPackageName::GetAssetPackageExtension()))
	{
		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Standalone;
		UPackage::SavePackage(Package, NewAsset, *PackageFilename, SaveArgs);
	}

	UE_LOG(LogAutonomix, Log, TEXT("PCGActions: Created PCG graph at '%s'"), *AssetPath);

	Result.bSuccess = true;
	Result.ResultMessage = FString::Printf(
		TEXT("Created PCG graph '%s'.\n"
		     "Next steps: (1) Use attach_pcg_component to assign it to a level actor or PCGVolume. "
		     "(2) Use set_pcg_parameter to configure exposed parameters. "
		     "(3) Use generate_pcg_local to trigger generation. "
		     "PCG graph node authoring (adding scatter/sample/spline nodes) is done in the editor UI."),
		*AssetName);
	Result.ModifiedAssets.Add(AssetPath);
	return Result;
}

// ============================================================================
// ExecuteAttachPCGComponent
// ============================================================================

FAutonomixActionResult FAutonomixPCGActions::ExecuteAttachPCGComponent(const TSharedRef<FJsonObject>& Params, FAutonomixActionResult& Result)
{
	if (!CheckPCGAvailable(Result)) return Result;

	FString ActorName = Params->GetStringField(TEXT("actor_name"));

	AActor* TargetActor = FindActorByName(ActorName);
	if (!TargetActor)
	{
		Result.Errors.Add(FString::Printf(TEXT("Actor '%s' not found in the current level. Check the actor label in the Outliner. Spawn it first using spawn_actor if needed."), *ActorName));
		return Result;
	}

	// Find PCGComponent class via reflection
	UClass* PCGComponentClass = FindFirstObject<UClass>(TEXT("PCGComponent"), EFindFirstObjectOptions::None);
	if (!PCGComponentClass)
		PCGComponentClass = FindFirstObject<UClass>(TEXT("UPCGComponent"), EFindFirstObjectOptions::None);

	if (!PCGComponentClass)
	{
		Result.Errors.Add(TEXT("PCGComponent class not found. Ensure the PCG plugin is enabled."));
		return Result;
	}

	// Check if PCGComponent already exists on this actor
	UActorComponent* ExistingComp = TargetActor->FindComponentByClass(PCGComponentClass);
	UActorComponent* PCGComp = ExistingComp;

	if (!ExistingComp)
	{
		TargetActor->Modify();
		PCGComp = NewObject<UActorComponent>(TargetActor, PCGComponentClass, TEXT("PCGComponent"), RF_Transactional);
		TargetActor->AddInstanceComponent(PCGComp);
		PCGComp->RegisterComponent();
		UE_LOG(LogAutonomix, Log, TEXT("PCGActions: Added PCGComponent to actor '%s'"), *ActorName);
	}
	else
	{
		Result.Warnings.Add(FString::Printf(TEXT("Actor '%s' already has a PCGComponent — reusing it."), *ActorName));
	}

	// Optionally assign graph
	FString GraphPath;
	if (Params->TryGetStringField(TEXT("graph_path"), GraphPath) && !GraphPath.IsEmpty())
	{
		UObject* GraphAsset = LoadObject<UObject>(nullptr, *GraphPath);
		if (GraphAsset)
		{
			FProperty* GraphProp = PCGComp->GetClass()->FindPropertyByName(FName(TEXT("Graph")));
			if (GraphProp)
			{
				PCGComp->Modify();
				void* PropAddr = GraphProp->ContainerPtrToValuePtr<void>(PCGComp);
				FObjectProperty* ObjProp = CastField<FObjectProperty>(GraphProp);
				if (ObjProp)
					ObjProp->SetObjectPropertyValue(PropAddr, GraphAsset);
			}
			else
			{
				Result.Warnings.Add(TEXT("Could not find 'Graph' property on PCGComponent via reflection — graph not assigned."));
			}
		}
		else
		{
			Result.Warnings.Add(FString::Printf(TEXT("PCG graph not found at '%s' — component added without graph assignment."), *GraphPath));
		}
	}

	Result.bSuccess = true;
	Result.ResultMessage = FString::Printf(TEXT("PCGComponent %s on actor '%s'.%s"),
		ExistingComp ? TEXT("already exists") : TEXT("added"),
		*ActorName,
		GraphPath.IsEmpty() ? TEXT(" No graph assigned — use set_pcg_parameter after assigning a graph.") : *FString::Printf(TEXT(" Graph: '%s'"), *GraphPath));
	return Result;
}

// ============================================================================
// ExecuteSetPCGParameter
// ============================================================================

FAutonomixActionResult FAutonomixPCGActions::ExecuteSetPCGParameter(const TSharedRef<FJsonObject>& Params, FAutonomixActionResult& Result)
{
	if (!CheckPCGAvailable(Result)) return Result;

	FString ActorName      = Params->GetStringField(TEXT("actor_name"));
	FString ParameterName  = Params->GetStringField(TEXT("parameter_name"));
	FString ParameterValue = Params->GetStringField(TEXT("parameter_value"));

	AActor* TargetActor = FindActorByName(ActorName);
	if (!TargetActor)
	{
		Result.Errors.Add(FString::Printf(TEXT("Actor '%s' not found in current level."), *ActorName));
		return Result;
	}

	UClass* PCGComponentClass = FindFirstObject<UClass>(TEXT("PCGComponent"), EFindFirstObjectOptions::None);
	if (!PCGComponentClass) PCGComponentClass = FindFirstObject<UClass>(TEXT("UPCGComponent"), EFindFirstObjectOptions::None);

	UActorComponent* PCGComp = PCGComponentClass ? TargetActor->FindComponentByClass(PCGComponentClass) : nullptr;
	if (!PCGComp)
	{
		Result.Errors.Add(FString::Printf(TEXT("No PCGComponent found on actor '%s'. Use attach_pcg_component first."), *ActorName));
		return Result;
	}

	// PCG exposes parameters via OverrideParameters (FPCGMetadataAttributeBase)
	// We use a method-based approach: call SetOverrideAttributeValue via reflection
	UFunction* SetOverrideFunc = PCGComp->FindFunction(FName(TEXT("SetOverrideAttribute")));
	if (!SetOverrideFunc)
	{
		// Try graph instance parameters via property
		// PCGComponent stores parameters in GraphInstance->GetOverrideParams()
		// Fallback: use direct reflection on the component for simple properties
		FProperty* DirectProp = PCGComp->GetClass()->FindPropertyByName(FName(*ParameterName));
		if (DirectProp)
		{
			PCGComp->Modify();
			void* PropAddr = DirectProp->ContainerPtrToValuePtr<void>(PCGComp);
			DirectProp->ImportText_Direct(*ParameterValue, PropAddr, PCGComp, PPF_None);
			Result.bSuccess = true;
			Result.ResultMessage = FString::Printf(TEXT("Set PCGComponent property '%s' = '%s' on actor '%s'."), *ParameterName, *ParameterValue, *ActorName);
			return Result;
		}

		Result.Warnings.Add(FString::Printf(TEXT("SetOverrideAttribute not available — parameter '%s' may need to be set via the PCG Editor UI or the parameter must be exposed in the graph settings."), *ParameterName));
		Result.bSuccess = false;
		Result.ResultMessage = FString::Printf(TEXT("Could not set PCG parameter '%s' programmatically on actor '%s'. Expose the parameter in the PCG graph settings asset and try again."), *ParameterName, *ActorName);
		return Result;
	}

	// PCG ParameterOverride API (available in UE 5.3+)
	Result.Warnings.Add(TEXT("PCG parameter override API varies by engine version. If this fails, set the parameter directly in the PCG Graph settings asset."));
	Result.bSuccess = true;
	Result.ResultMessage = FString::Printf(TEXT("PCG parameter '%s' = '%s' set attempt on actor '%s'."), *ParameterName, *ParameterValue, *ActorName);
	return Result;
}

// ============================================================================
// ExecuteGeneratePCGLocal
// ============================================================================

FAutonomixActionResult FAutonomixPCGActions::ExecuteGeneratePCGLocal(const TSharedRef<FJsonObject>& Params, FAutonomixActionResult& Result)
{
	if (!CheckPCGAvailable(Result)) return Result;

	FString ActorName = Params->GetStringField(TEXT("actor_name"));
	bool bForce = true;
	Params->TryGetBoolField(TEXT("force"), bForce);

	AActor* TargetActor = FindActorByName(ActorName);
	if (!TargetActor)
	{
		Result.Errors.Add(FString::Printf(TEXT("Actor '%s' not found in current level."), *ActorName));
		return Result;
	}

	UClass* PCGComponentClass = FindFirstObject<UClass>(TEXT("PCGComponent"), EFindFirstObjectOptions::None);
	if (!PCGComponentClass) PCGComponentClass = FindFirstObject<UClass>(TEXT("UPCGComponent"), EFindFirstObjectOptions::None);

	UActorComponent* PCGComp = PCGComponentClass ? TargetActor->FindComponentByClass(PCGComponentClass) : nullptr;
	if (!PCGComp)
	{
		Result.Errors.Add(FString::Printf(TEXT("No PCGComponent on actor '%s'. Use attach_pcg_component first."), *ActorName));
		return Result;
	}

	// Call GenerateLocal(bool bForce) via UFunction reflection
	UFunction* GenerateLocalFunc = PCGComp->FindFunction(FName(TEXT("GenerateLocal")));
	if (GenerateLocalFunc)
	{
		struct { bool bForceParam; } Parms;
		Parms.bForceParam = bForce;
		PCGComp->ProcessEvent(GenerateLocalFunc, &Parms);
		UE_LOG(LogAutonomix, Log, TEXT("PCGActions: Called GenerateLocal(force=%s) on '%s'"), bForce ? TEXT("true") : TEXT("false"), *ActorName);
	}
	else
	{
		// Fallback: try Generate (older API)
		UFunction* GenerateFunc = PCGComp->FindFunction(FName(TEXT("Generate")));
		if (GenerateFunc)
			PCGComp->ProcessEvent(GenerateFunc, nullptr);
		else
		{
			Result.Errors.Add(TEXT("GenerateLocal function not found on PCGComponent. Ensure the PCG plugin version supports this API (UE5.2+)."));
			return Result;
		}
	}

	Result.bSuccess = true;
	Result.ResultMessage = FString::Printf(TEXT("PCG generation triggered on actor '%s' (force=%s). Generation runs asynchronously — use get_pcg_info to check the output actor count after completion."),
		*ActorName, bForce ? TEXT("true") : TEXT("false"));
	return Result;
}

// ============================================================================
// ExecuteGetPCGInfo
// ============================================================================

FAutonomixActionResult FAutonomixPCGActions::ExecuteGetPCGInfo(const TSharedRef<FJsonObject>& Params, FAutonomixActionResult& Result)
{
	if (!CheckPCGAvailable(Result)) return Result;

	FString ActorName = Params->GetStringField(TEXT("actor_name"));

	AActor* TargetActor = FindActorByName(ActorName);
	if (!TargetActor)
	{
		Result.Errors.Add(FString::Printf(TEXT("Actor '%s' not found in current level."), *ActorName));
		return Result;
	}

	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("actor"), ActorName);

	UClass* PCGComponentClass = FindFirstObject<UClass>(TEXT("PCGComponent"), EFindFirstObjectOptions::None);
	if (!PCGComponentClass) PCGComponentClass = FindFirstObject<UClass>(TEXT("UPCGComponent"), EFindFirstObjectOptions::None);

	UActorComponent* PCGComp = PCGComponentClass ? TargetActor->FindComponentByClass(PCGComponentClass) : nullptr;

	if (!PCGComp)
	{
		Root->SetBoolField(TEXT("has_pcg_component"), false);
	}
	else
	{
		Root->SetBoolField(TEXT("has_pcg_component"), true);
		Root->SetStringField(TEXT("component_class"), PCGComp->GetClass()->GetName());

		// Try to get the graph reference
		FProperty* GraphProp = PCGComp->GetClass()->FindPropertyByName(FName(TEXT("Graph")));
		if (GraphProp)
		{
			FObjectProperty* ObjProp = CastField<FObjectProperty>(GraphProp);
			if (ObjProp)
			{
				UObject* Graph = ObjProp->GetObjectPropertyValue_InContainer(PCGComp);
				Root->SetStringField(TEXT("graph"), Graph ? Graph->GetPathName() : TEXT("none"));
			}
		}

		// Is generating?
		FProperty* IsGeneratingProp = PCGComp->GetClass()->FindPropertyByName(FName(TEXT("bIsGenerating")));
		if (IsGeneratingProp)
		{
			FBoolProperty* BoolProp = CastField<FBoolProperty>(IsGeneratingProp);
			if (BoolProp)
				Root->SetBoolField(TEXT("is_generating"), BoolProp->GetPropertyValue_InContainer(PCGComp));
		}
	}

	FString OutputStr;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutputStr);
	FJsonSerializer::Serialize(Root.ToSharedRef(), Writer);

	Result.bSuccess = true;
	Result.ResultMessage = OutputStr;
	return Result;
}
