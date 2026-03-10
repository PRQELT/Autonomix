// Copyright Autonomix. All Rights Reserved.

#include "Blueprint/AutonomixBlueprintActions.h"
#include "AutonomixCoreModule.h"

// Kismet / Blueprint API
#include "Kismet2/KismetEditorUtilities.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "KismetCompiler.h"
#include "EdGraphUtilities.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_K2.h"
#include "K2Node_Event.h"
#include "K2Node_CallFunction.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_FunctionResult.h"
#include "K2Node_VariableGet.h"
#include "K2Node_VariableSet.h"
#include "K2Node_CustomEvent.h"
#include "K2Node_EnhancedInputAction.h"
#include "InputAction.h"

// UE classes
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/SimpleConstructionScript.h"
#include "Engine/SCS_Node.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/SkeletalMesh.h"
#include "GameFramework/Character.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/PlayerController.h"

// Asset management
#include "AssetToolsModule.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Factories/BlueprintFactory.h"
#include "FileHelpers.h"
#include "UObject/SavePackage.h"
#include "Misc/PackageName.h"

// Serialization / JSON
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

// Misc
#include "ScopedTransaction.h"
#include "Internationalization/Regex.h"

// ============================================================================
// Statics / Lifecycle
// ============================================================================

FAutonomixBlueprintActions::FAutonomixBlueprintActions() {}
FAutonomixBlueprintActions::~FAutonomixBlueprintActions() {}

FName FAutonomixBlueprintActions::GetActionName() const { return FName(TEXT("Blueprint")); }
FText FAutonomixBlueprintActions::GetDisplayName() const { return FText::FromString(TEXT("Blueprint Actions")); }
EAutonomixActionCategory FAutonomixBlueprintActions::GetCategory() const { return EAutonomixActionCategory::Blueprint; }
EAutonomixRiskLevel FAutonomixBlueprintActions::GetDefaultRiskLevel() const { return EAutonomixRiskLevel::Medium; }
bool FAutonomixBlueprintActions::CanUndo() const { return true; }
bool FAutonomixBlueprintActions::UndoAction() { return false; }

TArray<FString> FAutonomixBlueprintActions::GetSupportedToolNames() const
{
	return {
		TEXT("create_blueprint_actor"),
		TEXT("add_blueprint_component"),
		TEXT("add_blueprint_variable"),
		TEXT("add_blueprint_function"),
		TEXT("add_blueprint_event"),
		TEXT("compile_blueprint"),
		TEXT("set_blueprint_defaults"),
		TEXT("set_component_properties"),
		TEXT("inject_blueprint_nodes_t3d"),
		TEXT("get_blueprint_info"),
		TEXT("connect_blueprint_pins"),
		TEXT("add_enhanced_input_node"),
		TEXT("modify_blueprint"),
		TEXT("verify_blueprint_connections")
	};
}

bool FAutonomixBlueprintActions::ValidateParams(const TSharedRef<FJsonObject>& Params, TArray<FString>& OutErrors) const
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

FAutonomixActionPlan FAutonomixBlueprintActions::PreviewAction(const TSharedRef<FJsonObject>& Params)
{
	FAutonomixActionPlan Plan;
	FString AssetPath = Params->GetStringField(TEXT("asset_path"));
	Plan.Summary = FString::Printf(TEXT("Blueprint operation at %s"), *AssetPath);

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

FAutonomixActionResult FAutonomixBlueprintActions::ExecuteAction(const TSharedRef<FJsonObject>& Params)
{
	FScopedTransaction Transaction(FText::FromString(TEXT("Autonomix Blueprint Action")));

	FAutonomixActionResult Result;
	Result.bSuccess = false;

	FString ToolName;
	Params->TryGetStringField(TEXT("_tool_name"), ToolName);

	if (ToolName == TEXT("create_blueprint_actor"))             return ExecuteCreateBlueprint(Params, Result);
	if (ToolName == TEXT("add_blueprint_component"))            return ExecuteAddComponent(Params, Result);
	if (ToolName == TEXT("add_blueprint_variable"))             return ExecuteAddVariable(Params, Result);
	if (ToolName == TEXT("add_blueprint_function"))             return ExecuteAddFunction(Params, Result);
	if (ToolName == TEXT("add_blueprint_event"))                return ExecuteAddEventHandler(Params, Result);
	if (ToolName == TEXT("compile_blueprint"))                  return ExecuteCompileBlueprint(Params, Result);
	if (ToolName == TEXT("set_blueprint_defaults"))             return ExecuteSetDefaults(Params, Result);
	if (ToolName == TEXT("set_component_properties"))           return ExecuteSetComponentProperties(Params, Result);
	if (ToolName == TEXT("inject_blueprint_nodes_t3d"))         return ExecuteInjectNodesT3D(Params, Result);
	if (ToolName == TEXT("get_blueprint_info"))                 return ExecuteGetBlueprintInfo(Params, Result);
	if (ToolName == TEXT("connect_blueprint_pins"))             return ExecuteConnectPins(Params, Result);
	if (ToolName == TEXT("add_enhanced_input_node"))            return ExecuteAddEnhancedInputNode(Params, Result);
	if (ToolName == TEXT("verify_blueprint_connections"))       return ExecuteVerifyConnections(Params, Result);

	// Legacy param-based fallback dispatch
	if (Params->HasField(TEXT("parent_class")))      return ExecuteCreateBlueprint(Params, Result);
	if (Params->HasField(TEXT("component_class")))   return ExecuteAddComponent(Params, Result);
	if (Params->HasField(TEXT("variable_name")))     return ExecuteAddVariable(Params, Result);
	if (Params->HasField(TEXT("function_name")))     return ExecuteAddFunction(Params, Result);
	if (Params->HasField(TEXT("t3d_text")))          return ExecuteInjectNodesT3D(Params, Result);
	if (Params->HasField(TEXT("defaults")))          return ExecuteSetDefaults(Params, Result);

	Result.Errors.Add(FString::Printf(TEXT("Unknown Blueprint tool: '%s'. Supported: create_blueprint_actor, add_blueprint_component, add_blueprint_variable, add_blueprint_function, add_blueprint_event, compile_blueprint, set_blueprint_defaults, set_component_properties, inject_blueprint_nodes_t3d, get_blueprint_info"), *ToolName));
	return Result;
}

// ============================================================================
// Helper: CompileAndReport
// ============================================================================

bool FAutonomixBlueprintActions::CompileAndReport(UBlueprint* Blueprint, FAutonomixActionResult& Result, bool bSkipGC)
{
	check(Blueprint);
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);

	FCompilerResultsLog Log;
	const EBlueprintCompileOptions Opts = bSkipGC
		? EBlueprintCompileOptions::SkipGarbageCollection
		: EBlueprintCompileOptions::None;

	FKismetEditorUtilities::CompileBlueprint(Blueprint, Opts, &Log);

	for (const TSharedRef<FTokenizedMessage>& Msg : Log.Messages)
	{
		FString MsgText = Msg->ToText().ToString();
		if (Msg->GetSeverity() == EMessageSeverity::Error)
		{
			Result.Errors.Add(FString::Printf(TEXT("COMPILE ERROR: %s"), *MsgText));
		}
		else if (Msg->GetSeverity() == EMessageSeverity::Warning)
		{
			Result.Warnings.Add(FString::Printf(TEXT("COMPILE WARNING: %s"), *MsgText));
		}
	}

	return (Blueprint->Status != BS_Error);
}

// ============================================================================
// Helper: FindOrCreateEventGraph
// ============================================================================

UEdGraph* FAutonomixBlueprintActions::FindOrCreateEventGraph(UBlueprint* Blueprint, const FString& GraphName)
{
	check(Blueprint);

	// Search UbergraphPages
	for (UEdGraph* Graph : Blueprint->UbergraphPages)
	{
		if (Graph && Graph->GetName() == GraphName)
			return Graph;
	}
	// Search FunctionGraphs
	for (UEdGraph* Graph : Blueprint->FunctionGraphs)
	{
		if (Graph && Graph->GetName() == GraphName)
			return Graph;
	}

	// If looking for EventGraph / NewEventGraph, create an uber-graph page
	if (GraphName == TEXT("EventGraph") || GraphName == TEXT("NewEventGraph"))
	{
		UEdGraph* NewGraph = FBlueprintEditorUtils::CreateNewGraph(
			Blueprint,
			FName(*GraphName),
			UEdGraph::StaticClass(),
			UEdGraphSchema_K2::StaticClass());

		FBlueprintEditorUtils::AddUbergraphPage(Blueprint, NewGraph);
		return NewGraph;
	}

	return nullptr;
}

// ============================================================================
// Helper: FindSCSNodeByName
// ============================================================================

USCS_Node* FAutonomixBlueprintActions::FindSCSNodeByName(UBlueprint* Blueprint, const FString& NodeName)
{
	if (!Blueprint || !Blueprint->SimpleConstructionScript)
		return nullptr;

	TArray<USCS_Node*> AllNodes = Blueprint->SimpleConstructionScript->GetAllNodes();
	for (USCS_Node* Node : AllNodes)
	{
		if (Node && Node->GetVariableName().ToString() == NodeName)
			return Node;
	}
	return nullptr;
}

// ============================================================================
// Helper: ResolveT3DPlaceholders
// ============================================================================

FString FAutonomixBlueprintActions::ResolveT3DPlaceholders(const FString& T3DText)
{
	// Build a deterministic map: placeholder token → fresh GUID string
	// Tokens look like: LINK_1, GUID_A, NODEREF_Entry, ID_Foo
	TMap<FString, FString> PlaceholderMap;

	// Pattern: word characters starting with LINK_|GUID_|NODEREF_|ID_
	const FRegexPattern Pattern(TEXT("(LINK_|GUID_|NODEREF_|ID_)[A-Za-z0-9_]+"));
	FRegexMatcher Matcher(Pattern, T3DText);

	while (Matcher.FindNext())
	{
		FString Token = Matcher.GetCaptureGroup(0);
		if (!PlaceholderMap.Contains(Token))
		{
			// Generate a UE-style 32-char uppercase GUID (no dashes)
			FGuid NewGuid = FGuid::NewGuid();
			FString GuidStr = NewGuid.ToString(EGuidFormats::Digits).ToUpper();
			PlaceholderMap.Add(Token, GuidStr);
		}
	}

	FString Result = T3DText;
	for (const auto& Pair : PlaceholderMap)
	{
		Result = Result.Replace(*Pair.Key, *Pair.Value, ESearchCase::CaseSensitive);
	}

	return Result;
}

// ============================================================================
// Helper: DetectInfiniteLoopRisk
// ============================================================================

bool FAutonomixBlueprintActions::DetectInfiniteLoopRisk(UBlueprint* Blueprint, TArray<FString>& OutWarnings) const
{
	if (!Blueprint) return false;
	bool bRiskDetected = false;

	for (UEdGraph* Graph : Blueprint->UbergraphPages)
	{
		if (!Graph) continue;
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			UK2Node_Event* EventNode = Cast<UK2Node_Event>(Node);
			if (!EventNode) continue;
			FName EventName = EventNode->GetFunctionName();
			if (EventName == FName(TEXT("ReceiveTick")))
			{
				OutWarnings.Add(TEXT("WARNING: EventTick detected. Avoid spawning actors, adding components, or performing heavy operations in Tick — this runs every frame and can cause severe performance degradation."));
				bRiskDetected = true;
			}
		}
	}

	if (Blueprint->SimpleConstructionScript)
	{
		TArray<USCS_Node*> AllNodes = Blueprint->SimpleConstructionScript->GetAllNodes();
		if (AllNodes.Num() > 50)
		{
			OutWarnings.Add(FString::Printf(TEXT("WARNING: Construction script has %d components. This may cause performance issues."), AllNodes.Num()));
			bRiskDetected = true;
		}
	}

	return bRiskDetected;
}

// ============================================================================
// Helper: BuildBlueprintInfoJson
// ============================================================================

