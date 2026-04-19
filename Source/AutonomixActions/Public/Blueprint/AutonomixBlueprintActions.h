// Copyright Autonomix. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AutonomixInterfaces.h"
#include "EdGraphSchema_K2.h"

class UBlueprint;
class UEdGraph;
class USCS_Node;

/**
 * FAutonomixBlueprintActions
 *
 * Handles all Blueprint-related tool calls from the AI.
 *
 * Architecture:
 *   - Structural operations (create BP, add components/variables): Kismet2 / SCS API
 *   - Logic graph operations (add nodes, wire pins): T3D clipboard injection via FEdGraphUtilities
 *   - Property defaults: CDO + SCS component template reflection writes
 *   - Compilation: FKismetEditorUtilities::CompileBlueprint with FCompilerResultsLog feedback
 *
 * T3D Injection Strategy (inject_blueprint_nodes_t3d):
 *   The primary path for LLM-driven logic graph construction. The AI outputs T3D text
 *   (identical to what the Blueprint editor puts in the clipboard on copy), the plugin resolves
 *   placeholder GUID tokens, then calls FEdGraphUtilities::ImportNodesFromText to "paste" the
 *   nodes into the target graph. This bypasses the need for the AI to navigate FGraphNodeCreator
 *   and UK2Node pin APIs directly, greatly improving generation accuracy.
 */
class AUTONOMIXACTIONS_API FAutonomixBlueprintActions : public IAutonomixActionExecutor
{
public:
	FAutonomixBlueprintActions();
	virtual ~FAutonomixBlueprintActions();

	// IAutonomixActionExecutor
	virtual FName GetActionName() const override;
	virtual FText GetDisplayName() const override;
	virtual EAutonomixActionCategory GetCategory() const override;
	virtual EAutonomixRiskLevel GetDefaultRiskLevel() const override;
	virtual FAutonomixActionPlan PreviewAction(const TSharedRef<FJsonObject>& Params) override;
	virtual FAutonomixActionResult ExecuteAction(const TSharedRef<FJsonObject>& Params) override;
	virtual bool CanUndo() const override;
	virtual bool UndoAction() override;
	virtual TArray<FString> GetSupportedToolNames() const override;
	virtual bool ValidateParams(const TSharedRef<FJsonObject>& Params, TArray<FString>& OutErrors) const override;

private:
	// =========================================================================
	// Original executors
	// =========================================================================

	/** Create a new Blueprint asset with optional inline components and variables. */
	FAutonomixActionResult ExecuteCreateBlueprint(const TSharedRef<FJsonObject>& Params, FAutonomixActionResult& Result);

	/** Add a component to an existing Blueprint via the Simple Construction Script. */
	FAutonomixActionResult ExecuteAddComponent(const TSharedRef<FJsonObject>& Params, FAutonomixActionResult& Result);

	/** Add a typed member variable to a Blueprint. */
	FAutonomixActionResult ExecuteAddVariable(const TSharedRef<FJsonObject>& Params, FAutonomixActionResult& Result);

	/** Compile a Blueprint and return all errors/warnings. */
	FAutonomixActionResult ExecuteCompileBlueprint(const TSharedRef<FJsonObject>& Params, FAutonomixActionResult& Result);

	/** Set default property values on a Blueprint's Class Default Object. */
	FAutonomixActionResult ExecuteSetDefaults(const TSharedRef<FJsonObject>& Params, FAutonomixActionResult& Result);

	// =========================================================================
	// New executors
	// =========================================================================

