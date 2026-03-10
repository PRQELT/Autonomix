// Copyright Autonomix. All Rights Reserved.

#include "Widget/AutonomixWidgetActions.h"
#include "AutonomixCoreModule.h"

// UMG Runtime
#include "Blueprint/WidgetTree.h"
#include "Components/PanelWidget.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/VerticalBox.h"
#include "Components/HorizontalBox.h"
#include "Components/TextBlock.h"
#include "Components/Button.h"
#include "Components/Image.h"
#include "Components/ScrollBox.h"
#include "Components/SizeBox.h"
#include "Components/Overlay.h"
#include "Components/GridPanel.h"
#include "Components/WidgetSwitcher.h"
#include "Components/ProgressBar.h"
#include "Components/EditableTextBox.h"
#include "Components/CheckBox.h"
#include "Components/Slider.h"

// UMG Editor (Widget Blueprint factory and asset type)
#include "WidgetBlueprint.h"
#include "WidgetBlueprintFactory.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Kismet2/BlueprintEditorUtils.h"

// Asset management
#include "AssetToolsModule.h"
#include "AssetRegistry/AssetRegistryModule.h"
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

// ============================================================================
// Statics / Lifecycle
// ============================================================================

FAutonomixWidgetActions::FAutonomixWidgetActions() {}
FAutonomixWidgetActions::~FAutonomixWidgetActions() {}

FName FAutonomixWidgetActions::GetActionName() const { return FName(TEXT("Widget")); }
FText FAutonomixWidgetActions::GetDisplayName() const { return FText::FromString(TEXT("Widget Actions")); }
EAutonomixActionCategory FAutonomixWidgetActions::GetCategory() const { return EAutonomixActionCategory::Blueprint; }
EAutonomixRiskLevel FAutonomixWidgetActions::GetDefaultRiskLevel() const { return EAutonomixRiskLevel::Medium; }
bool FAutonomixWidgetActions::CanUndo() const { return true; }
bool FAutonomixWidgetActions::UndoAction() { return false; }

TArray<FString> FAutonomixWidgetActions::GetSupportedToolNames() const
{
	return {
		TEXT("create_widget_blueprint"),
		TEXT("add_widget"),
		TEXT("set_widget_property"),
		TEXT("get_widget_tree"),
		TEXT("compile_widget_blueprint")
	};
}

bool FAutonomixWidgetActions::ValidateParams(const TSharedRef<FJsonObject>& Params, TArray<FString>& OutErrors) const
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