FString FAutonomixBlueprintActions::BuildBlueprintInfoJson(UBlueprint* Blueprint)
{
	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();

	// Basic info
	Root->SetStringField(TEXT("asset_path"), Blueprint->GetPathName());
	Root->SetStringField(TEXT("parent_class"), Blueprint->ParentClass ? Blueprint->ParentClass->GetName() : TEXT("unknown"));

	FString StatusStr = TEXT("unknown");
	switch (Blueprint->Status)
	{
	case BS_UpToDate: StatusStr = TEXT("up_to_date"); break;
	case BS_Dirty:    StatusStr = TEXT("dirty"); break;
	case BS_Error:    StatusStr = TEXT("error"); break;
	case BS_Unknown:  StatusStr = TEXT("unknown"); break;
	default:          break;
	}
	Root->SetStringField(TEXT("compile_status"), StatusStr);

	// Variables
	TArray<TSharedPtr<FJsonValue>> VarsArray;
	for (const FBPVariableDescription& Var : Blueprint->NewVariables)
	{
		TSharedPtr<FJsonObject> VarObj = MakeShared<FJsonObject>();
		VarObj->SetStringField(TEXT("name"), Var.VarName.ToString());
		VarObj->SetStringField(TEXT("type"), Var.VarType.PinCategory.ToString());
		if (!Var.Category.IsEmpty())
			VarObj->SetStringField(TEXT("category"), Var.Category.ToString());
		VarsArray.Add(MakeShared<FJsonValueObject>(VarObj));
	}
	Root->SetArrayField(TEXT("variables"), VarsArray);

	// SCS Components
	TArray<TSharedPtr<FJsonValue>> CompsArray;
	if (Blueprint->SimpleConstructionScript)
	{
		TArray<USCS_Node*> AllNodes = Blueprint->SimpleConstructionScript->GetAllNodes();
		for (USCS_Node* Node : AllNodes)
		{
			if (!Node) continue;
			TSharedPtr<FJsonObject> CompObj = MakeShared<FJsonObject>();
			CompObj->SetStringField(TEXT("name"), Node->GetVariableName().ToString());
			CompObj->SetStringField(TEXT("class"), Node->ComponentClass ? Node->ComponentClass->GetName() : TEXT("unknown"));

			// Parent node
			USCS_Node* ParentNode = Blueprint->SimpleConstructionScript->FindParentNode(Node);
			if (ParentNode)
				CompObj->SetStringField(TEXT("parent"), ParentNode->GetVariableName().ToString());
			else
				CompObj->SetStringField(TEXT("parent"), TEXT("root"));

			CompsArray.Add(MakeShared<FJsonValueObject>(CompObj));
		}
	}
	Root->SetArrayField(TEXT("components"), CompsArray);

	// Graphs
	TArray<TSharedPtr<FJsonValue>> GraphsArray;

	auto AddGraphEntry = [&](UEdGraph* Graph, const FString& GraphType)
	{
		if (!Graph) return;
		TSharedPtr<FJsonObject> GObj = MakeShared<FJsonObject>();
		GObj->SetStringField(TEXT("name"), Graph->GetName());
		GObj->SetStringField(TEXT("type"), GraphType);
		GObj->SetNumberField(TEXT("node_count"), Graph->Nodes.Num());

		// Enumerate nodes with class, title, position, and connections
		TArray<TSharedPtr<FJsonValue>> NodesArray;
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (!Node) continue;
			TSharedPtr<FJsonObject> NodeObj = MakeShared<FJsonObject>();
			NodeObj->SetStringField(TEXT("name"), Node->GetName());
			NodeObj->SetStringField(TEXT("class"), Node->GetClass()->GetName());
			NodeObj->SetStringField(TEXT("title"), Node->GetNodeTitle(ENodeTitleType::FullTitle).ToString());
			NodeObj->SetNumberField(TEXT("pos_x"), Node->NodePosX);
			NodeObj->SetNumberField(TEXT("pos_y"), Node->NodePosY);

			if (!Node->NodeComment.IsEmpty())
				NodeObj->SetStringField(TEXT("comment"), Node->NodeComment);

			// For K2Node_CallFunction, include the function reference
			if (Node->GetClass()->GetName().Contains(TEXT("CallFunction")))
			{
				FString FuncRef;
				if (FProperty* Prop = Node->GetClass()->FindPropertyByName(TEXT("FunctionReference")))
				{
					FString ExportedText;
					Prop->ExportTextItem_Direct(ExportedText, Prop->ContainerPtrToValuePtr<void>(Node), nullptr, Node, PPF_None);
					if (!ExportedText.IsEmpty())
						NodeObj->SetStringField(TEXT("function_ref"), ExportedText);
				}
			}

			// Enumerate pins with connections
			TArray<TSharedPtr<FJsonValue>> PinsArray;
			for (UEdGraphPin* Pin : Node->Pins)
			{
				if (!Pin) continue;
				TSharedPtr<FJsonObject> PinObj = MakeShared<FJsonObject>();
				PinObj->SetStringField(TEXT("name"), Pin->PinName.ToString());
				PinObj->SetStringField(TEXT("direction"), Pin->Direction == EGPD_Input ? TEXT("input") : TEXT("output"));
				PinObj->SetStringField(TEXT("type"), Pin->PinType.PinCategory.ToString());
				if (!Pin->DefaultValue.IsEmpty())
					PinObj->SetStringField(TEXT("default"), Pin->DefaultValue);

				// Connected pins
				if (Pin->LinkedTo.Num() > 0)
				{
					TArray<TSharedPtr<FJsonValue>> LinksArray;
					for (UEdGraphPin* LinkedPin : Pin->LinkedTo)
					{
						if (!LinkedPin || !LinkedPin->GetOwningNode()) continue;
						TSharedPtr<FJsonObject> LinkObj = MakeShared<FJsonObject>();
						LinkObj->SetStringField(TEXT("node"), LinkedPin->GetOwningNode()->GetName());
						LinkObj->SetStringField(TEXT("pin"), LinkedPin->PinName.ToString());
						LinksArray.Add(MakeShared<FJsonValueObject>(LinkObj));
					}
					PinObj->SetArrayField(TEXT("linked_to"), LinksArray);
				}

				PinsArray.Add(MakeShared<FJsonValueObject>(PinObj));
			}
			NodeObj->SetArrayField(TEXT("pins"), PinsArray);

			NodesArray.Add(MakeShared<FJsonValueObject>(NodeObj));
		}
		GObj->SetArrayField(TEXT("nodes"), NodesArray);

		GraphsArray.Add(MakeShared<FJsonValueObject>(GObj));
	};

	for (UEdGraph* G : Blueprint->UbergraphPages)   AddGraphEntry(G, TEXT("event_graph"));
	for (UEdGraph* G : Blueprint->FunctionGraphs)   AddGraphEntry(G, TEXT("function"));
	for (UEdGraph* G : Blueprint->MacroGraphs)      AddGraphEntry(G, TEXT("macro"));

	Root->SetArrayField(TEXT("graphs"), GraphsArray);

	FString OutputStr;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutputStr);
	FJsonSerializer::Serialize(Root.ToSharedRef(), Writer);
	return OutputStr;
}

// ============================================================================
// Helper: ResolveEventMapping
// ============================================================================

bool FAutonomixBlueprintActions::ResolveEventMapping(const FString& EventName, FName& OutFunctionName, UClass*& OutOwnerClass)
{
	// Actor events
	if (EventName == TEXT("BeginPlay"))           { OutFunctionName = FName(TEXT("ReceiveBeginPlay"));             OutOwnerClass = AActor::StaticClass();      return true; }
	if (EventName == TEXT("EndPlay"))             { OutFunctionName = FName(TEXT("ReceiveEndPlay"));               OutOwnerClass = AActor::StaticClass();      return true; }
	if (EventName == TEXT("Tick"))                { OutFunctionName = FName(TEXT("ReceiveTick"));                  OutOwnerClass = AActor::StaticClass();      return true; }
	if (EventName == TEXT("ActorBeginOverlap"))   { OutFunctionName = FName(TEXT("ReceiveActorBeginOverlap"));     OutOwnerClass = AActor::StaticClass();      return true; }
	if (EventName == TEXT("ActorEndOverlap"))     { OutFunctionName = FName(TEXT("ReceiveActorEndOverlap"));       OutOwnerClass = AActor::StaticClass();      return true; }
	if (EventName == TEXT("Hit"))                 { OutFunctionName = FName(TEXT("ReceiveHit"));                   OutOwnerClass = AActor::StaticClass();      return true; }
	if (EventName == TEXT("AnyDamage"))           { OutFunctionName = FName(TEXT("ReceiveAnyDamage"));             OutOwnerClass = AActor::StaticClass();      return true; }
	if (EventName == TEXT("PointDamage"))         { OutFunctionName = FName(TEXT("ReceivePointDamage"));           OutOwnerClass = AActor::StaticClass();      return true; }
	if (EventName == TEXT("RadialDamage"))        { OutFunctionName = FName(TEXT("ReceiveRadialDamage"));          OutOwnerClass = AActor::StaticClass();      return true; }
	if (EventName == TEXT("Destroyed"))           { OutFunctionName = FName(TEXT("ReceiveDestroyed"));             OutOwnerClass = AActor::StaticClass();      return true; }
	if (EventName == TEXT("ActorBeginCursorOver")){ OutFunctionName = FName(TEXT("ReceiveActorBeginCursorOver"));  OutOwnerClass = AActor::StaticClass();      return true; }
	if (EventName == TEXT("ActorEndCursorOver"))  { OutFunctionName = FName(TEXT("ReceiveActorEndCursorOver"));   OutOwnerClass = AActor::StaticClass();      return true; }

	// Pawn events
	if (EventName == TEXT("PossessedBy"))         { OutFunctionName = FName(TEXT("ReceivePossessed"));             OutOwnerClass = APawn::StaticClass();       return true; }
	if (EventName == TEXT("UnPossessed"))         { OutFunctionName = FName(TEXT("ReceiveUnpossessed"));           OutOwnerClass = APawn::StaticClass();       return true; }
	// SetupPlayerInputComponent — correct UE internal name is ReceiveSetUpPlayerInputComponent (capital U on "Up")
	if (EventName == TEXT("SetupPlayerInputComponent")) { OutFunctionName = FName(TEXT("ReceiveSetUpPlayerInputComponent")); OutOwnerClass = APawn::StaticClass(); return true; }

	// Character events
	if (EventName == TEXT("Landed"))              { OutFunctionName = FName(TEXT("OnLanded"));                     OutOwnerClass = ACharacter::StaticClass();  return true; }

	return false;
}

// ============================================================================
// ExecuteCreateBlueprint
// ============================================================================

FAutonomixActionResult FAutonomixBlueprintActions::ExecuteCreateBlueprint(const TSharedRef<FJsonObject>& Params, FAutonomixActionResult& Result)
{
	FString AssetPath      = Params->GetStringField(TEXT("asset_path"));
	FString ParentClassName = Params->GetStringField(TEXT("parent_class"));

	// Resolve parent class — try aliases first, then full reflection search
	UClass* ParentClass = nullptr;
	if (ParentClassName == TEXT("Actor"))                ParentClass = AActor::StaticClass();
	else if (ParentClassName == TEXT("Pawn"))            ParentClass = APawn::StaticClass();
	else if (ParentClassName == TEXT("Character"))       ParentClass = ACharacter::StaticClass();
	else if (ParentClassName == TEXT("PlayerController"))ParentClass = APlayerController::StaticClass();
	else if (ParentClassName == TEXT("GameModeBase"))    ParentClass = AGameModeBase::StaticClass();
	else
	{
		ParentClass = FindObject<UClass>(nullptr, *ParentClassName);
		if (!ParentClass)
			ParentClass = FindFirstObject<UClass>(*ParentClassName, EFindFirstObjectOptions::None);
		if (!ParentClass)
			ParentClass = FindFirstObject<UClass>(*(FString(TEXT("/Script/Engine.")) + ParentClassName), EFindFirstObjectOptions::None);
	}

	if (!ParentClass)
	{
		Result.Errors.Add(FString::Printf(TEXT("Could not find parent class: '%s'. Try 'Actor', 'Pawn', 'Character', 'PlayerController', or 'GameModeBase'."), *ParentClassName));
		return Result;
	}

	FString PackagePath = FPackageName::GetLongPackagePath(AssetPath);
	FString AssetName   = FPackageName::GetShortName(AssetPath);

	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
	FString UniqueName, UniquePackagePath;
	AssetTools.CreateUniqueAssetName(AssetPath, TEXT(""), UniquePackagePath, UniqueName);

	UBlueprintFactory* Factory = NewObject<UBlueprintFactory>();
	Factory->ParentClass = ParentClass;

	UObject* NewAsset = AssetTools.CreateAsset(AssetName, PackagePath, UBlueprint::StaticClass(), Factory);
	UBlueprint* NewBlueprint = Cast<UBlueprint>(NewAsset);

	if (!NewBlueprint)
	{
		Result.Errors.Add(FString::Printf(TEXT("Failed to create Blueprint at '%s'. Check that the path is valid and under /Game/."), *AssetPath));
		return Result;
	}

	UE_LOG(LogAutonomix, Log, TEXT("BlueprintActions: Created Blueprint '%s' with parent '%s'"), *AssetPath, *ParentClassName);

	// --- Inline Components ---
	const TArray<TSharedPtr<FJsonValue>>* ComponentsArray = nullptr;
	if (Params->TryGetArrayField(TEXT("components"), ComponentsArray))
	{
		for (const TSharedPtr<FJsonValue>& CompValue : *ComponentsArray)
		{
			const TSharedPtr<FJsonObject> CompObj = CompValue->AsObject();
			if (!CompObj.IsValid()) continue;

			FString CompName      = CompObj->GetStringField(TEXT("name"));
			FString CompClassName = CompObj->GetStringField(TEXT("class"));

			UClass* CompClass = FindFirstObject<UClass>(*CompClassName, EFindFirstObjectOptions::None);
			if (!CompClass)
				CompClass = FindFirstObject<UClass>(*(TEXT("U") + CompClassName), EFindFirstObjectOptions::None);
			if (!CompClass)
				CompClass = FindFirstObject<UClass>(*(FString(TEXT("/Script/Engine.")) + CompClassName), EFindFirstObjectOptions::None);

			if (CompClass && NewBlueprint->SimpleConstructionScript)
			{
				NewBlueprint->Modify();
				USCS_Node* NewNode = NewBlueprint->SimpleConstructionScript->CreateNode(CompClass, *CompName);
				if (NewNode)
				{
					FString AttachTo;
					if (CompObj->TryGetStringField(TEXT("attach_to"), AttachTo) && !AttachTo.IsEmpty())
					{
						USCS_Node* ParentNode = FindSCSNodeByName(NewBlueprint, AttachTo);
						if (ParentNode)
							ParentNode->AddChildNode(NewNode);
						else
							NewBlueprint->SimpleConstructionScript->AddNode(NewNode);
					}
					else
					{
						NewBlueprint->SimpleConstructionScript->AddNode(NewNode);
					}
					UE_LOG(LogAutonomix, Log, TEXT("BlueprintActions: Added inline component '%s' (%s)"), *CompName, *CompClassName);
				}
			}
			else
			{
				Result.Warnings.Add(FString::Printf(TEXT("Component class not found: '%s'. Check the class name spelling."), *CompClassName));
			}
		}
	}

	// --- Inline Variables ---
	const TArray<TSharedPtr<FJsonValue>>* VariablesArray = nullptr;
	if (Params->TryGetArrayField(TEXT("variables"), VariablesArray))
	{
		for (const TSharedPtr<FJsonValue>& VarValue : *VariablesArray)
		{
			const TSharedPtr<FJsonObject> VarObj = VarValue->AsObject();
			if (!VarObj.IsValid()) continue;

			FString VarName = VarObj->GetStringField(TEXT("name"));
			FString VarType = VarObj->GetStringField(TEXT("type"));

			FEdGraphPinType PinType;
			ResolvePinType(VarType, PinType);

			NewBlueprint->Modify();
			bool bSuccess = FBlueprintEditorUtils::AddMemberVariable(NewBlueprint, FName(*VarName), PinType);
			if (!bSuccess)
				Result.Warnings.Add(FString::Printf(TEXT("Failed to add inline variable: '%s'"), *VarName));
		}
	}

	// Compile and report
	bool bCompileOk = CompileAndReport(NewBlueprint, Result, true);

	// Infinite loop guard
	TArray<FString> LoopWarnings;
	DetectInfiniteLoopRisk(NewBlueprint, LoopWarnings);
	Result.Warnings.Append(LoopWarnings);

	if (!bCompileOk)
	{
		Result.bSuccess = false;
		Result.Errors.Add(FString::Printf(
			TEXT("Blueprint '%s' created but FAILED to compile. Fix the COMPILE ERROR messages above, then call compile_blueprint to verify."),
			*AssetPath));
	}

	// Save
	UPackage* Package = NewBlueprint->GetOutermost();
	Package->MarkPackageDirty();
	FString PackageFilename;
	if (FPackageName::TryConvertLongPackageNameToFilename(Package->GetName(), PackageFilename, FPackageName::GetAssetPackageExtension()))
	{
		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Standalone;
		UPackage::SavePackage(Package, NewBlueprint, *PackageFilename, SaveArgs);
	}

	FAssetRegistryModule::AssetCreated(NewBlueprint);

	Result.bSuccess = bCompileOk;
	Result.ResultMessage = FString::Printf(TEXT("Created Blueprint '%s' (parent: %s). Compile: %s."),
		*AssetName, *ParentClassName, bCompileOk ? TEXT("SUCCESS") : TEXT("FAILED"));
	Result.ModifiedAssets.Add(AssetPath);
	return Result;
}

