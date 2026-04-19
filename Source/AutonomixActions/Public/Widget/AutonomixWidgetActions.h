// Copyright Autonomix. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AutonomixInterfaces.h"

class UWidgetBlueprint;
class UWidget;
class UPanelSlot;

/**
 * FAutonomixWidgetActions
 *
 * Handles all UMG Widget Blueprint tool calls from the AI.
 *
 * Architecture:
 *   - Widget Blueprint creation: UWidgetBlueprintFactory + IAssetTools
 *   - Widget tree manipulation: UWidgetTree::ConstructWidget, FindWidget, UPanelWidget::AddChild
 *   - Slot configuration: UCanvasPanelSlot, UVerticalBoxSlot, etc. — anchors, offsets, padding, alignment
 *   - Property writes: reflection (FProperty) with allowlisted key guidance
 *   - Font configuration: FSlateFontInfo with engine font paths and typeface selection
 *   - Brush configuration: FSlateBrush with texture loading, solid colors, and 9-slice
 *   - Event binding: K2Node_ComponentBoundEvent creation for widget delegates
 *   - Widget removal: UPanelWidget::RemoveChild
 *   - Compilation: FKismetEditorUtilities::CompileBlueprint (same pipeline as Blueprint actors)
 *
 * UMG Widget Blueprint Dual Systems (analogous to Blueprint SCS + EventGraph):
 *   1. Widget Tree (SCS equivalent): UWidgetTree manages the visual hierarchy
 *      of UWidget objects (UPanelWidget containers + leaf widgets like TextBlock, Button).
 *      Mutate via add_widget / set_widget_property / set_widget_slot / set_widget_font / set_widget_brush.
 *   2. Widget Blueprint Graph: standard K2 nodes for event binding (OnClicked, etc.)
 *      — use bind_widget_event to create event nodes, then inject_blueprint_nodes_t3d for logic.
 *
 * Supported tools (10):
 *   create_widget_blueprint, add_widget, set_widget_slot, set_widget_property,
 *   set_widget_font, set_widget_brush, bind_widget_event, remove_widget,
 *   get_widget_tree, compile_widget_blueprint
 */
class AUTONOMIXACTIONS_API FAutonomixWidgetActions : public IAutonomixActionExecutor
{
public:
	FAutonomixWidgetActions();
	virtual ~FAutonomixWidgetActions();

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
	// ======================================================================
	// Tool Handlers
	// ======================================================================

	/** Create a new Widget Blueprint asset with optional root widget and parent class. */
	FAutonomixActionResult ExecuteCreateWidgetBlueprint(const TSharedRef<FJsonObject>& Params, FAutonomixActionResult& Result);

	/** Add a widget to the tree hierarchy. */
	FAutonomixActionResult ExecuteAddWidget(const TSharedRef<FJsonObject>& Params, FAutonomixActionResult& Result);

	/** Configure the layout slot of a widget (anchors, offsets, padding, alignment, fill). */
	FAutonomixActionResult ExecuteSetWidgetSlot(const TSharedRef<FJsonObject>& Params, FAutonomixActionResult& Result);

	/** Set a property on a named widget via reflection. */
	FAutonomixActionResult ExecuteSetWidgetProperty(const TSharedRef<FJsonObject>& Params, FAutonomixActionResult& Result);

	/** Set font properties on a text widget (FSlateFontInfo). */
	FAutonomixActionResult ExecuteSetWidgetFont(const TSharedRef<FJsonObject>& Params, FAutonomixActionResult& Result);

	/** Set a brush (image/texture/solid color) on Image, Button, Border, ProgressBar widgets. */
	FAutonomixActionResult ExecuteSetWidgetBrush(const TSharedRef<FJsonObject>& Params, FAutonomixActionResult& Result);

	/** Bind a widget event (OnClicked, etc.) to a K2 event node in the EventGraph. */
	FAutonomixActionResult ExecuteBindWidgetEvent(const TSharedRef<FJsonObject>& Params, FAutonomixActionResult& Result);

	/** Remove a widget from the tree. */
	FAutonomixActionResult ExecuteRemoveWidget(const TSharedRef<FJsonObject>& Params, FAutonomixActionResult& Result);

	/** Read-only: return the full widget tree hierarchy as JSON. */
	FAutonomixActionResult ExecuteGetWidgetTree(const TSharedRef<FJsonObject>& Params, FAutonomixActionResult& Result);

	/** Compile a Widget Blueprint and return errors/warnings. */
	FAutonomixActionResult ExecuteCompileWidgetBlueprint(const TSharedRef<FJsonObject>& Params, FAutonomixActionResult& Result);

	// ======================================================================
	// Helpers
	// ======================================================================

	/** Serialize the widget tree to JSON (with slot info, parent names). */
	static FString BuildWidgetTreeJson(UWidgetBlueprint* WidgetBlueprint);

	/** Resolve a widget class name (with or without U prefix) to a UClass. */
	static UClass* ResolveWidgetClass(const FString& ClassName);

	/** Load a Widget Blueprint from an asset path, adding error to Result if not found. */
	static UWidgetBlueprint* LoadWidgetBP(const FString& AssetPath, FAutonomixActionResult& Result);

	/** Find a widget by name in a Widget Blueprint, adding error to Result if not found. */
	static UWidget* FindWidgetByName(UWidgetBlueprint* WidgetBP, const FString& WidgetName, FAutonomixActionResult& Result);

	/** Parse a "X,Y" string to FVector2D. Returns false on parse failure. */
	static bool ParseVector2D(const FString& Str, FVector2D& OutVec);

	/** Parse a "Left,Top,Right,Bottom" or single-value string to FMargin. */
	static bool ParseMargin(const FString& Str, FMargin& OutMargin);

	/** Parse an alignment string (Left/Center/Right/Fill) to EHorizontalAlignment. */
	static EHorizontalAlignment ParseHAlign(const FString& Str);

	/** Parse an alignment string (Top/Center/Bottom/Fill) to EVerticalAlignment. */
	static EVerticalAlignment ParseVAlign(const FString& Str);

	/** Parse a linear color string "(R=1,G=0,B=0,A=1)" to FLinearColor. */
	static bool ParseLinearColor(const FString& Str, FLinearColor& OutColor);

	/** Compile and mark dirty — common post-modification step. */
	static void CompileAndMarkDirty(UWidgetBlueprint* WidgetBP);
};