FAutonomixActionPlan FAutonomixWidgetActions::PreviewAction(const TSharedRef<FJsonObject>& Params)
{
	FAutonomixActionPlan Plan;
	FString AssetPath;
	Params->TryGetStringField(TEXT("asset_path"), AssetPath);
	Plan.Summary = FString::Printf(TEXT("Widget Blueprint operation at %s"), *AssetPath);

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

FAutonomixActionResult FAutonomixWidgetActions::ExecuteAction(const TSharedRef<FJsonObject>& Params)
{
	FScopedTransaction Transaction(FText::FromString(TEXT("Autonomix Widget Action")));

	FAutonomixActionResult Result;
	Result.bSuccess = false;

	FString ToolName;
	Params->TryGetStringField(TEXT("_tool_name"), ToolName);

	if (ToolName == TEXT("create_widget_blueprint"))  return ExecuteCreateWidgetBlueprint(Params, Result);
	if (ToolName == TEXT("add_widget"))               return ExecuteAddWidget(Params, Result);
	if (ToolName == TEXT("set_widget_property"))      return ExecuteSetWidgetProperty(Params, Result);
	if (ToolName == TEXT("get_widget_tree"))          return ExecuteGetWidgetTree(Params, Result);
	if (ToolName == TEXT("compile_widget_blueprint")) return ExecuteCompileWidgetBlueprint(Params, Result);

	Result.Errors.Add(FString::Printf(TEXT("Unknown Widget tool: '%s'. Supported: create_widget_blueprint, add_widget, set_widget_property, get_widget_tree, compile_widget_blueprint"), *ToolName));
	return Result;
}

// ============================================================================
// Helper: ResolveWidgetClass
// ============================================================================

UClass* FAutonomixWidgetActions::ResolveWidgetClass(const FString& ClassName)
{
	// Common aliases
	if (ClassName == TEXT("CanvasPanel"))     return UCanvasPanel::StaticClass();
	if (ClassName == TEXT("VerticalBox"))     return UVerticalBox::StaticClass();
	if (ClassName == TEXT("HorizontalBox"))   return UHorizontalBox::StaticClass();
	if (ClassName == TEXT("TextBlock"))       return UTextBlock::StaticClass();
	if (ClassName == TEXT("Button"))          return UButton::StaticClass();
	if (ClassName == TEXT("Image"))           return UImage::StaticClass();
	if (ClassName == TEXT("ScrollBox"))       return UScrollBox::StaticClass();
	if (ClassName == TEXT("SizeBox"))         return USizeBox::StaticClass();
	if (ClassName == TEXT("Overlay"))         return UOverlay::StaticClass();
	if (ClassName == TEXT("GridPanel"))       return UGridPanel::StaticClass();
	if (ClassName == TEXT("WidgetSwitcher")) return UWidgetSwitcher::StaticClass();
	if (ClassName == TEXT("ProgressBar"))     return UProgressBar::StaticClass();
	if (ClassName == TEXT("EditableTextBox")) return UEditableTextBox::StaticClass();
	if (ClassName == TEXT("CheckBox"))        return UCheckBox::StaticClass();
	if (ClassName == TEXT("Slider"))          return USlider::StaticClass();

	// Reflection search — try with U prefix
	UClass* Found = FindFirstObject<UClass>(*ClassName, EFindFirstObjectOptions::None);
	if (!Found)
		Found = FindFirstObject<UClass>(*(TEXT("U") + ClassName), EFindFirstObjectOptions::None);
	return Found;
}

// ============================================================================
// Helper: BuildWidgetTreeJson
// ============================================================================

FString FAutonomixWidgetActions::BuildWidgetTreeJson(UWidgetBlueprint* WidgetBlueprint)
{
	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("asset_path"), WidgetBlueprint->GetPathName());
	Root->SetStringField(TEXT("parent_class"), WidgetBlueprint->ParentClass ? WidgetBlueprint->ParentClass->GetName() : TEXT("UserWidget"));

	TArray<TSharedPtr<FJsonValue>> WidgetsArray;

	if (WidgetBlueprint->WidgetTree)
	{
		WidgetBlueprint->WidgetTree->ForEachWidget([&](UWidget* Widget)
		{
			if (!Widget) return;
			TSharedPtr<FJsonObject> WObj = MakeShared<FJsonObject>();
			WObj->SetStringField(TEXT("name"), Widget->GetName());
			WObj->SetStringField(TEXT("class"), Widget->GetClass()->GetName());

			// Is this widget the root?
			WObj->SetBoolField(TEXT("is_root"), Widget == WidgetBlueprint->WidgetTree->RootWidget);

			// Is it a panel (can contain children)?
			UPanelWidget* Panel = Cast<UPanelWidget>(Widget);
			if (Panel)
			{
				WObj->SetBoolField(TEXT("is_panel"), true);
				WObj->SetNumberField(TEXT("child_count"), Panel->GetChildrenCount());
			}
			else
			{
				WObj->SetBoolField(TEXT("is_panel"), false);
			}

			WidgetsArray.Add(MakeShared<FJsonValueObject>(WObj));
		});
	}

	Root->SetArrayField(TEXT("widgets"), WidgetsArray);

	FString OutputStr;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutputStr);
	FJsonSerializer::Serialize(Root.ToSharedRef(), Writer);
	return OutputStr;
}

// ============================================================================
// ExecuteCreateWidgetBlueprint
// ============================================================================