// ============================================================================
// ExecuteAddComponent
// ============================================================================

FAutonomixActionResult FAutonomixBlueprintActions::ExecuteAddComponent(const TSharedRef<FJsonObject>& Params, FAutonomixActionResult& Result)
{
	FString AssetPath    = Params->GetStringField(TEXT("asset_path"));
	FString CompClassName = Params->GetStringField(TEXT("component_class"));
	FString CompName     = Params->GetStringField(TEXT("component_name"));

	UBlueprint* Blueprint = LoadObject<UBlueprint>(nullptr, *AssetPath);
	if (!Blueprint)
	{
		Result.Errors.Add(FString::Printf(TEXT("Blueprint not found: '%s'"), *AssetPath));
		return Result;
	}

	UClass* CompClass = FindFirstObject<UClass>(*CompClassName, EFindFirstObjectOptions::None);
	if (!CompClass)
		CompClass = FindFirstObject<UClass>(*(TEXT("U") + CompClassName), EFindFirstObjectOptions::None);

	if (!CompClass)
	{
		Result.Errors.Add(FString::Printf(TEXT("Component class not found: '%s'. Include the 'U' prefix or use the exact class name like 'StaticMeshComponent'."), *CompClassName));
		return Result;
	}

	if (!Blueprint->SimpleConstructionScript)
	{
		Result.Errors.Add(TEXT("Blueprint has no SimpleConstructionScript. Actors, Pawns, and Characters all have SCS; Interfaces and Function Libraries do not."));
		return Result;
	}

	Blueprint->Modify();
	USCS_Node* NewNode = Blueprint->SimpleConstructionScript->CreateNode(CompClass, *CompName);
	if (!NewNode)
	{
		Result.Errors.Add(FString::Printf(TEXT("Failed to create SCS node for component '%s'."), *CompName));
		return Result;
	}

	// Attach-to support (full implementation)
	FString AttachTo;
	if (Params->TryGetStringField(TEXT("attach_to"), AttachTo) && !AttachTo.IsEmpty())
	{
		USCS_Node* ParentNode = FindSCSNodeByName(Blueprint, AttachTo);
		if (ParentNode)
			ParentNode->AddChildNode(NewNode);
		else
		{
			Result.Warnings.Add(FString::Printf(TEXT("Parent component '%s' not found in SCS; attaching to root instead."), *AttachTo));
			Blueprint->SimpleConstructionScript->AddNode(NewNode);
		}
	}
	else
	{
		Blueprint->SimpleConstructionScript->AddNode(NewNode);
	}

	bool bCompileOk = CompileAndReport(Blueprint, Result, true);
	Blueprint->GetOutermost()->MarkPackageDirty();

	Result.bSuccess = bCompileOk;
	Result.ResultMessage = FString::Printf(TEXT("Added %s component '%s' to '%s'. Compile: %s."),
		*CompClassName, *CompName, *AssetPath, bCompileOk ? TEXT("SUCCESS") : TEXT("FAILED"));
	Result.ModifiedAssets.Add(AssetPath);
	return Result;
}

// ============================================================================
// ExecuteAddVariable
// ============================================================================

FAutonomixActionResult FAutonomixBlueprintActions::ExecuteAddVariable(const TSharedRef<FJsonObject>& Params, FAutonomixActionResult& Result)
{
	FString AssetPath = Params->GetStringField(TEXT("asset_path"));
	FString VarName   = Params->GetStringField(TEXT("variable_name"));
	FString VarType   = Params->GetStringField(TEXT("variable_type"));

	UBlueprint* Blueprint = LoadObject<UBlueprint>(nullptr, *AssetPath);
	if (!Blueprint)
	{
		Result.Errors.Add(FString::Printf(TEXT("Blueprint not found: '%s'"), *AssetPath));
		return Result;
	}

	FEdGraphPinType PinType;
	ResolvePinType(VarType, PinType);

	Blueprint->Modify();
	bool bAdded = FBlueprintEditorUtils::AddMemberVariable(Blueprint, FName(*VarName), PinType);
	if (!bAdded)
	{
		Result.Errors.Add(FString::Printf(TEXT("Failed to add variable '%s' — it may already exist or the type is invalid."), *VarName));
		return Result;
	}

	// Optional: expose as editable / category
	FString Category;
	if (Params->TryGetStringField(TEXT("category"), Category) && !Category.IsEmpty())
		FBlueprintEditorUtils::SetBlueprintVariableCategory(Blueprint, FName(*VarName), nullptr, FText::FromString(Category));

	bool bEditable = true;
	Params->TryGetBoolField(TEXT("editable"), bEditable);
	FBlueprintEditorUtils::SetBlueprintOnlyEditableFlag(Blueprint, FName(*VarName), !bEditable);

	bool bCompileOk = CompileAndReport(Blueprint, Result, true);
	Blueprint->GetOutermost()->MarkPackageDirty();

	Result.bSuccess = bCompileOk;
	Result.ResultMessage = FString::Printf(TEXT("Added variable '%s' (%s) to '%s'. Compile: %s."),
		*VarName, *VarType, *AssetPath, bCompileOk ? TEXT("SUCCESS") : TEXT("FAILED"));
	Result.ModifiedAssets.Add(AssetPath);
	return Result;
}

// ============================================================================
// ExecuteAddFunction
// ============================================================================

FAutonomixActionResult FAutonomixBlueprintActions::ExecuteAddFunction(const TSharedRef<FJsonObject>& Params, FAutonomixActionResult& Result)
{
	FString AssetPath    = Params->GetStringField(TEXT("asset_path"));
	FString FunctionName = Params->GetStringField(TEXT("function_name"));

	UBlueprint* Blueprint = LoadObject<UBlueprint>(nullptr, *AssetPath);
	if (!Blueprint)
	{
		Result.Errors.Add(FString::Printf(TEXT("Blueprint not found: '%s'"), *AssetPath));
		return Result;
	}

	// Check if function already exists
	UEdGraph* ExistingGraph = nullptr;
	for (UEdGraph* G : Blueprint->FunctionGraphs)
	{
		if (G && G->GetName() == FunctionName)
		{
			ExistingGraph = G;
			break;
		}
	}

	if (ExistingGraph)
	{
		Result.Warnings.Add(FString::Printf(TEXT("Function '%s' already exists in '%s'. Use inject_blueprint_nodes_t3d to add nodes to it."), *FunctionName, *AssetPath));
		Result.bSuccess = true;
		Result.ResultMessage = FString::Printf(TEXT("Function '%s' already exists — skipping creation."), *FunctionName);
		return Result;
	}

	Blueprint->Modify();
	UEdGraph* NewFunctionGraph = FBlueprintEditorUtils::CreateNewGraph(
		Blueprint,
		FName(*FunctionName),
		UEdGraph::StaticClass(),
		UEdGraphSchema_K2::StaticClass());

	FBlueprintEditorUtils::AddFunctionGraph(Blueprint, NewFunctionGraph, /*bIsUserCreated=*/true, (UClass*)nullptr);

	// Add typed inputs if provided
	const TArray<TSharedPtr<FJsonValue>>* InputsArray = nullptr;
	if (Params->TryGetArrayField(TEXT("inputs"), InputsArray))
	{
		// Find the function entry node
		UK2Node_FunctionEntry* EntryNode = nullptr;
		for (UEdGraphNode* Node : NewFunctionGraph->Nodes)
		{
			EntryNode = Cast<UK2Node_FunctionEntry>(Node);
			if (EntryNode) break;
		}

		if (EntryNode)
		{
			for (const TSharedPtr<FJsonValue>& InputVal : *InputsArray)
			{
				const TSharedPtr<FJsonObject> InputObj = InputVal->AsObject();
				if (!InputObj.IsValid()) continue;

				FString ParamName = InputObj->GetStringField(TEXT("name"));
				FString ParamType = InputObj->GetStringField(TEXT("type"));

				FEdGraphPinType PinType;
				ResolvePinType(ParamType, PinType);

				EntryNode->Modify();
				TSharedPtr<FUserPinInfo> PinInfo = MakeShared<FUserPinInfo>();
				PinInfo->PinName = FName(*ParamName);
				PinInfo->PinType = PinType;
				EntryNode->UserDefinedPins.Add(PinInfo);
			}
			EntryNode->ReconstructNode();
		}
	}

	// Add typed outputs (return values) if provided
	const TArray<TSharedPtr<FJsonValue>>* OutputsArray = nullptr;
	if (Params->TryGetArrayField(TEXT("outputs"), OutputsArray))
	{
		UK2Node_FunctionResult* ResultNode = nullptr;
		for (UEdGraphNode* Node : NewFunctionGraph->Nodes)
		{
			ResultNode = Cast<UK2Node_FunctionResult>(Node);
			if (ResultNode) break;
		}

		if (ResultNode)
		{
			for (const TSharedPtr<FJsonValue>& OutputVal : *OutputsArray)
			{
				const TSharedPtr<FJsonObject> OutputObj = OutputVal->AsObject();
				if (!OutputObj.IsValid()) continue;

				FString RetName = OutputObj->GetStringField(TEXT("name"));
				FString RetType = OutputObj->GetStringField(TEXT("type"));

				FEdGraphPinType PinType;
				ResolvePinType(RetType, PinType);

				ResultNode->Modify();
				TSharedPtr<FUserPinInfo> PinInfo = MakeShared<FUserPinInfo>();
				PinInfo->PinName = FName(*RetName);
				PinInfo->PinType = PinType;
				ResultNode->UserDefinedPins.Add(PinInfo);
			}
			ResultNode->ReconstructNode();
		}
	}

	bool bCompileOk = CompileAndReport(Blueprint, Result, true);
	Blueprint->GetOutermost()->MarkPackageDirty();

	Result.bSuccess = bCompileOk;
	Result.ResultMessage = FString::Printf(
		TEXT("Added function '%s' to '%s'.\n")
		TEXT("NESTING GUIDANCE: Build this function's graph bottom-up:\n")
		TEXT("  1. Inject leaf nodes first (the deepest utility calls, variable gets/sets)\n")
		TEXT("  2. Inject intermediate nodes that call those leaves\n")
		TEXT("  3. Wire execution chain: FunctionEntry (then exec) -> all intermediary nodes -> FunctionResult (execute)\n")
		TEXT("  4. Wire data pins from producer nodes to consumer nodes\n")
		TEXT("  5. Call verify_blueprint_connections after injecting to catch any missing wires\n")
		TEXT("Use inject_blueprint_nodes_t3d with graph_name='%s' to add logic.\n")
		TEXT("Compile: %s."),
		*FunctionName, *AssetPath, *FunctionName, bCompileOk ? TEXT("SUCCESS") : TEXT("FAILED"));
	Result.ModifiedAssets.Add(AssetPath);
	return Result;
}

// ============================================================================
// ExecuteAddEventHandler
// ============================================================================

