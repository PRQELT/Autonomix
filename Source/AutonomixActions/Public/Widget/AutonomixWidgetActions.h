// Copyright Autonomix. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AutonomixInterfaces.h"

class UWidgetBlueprint;

/**
 * FAutonomixWidgetActions
 *
 * Handles all UMG Widget Blueprint tool calls from the AI.
 *
 * Architecture:
 *   - Widget Blueprint creation: UWidgetBlueprintFactory + IAssetTools
 *   - Widget tree manipulation: UWidgetTree::ConstructWidget, FindWidget, UPanelWidget::AddChild
 *   - Property writes: reflection (FProperty) with allowlisted key guidance
 *   - Compilation: FKismetEditorUtilities::CompileBlueprint (same pipeline as Blueprint actors)
 *
 * UMG Widget Blueprint Dual Systems (analogous to Blueprint SCS + EventGraph):
 *   1. Widget Tree (SCS equivalent): UWidgetTree manages the visual hierarchy
 *      of UWidget objects (UPanelWidget containers + leaf widgets like TextBlock, Button).
 *      Mutate via add_widget / set_widget_property.
 *   2. Widget Blueprint Graph: standard K2 nodes for event binding (OnClicked, etc.)
 *      — use inject_blueprint_nodes_t3d with asset_path pointing to the WBP.
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
	/**
	 * Create a new Widget Blueprint asset (WBP_) with optional root widget type.
	 * Parent class defaults to UUserWidget. Root widget class defaults to CanvasPanel.
	 */
	FAutonomixActionResult ExecuteCreateWidgetBlueprint(const TSharedRef<FJsonObject>& Params, FAutonomixActionResult& Result);

	/**
	 * Add a UMG widget to the widget tree of an existing Widget Blueprint.
	 * The parent_widget must be a UPanelWidget (CanvasPanel, VerticalBox, HorizontalBox, etc.).
	 * If no parent_widget is specified and no root exists, the new widget becomes the root.
	 */
	FAutonomixActionResult ExecuteAddWidget(const TSharedRef<FJsonObject>& Params, FAutonomixActionResult& Result);

	/**
	 * Set a property on a named widget via reflection.
	 * Property names are exact C++ names (case-sensitive):
	 *   TextBlock: Text (FText), ColorAndOpacity, Font, Justification
	 *   Button: BackgroundColor, IsFocusable
	 *   Image: Brush (FSlateBrush), ColorAndOpacity
	 *   Any widget: Visibility (0=Visible,1=Collapsed,2=Hidden), Padding, bIsEnabled
	 */
	FAutonomixActionResult ExecuteSetWidgetProperty(const TSharedRef<FJsonObject>& Params, FAutonomixActionResult& Result);

	/**
	 * Read-only query: return the full widget tree hierarchy and all widget names as JSON.
	 * Call this before add_widget or set_widget_property to verify widget names.
	 */
	FAutonomixActionResult ExecuteGetWidgetTree(const TSharedRef<FJsonObject>& Params, FAutonomixActionResult& Result);

	/**
	 * Compile a Widget Blueprint and return errors/warnings.
	 * Widget Blueprints use the same compiler as regular Blueprints.
	 */
	FAutonomixActionResult ExecuteCompileWidgetBlueprint(const TSharedRef<FJsonObject>& Params, FAutonomixActionResult& Result);

	/** Serialize the widget tree to JSON. Used by ExecuteGetWidgetTree. */
	static FString BuildWidgetTreeJson(UWidgetBlueprint* WidgetBlueprint);

	/** Resolve a widget class name (with or without U prefix) to a UClass. */
	static UClass* ResolveWidgetClass(const FString& ClassName);
};