FAutonomixActionResult FAutonomixWidgetActions::ExecuteCreateWidgetBlueprint(const TSharedRef<FJsonObject>& Params, FAutonomixActionResult& Result)
{
	FString AssetPath = Params->GetStringField(TEXT("asset_path"));
	FString RootWidgetClassName = TEXT("CanvasPanel");
	Params->TryGetStringField(TEXT("root_widget_class"), RootWidgetClassName);

	FString PackagePath = FPackageName::GetLongPackagePath(AssetPath);
	FString AssetName   = FPackageName::GetShortName(AssetPath);

	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
	FString UniqueName, UniquePackagePath;
	AssetTools.CreateUniqueAssetName(AssetPath, TEXT(""), UniquePackagePath, UniqueName);

	UWidgetBlueprintFactory* Factory = NewObject<UWidgetBlueprintFactory>();
	Factory->ParentClass = UUserWidget::StaticClass();

	UObject* NewAsset = AssetTools.CreateAsset(AssetName, PackagePath, UWidgetBlueprint::StaticClass(), Factory);
	UWidgetBlueprint* NewWidget = Cast<UWidgetBlueprint>(NewAsset);

	if (!NewWidget)
	{
		Result.Errors.Add(FString::Printf(TEXT("Failed to create Widget Blueprint at '%s'. Check the path is valid and under /Game/."), *AssetPath));
		return Result;
	}

	// Set the root widget
	if (NewWidget->WidgetTree && RootWidgetClassName != TEXT("none") && !RootWidgetClassName.IsEmpty())
	{
		UClass* RootClass = ResolveWidgetClass(RootWidgetClassName);
		if (RootClass)
		{
			NewWidget->Modify();
			NewWidget->WidgetTree->Modify();
			UWidget* RootWidget = NewWidget->WidgetTree->ConstructWidget<UWidget>(RootClass, FName(*RootWidgetClassName));
			if (RootWidget)
			{
				NewWidget->WidgetTree->RootWidget = RootWidget;
				UE_LOG(LogAutonomix, Log, TEXT("WidgetActions: Set root widget '%s' on '%s'"), *RootWidgetClassName, *AssetName);
			}
		}
		else
		{
			Result.Warnings.Add(FString::Printf(TEXT("Root widget class '%s' not found — Widget Blueprint created without a root widget."), *RootWidgetClassName));
		}
	}

	// Compile
	FKismetEditorUtilities::CompileBlueprint(NewWidget, EBlueprintCompileOptions::SkipGarbageCollection);

	// Save
	UPackage* Package = NewWidget->GetOutermost();
	Package->MarkPackageDirty();
	FString PackageFilename;
	if (FPackageName::TryConvertLongPackageNameToFilename(Package->GetName(), PackageFilename, FPackageName::GetAssetPackageExtension()))
	{
		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Standalone;
		UPackage::SavePackage(Package, NewWidget, *PackageFilename, SaveArgs);
	}

	FAssetRegistryModule::AssetCreated(NewWidget);

	Result.bSuccess = true;
	Result.ResultMessage = FString::Printf(TEXT("Created Widget Blueprint '%s' with root widget '%s'. Use add_widget to populate the tree."), *AssetName, *RootWidgetClassName);
	Result.ModifiedAssets.Add(AssetPath);
	return Result;
}

// ============================================================================
// ExecuteAddWidget
// ============================================================================