FAutonomixActionResult FAutonomixBlueprintActions::ExecuteAddEventHandler(const TSharedRef<FJsonObject>& Params, FAutonomixActionResult& Result)
{
	FString AssetPath  = Params->GetStringField(TEXT("asset_path"));
	FString EventName  = Params->GetStringField(TEXT("event_name"));

	UBlueprint* Blueprint = LoadObject<UBlueprint>(nullptr, *AssetPath);
	if (!Blueprint)
	{
		Result.Errors.Add(FString::Printf(TEXT("Blueprint not found: '%s'"), *AssetPath));
		return Result;
	}

	FName FunctionName;
	UClass* OwnerClass = nullptr;
	if (!ResolveEventMapping(EventName, FunctionName, OwnerClass))
	{
		Result.Errors.Add(FString::Printf(
			TEXT("Unknown event name '%s'. Supported Actor events: BeginPlay, EndPlay, Tick, ActorBeginOverlap, ActorEndOverlap, Hit, AnyDamage, PointDamage, RadialDamage, Destroyed, ActorBeginCursorOver, ActorEndCursorOver. "
			     "Pawn events: PossessedBy, UnPossessed, SetupPlayerInputComponent. "
			     "Character events: Landed."),
			*EventName));
		return Result;
	}

	// Find the EventGraph
	UEdGraph* EventGraph = FindOrCreateEventGraph(Blueprint, TEXT("EventGraph"));
	if (!EventGraph)
	{
		Result.Errors.Add(TEXT("Could not find or create EventGraph."));
		return Result;
	}

	// Check if the event node already exists to avoid duplicates
	for (UEdGraphNode* Node : EventGraph->Nodes)
	{
		UK2Node_Event* ExistingEvent = Cast<UK2Node_Event>(Node);
		if (ExistingEvent && ExistingEvent->GetFunctionName() == FunctionName)
		{
			int32 PosX = ExistingEvent->NodePosX;
			int32 PosY = ExistingEvent->NodePosY;
			Result.bSuccess = true;
			Result.ResultMessage = FString::Printf(
				TEXT("Event '%s' already exists at position (%d, %d). Wire new logic starting from NodePosX=%d, NodePosY=%d."),
				*EventName, PosX, PosY, PosX + 300, PosY);
			return Result;
		}
	}

	// Find the desired Y position
	int32 NodePosX = 0;
	int32 NodePosY = 0;
	Params->TryGetNumberField(TEXT("node_pos_x"), NodePosX);
	Params->TryGetNumberField(TEXT("node_pos_y"), NodePosY);

	// Auto Y: place below existing nodes
	if (NodePosY == 0 && EventGraph->Nodes.Num() > 0)
	{
		for (const UEdGraphNode* ExistingNode : EventGraph->Nodes)
		{
			int32 Bottom = ExistingNode->NodePosY + 100;
			if (Bottom > NodePosY) NodePosY = Bottom;
		}
	}

	// Create the event node via FGraphNodeCreator
	FGraphNodeCreator<UK2Node_Event> EventCreator(*EventGraph);
	UK2Node_Event* NewEventNode = EventCreator.CreateNode();
	NewEventNode->EventReference.SetExternalMember(FunctionName, OwnerClass);
	NewEventNode->bOverrideFunction = true;
	NewEventNode->NodePosX = NodePosX;
	NewEventNode->NodePosY = NodePosY;
	EventCreator.Finalize();

	Blueprint->Modify();
	bool bCompileOk = CompileAndReport(Blueprint, Result, true);
	Blueprint->GetOutermost()->MarkPackageDirty();

	// Return the internal node name so the AI can reference it in connect_blueprint_pins
	// or in a subsequent T3D block. The node name is of the form "K2Node_Event_N".
	FString NewNodeName = NewEventNode->GetName();

	Result.bSuccess = bCompileOk;
	Result.ResultMessage = FString::Printf(
		TEXT("Added '%s' event node.\n")
		TEXT("Internal node name: \"%s\"\n")
		TEXT("Position: (%d, %d)\n")
		TEXT("Execution output pin: \"then\"\n")
		TEXT("To wire logic after this event: use connect_blueprint_pins with source_node=\"%s\", source_pin=\"then\".\n")
		TEXT("Or inject a T3D block that references this node by name.\n")
		TEXT("Compile: %s."),
		*EventName,
		*NewNodeName,
		NodePosX, NodePosY,
		*NewNodeName,
		bCompileOk ? TEXT("SUCCESS") : TEXT("FAILED"));
	Result.ModifiedAssets.Add(AssetPath);
	return Result;
}

// ============================================================================
// ExecuteCompileBlueprint
// ============================================================================

FAutonomixActionResult FAutonomixBlueprintActions::ExecuteCompileBlueprint(const TSharedRef<FJsonObject>& Params, FAutonomixActionResult& Result)
{
	FString AssetPath = Params->GetStringField(TEXT("asset_path"));

	UBlueprint* Blueprint = LoadObject<UBlueprint>(nullptr, *AssetPath);
	if (!Blueprint)
	{
		Result.Errors.Add(FString::Printf(TEXT("Blueprint not found: '%s'"), *AssetPath));
		return Result;
	}

	TArray<FString> LoopWarnings;
	DetectInfiniteLoopRisk(Blueprint, LoopWarnings);
	Result.Warnings.Append(LoopWarnings);

	bool bOk = CompileAndReport(Blueprint, Result, false);
	Result.bSuccess = bOk;
	Result.ResultMessage = bOk
		? FString::Printf(TEXT("Blueprint '%s' compiled successfully."), *AssetPath)
		: FString::Printf(TEXT("Blueprint '%s' compiled with ERRORS. Fix the issues listed above."), *AssetPath);
	Result.ModifiedAssets.Add(AssetPath);
	return Result;
}

// ============================================================================
// ExecuteSetDefaults
// ============================================================================

FAutonomixActionResult FAutonomixBlueprintActions::ExecuteSetDefaults(const TSharedRef<FJsonObject>& Params, FAutonomixActionResult& Result)
{
	FString AssetPath = Params->GetStringField(TEXT("asset_path"));

	UBlueprint* Blueprint = LoadObject<UBlueprint>(nullptr, *AssetPath);
	if (!Blueprint)
	{
		Result.Errors.Add(FString::Printf(TEXT("Blueprint not found: '%s'"), *AssetPath));
		return Result;
	}

	// Ensure compiled
	if (Blueprint->Status == BS_Dirty || Blueprint->Status == BS_Unknown)
		FKismetEditorUtilities::CompileBlueprint(Blueprint, EBlueprintCompileOptions::SkipGarbageCollection);

	if (!Blueprint->GeneratedClass)
	{
		Result.Errors.Add(TEXT("Blueprint has no GeneratedClass — it must compile successfully before setting defaults."));
		return Result;
	}

	UObject* CDO = Blueprint->GeneratedClass->GetDefaultObject();
	if (!CDO)
	{
		Result.Errors.Add(TEXT("Could not get Class Default Object."));
		return Result;
	}

	CDO->Modify();

	const TSharedPtr<FJsonObject>* DefaultsObj = nullptr;
	if (!Params->TryGetObjectField(TEXT("defaults"), DefaultsObj) || !DefaultsObj)
	{
		Result.Errors.Add(TEXT("Missing 'defaults' object field."));
		return Result;
	}

	for (const auto& Pair : (*DefaultsObj)->Values)
	{
		// Support "Component.Property" notation
		FString FullKey  = Pair.Key;
		FString CompName = TEXT("");
		FString PropName = FullKey;

		if (FullKey.Contains(TEXT(".")))
		{
			FullKey.Split(TEXT("."), &CompName, &PropName);
		}

		UObject* TargetObject = CDO;

		// If component name is specified, find the component sub-object
		if (!CompName.IsEmpty())
		{
			FObjectPropertyBase* CompProp = FindFProperty<FObjectPropertyBase>(Blueprint->GeneratedClass, *CompName);
			if (CompProp)
			{
				UObject* CompObject = CompProp->GetObjectPropertyValue_InContainer(CDO);
				if (CompObject) TargetObject = CompObject;
			}
			else
			{
				// Try finding via SCS component templates
				if (Blueprint->SimpleConstructionScript)
				{
					USCS_Node* SCSNode = FindSCSNodeByName(Blueprint, CompName);
					if (SCSNode && SCSNode->ComponentTemplate)
					{
						TargetObject = SCSNode->ComponentTemplate;
					}
				}
			}
		}

		if (!TargetObject)
		{
			Result.Warnings.Add(FString::Printf(TEXT("Component or object '%s' not found on CDO."), *CompName));
			continue;
		}

		FProperty* Prop = TargetObject->GetClass()->FindPropertyByName(FName(*PropName));
		if (!Prop)
		{
			Result.Warnings.Add(FString::Printf(TEXT("Property '%s' not found on '%s'. Property names are case-sensitive C++ names."), *PropName, *TargetObject->GetClass()->GetName()));
			continue;
		}

		FString ValueStr;
		if (Pair.Value->TryGetString(ValueStr))
		{
			TargetObject->Modify();
			void* PropAddr = Prop->ContainerPtrToValuePtr<void>(TargetObject);
			Prop->ImportText_Direct(*ValueStr, PropAddr, TargetObject, PPF_None);
			UE_LOG(LogAutonomix, Log, TEXT("BlueprintActions: Set default '%s' = '%s' on '%s'"), *FullKey, *ValueStr, *TargetObject->GetClass()->GetName());
		}
		else
		{
			Result.Warnings.Add(FString::Printf(TEXT("Property '%s' value is not a string."), *FullKey));
		}
	}

	Blueprint->GetOutermost()->MarkPackageDirty();
	Result.bSuccess = true;
	Result.ResultMessage = TEXT("Blueprint defaults updated successfully.");
	Result.ModifiedAssets.Add(AssetPath);
	return Result;
}

// ============================================================================
// ExecuteSetComponentProperties (NEW)
// ============================================================================

FAutonomixActionResult FAutonomixBlueprintActions::ExecuteSetComponentProperties(const TSharedRef<FJsonObject>& Params, FAutonomixActionResult& Result)
{
	FString AssetPath     = Params->GetStringField(TEXT("asset_path"));
	FString ComponentName = Params->GetStringField(TEXT("component_name"));

	UBlueprint* Blueprint = LoadObject<UBlueprint>(nullptr, *AssetPath);
	if (!Blueprint)
	{
		Result.Errors.Add(FString::Printf(TEXT("Blueprint not found: '%s'"), *AssetPath));
		return Result;
	}

	USCS_Node* SCSNode = FindSCSNodeByName(Blueprint, ComponentName);
	if (!SCSNode)
	{
		Result.Errors.Add(FString::Printf(
			TEXT("Component '%s' not found in SCS. Available components: use get_blueprint_info to list them."),
			*ComponentName));
		return Result;
	}

	UActorComponent* CompTemplate = SCSNode->ComponentTemplate;
	if (!CompTemplate)
	{
		Result.Errors.Add(FString::Printf(TEXT("Component '%s' has no template object."), *ComponentName));
		return Result;
	}

	CompTemplate->Modify();
	Blueprint->Modify();

	// --- Static Mesh ---
	FString StaticMeshPath;
	if (Params->TryGetStringField(TEXT("static_mesh"), StaticMeshPath) && !StaticMeshPath.IsEmpty())
	{
		UStaticMeshComponent* SMC = Cast<UStaticMeshComponent>(CompTemplate);
		if (!SMC)
		{
			Result.Warnings.Add(FString::Printf(TEXT("Component '%s' is not a StaticMeshComponent — cannot assign static_mesh."), *ComponentName));
		}
		else
		{
			UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, *StaticMeshPath);
			if (Mesh)
			{
				SMC->SetStaticMesh(Mesh);
				UE_LOG(LogAutonomix, Log, TEXT("BlueprintActions: Set StaticMesh '%s' on component '%s'"), *StaticMeshPath, *ComponentName);
			}
			else
			{
				Result.Warnings.Add(FString::Printf(TEXT("StaticMesh asset not found: '%s'. Make sure the asset exists in the project."), *StaticMeshPath));
			}
		}
	}

	// --- Skeletal Mesh ---
	FString SkeletalMeshPath;
	if (Params->TryGetStringField(TEXT("skeletal_mesh"), SkeletalMeshPath) && !SkeletalMeshPath.IsEmpty())
	{
		USkeletalMeshComponent* SKC = Cast<USkeletalMeshComponent>(CompTemplate);
		if (!SKC)
		{
			Result.Warnings.Add(FString::Printf(TEXT("Component '%s' is not a SkeletalMeshComponent — cannot assign skeletal_mesh."), *ComponentName));
		}
		else
		{
			USkeletalMesh* Mesh = LoadObject<USkeletalMesh>(nullptr, *SkeletalMeshPath);
			if (Mesh)
			{
				SKC->SetSkeletalMeshAsset(Mesh);
				UE_LOG(LogAutonomix, Log, TEXT("BlueprintActions: Set SkeletalMesh '%s' on component '%s'"), *SkeletalMeshPath, *ComponentName);
			}
			else
			{
				Result.Warnings.Add(FString::Printf(TEXT("SkeletalMesh asset not found: '%s'."), *SkeletalMeshPath));
			}
		}
	}

	// --- Transform: Relative Location ---
	const TSharedPtr<FJsonObject>* RelLocObj = nullptr;
	if (Params->TryGetObjectField(TEXT("relative_location"), RelLocObj))
	{
		USceneComponent* SC = Cast<USceneComponent>(CompTemplate);
		if (SC)
		{
			double X = 0, Y = 0, Z = 0;
			(*RelLocObj)->TryGetNumberField(TEXT("x"), X);
			(*RelLocObj)->TryGetNumberField(TEXT("y"), Y);
			(*RelLocObj)->TryGetNumberField(TEXT("z"), Z);
			SC->SetRelativeLocation(FVector(X, Y, Z));
		}
	}

	// --- Transform: Relative Rotation ---
	const TSharedPtr<FJsonObject>* RelRotObj = nullptr;
	if (Params->TryGetObjectField(TEXT("relative_rotation"), RelRotObj))
	{
		USceneComponent* SC = Cast<USceneComponent>(CompTemplate);
		if (SC)
		{
			double Pitch = 0, Yaw = 0, Roll = 0;
			(*RelRotObj)->TryGetNumberField(TEXT("pitch"), Pitch);
			(*RelRotObj)->TryGetNumberField(TEXT("yaw"), Yaw);
			(*RelRotObj)->TryGetNumberField(TEXT("roll"), Roll);
			SC->SetRelativeRotation(FRotator(Pitch, Yaw, Roll));
		}
	}

	// --- Transform: Relative Scale ---
	const TSharedPtr<FJsonObject>* RelScaleObj = nullptr;
	if (Params->TryGetObjectField(TEXT("relative_scale"), RelScaleObj))
	{
		USceneComponent* SC = Cast<USceneComponent>(CompTemplate);
		if (SC)
		{
			double X = 1, Y = 1, Z = 1;
			(*RelScaleObj)->TryGetNumberField(TEXT("x"), X);
			(*RelScaleObj)->TryGetNumberField(TEXT("y"), Y);
			(*RelScaleObj)->TryGetNumberField(TEXT("z"), Z);
			SC->SetRelativeScale3D(FVector(X, Y, Z));
		}
	}

	// --- Collision Profile ---
	FString CollisionProfile;
	if (Params->TryGetStringField(TEXT("collision_profile"), CollisionProfile) && !CollisionProfile.IsEmpty())
	{
		UPrimitiveComponent* PC = Cast<UPrimitiveComponent>(CompTemplate);
		if (PC)
			PC->SetCollisionProfileName(FName(*CollisionProfile));
		else
			Result.Warnings.Add(FString::Printf(TEXT("Component '%s' is not a PrimitiveComponent — cannot set collision profile."), *ComponentName));
	}

	// --- Generic reflection-based properties ---
	const TSharedPtr<FJsonObject>* PropsObj = nullptr;
	if (Params->TryGetObjectField(TEXT("properties"), PropsObj))
	{
		for (const auto& Pair : (*PropsObj)->Values)
		{
			FProperty* Prop = CompTemplate->GetClass()->FindPropertyByName(FName(*Pair.Key));
			if (!Prop)
			{
				Result.Warnings.Add(FString::Printf(TEXT("Property '%s' not found on component '%s'."), *Pair.Key, *ComponentName));
				continue;
			}
			FString ValStr;
			if (Pair.Value->TryGetString(ValStr))
			{
				void* PropAddr = Prop->ContainerPtrToValuePtr<void>(CompTemplate);
				Prop->ImportText_Direct(*ValStr, PropAddr, CompTemplate, PPF_None);
			}
		}
	}

	bool bCompileOk = CompileAndReport(Blueprint, Result, true);
	Blueprint->GetOutermost()->MarkPackageDirty();

	Result.bSuccess = bCompileOk;
	Result.ResultMessage = FString::Printf(TEXT("Updated component '%s' properties in '%s'. Compile: %s."),
		*ComponentName, *AssetPath, bCompileOk ? TEXT("SUCCESS") : TEXT("FAILED"));
	Result.ModifiedAssets.Add(AssetPath);
	return Result;
}