	/**
	 * Inject T3D-serialized Blueprint node text directly into a Blueprint graph.
	 *
	 * Uses FEdGraphUtilities::ImportNodesFromText — the exact same mechanism as the
	 * Unreal Blueprint editor's paste operation. The AI produces T3D text describing
	 * nodes, pin configurations, default values, and inter-node links using placeholder
	 * GUID tokens (e.g. LINK_1, GUID_A). The plugin resolves those placeholders to
	 * fresh UE GUIDs, then injects the nodes into the target graph.
	 *
	 * This is the PREFERRED method for all logic graph construction. It handles:
	 *   - Control flow nodes (Branch, Sequence, ForEachLoop, WhileLoop)
	 *   - Function call nodes (UK2Node_CallFunction for any BlueprintCallable UFUNCTION)
	 *   - Variable get/set nodes (UK2Node_VariableGet, UK2Node_VariableSet)
	 *   - Cast nodes (UK2Node_DynamicCast)
	 *   - Pure math/utility nodes (all UKismetMathLibrary and UKismetSystemLibrary functions)
	 *   - Custom Events (UK2Node_CustomEvent)
	 *   - Latent nodes (Delay, MoveComponentTo, AIMoveTo) — in EventGraph or Macros only
	 *
	 * Parameters:
	 *   - asset_path (string, required): Content path of the target Blueprint
	 *   - t3d_text (string, required): T3D-formatted node block(s)
	 *   - graph_name (string, optional): Target graph name. Default: "EventGraph".
	 *     Use the function name to target a function graph.
	 */
	FAutonomixActionResult ExecuteInjectNodesT3D(const TSharedRef<FJsonObject>& Params, FAutonomixActionResult& Result);

	/**
	 * Add a new named function graph to a Blueprint.
	 *
	 * Creates the function entry and result nodes automatically. Optionally adds
	 * typed input parameters and output return values. After creation, use
	 * inject_blueprint_nodes_t3d with graph_name equal to the function name
	 * to populate the function body with logic.
	 *
	 * Parameters:
	 *   - asset_path (string, required): Content path of the Blueprint
	 *   - function_name (string, required): Name for the new function
	 *   - inputs (array, optional): [{name, type}] input parameter definitions
	 *   - outputs (array, optional): [{name, type}] return value definitions
	 */
	FAutonomixActionResult ExecuteAddFunction(const TSharedRef<FJsonObject>& Params, FAutonomixActionResult& Result);

	/**
	 * Set properties on a specific SCS component template inside a Blueprint.
	 *
	 * This is the authoritative way to assign meshes, change collision profiles,
	 * set socket offsets, and configure component-specific properties on the
	 * Blueprint's component hierarchy (SCS).
	 *
	 * Parameters:
	 *   - asset_path (string, required): Content path of the Blueprint
	 *   - component_name (string, required): Variable name of the SCS component to modify
	 *   - static_mesh (string, optional): Content path to a UStaticMesh asset
	 *   - skeletal_mesh (string, optional): Content path to a USkeletalMesh asset
	 *   - relative_location ({x,y,z}, optional): Relative location offset
	 *   - relative_rotation ({pitch,yaw,roll}, optional): Relative rotation
	 *   - relative_scale ({x,y,z}, optional): Relative scale
	 *   - collision_profile (string, optional): Collision preset name (e.g. "BlockAll", "OverlapAll", "NoCollision")
	 *   - properties (object, optional): Generic reflection-based property writes (PropertyName: ValueString)
	 */
	FAutonomixActionResult ExecuteSetComponentProperties(const TSharedRef<FJsonObject>& Params, FAutonomixActionResult& Result);

	/**
	 * Read-only query: return a structured JSON description of a Blueprint's current state.
	 *
	 * Returns compile_status, parent_class, all variables (name/type/category),
	 * all SCS components with their class and parent hierarchy, and all graphs
	 * (name/type/node_count). Use this before injecting nodes to understand
	 * the existing graph layout and component variable names.
	 *
	 * Parameters:
	 *   - asset_path (string, required): Content path of the Blueprint
	 */
	FAutonomixActionResult ExecuteGetBlueprintInfo(const TSharedRef<FJsonObject>& Params, FAutonomixActionResult& Result);