FAutonomixActionResult FAutonomixWidgetActions::ExecuteAddWidget(const TSharedRef<FJsonObject>& Params, FAutonomixActionResult& Result)
{
	FString AssetPath      = Params->GetStringField(TEXT("asset_path"));
	FString WidgetClassName = Params->GetStringField(TEXT("widget_class"));
	FString WidgetName     = Params->GetStringField(TEXT("widget_name"));

	UWidgetBlueprint* WidgetBP = LoadObject<UWidgetBlueprint>(nullptr, *AssetPath);
	if (!WidgetBP)
	{
		Result.Errors.Add(FString::Printf(TEXT("Widget Blueprint not found: '%s'"), *AssetPath));
		return Result;
	}

	if (!WidgetBP->WidgetTree)
	{
		Result.Errors.Add(TEXT("Widget Blueprint has no WidgetTree — recreate the asset."));
		return Result;
	}

	UClass* WidgetClass = ResolveWidgetClass(WidgetClassName);
	if (!WidgetClass)
	{
		Result.Errors.Add(FString::Printf(TEXT("Widget class not found: '%s'. Common classes: CanvasPanel, VerticalBox, HorizontalBox, TextBlock, Button, Image, ScrollBox, SizeBox, Overlay, WidgetSwitcher, ProgressBar, Slider, CheckBox"), *WidgetClassName));
		return Result;
	}

	WidgetBP->Modify();
	WidgetBP->WidgetTree->Modify();

	UWidget* NewWidget = WidgetBP->WidgetTree->ConstructWidget<UWidget>(WidgetClass, FName(*WidgetName));
	if (!NewWidget)
	{
		Result.Errors.Add(FString::Printf(TEXT("Failed to construct widget of class '%s'."), *WidgetClassName));
		return Result;
	}

	// Attach to parent panel if specified
	FString ParentWidgetName;
	bool bAddedToParent = false;
	if (Params->TryGetStringField(TEXT("parent_widget"), ParentWidgetName) && !ParentWidgetName.IsEmpty())
	{
		UWidget* ParentWidgetRaw = WidgetBP->WidgetTree->FindWidget(FName(*ParentWidgetName));
		UPanelWidget* ParentPanel = Cast<UPanelWidget>(ParentWidgetRaw);
		if (ParentPanel)
		{
			ParentPanel->AddChild(NewWidget);
			bAddedToParent = true;
		}
		else if (ParentWidgetRaw)
		{
			Result.Warnings.Add(FString::Printf(TEXT("Parent widget '%s' is not a panel widget (not UPanelWidget). Supported panels: CanvasPanel, VerticalBox, HorizontalBox, ScrollBox, GridPanel, Overlay, WidgetSwitcher, SizeBox."), *ParentWidgetName));
		}
		else
		{
			Result.Warnings.Add(FString::Printf(TEXT("Parent widget '%s' not found in widget tree. Use get_widget_tree to see all widget names."), *ParentWidgetName));
		}
	}

	// If not added to a parent and no root exists, set as root
	if (!bAddedToParent && !WidgetBP->WidgetTree->RootWidget)
	{
		WidgetBP->WidgetTree->RootWidget = NewWidget;
	}
	else if (!bAddedToParent)
	{
		// Try to attach to root if it's a panel
		UPanelWidget* RootPanel = Cast<UPanelWidget>(WidgetBP->WidgetTree->RootWidget);
		if (RootPanel)
		{
			RootPanel->AddChild(NewWidget);
			Result.Warnings.Add(FString::Printf(TEXT("No parent_widget specified — attached '%s' to root panel '%s' by default."), *WidgetName, *RootPanel->GetName()));
		}
		else
		{
			Result.Warnings.Add(TEXT("Could not attach widget — specify parent_widget or set a panel as the root first."));
		}
	}

	// Compile
	FKismetEditorUtilities::CompileBlueprint(WidgetBP, EBlueprintCompileOptions::SkipGarbageCollection);
	WidgetBP->GetOutermost()->MarkPackageDirty();

	Result.bSuccess = true;
	Result.ResultMessage = FString::Printf(TEXT("Added widget '%s' (%s) to '%s'."), *WidgetName, *WidgetClassName, *AssetPath);
	Result.ModifiedAssets.Add(AssetPath);
	return Result;
}

// ============================================================================
// ExecuteSetWidgetProperty
// ============================================================================