// ============================================================================
// ExecuteInjectNodesT3D (NEW — Primary Logic Graph Construction Method)
// ============================================================================

FAutonomixActionResult FAutonomixBlueprintActions::ExecuteInjectNodesT3D(const TSharedRef<FJsonObject>& Params, FAutonomixActionResult& Result)
{
	FString AssetPath = Params->GetStringField(TEXT("asset_path"));
	FString T3DText;
	if (!Params->TryGetStringField(TEXT("t3d_text"), T3DText) || T3DText.IsEmpty())
	{
		Result.Errors.Add(TEXT("Missing required field: 't3d_text'. Provide T3D-formatted node block(s) as a string."));
		return Result;
	}

	FString GraphName = TEXT("EventGraph");
	Params->TryGetStringField(TEXT("graph_name"), GraphName);

	UBlueprint* Blueprint = LoadObject<UBlueprint>(nullptr, *AssetPath);
	if (!Blueprint)
	{
		Result.Errors.Add(FString::Printf(TEXT("Blueprint not found: '%s'"), *AssetPath));
		return Result;
	}

	// Find or create the target graph
	UEdGraph* TargetGraph = FindOrCreateEventGraph(Blueprint, GraphName);
	if (!TargetGraph)
	{
		// Try FunctionGraphs
		for (UEdGraph* G : Blueprint->FunctionGraphs)
		{
			if (G && G->GetName() == GraphName)
			{
				TargetGraph = G;
				break;
			}
		}
	}

	if (!TargetGraph)
	{
		Result.Errors.Add(FString::Printf(
			TEXT("Graph '%s' not found in Blueprint '%s'. Use add_blueprint_function to create it first, or use 'EventGraph' for the main event graph."),
			*GraphName, *AssetPath));
		return Result;
	}

	// Resolve placeholder GUID tokens → real GUIDs
	FString ResolvedT3D = ResolveT3DPlaceholders(T3DText);

	UE_LOG(LogAutonomix, Log, TEXT("BlueprintActions: Injecting T3D nodes into graph '%s' of '%s'"), *GraphName, *AssetPath);

	// Import the nodes via FEdGraphUtilities (same as editor paste)
	TSet<UEdGraphNode*> ImportedNodes;
	const UEdGraphSchema* Schema = TargetGraph->GetSchema();

	// FEdGraphUtilities::ImportNodesFromText appends nodes from T3D text into the graph.
	// It uses the same serialization format as Ctrl+C in the Blueprint editor.
	FEdGraphUtilities::ImportNodesFromText(TargetGraph, ResolvedT3D, /*out*/ImportedNodes);

	if (ImportedNodes.Num() == 0)
	{
		// ImportNodesFromText produces a silent failure on malformed T3D
		Result.Errors.Add(FString::Printf(
			TEXT("T3D injection produced 0 nodes in graph '%s'. Possible causes: malformed T3D syntax, wrong Class= specifier, missing 'Begin Object'/'End Object' wrapper, or node Class not available. "
				 "Verify T3D format: each block must start with 'Begin Object Class=/Script/BlueprintGraph.K2Node_XXX Name=\"NodeName\"' and end with 'End Object'."),
			*GraphName));
		return Result;
	}

	Blueprint->Modify();

	// CRITICAL: Sanitize imported nodes before compilation.
	//
	// When AI-generated T3D has LinkedTo references to nodes that aren't present
	// in the graph (wrong node names, non-existent nodes, malformed cross-refs),
	// FEdGraphUtilities::ImportNodesFromText creates broken UEdGraphPin::LinkedTo
	// entries pointing to null or destroyed UEdGraphPin objects.
	//
	// If these are not cleaned up before CompileBlueprint, the internal call to
	// FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified → RefreshNodes
	// → ReconstructNode hits check(Pin) in EdGraphSchema_K2.cpp causing a crash.
	//
	// Fix: walk every pin on every imported node and remove null/stale LinkedTo refs.
	// Also remove links that point back to pins on nodes NOT in our graph (orphaned refs).
	{
		// Build a set of all node names currently in the graph for fast lookup
		TSet<FString> GraphNodeNames;
		for (UEdGraphNode* GNode : TargetGraph->Nodes)
		{
			if (GNode) GraphNodeNames.Add(GNode->GetName());
		}

		int32 TotalSanitisedLinks = 0;
		for (UEdGraphNode* Node : ImportedNodes)
		{
			if (!Node) continue;

			for (UEdGraphPin* Pin : Node->Pins)
			{
				if (!Pin) continue;

				// Remove any LinkedTo entry that:
				// (a) is a null pointer
				// (b) has a null owning node
				// (c) points to a node not present in the graph (orphaned T3D cross-ref)
				int32 RemovedCount = Pin->LinkedTo.RemoveAll([&GraphNodeNames](UEdGraphPin* LinkedPin) -> bool
				{
					if (!LinkedPin) return true;
					UEdGraphNode* OwningNode = LinkedPin->GetOwningNodeUnchecked();
					if (!OwningNode) return true;
					if (!GraphNodeNames.Contains(OwningNode->GetName())) return true;
					return false;
				});

				if (RemovedCount > 0)
				{
					TotalSanitisedLinks += RemovedCount;
					UE_LOG(LogAutonomix, Warning,
						TEXT("BlueprintActions: Removed %d broken LinkedTo ref(s) on pin '%s' of node '%s' (orphaned T3D cross-references)."),
						RemovedCount,
						*Pin->PinName.ToString(),
						*Node->GetName());
				}
			}
		}

		if (TotalSanitisedLinks > 0)
		{
			Result.Warnings.Add(FString::Printf(
				TEXT("SANITISER: Removed %d broken LinkedTo reference(s) from injected nodes. ")
				TEXT("These are T3D cross-references that point to nodes not present in the current graph injection batch. ")
				TEXT("Affected pins are now disconnected. Call verify_blueprint_connections to diagnose and repair missing wires."),
				TotalSanitisedLinks));
		}
	}

	// -----------------------------------------------------------------------
	// PIN AUDIT — delegate to shared BuildPinAuditReport helper
	// (object pins with no DefaultObject, zero numeric pins, all-zero struct pins)
	// -----------------------------------------------------------------------
	FString PinAuditReport = BuildPinAuditReport(ImportedNodes);

	bool bCompileOk = CompileAndReport(Blueprint, Result, true);
	Blueprint->GetOutermost()->MarkPackageDirty();

	// Report node positions so AI can place subsequent nodes without overlap
	FString NodePositions;
	for (UEdGraphNode* Node : ImportedNodes)
	{
		if (Node)
		{
			NodePositions += FString::Printf(TEXT("  %s @ (%d, %d)\n"), *Node->GetNodeTitle(ENodeTitleType::FullTitle).ToString(), Node->NodePosX, Node->NodePosY);
		}
	}

	// -----------------------------------------------------------------------
	// NODE READBACK — Export the actual post-injection T3D of every imported
	// node, identical to what UE Blueprint editor produces on Ctrl+C.
	// The AI receives the full pin state: DefaultValue, DefaultObject, LinkedTo.
	// This lets the AI self-audit values and connection correctness.
	// -----------------------------------------------------------------------
	FString NodeReadback;
	{
		TSet<UEdGraphNode*> NodeSet;
		for (UEdGraphNode* Node : ImportedNodes)
		{
			if (Node) NodeSet.Add(Node);
		}
		if (NodeSet.Num() > 0)
		{
			FString ExportedT3D;
			TSet<UObject*> ExportObjSet;
			for (UEdGraphNode* N : NodeSet) { if (N) ExportObjSet.Add(N); }
			FEdGraphUtilities::ExportNodesToText(ExportObjSet, ExportedT3D);
			NodeReadback  = TEXT("\n\n=== NODE READBACK (actual post-injection state) ===\n");
			NodeReadback += TEXT("The following is the real T3D export of every injected node as it\n");
			NodeReadback += TEXT("exists in the graph RIGHT NOW. Study every pin carefully:\n");
			NodeReadback += TEXT("  • DefaultValue=\"0.0\" on R/G/B float pins → color is BLACK — set explicit values\n");
			NodeReadback += TEXT("  • No DefaultObject on an object pin → asset is EMPTY — call search_assets first\n");
			NodeReadback += TEXT("  • LinkedTo=(NodeX PinY) → verify the connected node+pin types are compatible\n");
			NodeReadback += TEXT("  • exec pin with no LinkedTo → execution chain is BROKEN — connect it\n");
			NodeReadback += TEXT("DO NOT declare the task complete until every pin above is correct.\n\n");
			NodeReadback += ExportedT3D;
		}
	}

	Result.bSuccess = bCompileOk;
	Result.ResultMessage = FString::Printf(
		TEXT("Injected %d nodes into graph '%s' of '%s'.\nImported nodes:\n%sCompile: %s.%s%s"),
		ImportedNodes.Num(), *GraphName, *AssetPath, *NodePositions,
		bCompileOk ? TEXT("SUCCESS") : TEXT("FAILED"),
		*PinAuditReport,
		*NodeReadback);
	Result.ModifiedAssets.Add(AssetPath);
	return Result;
}

// ============================================================================
// ExecuteGetBlueprintInfo (NEW — Read-Only Query)
// ============================================================================

FAutonomixActionResult FAutonomixBlueprintActions::ExecuteGetBlueprintInfo(const TSharedRef<FJsonObject>& Params, FAutonomixActionResult& Result)
{
	FString AssetPath = Params->GetStringField(TEXT("asset_path"));

	UBlueprint* Blueprint = LoadObject<UBlueprint>(nullptr, *AssetPath);
	if (!Blueprint)
	{
		Result.Errors.Add(FString::Printf(TEXT("Blueprint not found: '%s'"), *AssetPath));
		return Result;
	}

	FString InfoJson = BuildBlueprintInfoJson(Blueprint);

	// -----------------------------------------------------------------------
	// FULL T3D READBACK — export every node from every graph so the AI can see
	// the exact pin connections, LinkedTo references, DefaultValues, and
	// DefaultObjects BEFORE it makes any modifications. This is the definitive
	// "current state" snapshot the AI must study before injecting new nodes.
	// -----------------------------------------------------------------------
	FString GraphReadbacks;
	{
		auto ExportGraph = [&](UEdGraph* Graph, const FString& GraphType)
		{
			if (!Graph) return;

			TSet<UEdGraphNode*> NodeSet;
				for (UEdGraphNode* Node : Graph->Nodes)
				{
					if (Node) NodeSet.Add(Node);
				}
				if (NodeSet.IsEmpty()) return;
	
				FString ExportedT3D;
				{
					TSet<UObject*> ExportObjSet;
					for (UEdGraphNode* N : NodeSet) { if (N) ExportObjSet.Add(N); }
					FEdGraphUtilities::ExportNodesToText(ExportObjSet, ExportedT3D);
				}
	
				GraphReadbacks += FString::Printf(
					TEXT("\n\n=== GRAPH T3D READBACK: \"%s\" (%s, %d nodes) ===\n"),
					*Graph->GetName(), *GraphType, NodeSet.Num());
				GraphReadbacks += TEXT("This is the REAL current state of every node. Study all pins:\n");
				GraphReadbacks += TEXT("  • LinkedTo=(Node Pin) → verify types are compatible on both ends\n");
				GraphReadbacks += TEXT("  • DefaultValue=\"0.0\" on R/G/B float pins → color is BLACK\n");
				GraphReadbacks += TEXT("  • No DefaultObject on an object pin → asset reference is EMPTY\n");
				GraphReadbacks += TEXT("  • exec pin with no LinkedTo → execution chain is BROKEN\n\n");
				GraphReadbacks += ExportedT3D;
	
				// Pin audit for all nodes in this graph
				FString GraphAudit = BuildPinAuditReport(NodeSet);
			if (!GraphAudit.IsEmpty())
			{
				GraphReadbacks += TEXT("\n[PIN AUDIT for graph \"");
				GraphReadbacks += Graph->GetName();
				GraphReadbacks += TEXT("\"]");
				GraphReadbacks += GraphAudit;
			}
		};

		for (UEdGraph* G : Blueprint->UbergraphPages)   ExportGraph(G, TEXT("EventGraph"));
		for (UEdGraph* G : Blueprint->FunctionGraphs)   ExportGraph(G, TEXT("Function"));
		for (UEdGraph* G : Blueprint->MacroGraphs)      ExportGraph(G, TEXT("Macro"));
	}

	Result.bSuccess = true;
	Result.ResultMessage = InfoJson + GraphReadbacks;
	return Result;
}