	/**
	 * Add a standard event handler node to a Blueprint's EventGraph.
	 *
	 * Adds or finds an existing event node (prevents duplicate creation). Returns the
	 * node's Y position so the AI can use that as an anchor for T3D injections.
	 *
	 * Supported event_name values:
	 *   Actor events: BeginPlay, EndPlay, Tick, ActorBeginOverlap, ActorEndOverlap,
	 *                 Hit, AnyDamage, PointDamage, Destroyed
	 *   Pawn events:  PossessedBy, UnPossessed
	 *   Character:    Landed
	 *
	 * Parameters:
	 *   - asset_path (string, required): Content path of the Blueprint
	 *   - event_name (string, required): One of the supported event names above
	 *   - node_pos_x (int, optional): Horizontal position for the event node. Default: 0.
	 *   - node_pos_y (int, optional): Starting vertical position hint. Default: auto.
	 */
	FAutonomixActionResult ExecuteAddEventHandler(const TSharedRef<FJsonObject>& Params, FAutonomixActionResult& Result);

	/**
	 * Explicitly connect two pins between existing nodes in a Blueprint graph.
	 *
	 * Use this when nodes were added separately (via add_blueprint_event + inject_blueprint_nodes_t3d)
	 * and need to be wired together. The tool resolves nodes by name (as returned from
	 * get_blueprint_info or add_blueprint_event) and connects the specified pins.
	 *
	 * IMPORTANT: The node names are the internal UEdGraphNode names (e.g. "K2Node_Event_0",
	 * "K2Node_CallFunction_3"), NOT user-visible titles. Always call get_blueprint_info first
	 * to obtain the current node names before calling this tool.
	 *
	 * Parameters:
	 *   - asset_path (string, required): Content path of the Blueprint
	 *   - graph_name (string, optional): Graph to operate in. Default: "EventGraph".
	 *   - source_node (string, required): Internal name of the source node (has the output pin)
	 *   - source_pin (string, required): Output pin name on the source node (e.g. "then", "ReturnValue")
	 *   - target_node (string, required): Internal name of the target node (has the input pin)
	 *   - target_pin (string, required): Input pin name on the target node (e.g. "execute", "Target", "Value")
	 */
	FAutonomixActionResult ExecuteConnectPins(const TSharedRef<FJsonObject>& Params, FAutonomixActionResult& Result);

	/**
	 * Add a K2Node_EnhancedInputAction to a Blueprint's EventGraph.
	 *
	 * Enhanced Input nodes cannot be created via T3D injection because their
	 * InputAction property (a UInputAction asset reference) doesn't serialize
	 * through text import. This tool creates the node programmatically.
	 *
	 * Parameters:
	 *   - asset_path (string, required): Content path of the Blueprint
	 *   - input_action (string, required): Content path of the UInputAction asset
	 *     (e.g. "/Game/ThirdPerson/Input/Actions/IA_Jump")
	 *   - node_pos_x (int, optional): X position. Default: 0
	 *   - node_pos_y (int, optional): Y position. Default: 0
	 */
	FAutonomixActionResult ExecuteAddEnhancedInputNode(const TSharedRef<FJsonObject>& Params, FAutonomixActionResult& Result);

	/**
	 * Scan all graphs (or a specific named graph) of a Blueprint for connection issues
	 * and attempt automatic repairs where unambiguous.
	 *
	 * Pass 1 — CollectIssues:
	 *   - Removes stale/null LinkedTo references left behind by T3D cross-batch injections
	 *   - Detects disconnected exec-output pins on FunctionEntry, CallFunction, Event nodes
	 *   - Detects disconnected exec-input pins on CallFunction and FunctionResult nodes
	 *
	 * Pass 2 — AttemptRepair:
	 *   - When a FunctionEntry exec-out is disconnected and exactly one CallFunction
	 *     in the same graph has a disconnected exec-in, auto-wires them via TryCreateConnection
	 *   - Recompiles the Blueprint if any repair was made
	 *
	 * Returns a human-readable report with sections: ISSUES FOUND, REPAIRS MADE, UNRESOLVED.
	 *
	 * Parameters:
	 *   - asset_path (string, required): Content path of the Blueprint
	 *   - graph_name (string, optional): Specific graph to check. Default: all graphs.
	 */
	FAutonomixActionResult ExecuteVerifyConnections(const TSharedRef<FJsonObject>& Params, FAutonomixActionResult& Result);