FAutonomixActionResult FAutonomixWidgetActions::ExecuteSetWidgetProperty(const TSharedRef<FJsonObject>& Params, FAutonomixActionResult& Result)
{
	FString AssetPath   = Params->GetStringField(TEXT("asset_path"));
	FString WidgetName  = Params->GetStringField(TEXT("widget_name"));
	FString PropertyName = Params->GetStringField(TEXT("property_name"));
	FString PropertyValue = Params->GetStringField(TEXT("property_value"));

	UWidgetBlueprint* WidgetBP = LoadObject<UWidgetBlueprint>(nullptr, *AssetPath);
	if (!WidgetBP)
	{
		Result.Errors.Add(FString::Printf(TEXT("Widget Blueprint not found: '%s'"), *AssetPath));
		return Result;
	}

	if (!WidgetBP->WidgetTree)
	{
		Result.Errors.Add(TEXT("Widget Blueprint has no WidgetTree."));
		return Result;
	}

	UWidget* TargetWidget = WidgetBP->WidgetTree->FindWidget(FName(*WidgetName));
	if (!TargetWidget)
	{
		Result.Errors.Add(FString::Printf(TEXT("Widget '%s' not found. Use get_widget_tree to see all widget names."), *WidgetName));
		return Result;
	}

	FProperty* Prop = TargetWidget->GetClass()->FindPropertyByName(FName(*PropertyName));
	if (!Prop)
	{
		Result.Errors.Add(FString::Printf(TEXT("Property '%s' not found on widget '%s' (%s). Property names are exact C++ names. Common properties: Text, ColorAndOpacity, Font, Justification, Visibility, Padding, bIsEnabled, BackgroundColor, Brush, Percent, Value, bIsFocusable"), *PropertyName, *WidgetName, *TargetWidget->GetClass()->GetName()));
		return Result;
	}

	TargetWidget->Modify();
	void* PropAddr = Prop->ContainerPtrToValuePtr<void>(TargetWidget);
	Prop->ImportText_Direct(*PropertyValue, PropAddr, TargetWidget, PPF_None);

	UE_LOG(LogAutonomix, Log, TEXT("WidgetActions: Set property '%s' = '%s' on widget '%s'"), *PropertyName, *PropertyValue, *WidgetName);

	FKismetEditorUtilities::CompileBlueprint(WidgetBP, EBlueprintCompileOptions::SkipGarbageCollection);
	WidgetBP->GetOutermost()->MarkPackageDirty();

	Result.bSuccess = true;
	Result.ResultMessage = FString::Printf(TEXT("Set '%s.%s' = '%s' in '%s'."), *WidgetName, *PropertyName, *PropertyValue, *AssetPath);
	Result.ModifiedAssets.Add(AssetPath);
	return Result;
}

// ============================================================================
// ExecuteGetWidgetTree
// ============================================================================

FAutonomixActionResult FAutonomixWidgetActions::ExecuteGetWidgetTree(const TSharedRef<FJsonObject>& Params, FAutonomixActionResult& Result)
{
	FString AssetPath = Params->GetStringField(TEXT("asset_path"));

	UWidgetBlueprint* WidgetBP = LoadObject<UWidgetBlueprint>(nullptr, *AssetPath);
	if (!WidgetBP)
	{
		Result.Errors.Add(FString::Printf(TEXT("Widget Blueprint not found: '%s'"), *AssetPath));
		return Result;
	}

	FString Json = BuildWidgetTreeJson(WidgetBP);
	Result.bSuccess = true;
	Result.ResultMessage = Json;
	return Result;
}

// ============================================================================
// ExecuteCompileWidgetBlueprint
// ============================================================================

FAutonomixActionResult FAutonomixWidgetActions::ExecuteCompileWidgetBlueprint(const TSharedRef<FJsonObject>& Params, FAutonomixActionResult& Result)
{
	FString AssetPath = Params->GetStringField(TEXT("asset_path"));

	UWidgetBlueprint* WidgetBP = LoadObject<UWidgetBlueprint>(nullptr, *AssetPath);
	if (!WidgetBP)
	{
		Result.Errors.Add(FString::Printf(TEXT("Widget Blueprint not found: '%s'"), *AssetPath));
		return Result;
	}

	FCompilerResultsLog Log;
	FKismetEditorUtilities::CompileBlueprint(WidgetBP, EBlueprintCompileOptions::None, &Log);

	for (const TSharedRef<FTokenizedMessage>& Msg : Log.Messages)
	{
		FString MsgText = Msg->ToText().ToString();
		if (Msg->GetSeverity() == EMessageSeverity::Error)
			Result.Errors.Add(FString::Printf(TEXT("COMPILE ERROR: %s"), *MsgText));
		else if (Msg->GetSeverity() == EMessageSeverity::Warning)
			Result.Warnings.Add(FString::Printf(TEXT("COMPILE WARNING: %s"), *MsgText));
	}

	bool bOk = (WidgetBP->Status != BS_Error);
	Result.bSuccess = bOk;
	Result.ResultMessage = bOk
		? FString::Printf(TEXT("Widget Blueprint '%s' compiled successfully."), *AssetPath)
		: FString::Printf(TEXT("Widget Blueprint '%s' compiled with ERRORS."), *AssetPath);
	Result.ModifiedAssets.Add(AssetPath);
	return Result;
}