// ============================================================================
// ExecuteConnectPins — Explicitly wire two nodes in a Blueprint graph
// ============================================================================

FAutonomixActionResult FAutonomixBlueprintActions::ExecuteConnectPins(const TSharedRef<FJsonObject>& Params, FAutonomixActionResult& Result)
{
	FString AssetPath;
	FString GraphName   = TEXT("EventGraph");
	FString SourceNode;
	FString SourcePin;
	FString TargetNode;
	FString TargetPin;

	if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath) || AssetPath.IsEmpty())
	{
		Result.Errors.Add(TEXT("Missing required field: asset_path"));
		return Result;
	}
	Params->TryGetStringField(TEXT("graph_name"), GraphName);
	if (!Params->TryGetStringField(TEXT("source_node"), SourceNode) || SourceNode.IsEmpty())
	{
		Result.Errors.Add(TEXT("Missing required field: source_node. Provide the internal node name from get_blueprint_info or add_blueprint_event result."));
		return Result;
	}
	if (!Params->TryGetStringField(TEXT("source_pin"), SourcePin) || SourcePin.IsEmpty())
	{
		Result.Errors.Add(TEXT("Missing required field: source_pin. Common exec output pins: \"then\", \"False\", \"True\". Common data output pins: \"ReturnValue\"."));
		return Result;
	}
	if (!Params->TryGetStringField(TEXT("target_node"), TargetNode) || TargetNode.IsEmpty())
	{
		Result.Errors.Add(TEXT("Missing required field: target_node. Provide the internal node name from get_blueprint_info."));
		return Result;
	}
	if (!Params->TryGetStringField(TEXT("target_pin"), TargetPin) || TargetPin.IsEmpty())
	{
		Result.Errors.Add(TEXT("Missing required field: target_pin. Common exec input pins: \"execute\". Common data input pins: \"Target\", \"Value\", \"Condition\"."));
		return Result;
	}

	UBlueprint* Blueprint = LoadObject<UBlueprint>(nullptr, *AssetPath);
	if (!Blueprint)
	{
		Result.Errors.Add(FString::Printf(TEXT("Blueprint not found: '%s'"), *AssetPath));
		return Result;
	}

	// Find the target graph
	UEdGraph* TargetGraph = nullptr;
	for (UEdGraph* G : Blueprint->UbergraphPages)
	{
		if (G && G->GetName() == GraphName)
		{
			TargetGraph = G;
			break;
		}
	}
	if (!TargetGraph)
	{
		for (UEdGraph* G : Blueprint->FunctionGraphs)
		{
			if (G && G->GetName() == GraphName)
			{
				TargetGraph = G;
				break;
			}
		}
	}
	if (!TargetGraph)
	{
		Result.Errors.Add(FString::Printf(
			TEXT("Graph '%s' not found in Blueprint '%s'. Use get_blueprint_info to list available graphs."),
			*GraphName, *AssetPath));
		return Result;
	}

	// Find source and target nodes by internal name
	UEdGraphNode* SrcNode = nullptr;
	UEdGraphNode* DstNode = nullptr;

	for (UEdGraphNode* Node : TargetGraph->Nodes)
	{
		if (!Node) continue;
		if (Node->GetName() == SourceNode) SrcNode = Node;
		if (Node->GetName() == TargetNode) DstNode = Node;
		if (SrcNode && DstNode) break;
	}

	if (!SrcNode)
	{
		// Build a helpful list of available node names for the AI to recover
		FString NodeList;
		for (UEdGraphNode* N : TargetGraph->Nodes)
		{
			if (N) NodeList += FString::Printf(TEXT("\n  \"%s\" (%s)"), *N->GetName(), *N->GetNodeTitle(ENodeTitleType::FullTitle).ToString());
		}
		Result.Errors.Add(FString::Printf(
			TEXT("Source node '%s' not found in graph '%s'. Available nodes:%s"),
			*SourceNode, *GraphName, *NodeList));
		return Result;
	}

	if (!DstNode)
	{
		FString NodeList;
		for (UEdGraphNode* N : TargetGraph->Nodes)
		{
			if (N) NodeList += FString::Printf(TEXT("\n  \"%s\" (%s)"), *N->GetName(), *N->GetNodeTitle(ENodeTitleType::FullTitle).ToString());
		}
		Result.Errors.Add(FString::Printf(
			TEXT("Target node '%s' not found in graph '%s'. Available nodes:%s"),
			*TargetNode, *GraphName, *NodeList));
		return Result;
	}

	// Find output pin on source node (case-insensitive for robustness)
	UEdGraphPin* OutputPin = nullptr;
	for (UEdGraphPin* Pin : SrcNode->Pins)
	{
		if (Pin && Pin->Direction == EGPD_Output &&
			Pin->PinName.ToString().Equals(SourcePin, ESearchCase::IgnoreCase))
		{
			OutputPin = Pin;
			break;
		}
	}
	if (!OutputPin)
	{
		FString PinList;
		for (UEdGraphPin* P : SrcNode->Pins)
		{
			if (P && P->Direction == EGPD_Output)
				PinList += FString::Printf(TEXT("\n  \"%s\" (type: %s)"), *P->PinName.ToString(), *P->PinType.PinCategory.ToString());
		}
		Result.Errors.Add(FString::Printf(
			TEXT("Output pin '%s' not found on node '%s'. Available output pins:%s"),
			*SourcePin, *SourceNode, *PinList));
		return Result;
	}

	// Find input pin on target node (case-insensitive)
	UEdGraphPin* InputPin = nullptr;
	for (UEdGraphPin* Pin : DstNode->Pins)
	{
		if (Pin && Pin->Direction == EGPD_Input &&
			Pin->PinName.ToString().Equals(TargetPin, ESearchCase::IgnoreCase))
		{
			InputPin = Pin;
			break;
		}
	}
	if (!InputPin)
	{
		FString PinList;
		for (UEdGraphPin* P : DstNode->Pins)
		{
			if (P && P->Direction == EGPD_Input)
				PinList += FString::Printf(TEXT("\n  \"%s\" (type: %s)"), *P->PinName.ToString(), *P->PinType.PinCategory.ToString());
		}
		Result.Errors.Add(FString::Printf(
			TEXT("Input pin '%s' not found on node '%s'. Available input pins:%s"),
			*TargetPin, *TargetNode, *PinList));
		return Result;
	}

	// Check if already connected
	for (UEdGraphPin* LinkedPin : OutputPin->LinkedTo)
	{
		if (LinkedPin == InputPin)
		{
			Result.bSuccess = true;
			Result.ResultMessage = FString::Printf(
				TEXT("Pins are already connected: %s.%s -> %s.%s"),
				*SourceNode, *SourcePin, *TargetNode, *TargetPin);
			return Result;
		}
	}

	// Perform the connection using the graph schema
	Blueprint->Modify();
	SrcNode->Modify();
	DstNode->Modify();

	const UEdGraphSchema* Schema = TargetGraph->GetSchema();
	FPinConnectionResponse Response = Schema->CanCreateConnection(OutputPin, InputPin);

	if (Response.Response == CONNECT_RESPONSE_DISALLOW)
	{
		Result.Errors.Add(FString::Printf(
			TEXT("Cannot connect %s.%s (type: %s) -> %s.%s (type: %s): %s"),
			*SourceNode, *SourcePin, *OutputPin->PinType.PinCategory.ToString(),
			*TargetNode, *TargetPin, *InputPin->PinType.PinCategory.ToString(),
			*Response.Message.ToString()));
		return Result;
	}

	Schema->TryCreateConnection(OutputPin, InputPin);

	bool bCompileOk = CompileAndReport(Blueprint, Result, true);
	Blueprint->GetOutermost()->MarkPackageDirty();

	Result.bSuccess = bCompileOk;
	Result.ResultMessage = FString::Printf(
		TEXT("Connected: %s.%s -> %s.%s\nCompile: %s."),
		*SourceNode, *SourcePin,
		*TargetNode, *TargetPin,
		bCompileOk ? TEXT("SUCCESS") : TEXT("FAILED"));
	Result.ModifiedAssets.Add(AssetPath);
	return Result;
}

// ============================================================================
// BuildPinAuditReport (shared helper — used by T3D injection, GetBlueprintInfo,
// and VerifyConnections so the AI always sees actionable pin-value feedback)
// ============================================================================

FString FAutonomixBlueprintActions::BuildPinAuditReport(const TSet<UEdGraphNode*>& Nodes)
{
	struct FPinAuditEntry
	{
		FString NodeTitle;
		FString PinName;
		FString Category;
		FString SubCategory;   // asset class name for object pins / struct name
		FString CurrentValue;  // DefaultValue or "[EMPTY]"
		FString Advice;
	};
	TArray<FPinAuditEntry> AuditEntries;

	for (UEdGraphNode* Node : Nodes)
	{
		if (!Node) continue;
		FString NodeTitle = Node->GetNodeTitle(ENodeTitleType::FullTitle).ToString();

		for (UEdGraphPin* Pin : Node->Pins)
		{
			if (!Pin) continue;
			if (Pin->Direction != EGPD_Input) continue;  // only input pins
			if (Pin->LinkedTo.Num() > 0) continue;        // already wired — skip
			if (Pin->bHidden) continue;                    // skip hidden/internal pins

			const FName PinCat = Pin->PinType.PinCategory;
			if (PinCat == TEXT("exec")) continue;          // skip execution flow pins

			FPinAuditEntry Entry;
			Entry.NodeTitle = NodeTitle;
			Entry.PinName   = Pin->PinName.ToString();
			Entry.Category  = PinCat.ToString();

			// --- Object / asset reference pins ---
			if (PinCat == TEXT("object")    ||
				PinCat == TEXT("interface") ||
				PinCat == TEXT("softobject")||
				PinCat == TEXT("class")     ||
				PinCat == TEXT("softclass"))
			{
				if (Pin->DefaultObject == nullptr)
				{
					FString ClassName = TEXT("Asset");
					if (UObject* SubCatObj = Pin->PinType.PinSubCategoryObject.Get())
						ClassName = SubCatObj->GetName();

					Entry.SubCategory  = ClassName;
					Entry.CurrentValue = TEXT("[EMPTY]");
					Entry.Advice = FString::Printf(
						TEXT("No asset assigned. Call search_assets(class_filter='%s') to find a valid asset, "
							 "then re-inject with DefaultObject set to the asset path."),
						*ClassName);
					AuditEntries.Add(Entry);
				}
			}
			// --- Numeric pins: real / float / double / int / int64 / byte ---
			else if (PinCat == TEXT("real")   ||
					 PinCat == TEXT("float")  ||
					 PinCat == TEXT("double") ||
					 PinCat == TEXT("int")    ||
					 PinCat == TEXT("int64")  ||
					 PinCat == TEXT("byte"))
			{
				const FString& Val = Pin->DefaultValue;
				const bool bIsZeroOrEmpty =
					Val.IsEmpty()         ||
					Val == TEXT("0")      ||
					Val == TEXT("0.0")    ||
					Val == TEXT("0.00")   ||
					Val == TEXT("0.000000");
				if (bIsZeroOrEmpty)
				{
					Entry.CurrentValue = Val.IsEmpty() ? TEXT("[EMPTY]") : Val;
					Entry.Advice = TEXT("Value is zero/empty — verify this is intentional or set an explicit "
						"non-zero value (e.g. 1.0 for the R/G/B channel of MakeColor when that channel should be fully on).");
					AuditEntries.Add(Entry);
				}
			}
			// --- Struct pins: FLinearColor, FVector, FRotator, etc. ---
			else if (PinCat == TEXT("struct"))
			{
				const FString& Val = Pin->DefaultValue;
				bool bLooksAllZero = Val.IsEmpty();
				if (!bLooksAllZero)
				{
					FString Stripped = Val.Replace(TEXT("("), TEXT("")).Replace(TEXT(")"), TEXT(""));
					TArray<FString> Tokens;
					Stripped.ParseIntoArray(Tokens, TEXT(","), true);
					bool bAllZero   = true;
					bool bHasTokens = Tokens.Num() > 0;
					for (const FString& Token : Tokens)
					{
						int32 EqIdx = INDEX_NONE;
						if (Token.FindChar(TEXT('='), EqIdx))
						{
							if (FMath::Abs(FCString::Atof(*Token.Mid(EqIdx + 1))) > SMALL_NUMBER)
							{
								bAllZero = false;
								break;
							}
						}
					}
					bLooksAllZero = bHasTokens && bAllZero;
				}
				if (bLooksAllZero)
				{
					FString StructName = TEXT("Struct");
					if (UObject* SubCatObj = Pin->PinType.PinSubCategoryObject.Get())
						StructName = SubCatObj->GetName();

					Entry.SubCategory  = StructName;
					Entry.CurrentValue = Val.IsEmpty() ? TEXT("[EMPTY]") : Val;
					Entry.Advice = FString::Printf(
						TEXT("%s has all-zero components — set explicit values where non-zero is intended "
							 "(e.g. B=1.0 for LinearColor pure blue, or the R/G/B channel for your target color)."),
						*StructName);
					AuditEntries.Add(Entry);
				}
			}
		}
	}

	if (AuditEntries.IsEmpty())
		return FString();

	FString Report;
	Report += TEXT("\n\n=== PIN VALUE AUDIT ===\n");
	Report += TEXT("The following unconnected input pins have empty, missing, or all-zero default values.\n");
	Report += TEXT("Review EVERY entry below and take the recommended action before declaring the task complete:\n\n");
	for (const FPinAuditEntry& E : AuditEntries)
	{
		Report += FString::Printf(
			TEXT("  • [%s] pin \"%s\" (%s%s) = %s\n    ACTION: %s\n"),
			*E.NodeTitle,
			*E.PinName,
			*E.Category,
			E.SubCategory.IsEmpty() ? TEXT("") : *FString::Printf(TEXT(":%s"), *E.SubCategory),
			*E.CurrentValue,
			*E.Advice);
	}
	return Report;
}