	/**
	 * Set a default value on an existing Blueprint graph node's input pin.
	 *
	 * Uses UEdGraphSchema_K2::TrySetDefaultValue (for literals/structs/enums),
	 * TrySetDefaultObject (for hard object/class references), or
	 * TrySetDefaultText (for FText pins) — the production-correct approach
	 * that validates type compatibility and integrates with the undo system.
	 *
	 * Parameters:
	 *   - asset_path (string, required): Content path of the Blueprint
	 *   - node_name (string, required): Internal node name (e.g. "K2Node_CallFunction_0")
	 *   - pin_name (string, required): Input pin name (e.g. "InString", "LevelName")
	 *   - value (string, required): The default value to set (format depends on pin type)
	 *   - graph_name (string, optional): Target graph. Default: "EventGraph".
	 */
	FAutonomixActionResult ExecuteSetNodePinDefault(const TSharedRef<FJsonObject>& Params, FAutonomixActionResult& Result);

	/**
	 * Delete one or more nodes from a Blueprint graph by their internal names.
	 *
	 * Use this to remove duplicate nodes, orphaned nodes, or nodes that are no longer
	 * needed for the task. Always call get_blueprint_info first to see the current
	 * node inventory before deleting.
	 *
	 * Parameters:
	 *   - asset_path (string, required): Content path of the Blueprint
	 *   - node_names (array of string, required): Internal node names to delete
	 *     (e.g. ["K2Node_CallFunction_3", "K2Node_Event_1"])
	 *   - graph_name (string, optional): Target graph. Default: "EventGraph".
	 */
	FAutonomixActionResult ExecuteDeleteNodes(const TSharedRef<FJsonObject>& Params, FAutonomixActionResult& Result);

	// =========================================================================
	// Helpers
	// =========================================================================

	/** Map a user-facing type string ("float", "FVector", "AActor") to FEdGraphPinType. */
	static void ResolvePinType(const FString& TypeName, FEdGraphPinType& OutPinType);

	/**
	 * Detect infinite loop risks before compilation:
	 *   - EventTick presence (warn about per-frame operations)
	 *   - Overpopulated ConstructionScripts (>50 SCS nodes)
	 * Returns true if any risk was detected; populates OutWarnings.
	 */
	bool DetectInfiniteLoopRisk(UBlueprint* Blueprint, TArray<FString>& OutWarnings) const;

	/**
	 * Find an existing UEdGraph by name in UbergraphPages or FunctionGraphs.
	 * If GraphName is "EventGraph" and no match exists, creates a new uber-graph page.
	 * Returns nullptr only if the graph cannot be found and creation is inappropriate.
	 */
	static UEdGraph* FindOrCreateEventGraph(UBlueprint* Blueprint, const FString& GraphName = TEXT("EventGraph"));

	/** Find a USCS_Node by its variable name in a Blueprint's SCS. Returns nullptr if not found. */
	static USCS_Node* FindSCSNodeByName(UBlueprint* Blueprint, const FString& NodeName);

	/**
	 * Replace placeholder GUID tokens in T3D text with freshly generated GUIDs.
	 *
	 * Recognizes tokens matching the pattern: (LINK_|GUID_|NODEREF_|ID_)[A-Za-z0-9_]+
	 * Each unique placeholder gets a consistent 32-char uppercase hex GUID replacement
	 * throughout the entire T3D block, so cross-node links remain valid.
	 *
	 * Example: "PinId=LINK_1" → "PinId=A1B2C3D4E5F6A1B2C3D4E5F6A1B2C3D4"
	 */
	static FString ResolveT3DPlaceholders(const FString& T3DText);

	/**
	 * Compile a Blueprint and collect all errors/warnings into Result.
	 * Returns true if compilation succeeded (status != BS_Error).
	 * Centralizes all compilation reporting to avoid code duplication.
	 */
	static bool CompileAndReport(UBlueprint* Blueprint, FAutonomixActionResult& Result, bool bSkipGC = true);

	/** Build a JSON string describing variables, SCS components, and graph inventory. */
	static FString BuildBlueprintInfoJson(UBlueprint* Blueprint);

	/**
	 * Audit unconnected input pins across the given node set for common authoring errors:
	 *   - Object/asset pins with no DefaultObject (unassigned asset reference → runtime null)
	 *   - Numeric pins (real/float/int) with zero or empty DefaultValue (often wrong, e.g. R/G/B color channels)
	 *   - Struct pins (LinearColor, Vector, Rotator, etc.) where ALL components are zero
	 *
	 * Returns a formatted multi-line report string; returns empty string if no issues found.
	 * Used by inject_blueprint_nodes_t3d, get_blueprint_info, and verify_blueprint_connections
	 * so the AI always receives actionable feedback on pin state after every graph operation.
	 */
	static FString BuildPinAuditReport(const TSet<UEdGraphNode*>& Nodes);

	/**
	 * Build a compact connection verification report for a set of nodes.
	 *
	 * Token-efficient replacement for FEdGraphUtilities::ExportNodesToText().
	 * Walks UEdGraphNode objects in memory to produce a structured summary of:
	 *   - Node inventory (name, title, position)
	 *   - Execution chain (exec pin connections and broken chains with ⚠ warnings)
	 *   - Data connections (non-exec pin wiring between nodes)
	 *   - Non-default pin values on unwired input pins
	 *   - Disconnected input pins with no value (potential authoring errors)
	 *
	 * Token savings: ~80-90% compared to ExportNodesToText()
	 * (200-500 tokens vs 3,000-8,000 for a typical 5-node injection)
	 *
	 * @param Nodes      Set of nodes to report on
	 * @param GraphName  Name of the containing graph (for the report header)
	 * @return Formatted multi-line report string
	 */
	static FString BuildCompactConnectionReport(const TSet<UEdGraphNode*>& Nodes, const FString& GraphName);

	/** Map a friendly event name to its Kismet function name and owning class. */
	static bool ResolveEventMapping(const FString& EventName, FName& OutFunctionName, UClass*& OutOwnerClass);

	// =========================================================================
	// v1.1: Auto-Layout & Pre-Flight Validation
	// =========================================================================

	/**
	 * Apply a deterministic Sugiyama-style layered graph layout to injected nodes.
	 *
	 * After T3D injection, nodes often stack at (0,0). This method performs:
	 *   1. Topological sort (BFS from entry/event nodes following exec pins)
	 *   2. Layer assignment (longest-path from sources)
	 *   3. In-layer ordering (minimize crossing via barycenter heuristic)
	 *   4. Coordinate assignment (fixed X per layer, Y spacing per node)
	 *
	 * @param Nodes          Set of nodes to layout
	 * @param StartX         Base X position (default: 0)
	 * @param StartY         Base Y position (default: 0)
	 * @param LayerSpacingX  Horizontal spacing between layers (default: 300)
	 * @param NodeSpacingY   Vertical spacing between nodes in same layer (default: 150)
	 */
	static void AutoLayoutNodes(
		const TSet<UEdGraphNode*>& Nodes,
		int32 StartX = 0,
		int32 StartY = 0,
		int32 LayerSpacingX = 300,
		int32 NodeSpacingY = 150);

	/**
	 * Pre-flight validation: check T3D node references against UE's reflection system.
	 *
	 * Scans the T3D text for node class references and pin connections, validates:
	 *   - Referenced node classes exist (e.g., K2Node_CallFunction)
	 *   - Referenced function names exist on the specified class
	 *   - Pin type compatibility (catches Float→ActorRef mismatches)
	 *
	 * @param T3DText        The raw T3D text to validate
	 * @param Blueprint       The target Blueprint (for variable/function resolution)
	 * @param OutWarnings     Detected issues (non-fatal: injection proceeds with warnings)
	 * @return               true if no blocking issues found
	 */
	static bool PreFlightValidateT3D(
		const FString& T3DText,
		const UBlueprint* Blueprint,
		TArray<FString>& OutWarnings);
};