// ============================================================================
// ResolvePinType
// ============================================================================

void FAutonomixBlueprintActions::ResolvePinType(const FString& TypeName, FEdGraphPinType& OutPinType)
{
	const FString TypeLower = TypeName.ToLower();

	if (TypeLower == TEXT("bool") || TypeLower == TEXT("boolean"))
	{
		OutPinType.PinCategory = UEdGraphSchema_K2::PC_Boolean;
	}
	else if (TypeLower == TEXT("int") || TypeLower == TEXT("int32") || TypeLower == TEXT("integer"))
	{
		OutPinType.PinCategory = UEdGraphSchema_K2::PC_Int;
	}
	else if (TypeLower == TEXT("int64"))
	{
		OutPinType.PinCategory = UEdGraphSchema_K2::PC_Int64;
	}
	else if (TypeLower == TEXT("float") || TypeLower == TEXT("single"))
	{
		OutPinType.PinCategory = UEdGraphSchema_K2::PC_Real;
		OutPinType.PinSubCategory = UEdGraphSchema_K2::PC_Float;
	}
	else if (TypeLower == TEXT("double"))
	{
		OutPinType.PinCategory = UEdGraphSchema_K2::PC_Real;
		OutPinType.PinSubCategory = UEdGraphSchema_K2::PC_Double;
	}
	else if (TypeLower == TEXT("fstring") || TypeLower == TEXT("string"))
	{
		OutPinType.PinCategory = UEdGraphSchema_K2::PC_String;
	}
	else if (TypeLower == TEXT("ftext") || TypeLower == TEXT("text"))
	{
		OutPinType.PinCategory = UEdGraphSchema_K2::PC_Text;
	}
	else if (TypeLower == TEXT("fname") || TypeLower == TEXT("name"))
	{
		OutPinType.PinCategory = UEdGraphSchema_K2::PC_Name;
	}
	else if (TypeLower == TEXT("byte"))
	{
		OutPinType.PinCategory = UEdGraphSchema_K2::PC_Byte;
	}
	else if (TypeLower == TEXT("fvector") || TypeLower == TEXT("vector"))
	{
		OutPinType.PinCategory = UEdGraphSchema_K2::PC_Struct;
		OutPinType.PinSubCategoryObject = TBaseStructure<FVector>::Get();
	}
	else if (TypeLower == TEXT("frotator") || TypeLower == TEXT("rotator"))
	{
		OutPinType.PinCategory = UEdGraphSchema_K2::PC_Struct;
		OutPinType.PinSubCategoryObject = TBaseStructure<FRotator>::Get();
	}
	else if (TypeLower == TEXT("ftransform") || TypeLower == TEXT("transform"))
	{
		OutPinType.PinCategory = UEdGraphSchema_K2::PC_Struct;
		OutPinType.PinSubCategoryObject = TBaseStructure<FTransform>::Get();
	}
	else if (TypeLower == TEXT("flinearcolor") || TypeLower == TEXT("linearcolor") || TypeLower == TEXT("color"))
	{
		OutPinType.PinCategory = UEdGraphSchema_K2::PC_Struct;
		OutPinType.PinSubCategoryObject = TBaseStructure<FLinearColor>::Get();
	}
	else if (TypeLower == TEXT("fvector2d") || TypeLower == TEXT("vector2d"))
	{
		OutPinType.PinCategory = UEdGraphSchema_K2::PC_Struct;
		OutPinType.PinSubCategoryObject = TBaseStructure<FVector2D>::Get();
	}
	else
	{
		// Try as UClass (for Object references)
		UClass* FoundClass = FindFirstObject<UClass>(*TypeName, EFindFirstObjectOptions::None);
		if (!FoundClass)
			FoundClass = FindFirstObject<UClass>(*(TEXT("A") + TypeName), EFindFirstObjectOptions::None);
		if (!FoundClass)
			FoundClass = FindFirstObject<UClass>(*(TEXT("U") + TypeName), EFindFirstObjectOptions::None);

		if (FoundClass)
		{
			OutPinType.PinCategory = UEdGraphSchema_K2::PC_Object;
			OutPinType.PinSubCategoryObject = FoundClass;
		}
		else
		{
			// Try as UScriptStruct (for custom structs)
			UScriptStruct* FoundStruct = FindFirstObject<UScriptStruct>(*TypeName, EFindFirstObjectOptions::None);
			if (!FoundStruct)
				FoundStruct = FindFirstObject<UScriptStruct>(*(TEXT("F") + TypeName), EFindFirstObjectOptions::None);

			if (FoundStruct)
			{
				OutPinType.PinCategory = UEdGraphSchema_K2::PC_Struct;
				OutPinType.PinSubCategoryObject = FoundStruct;
			}
			else
			{
				// Last resort: wildcard — will likely cause a compile error but won't crash
				OutPinType.PinCategory = UEdGraphSchema_K2::PC_Wildcard;
				UE_LOG(LogAutonomix, Warning, TEXT("BlueprintActions: Unresolved pin type '%s' — using wildcard. Check the type name spelling."), *TypeName);
			}
		}
		}
	}
	
	// ============================================================================
	// ExecuteAddEnhancedInputNode
	// ============================================================================
	
	FAutonomixActionResult FAutonomixBlueprintActions::ExecuteAddEnhancedInputNode(
		const TSharedRef<FJsonObject>& Params, FAutonomixActionResult& Result)
	{
		FString AssetPath = Params->GetStringField(TEXT("asset_path"));
		FString InputActionPath;
		Params->TryGetStringField(TEXT("input_action"), InputActionPath);
	
		if (InputActionPath.IsEmpty())
		{
			Result.Errors.Add(TEXT("Missing required field: input_action. Provide the content path of the UInputAction asset (e.g. '/Game/ThirdPerson/Input/Actions/IA_Jump')."));
			return Result;
		}
	
		// Load Blueprint
		UBlueprint* Blueprint = LoadObject<UBlueprint>(nullptr, *AssetPath);
		if (!Blueprint)
		{
			Result.Errors.Add(FString::Printf(TEXT("Blueprint not found: '%s'"), *AssetPath));
			return Result;
		}
	
		// Load the Input Action asset
		UInputAction* InputAction = LoadObject<UInputAction>(nullptr, *InputActionPath);
		if (!InputAction)
		{
			Result.Errors.Add(FString::Printf(TEXT("Input Action asset not found: '%s'. Use search_assets to find the correct path."), *InputActionPath));
			return Result;
		}
	
		// Find or create the EventGraph
		UEdGraph* EventGraph = FindOrCreateEventGraph(Blueprint);
		if (!EventGraph)
		{
			Result.Errors.Add(TEXT("Could not find or create EventGraph."));
			return Result;
		}
	
		Blueprint->Modify();
	
		// Create the K2Node_EnhancedInputAction node
		UK2Node_EnhancedInputAction* InputNode = NewObject<UK2Node_EnhancedInputAction>(EventGraph);
		InputNode->SetFlags(RF_Transactional);
	
		// Set the InputAction property
		InputNode->InputAction = InputAction;
	
		// Set position
		int32 PosX = 0, PosY = 0;
		Params->TryGetNumberField(TEXT("node_pos_x"), PosX);
		Params->TryGetNumberField(TEXT("node_pos_y"), PosY);
		InputNode->NodePosX = PosX;
		InputNode->NodePosY = PosY;
	
		// Add to graph and allocate default pins
		EventGraph->AddNode(InputNode, false, false);
		InputNode->CreateNewGuid();
		InputNode->PostPlacedNewNode();
		InputNode->AllocateDefaultPins();
	
		// Notify the blueprint
		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
	
		// Compile
		bool bCompileOk = CompileAndReport(Blueprint, Result, true);
		Blueprint->GetOutermost()->MarkPackageDirty();
	
		FString NodeName = InputNode->GetName();
		FString NodeTitle = InputNode->GetNodeTitle(ENodeTitleType::FullTitle).ToString();
	
		// Build result with pin info for the AI
		FString PinInfo;
		for (UEdGraphPin* Pin : InputNode->Pins)
		{
			if (Pin)
			{
				FString Dir = (Pin->Direction == EGPD_Output) ? TEXT("OUT") : TEXT("IN");
				PinInfo += FString::Printf(TEXT("  [%s] %s (%s)\n"), *Dir, *Pin->PinName.ToString(), *Pin->PinType.PinCategory.ToString());
			}
		}
	
		Result.bSuccess = true;
		Result.ResultMessage = FString::Printf(
			TEXT("Added Enhanced Input Action node '%s' (%s) for input action '%s'.\n"
				 "Internal node name: %s\n"
				 "Position: (%d, %d)\n"
				 "Pins:\n%s\n"
				 "Use connect_blueprint_pins to wire 'Triggered' or 'Started' output to your function call.\n"
				 "Compiled: %s"),
			*NodeTitle, *InputNode->GetClass()->GetName(),
			*InputAction->GetName(),
			*NodeName,
			PosX, PosY,
			*PinInfo,
			bCompileOk ? TEXT("OK") : TEXT("with errors (see warnings)"));
		Result.ModifiedAssets.Add(AssetPath);
	
		UE_LOG(LogAutonomix, Log, TEXT("BlueprintActions: Added Enhanced Input node '%s' for IA '%s' in '%s'"),
			*NodeName, *InputAction->GetName(), *AssetPath);
		
		return Result;
	}
	
	// ============================================================================
	// ExecuteVerifyConnections
	// ============================================================================
	
	FAutonomixActionResult FAutonomixBlueprintActions::ExecuteVerifyConnections(const TSharedRef<FJsonObject>& Params, FAutonomixActionResult& Result)
	{
		FString AssetPath;
		if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
		{
			Result.Errors.Add(TEXT("Missing required parameter: asset_path"));
			return Result;
		}
	
		FString FilterGraphName;
		Params->TryGetStringField(TEXT("graph_name"), FilterGraphName);
	
		// -------------------------------------------------------------------------
		// Load Blueprint
		// -------------------------------------------------------------------------
		UBlueprint* Blueprint = Cast<UBlueprint>(
			StaticLoadObject(UBlueprint::StaticClass(), nullptr, *AssetPath));
		if (!Blueprint)
		{
			Result.Errors.Add(FString::Printf(TEXT("Could not load Blueprint at '%s'"), *AssetPath));
			return Result;
		}
	
		const UEdGraphSchema_K2* Schema = GetDefault<UEdGraphSchema_K2>();
	
		// -------------------------------------------------------------------------
		// Collect graphs to inspect
		// -------------------------------------------------------------------------
		TArray<UEdGraph*> GraphsToCheck;
		if (!FilterGraphName.IsEmpty())
		{
			// Specific graph requested
			for (UEdGraph* G : Blueprint->UbergraphPages)
			{
				if (G && G->GetName() == FilterGraphName)
				{
					GraphsToCheck.Add(G);
					break;
				}
			}
			for (UEdGraph* G : Blueprint->FunctionGraphs)
			{
				if (G && G->GetName() == FilterGraphName)
				{
					GraphsToCheck.Add(G);
					break;
				}
			}
			if (GraphsToCheck.IsEmpty())
			{
				Result.Errors.Add(FString::Printf(TEXT("Graph '%s' not found in Blueprint '%s'"), *FilterGraphName, *AssetPath));
				return Result;
			}
		}
		else
		{
			for (UEdGraph* G : Blueprint->UbergraphPages) { if (G) GraphsToCheck.Add(G); }
			for (UEdGraph* G : Blueprint->FunctionGraphs) { if (G) GraphsToCheck.Add(G); }
		}
	
		// -------------------------------------------------------------------------
		// Issue + repair structures
		// -------------------------------------------------------------------------
		struct FConnectionIssue
		{
			FString GraphName;
			FString NodeName;
			FString PinName;
			FString PinDirection; // "OUT" or "IN"
			FString IssueType;    // "DISCONNECTED_EXEC", "STALE_LINK"
		};
	
		TArray<FConnectionIssue> IssuesFound;
		TArray<FConnectionIssue> RepairsMade;
		TArray<FConnectionIssue> Unresolved;
	
		int32 TotalStaleLinksRemoved = 0;
	
		// -------------------------------------------------------------------------
		// Pass 1: Collect issues and remove stale links
		// -------------------------------------------------------------------------
		for (UEdGraph* Graph : GraphsToCheck)
		{
			if (!Graph) continue;
	
			// Build current node name set for stale-link detection
			TSet<FString> NodeNamesInGraph;
			for (UEdGraphNode* GNode : Graph->Nodes)
			{
				if (GNode) NodeNamesInGraph.Add(GNode->GetName());
			}
	
			for (UEdGraphNode* Node : Graph->Nodes)
			{
				if (!Node) continue;
	
				for (UEdGraphPin* Pin : Node->Pins)
				{
					if (!Pin) continue;
	
					// --- Remove stale LinkedTo references ---
					int32 StaleCount = Pin->LinkedTo.RemoveAll([&NodeNamesInGraph](UEdGraphPin* LP) -> bool
					{
						if (!LP) return true;
						UEdGraphNode* Owner = LP->GetOwningNodeUnchecked();
						if (!Owner) return true;
						if (!NodeNamesInGraph.Contains(Owner->GetName())) return true;
						return false;
					});
					if (StaleCount > 0)
					{
						TotalStaleLinksRemoved += StaleCount;
						FConnectionIssue Issue;
						Issue.GraphName = Graph->GetName();
						Issue.NodeName = Node->GetName();
						Issue.PinName = Pin->PinName.ToString();
						Issue.PinDirection = (Pin->Direction == EGPD_Output) ? TEXT("OUT") : TEXT("IN");
						Issue.IssueType = FString::Printf(TEXT("STALE_LINK (removed %d)"), StaleCount);
						IssuesFound.Add(Issue);
					}
	
					// --- Detect disconnected exec output pins ---
					if (Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec
						&& Pin->Direction == EGPD_Output
						&& Pin->LinkedTo.IsEmpty())
					{
						// Only report if this node has a non-exec output (i.e., it's in the middle of a chain)
						// or if it's a FunctionEntry node (must be connected to something)
						bool bIsFunctionEntry = Node->IsA<UK2Node_FunctionEntry>();
						bool bIsCallFunction  = Node->IsA<UK2Node_CallFunction>();
						bool bIsEvent         = Node->IsA<UK2Node_Event>() || Node->IsA<UK2Node_CustomEvent>();
	
						if (bIsFunctionEntry || bIsCallFunction || bIsEvent)
						{
							FConnectionIssue Issue;
							Issue.GraphName = Graph->GetName();
							Issue.NodeName = Node->GetName();
							Issue.PinName = Pin->PinName.ToString();
							Issue.PinDirection = TEXT("OUT");
							Issue.IssueType = TEXT("DISCONNECTED_EXEC_OUTPUT");
							IssuesFound.Add(Issue);
						}
					}
	
					// --- Detect disconnected exec input pins ---
					if (Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec
						&& Pin->Direction == EGPD_Input
						&& Pin->LinkedTo.IsEmpty())
					{
						bool bIsCallFunction  = Node->IsA<UK2Node_CallFunction>();
						bool bIsFunctionResult = Node->IsA<UK2Node_FunctionResult>();
	
						if (bIsCallFunction || bIsFunctionResult)
						{
							FConnectionIssue Issue;
							Issue.GraphName = Graph->GetName();
							Issue.NodeName = Node->GetName();
							Issue.PinName = Pin->PinName.ToString();
							Issue.PinDirection = TEXT("IN");
							Issue.IssueType = TEXT("DISCONNECTED_EXEC_INPUT");
							IssuesFound.Add(Issue);
						}
					}
				}
			}
		}
	
		// -------------------------------------------------------------------------
		// Pass 2: Attempt to repair DISCONNECTED_EXEC chains by linking adjacent
		// FunctionEntry → first call node, and call nodes with unique exec-in targets.
		// -------------------------------------------------------------------------
		for (UEdGraph* Graph : GraphsToCheck)
		{
			if (!Graph) continue;
	
			// Collect FunctionEntry nodes with disconnected exec-out
			for (UEdGraphNode* Node : Graph->Nodes)
			{
				if (!Node) continue;
				if (!Node->IsA<UK2Node_FunctionEntry>()) continue;
	
				UEdGraphPin* ExecOutPin = nullptr;
				for (UEdGraphPin* Pin : Node->Pins)
				{
					if (Pin
						&& Pin->Direction == EGPD_Output
						&& Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec
						&& Pin->LinkedTo.IsEmpty())
					{
						ExecOutPin = Pin;
						break;
					}
				}
				if (!ExecOutPin) continue;
	
				// Find call nodes in this graph that have a disconnected exec-in
				TArray<UEdGraphNode*> CandidateTargets;
				for (UEdGraphNode* TargetNode : Graph->Nodes)
				{
					if (!TargetNode || TargetNode == Node) continue;
					if (!TargetNode->IsA<UK2Node_CallFunction>()) continue;
	
					for (UEdGraphPin* TargetPin : TargetNode->Pins)
					{
						if (TargetPin
							&& TargetPin->Direction == EGPD_Input
							&& TargetPin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec
							&& TargetPin->LinkedTo.IsEmpty())
						{
							CandidateTargets.Add(TargetNode);
							break;
						}
					}
				}
	
				// Only auto-repair if exactly one candidate (unambiguous)
				if (CandidateTargets.Num() == 1)
				{
					UEdGraphNode* TargetNode = CandidateTargets[0];
					UEdGraphPin* TargetExecIn = nullptr;
					for (UEdGraphPin* TargetPin : TargetNode->Pins)
					{
						if (TargetPin
							&& TargetPin->Direction == EGPD_Input
							&& TargetPin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec
							&& TargetPin->LinkedTo.IsEmpty())
						{
							TargetExecIn = TargetPin;
							break;
						}
					}
	
					if (TargetExecIn)
					{
						FPinConnectionResponse ConnResponse = Schema->CanCreateConnection(ExecOutPin, TargetExecIn);
						if (ConnResponse.Response == CONNECT_RESPONSE_MAKE
							|| ConnResponse.Response == CONNECT_RESPONSE_MAKE_WITH_CONVERSION_NODE)
						{
							Schema->TryCreateConnection(ExecOutPin, TargetExecIn);
							FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
	
							FConnectionIssue Repair;
							Repair.GraphName = Graph->GetName();
							Repair.NodeName = FString::Printf(TEXT("%s -> %s"), *Node->GetName(), *TargetNode->GetName());
							Repair.PinName = FString::Printf(TEXT("%s -> %s"), *ExecOutPin->PinName.ToString(), *TargetExecIn->PinName.ToString());
							Repair.PinDirection = TEXT("OUT->IN");
							Repair.IssueType = TEXT("EXEC_CHAIN_REPAIRED");
							RepairsMade.Add(Repair);
						}
					}
				}
				else if (CandidateTargets.Num() > 1)
				{
					// Ambiguous: report as unresolved
					FConnectionIssue Issue;
					Issue.GraphName = Graph->GetName();
					Issue.NodeName = Node->GetName();
					Issue.PinName = ExecOutPin->PinName.ToString();
					Issue.PinDirection = TEXT("OUT");
					Issue.IssueType = FString::Printf(
						TEXT("DISCONNECTED_EXEC_OUTPUT (ambiguous: %d possible targets — use connect_blueprint_pins to wire manually)"),
						CandidateTargets.Num());
					Unresolved.Add(Issue);
				}
			}
		}
	
		// -------------------------------------------------------------------------
		// Pass 3: Full pin-value audit across ALL nodes in checked graphs
		// (object pins with no DefaultObject, zero numeric pins, all-zero structs)
		// -------------------------------------------------------------------------
		FString GlobalPinAudit;
		{
			for (UEdGraph* Graph : GraphsToCheck)
			{
				if (!Graph) continue;
				TSet<UEdGraphNode*> NodeSet;
				for (UEdGraphNode* Node : Graph->Nodes)
				{
					if (Node) NodeSet.Add(Node);
				}
				FString GraphAudit = BuildPinAuditReport(NodeSet);
				if (!GraphAudit.IsEmpty())
				{
					GlobalPinAudit += FString::Printf(TEXT("\n[PIN AUDIT — graph \"%s\"]"), *Graph->GetName());
					GlobalPinAudit += GraphAudit;
				}
			}
		}
	
		// -------------------------------------------------------------------------
		// Pass 4: Full T3D readback of every checked graph
		// -------------------------------------------------------------------------
		FString GraphReadbacks;
		{
			for (UEdGraph* Graph : GraphsToCheck)
			{
				if (!Graph) continue;
				TSet<UEdGraphNode*> NodeSet;
				for (UEdGraphNode* Node : Graph->Nodes)
				{
					if (Node) NodeSet.Add(Node);
				}
				if (NodeSet.IsEmpty()) continue;
	
				FString ExportedT3D;
				{
					TSet<UObject*> ExportObjSet;
					for (UEdGraphNode* N : NodeSet) { if (N) ExportObjSet.Add(N); }
					FEdGraphUtilities::ExportNodesToText(ExportObjSet, ExportedT3D);
				}
	
				GraphReadbacks += FString::Printf(
					TEXT("\n\n=== GRAPH T3D READBACK: \"%s\" (%d nodes) ===\n"),
					*Graph->GetName(), NodeSet.Num());
				GraphReadbacks += TEXT("This is the REAL current state of every node in this graph.\n");
				GraphReadbacks += TEXT("  • Check every LinkedTo=(NodeName PinId) — both sides must exist and be type-compatible\n");
				GraphReadbacks += TEXT("  • DefaultValue=\"0.0\" on R/G/B float pins → color component is BLACK\n");
				GraphReadbacks += TEXT("  • No DefaultObject on an object pin → asset reference is EMPTY at runtime\n");
				GraphReadbacks += TEXT("  • exec output pin with no LinkedTo → execution chain is BROKEN\n\n");
				GraphReadbacks += ExportedT3D;
			}
		}
	
		// -------------------------------------------------------------------------
		// Build result message
		// -------------------------------------------------------------------------
		bool bHadIssues     = IssuesFound.Num() > 0;
		bool bHadRepairs    = RepairsMade.Num() > 0;
		bool bHadUnresolved = Unresolved.Num() > 0;
	
		// Recompile if we made any repairs
		bool bCompileOk = true;
		if (bHadRepairs)
		{
			bCompileOk = CompileAndReport(Blueprint, Result, true);
			Blueprint->GetOutermost()->MarkPackageDirty();
		}
	
		FString Report;
		Report += FString::Printf(TEXT("verify_blueprint_connections — Blueprint: %s\n"), *AssetPath);
		if (!FilterGraphName.IsEmpty())
			Report += FString::Printf(TEXT("Graph filter: '%s'\n"), *FilterGraphName);
		Report += TEXT("---\n");
	
		if (!bHadIssues && !bHadRepairs && !bHadUnresolved)
		{
			Report += TEXT("STATUS: CLEAN — No exec-pin or stale-link issues detected.\n");
		}
		else
		{
			Report += FString::Printf(TEXT("Issues found: %d | Repairs made: %d | Unresolved: %d | Stale links removed: %d\n"),
				IssuesFound.Num(), RepairsMade.Num(), Unresolved.Num(), TotalStaleLinksRemoved);
	
			if (IssuesFound.Num() > 0)
			{
				Report += TEXT("\n[ISSUES FOUND]\n");
				for (const FConnectionIssue& Issue : IssuesFound)
				{
					Report += FString::Printf(TEXT("  Graph='%s' Node='%s' Pin='%s' Dir=%s  → %s\n"),
						*Issue.GraphName, *Issue.NodeName, *Issue.PinName, *Issue.PinDirection, *Issue.IssueType);
				}
			}
	
			if (RepairsMade.Num() > 0)
			{
				Report += TEXT("\n[REPAIRS MADE]\n");
				for (const FConnectionIssue& Repair : RepairsMade)
				{
					Report += FString::Printf(TEXT("  Graph='%s' Nodes='%s' Pins='%s'  → %s\n"),
						*Repair.GraphName, *Repair.NodeName, *Repair.PinName, *Repair.IssueType);
				}
			}
	
			if (Unresolved.Num() > 0)
			{
				Report += TEXT("\n[UNRESOLVED — manual action needed]\n");
				for (const FConnectionIssue& Issue : Unresolved)
				{
					Report += FString::Printf(TEXT("  Graph='%s' Node='%s' Pin='%s' Dir=%s  → %s\n"),
						*Issue.GraphName, *Issue.NodeName, *Issue.PinName, *Issue.PinDirection, *Issue.IssueType);
				}
				Report += TEXT("\nACTION: Use connect_blueprint_pins to wire the unresolved pins listed above.\n");
			}
	
			if (bHadRepairs)
			{
				Report += FString::Printf(TEXT("\nPost-repair compile: %s\n"), bCompileOk ? TEXT("SUCCESS") : TEXT("FAILED (see Errors)"));
			}
		}
	
		// Append pin audit (Pass 3) and T3D readback (Pass 4)
		if (!GlobalPinAudit.IsEmpty())
		{
			Report += TEXT("\n\n=== FULL PIN VALUE AUDIT (all graphs) ===");
			Report += TEXT("\nDO NOT declare the task complete until every issue below is resolved:\n");
			Report += GlobalPinAudit;
		}
	
		Report += GraphReadbacks;
	
		Result.bSuccess = true;
		Result.ResultMessage = Report;
		Result.ModifiedAssets.Add(AssetPath);
	
		UE_LOG(LogAutonomix, Log, TEXT("BlueprintActions: VerifyConnections completed for '%s' — %d issues, %d repairs, %d unresolved"),
			*AssetPath, IssuesFound.Num(), RepairsMade.Num(), Unresolved.Num());
	
		return Result;
	}
