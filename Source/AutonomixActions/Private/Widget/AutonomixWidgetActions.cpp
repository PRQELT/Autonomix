// Copyright Autonomix. All Rights Reserved.

#include "Widget/AutonomixWidgetActions.h"
#include "AutonomixCoreModule.h"

// UMG Runtime — Panels
#include "Blueprint/WidgetTree.h"
#include "Components/PanelWidget.h"
#include "Components/ContentWidget.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/ScrollBox.h"
#include "Components/ScrollBoxSlot.h"
#include "Components/SizeBox.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/GridPanel.h"
#include "Components/GridSlot.h"
#include "Components/UniformGridPanel.h"
#include "Components/UniformGridSlot.h"
#include "Components/WidgetSwitcher.h"
#include "Components/WidgetSwitcherSlot.h"
#include "Components/WrapBox.h"
#include "Components/WrapBoxSlot.h"
#include "Components/ScaleBox.h"
#include "Components/Border.h"
#include "Components/BackgroundBlur.h"
#include "Components/InvalidationBox.h"
#include "Components/RetainerBox.h"
#include "Components/NamedSlot.h"

// UMG Runtime — Leaf Widgets
#include "Components/TextBlock.h"
#include "Components/RichTextBlock.h"
#include "Components/Button.h"
#include "Components/Image.h"
#include "Components/ProgressBar.h"
#include "Components/EditableTextBox.h"
#include "Components/MultiLineEditableTextBox.h"
#include "Components/CheckBox.h"
#include "Components/Slider.h"
#include "Components/ComboBoxString.h"
#include "Components/SpinBox.h"
#include "Components/Spacer.h"
#include "Components/Throbber.h"
#include "Components/CircularThrobber.h"
#include "Components/ExpandableArea.h"
#include "Components/MenuAnchor.h"

// UMG Editor (Widget Blueprint factory and asset type)
#include "WidgetBlueprint.h"
#include "WidgetBlueprintFactory.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Kismet2/BlueprintEditorUtils.h"

// Blueprint Graph (for event binding)
#include "K2Node_ComponentBoundEvent.h"
#include "EdGraphSchema_K2.h"
#include "EdGraph/EdGraph.h"

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
#include "Engine/Texture2D.h"
#include "Engine/Font.h"
#include "Styling/SlateBrush.h"
#include "Styling/SlateTypes.h"
#include "Fonts/SlateFontInfo.h"
#include "Blueprint/UserWidget.h"

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
		TEXT("set_widget_slot"),
		TEXT("set_widget_property"),
		TEXT("set_widget_font"),
		TEXT("set_widget_brush"),
		TEXT("bind_widget_event"),
		TEXT("remove_widget"),
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

	FString AssetPath;
	Params->TryGetStringField(TEXT("asset_path"), AssetPath);
	if (AssetPath.IsEmpty())
	{
		OutErrors.Add(TEXT("asset_path cannot be empty. Provide a valid content path starting with /Game/, e.g. /Game/UI/WBP_MainMenu"));
		return false;
	}

	if (!AssetPath.StartsWith(TEXT("/Game/")))
	{
		OutErrors.Add(FString::Printf(TEXT("asset_path '%s' must start with /Game/. Example: /Game/UI/WBP_MainMenu"), *AssetPath));
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
	if (ToolName == TEXT("set_widget_slot"))          return ExecuteSetWidgetSlot(Params, Result);
	if (ToolName == TEXT("set_widget_property"))      return ExecuteSetWidgetProperty(Params, Result);
	if (ToolName == TEXT("set_widget_font"))          return ExecuteSetWidgetFont(Params, Result);
	if (ToolName == TEXT("set_widget_brush"))         return ExecuteSetWidgetBrush(Params, Result);
	if (ToolName == TEXT("bind_widget_event"))        return ExecuteBindWidgetEvent(Params, Result);
	if (ToolName == TEXT("remove_widget"))            return ExecuteRemoveWidget(Params, Result);
	if (ToolName == TEXT("get_widget_tree"))          return ExecuteGetWidgetTree(Params, Result);
	if (ToolName == TEXT("compile_widget_blueprint")) return ExecuteCompileWidgetBlueprint(Params, Result);

	Result.Errors.Add(FString::Printf(TEXT("Unknown Widget tool: '%s'. Supported: create_widget_blueprint, add_widget, set_widget_slot, set_widget_property, set_widget_font, set_widget_brush, bind_widget_event, remove_widget, get_widget_tree, compile_widget_blueprint"), *ToolName));
	return Result;
}

// ============================================================================
// Shared Helpers
// ============================================================================

UClass* FAutonomixWidgetActions::ResolveWidgetClass(const FString& ClassName)
{
	// Panel widgets
	if (ClassName == TEXT("CanvasPanel"))       return UCanvasPanel::StaticClass();
	if (ClassName == TEXT("VerticalBox"))       return UVerticalBox::StaticClass();
	if (ClassName == TEXT("HorizontalBox"))     return UHorizontalBox::StaticClass();
	if (ClassName == TEXT("ScrollBox"))         return UScrollBox::StaticClass();
	if (ClassName == TEXT("Overlay"))           return UOverlay::StaticClass();
	if (ClassName == TEXT("GridPanel"))         return UGridPanel::StaticClass();
	if (ClassName == TEXT("UniformGridPanel"))  return UUniformGridPanel::StaticClass();
	if (ClassName == TEXT("WidgetSwitcher"))    return UWidgetSwitcher::StaticClass();
	if (ClassName == TEXT("WrapBox"))           return UWrapBox::StaticClass();
	if (ClassName == TEXT("MenuAnchor"))        return UMenuAnchor::StaticClass();

	// Content widgets (single child)
	if (ClassName == TEXT("SizeBox"))           return USizeBox::StaticClass();
	if (ClassName == TEXT("ScaleBox"))          return UScaleBox::StaticClass();
	if (ClassName == TEXT("Border"))            return UBorder::StaticClass();
	if (ClassName == TEXT("Button"))            return UButton::StaticClass();
	if (ClassName == TEXT("BackgroundBlur"))    return UBackgroundBlur::StaticClass();
	if (ClassName == TEXT("InvalidationBox"))   return UInvalidationBox::StaticClass();
	if (ClassName == TEXT("RetainerBox"))       return URetainerBox::StaticClass();
	if (ClassName == TEXT("NamedSlot"))         return UNamedSlot::StaticClass();

	// Leaf widgets
	if (ClassName == TEXT("TextBlock"))         return UTextBlock::StaticClass();
	if (ClassName == TEXT("RichTextBlock"))     return URichTextBlock::StaticClass();
	if (ClassName == TEXT("Image"))             return UImage::StaticClass();
	if (ClassName == TEXT("ProgressBar"))       return UProgressBar::StaticClass();
	if (ClassName == TEXT("Slider"))            return USlider::StaticClass();
	if (ClassName == TEXT("CheckBox"))          return UCheckBox::StaticClass();
	if (ClassName == TEXT("EditableTextBox"))   return UEditableTextBox::StaticClass();
	if (ClassName == TEXT("MultiLineEditableTextBox")) return UMultiLineEditableTextBox::StaticClass();
	if (ClassName == TEXT("ComboBoxString"))    return UComboBoxString::StaticClass();
	if (ClassName == TEXT("SpinBox"))           return USpinBox::StaticClass();
	if (ClassName == TEXT("Spacer"))            return USpacer::StaticClass();
	if (ClassName == TEXT("Throbber"))          return UThrobber::StaticClass();
	if (ClassName == TEXT("CircularThrobber"))  return UCircularThrobber::StaticClass();
	if (ClassName == TEXT("ExpandableArea"))    return UExpandableArea::StaticClass();

	// Reflection search — try with and without U prefix
	UClass* Found = FindFirstObject<UClass>(*ClassName, EFindFirstObjectOptions::None);
	if (!Found)
		Found = FindFirstObject<UClass>(*(TEXT("U") + ClassName), EFindFirstObjectOptions::None);
	return Found;
}

UWidgetBlueprint* FAutonomixWidgetActions::LoadWidgetBP(const FString& AssetPath, FAutonomixActionResult& Result)
{
	UWidgetBlueprint* WidgetBP = LoadObject<UWidgetBlueprint>(nullptr, *AssetPath);
	if (!WidgetBP)
	{
		Result.Errors.Add(FString::Printf(TEXT("Widget Blueprint not found: '%s'. Verify the asset exists and the path starts with /Game/."), *AssetPath));
	}
	return WidgetBP;
}

UWidget* FAutonomixWidgetActions::FindWidgetByName(UWidgetBlueprint* WidgetBP, const FString& WidgetName, FAutonomixActionResult& Result)
{
	if (!WidgetBP->WidgetTree)
	{
		Result.Errors.Add(TEXT("Widget Blueprint has no WidgetTree — recreate the asset."));
		return nullptr;
	}

	UWidget* Widget = WidgetBP->WidgetTree->FindWidget(FName(*WidgetName));
	if (!Widget)
	{
		// Build helpful list of existing widget names
		TArray<UWidget*> AllWidgets;
		WidgetBP->WidgetTree->GetAllWidgets(AllWidgets);
		FString AvailableNames;
		for (UWidget* W : AllWidgets)
		{
			if (!AvailableNames.IsEmpty()) AvailableNames += TEXT(", ");
			AvailableNames += W->GetName();
		}
		Result.Errors.Add(FString::Printf(TEXT("Widget '%s' not found. Available widgets: [%s]. Use get_widget_tree to see all widget names."), *WidgetName, *AvailableNames));
	}
	return Widget;
}

bool FAutonomixWidgetActions::ParseVector2D(const FString& Str, FVector2D& OutVec)
{
	TArray<FString> Parts;
	Str.ParseIntoArray(Parts, TEXT(","));
	if (Parts.Num() >= 2)
	{
		OutVec.X = FCString::Atof(*Parts[0].TrimStartAndEnd());
		OutVec.Y = FCString::Atof(*Parts[1].TrimStartAndEnd());
		return true;
	}
	return false;
}

bool FAutonomixWidgetActions::ParseMargin(const FString& Str, FMargin& OutMargin)
{
	TArray<FString> Parts;
	Str.ParseIntoArray(Parts, TEXT(","));
	if (Parts.Num() >= 4)
	{
		OutMargin.Left   = FCString::Atof(*Parts[0].TrimStartAndEnd());
		OutMargin.Top    = FCString::Atof(*Parts[1].TrimStartAndEnd());
		OutMargin.Right  = FCString::Atof(*Parts[2].TrimStartAndEnd());
		OutMargin.Bottom = FCString::Atof(*Parts[3].TrimStartAndEnd());
		return true;
	}
	if (Parts.Num() == 1)
	{
		float Val = FCString::Atof(*Parts[0].TrimStartAndEnd());
		OutMargin = FMargin(Val);
		return true;
	}
	if (Parts.Num() == 2)
	{
		// Horizontal, Vertical
		float H = FCString::Atof(*Parts[0].TrimStartAndEnd());
		float V = FCString::Atof(*Parts[1].TrimStartAndEnd());
		OutMargin = FMargin(H, V, H, V);
		return true;
	}
	return false;
}

EHorizontalAlignment FAutonomixWidgetActions::ParseHAlign(const FString& Str)
{
	if (Str.Equals(TEXT("Left"),   ESearchCase::IgnoreCase)) return HAlign_Left;
	if (Str.Equals(TEXT("Center"), ESearchCase::IgnoreCase)) return HAlign_Center;
	if (Str.Equals(TEXT("Right"),  ESearchCase::IgnoreCase)) return HAlign_Right;
	if (Str.Equals(TEXT("Fill"),   ESearchCase::IgnoreCase)) return HAlign_Fill;
	return HAlign_Fill; // default
}

EVerticalAlignment FAutonomixWidgetActions::ParseVAlign(const FString& Str)
{
	if (Str.Equals(TEXT("Top"),    ESearchCase::IgnoreCase)) return VAlign_Top;
	if (Str.Equals(TEXT("Center"), ESearchCase::IgnoreCase)) return VAlign_Center;
	if (Str.Equals(TEXT("Bottom"), ESearchCase::IgnoreCase)) return VAlign_Bottom;
	if (Str.Equals(TEXT("Fill"),   ESearchCase::IgnoreCase)) return VAlign_Fill;
	return VAlign_Fill; // default
}

bool FAutonomixWidgetActions::ParseLinearColor(const FString& Str, FLinearColor& OutColor)
{
	// Accept format "(R=1.0,G=0.5,B=0.0,A=1.0)"
	FString Clean = Str;
	Clean.ReplaceInline(TEXT("("), TEXT(""));
	Clean.ReplaceInline(TEXT(")"), TEXT(""));

	TMap<FString, float> Values;
	TArray<FString> Parts;
	Clean.ParseIntoArray(Parts, TEXT(","));
	for (const FString& Part : Parts)
	{
		FString Key, Val;
		if (Part.Split(TEXT("="), &Key, &Val))
		{
			Values.Add(Key.TrimStartAndEnd(), FCString::Atof(*Val.TrimStartAndEnd()));
		}
	}

	if (Values.Contains(TEXT("R")))
	{
		OutColor.R = Values.FindRef(TEXT("R"));
		OutColor.G = Values.FindRef(TEXT("G"));
		OutColor.B = Values.FindRef(TEXT("B"));
		OutColor.A = Values.Contains(TEXT("A")) ? Values.FindRef(TEXT("A")) : 1.0f;
		return true;
	}
	return false;
}

void FAutonomixWidgetActions::CompileAndMarkDirty(UWidgetBlueprint* WidgetBP)
{
	FKismetEditorUtilities::CompileBlueprint(WidgetBP, EBlueprintCompileOptions::SkipGarbageCollection);
	WidgetBP->GetOutermost()->MarkPackageDirty();
}

// ============================================================================
// BuildWidgetTreeJson
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

			// Parent name
			UPanelWidget* ParentPanel = Widget->GetParent();
			if (ParentPanel)
			{
				WObj->SetStringField(TEXT("parent"), ParentPanel->GetName());
			}
			else if (Widget != WidgetBlueprint->WidgetTree->RootWidget)
			{
				WObj->SetStringField(TEXT("parent"), TEXT("(orphaned)"));
			}

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

			// Slot type
			if (Widget->Slot)
			{
				WObj->SetStringField(TEXT("slot_type"), Widget->Slot->GetClass()->GetName());
			}

			WidgetsArray.Add(MakeShared<FJsonValueObject>(WObj));
		});
	}

	Root->SetArrayField(TEXT("widgets"), WidgetsArray);

	int32 WidgetCount = WidgetsArray.Num();
	Root->SetNumberField(TEXT("total_widget_count"), WidgetCount);

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

	// Validate asset_path early
	if (AssetPath.IsEmpty())
	{
		Result.Errors.Add(TEXT("asset_path is empty. You MUST provide a valid content path starting with /Game/, e.g. /Game/UI/WBP_MainMenu."));
		return Result;
	}
	if (!AssetPath.StartsWith(TEXT("/Game/")))
	{
		Result.Errors.Add(FString::Printf(TEXT("asset_path '%s' must start with /Game/. Example: /Game/UI/WBP_MainMenu"), *AssetPath));
		return Result;
	}

	FString RootWidgetClassName = TEXT("CanvasPanel");
	Params->TryGetStringField(TEXT("root_widget_class"), RootWidgetClassName);

	FString ParentClassName = TEXT("UserWidget");
	Params->TryGetStringField(TEXT("parent_class"), ParentClassName);

	FString PackagePath = FPackageName::GetLongPackagePath(AssetPath);
	FString AssetName   = FPackageName::GetShortName(AssetPath);

	// Resolve parent class
	UClass* ParentClass = UUserWidget::StaticClass();
	if (!ParentClassName.IsEmpty() && ParentClassName != TEXT("UserWidget"))
	{
		UClass* FoundClass = FindFirstObject<UClass>(*ParentClassName, EFindFirstObjectOptions::None);
		if (!FoundClass)
			FoundClass = FindFirstObject<UClass>(*(TEXT("U") + ParentClassName), EFindFirstObjectOptions::None);
		if (FoundClass && FoundClass->IsChildOf(UUserWidget::StaticClass()))
		{
			ParentClass = FoundClass;
		}
		else
		{
			Result.Warnings.Add(FString::Printf(TEXT("Parent class '%s' not found or not a UUserWidget subclass. Using default UUserWidget."), *ParentClassName));
		}
	}

	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();

	UWidgetBlueprintFactory* Factory = NewObject<UWidgetBlueprintFactory>();
	Factory->ParentClass = ParentClass;

	UObject* NewAsset = AssetTools.CreateAsset(AssetName, PackagePath, UWidgetBlueprint::StaticClass(), Factory);
	UWidgetBlueprint* NewWidget = Cast<UWidgetBlueprint>(NewAsset);

	if (!NewWidget)
	{
		Result.Errors.Add(FString::Printf(TEXT("Failed to create Widget Blueprint at '%s'. Verify the path is valid, starts with /Game/, and the directory exists. Example: /Game/UI/WBP_MainMenu"), *AssetPath));
		return Result;
	}

	// Set the root widget
	if (NewWidget->WidgetTree && !RootWidgetClassName.IsEmpty() && RootWidgetClassName != TEXT("none"))
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
			Result.Warnings.Add(FString::Printf(TEXT("Root widget class '%s' not found — Widget Blueprint created without a root widget. Valid classes: CanvasPanel, VerticalBox, HorizontalBox, SizeBox, Overlay, GridPanel, ScrollBox, WrapBox."), *RootWidgetClassName));
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
	Result.ResultMessage = FString::Printf(TEXT("Created Widget Blueprint '%s' (parent: %s) with root widget '%s'. Next: use add_widget to populate the tree, then set_widget_slot to configure layout."), *AssetName, *ParentClass->GetName(), *RootWidgetClassName);
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

	UWidgetBlueprint* WidgetBP = LoadWidgetBP(AssetPath, Result);
	if (!WidgetBP) return Result;

	if (!WidgetBP->WidgetTree)
	{
		Result.Errors.Add(TEXT("Widget Blueprint has no WidgetTree — recreate the asset."));
		return Result;
	}

	UClass* WidgetClass = ResolveWidgetClass(WidgetClassName);
	if (!WidgetClass)
	{
		Result.Errors.Add(FString::Printf(TEXT("Widget class not found: '%s'. Valid classes: CanvasPanel, VerticalBox, HorizontalBox, TextBlock, Button, Image, ScrollBox, SizeBox, Overlay, WidgetSwitcher, ProgressBar, Slider, CheckBox, EditableTextBox, MultiLineEditableTextBox, ComboBoxString, SpinBox, Spacer, Border, BackgroundBlur, WrapBox, ScaleBox, RichTextBlock, Throbber, CircularThrobber, ExpandableArea, GridPanel, UniformGridPanel"), *WidgetClassName));
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
			UPanelSlot* Slot = ParentPanel->AddChild(NewWidget);
			bAddedToParent = true;

			// Report the slot type so the AI knows what properties are available
			if (Slot)
			{
				FString SlotType = Slot->GetClass()->GetName();
				Result.Warnings.Add(FString::Printf(TEXT("Widget added to '%s'. Slot type: %s. Use set_widget_slot to configure layout (anchors, padding, alignment)."), *ParentWidgetName, *SlotType));
			}
		}
		else if (ParentWidgetRaw)
		{
			// Check if it's a content widget (single child) — like Button, SizeBox, Border
			UContentWidget* ContentParent = Cast<UContentWidget>(ParentWidgetRaw);
			if (ContentParent)
			{
				if (ContentParent->GetChildrenCount() > 0)
				{
					Result.Warnings.Add(FString::Printf(TEXT("Content widget '%s' already has a child. Replacing it."), *ParentWidgetName));
					ContentParent->ClearChildren();
				}
				ContentParent->AddChild(NewWidget);
				bAddedToParent = true;
			}
			else
			{
				Result.Warnings.Add(FString::Printf(TEXT("Parent widget '%s' (%s) is not a panel or content widget. Supported parents: CanvasPanel, VerticalBox, HorizontalBox, ScrollBox, GridPanel, Overlay, WidgetSwitcher, SizeBox, Border, Button, WrapBox, ScaleBox, BackgroundBlur."), *ParentWidgetName, *ParentWidgetRaw->GetClass()->GetName()));
			}
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
		Result.Warnings.Add(FString::Printf(TEXT("No parent_widget specified and no root exists — '%s' set as root widget."), *WidgetName));
	}
	else if (!bAddedToParent)
	{
		// Try to attach to root if it's a panel
		UPanelWidget* RootPanel = Cast<UPanelWidget>(WidgetBP->WidgetTree->RootWidget);
		if (RootPanel)
		{
			RootPanel->AddChild(NewWidget);
			Result.Warnings.Add(FString::Printf(TEXT("No parent_widget specified — attached '%s' to root panel '%s' by default. Use set_widget_slot to configure layout."), *WidgetName, *RootPanel->GetName()));
		}
		else
		{
			Result.Warnings.Add(TEXT("Could not attach widget — specify parent_widget or set a panel as the root first."));
		}
	}

	CompileAndMarkDirty(WidgetBP);

	Result.bSuccess = true;
	Result.ResultMessage = FString::Printf(TEXT("Added widget '%s' (%s) to '%s'. IMPORTANT: If the parent is a CanvasPanel, you MUST call set_widget_slot to set anchors/position/size — otherwise the widget will be invisible (zero size at 0,0)."), *WidgetName, *WidgetClassName, *AssetPath);
	Result.ModifiedAssets.Add(AssetPath);
	return Result;
}

// ============================================================================
// ExecuteSetWidgetSlot
// ============================================================================

FAutonomixActionResult FAutonomixWidgetActions::ExecuteSetWidgetSlot(const TSharedRef<FJsonObject>& Params, FAutonomixActionResult& Result)
{
	FString AssetPath  = Params->GetStringField(TEXT("asset_path"));
	FString WidgetName = Params->GetStringField(TEXT("widget_name"));

	UWidgetBlueprint* WidgetBP = LoadWidgetBP(AssetPath, Result);
	if (!WidgetBP) return Result;

	UWidget* Widget = FindWidgetByName(WidgetBP, WidgetName, Result);
	if (!Widget) return Result;

	if (!Widget->Slot)
	{
		Result.Errors.Add(FString::Printf(TEXT("Widget '%s' has no slot. It may be the root widget (root widgets don't have slots) or not yet attached to a parent panel. Add it to a panel first via add_widget."), *WidgetName));
		return Result;
	}

	WidgetBP->Modify();
	Widget->Slot->Modify();

	FString AppliedSettings;

	// ====== CanvasPanelSlot ======
	UCanvasPanelSlot* CanvasSlot = Cast<UCanvasPanelSlot>(Widget->Slot);
	if (CanvasSlot)
	{
		FString AnchorsMinStr, AnchorsMaxStr, OffsetsStr, AlignmentStr;
		bool bAutoSize = false;
		int32 ZOrder = 0;

		if (Params->TryGetStringField(TEXT("anchors_min"), AnchorsMinStr))
		{
			FVector2D AnchorMin;
			if (ParseVector2D(AnchorsMinStr, AnchorMin))
			{
				FAnchors Anchors = CanvasSlot->GetAnchors();
				Anchors.Minimum = AnchorMin;

				// If max is also provided, set both together
				if (Params->TryGetStringField(TEXT("anchors_max"), AnchorsMaxStr))
				{
					FVector2D AnchorMax;
					if (ParseVector2D(AnchorsMaxStr, AnchorMax))
					{
						Anchors.Maximum = AnchorMax;
					}
				}
				CanvasSlot->SetAnchors(Anchors);
				AppliedSettings += FString::Printf(TEXT("anchors=(%.1f,%.1f)-(%.1f,%.1f) "), Anchors.Minimum.X, Anchors.Minimum.Y, Anchors.Maximum.X, Anchors.Maximum.Y);
			}
		}
		else if (Params->TryGetStringField(TEXT("anchors_max"), AnchorsMaxStr))
		{
			FVector2D AnchorMax;
			if (ParseVector2D(AnchorsMaxStr, AnchorMax))
			{
				FAnchors Anchors = CanvasSlot->GetAnchors();
				Anchors.Maximum = AnchorMax;
				CanvasSlot->SetAnchors(Anchors);
				AppliedSettings += FString::Printf(TEXT("anchors_max=(%.1f,%.1f) "), AnchorMax.X, AnchorMax.Y);
			}
		}

		if (Params->TryGetStringField(TEXT("offsets"), OffsetsStr))
		{
			FMargin Offsets;
			if (ParseMargin(OffsetsStr, Offsets))
			{
				CanvasSlot->SetOffsets(Offsets);
				AppliedSettings += FString::Printf(TEXT("offsets=(%.0f,%.0f,%.0f,%.0f) "), Offsets.Left, Offsets.Top, Offsets.Right, Offsets.Bottom);
			}
		}

		if (Params->TryGetStringField(TEXT("alignment"), AlignmentStr))
		{
			FVector2D Alignment;
			if (ParseVector2D(AlignmentStr, Alignment))
			{
				CanvasSlot->SetAlignment(Alignment);
				AppliedSettings += FString::Printf(TEXT("alignment=(%.1f,%.1f) "), Alignment.X, Alignment.Y);
			}
		}

		if (Params->TryGetBoolField(TEXT("auto_size"), bAutoSize))
		{
			CanvasSlot->SetAutoSize(bAutoSize);
			AppliedSettings += FString::Printf(TEXT("auto_size=%s "), bAutoSize ? TEXT("true") : TEXT("false"));
		}

		if (Params->TryGetNumberField(TEXT("z_order"), ZOrder))
		{
			CanvasSlot->SetZOrder(ZOrder);
			AppliedSettings += FString::Printf(TEXT("z_order=%d "), ZOrder);
		}
	}

	// ====== VerticalBoxSlot ======
	UVerticalBoxSlot* VBSlot = Cast<UVerticalBoxSlot>(Widget->Slot);
	if (VBSlot)
	{
		FString PaddingStr, SizeRuleStr, HAlignStr, VAlignStr;

		if (Params->TryGetStringField(TEXT("padding"), PaddingStr))
		{
			FMargin Padding;
			if (ParseMargin(PaddingStr, Padding))
			{
				VBSlot->SetPadding(Padding);
				AppliedSettings += FString::Printf(TEXT("padding=(%.0f,%.0f,%.0f,%.0f) "), Padding.Left, Padding.Top, Padding.Right, Padding.Bottom);
			}
		}

		if (Params->TryGetStringField(TEXT("size_rule"), SizeRuleStr))
		{
			if (SizeRuleStr.Equals(TEXT("Fill"), ESearchCase::IgnoreCase))
			{
				VBSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
				AppliedSettings += TEXT("size=Fill ");
			}
			else
			{
				VBSlot->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));
				AppliedSettings += TEXT("size=Auto ");
			}
		}

		if (Params->TryGetStringField(TEXT("h_align"), HAlignStr))
		{
			VBSlot->SetHorizontalAlignment(ParseHAlign(HAlignStr));
			AppliedSettings += FString::Printf(TEXT("h_align=%s "), *HAlignStr);
		}

		if (Params->TryGetStringField(TEXT("v_align"), VAlignStr))
		{
			VBSlot->SetVerticalAlignment(ParseVAlign(VAlignStr));
			AppliedSettings += FString::Printf(TEXT("v_align=%s "), *VAlignStr);
		}
	}

	// ====== HorizontalBoxSlot ======
	UHorizontalBoxSlot* HBSlot = Cast<UHorizontalBoxSlot>(Widget->Slot);
	if (HBSlot)
	{
		FString PaddingStr, SizeRuleStr, HAlignStr, VAlignStr;

		if (Params->TryGetStringField(TEXT("padding"), PaddingStr))
		{
			FMargin Padding;
			if (ParseMargin(PaddingStr, Padding))
			{
				HBSlot->SetPadding(Padding);
				AppliedSettings += FString::Printf(TEXT("padding=(%.0f,%.0f,%.0f,%.0f) "), Padding.Left, Padding.Top, Padding.Right, Padding.Bottom);
			}
		}

		if (Params->TryGetStringField(TEXT("size_rule"), SizeRuleStr))
		{
			if (SizeRuleStr.Equals(TEXT("Fill"), ESearchCase::IgnoreCase))
			{
				HBSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
				AppliedSettings += TEXT("size=Fill ");
			}
			else
			{
				HBSlot->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));
				AppliedSettings += TEXT("size=Auto ");
			}
		}

		if (Params->TryGetStringField(TEXT("h_align"), HAlignStr))
		{
			HBSlot->SetHorizontalAlignment(ParseHAlign(HAlignStr));
			AppliedSettings += FString::Printf(TEXT("h_align=%s "), *HAlignStr);
		}

		if (Params->TryGetStringField(TEXT("v_align"), VAlignStr))
		{
			HBSlot->SetVerticalAlignment(ParseVAlign(VAlignStr));
			AppliedSettings += FString::Printf(TEXT("v_align=%s "), *VAlignStr);
		}
	}

	// ====== OverlaySlot ======
	UOverlaySlot* OverlaySlotPtr = Cast<UOverlaySlot>(Widget->Slot);
	if (OverlaySlotPtr)
	{
		FString PaddingStr, HAlignStr, VAlignStr;

		if (Params->TryGetStringField(TEXT("padding"), PaddingStr))
		{
			FMargin Padding;
			if (ParseMargin(PaddingStr, Padding))
			{
				OverlaySlotPtr->SetPadding(Padding);
				AppliedSettings += FString::Printf(TEXT("padding=(%.0f,%.0f,%.0f,%.0f) "), Padding.Left, Padding.Top, Padding.Right, Padding.Bottom);
			}
		}

		if (Params->TryGetStringField(TEXT("h_align"), HAlignStr))
		{
			OverlaySlotPtr->SetHorizontalAlignment(ParseHAlign(HAlignStr));
			AppliedSettings += FString::Printf(TEXT("h_align=%s "), *HAlignStr);
		}

		if (Params->TryGetStringField(TEXT("v_align"), VAlignStr))
		{
			OverlaySlotPtr->SetVerticalAlignment(ParseVAlign(VAlignStr));
			AppliedSettings += FString::Printf(TEXT("v_align=%s "), *VAlignStr);
		}
	}

	// ====== GridSlot ======
	UGridSlot* GridSlotPtr = Cast<UGridSlot>(Widget->Slot);
	if (GridSlotPtr)
	{
		int32 Row = 0, Column = 0, RowSpan = 1, ColumnSpan = 1;
		FString PaddingStr, HAlignStr, VAlignStr;

		if (Params->TryGetNumberField(TEXT("row"), Row))
		{
			GridSlotPtr->SetRow(Row);
			AppliedSettings += FString::Printf(TEXT("row=%d "), Row);
		}
		if (Params->TryGetNumberField(TEXT("column"), Column))
		{
			GridSlotPtr->SetColumn(Column);
			AppliedSettings += FString::Printf(TEXT("column=%d "), Column);
		}
		if (Params->TryGetNumberField(TEXT("row_span"), RowSpan))
		{
			GridSlotPtr->SetRowSpan(RowSpan);
			AppliedSettings += FString::Printf(TEXT("row_span=%d "), RowSpan);
		}
		if (Params->TryGetNumberField(TEXT("column_span"), ColumnSpan))
		{
			GridSlotPtr->SetColumnSpan(ColumnSpan);
			AppliedSettings += FString::Printf(TEXT("column_span=%d "), ColumnSpan);
		}

		if (Params->TryGetStringField(TEXT("padding"), PaddingStr))
		{
			FMargin Padding;
			if (ParseMargin(PaddingStr, Padding))
			{
				GridSlotPtr->SetPadding(Padding);
				AppliedSettings += FString::Printf(TEXT("padding=(%.0f,%.0f,%.0f,%.0f) "), Padding.Left, Padding.Top, Padding.Right, Padding.Bottom);
			}
		}

		if (Params->TryGetStringField(TEXT("h_align"), HAlignStr))
		{
			GridSlotPtr->SetHorizontalAlignment(ParseHAlign(HAlignStr));
			AppliedSettings += FString::Printf(TEXT("h_align=%s "), *HAlignStr);
		}
		if (Params->TryGetStringField(TEXT("v_align"), VAlignStr))
		{
			GridSlotPtr->SetVerticalAlignment(ParseVAlign(VAlignStr));
			AppliedSettings += FString::Printf(TEXT("v_align=%s "), *VAlignStr);
		}
	}

	// ====== ScrollBoxSlot ======
	UScrollBoxSlot* ScrollSlot = Cast<UScrollBoxSlot>(Widget->Slot);
	if (ScrollSlot)
	{
		FString PaddingStr, SizeRuleStr, HAlignStr;

		if (Params->TryGetStringField(TEXT("padding"), PaddingStr))
		{
			FMargin Padding;
			if (ParseMargin(PaddingStr, Padding))
			{
				ScrollSlot->SetPadding(Padding);
				AppliedSettings += FString::Printf(TEXT("padding=(%.0f,%.0f,%.0f,%.0f) "), Padding.Left, Padding.Top, Padding.Right, Padding.Bottom);
			}
		}

		if (Params->TryGetStringField(TEXT("h_align"), HAlignStr))
		{
			ScrollSlot->SetHorizontalAlignment(ParseHAlign(HAlignStr));
			AppliedSettings += FString::Printf(TEXT("h_align=%s "), *HAlignStr);
		}
	}

	// ====== WrapBoxSlot ======
	UWrapBoxSlot* WrapSlot = Cast<UWrapBoxSlot>(Widget->Slot);
	if (WrapSlot)
	{
		FString PaddingStr, HAlignStr, VAlignStr;
		bool bFillEmptySpace = false;

		if (Params->TryGetStringField(TEXT("padding"), PaddingStr))
		{
			FMargin Padding;
			if (ParseMargin(PaddingStr, Padding))
			{
				WrapSlot->SetPadding(Padding);
				AppliedSettings += FString::Printf(TEXT("padding=(%.0f,%.0f,%.0f,%.0f) "), Padding.Left, Padding.Top, Padding.Right, Padding.Bottom);
			}
		}

		if (Params->TryGetStringField(TEXT("h_align"), HAlignStr))
		{
			WrapSlot->SetHorizontalAlignment(ParseHAlign(HAlignStr));
			AppliedSettings += FString::Printf(TEXT("h_align=%s "), *HAlignStr);
		}

		if (Params->TryGetStringField(TEXT("v_align"), VAlignStr))
		{
			WrapSlot->SetVerticalAlignment(ParseVAlign(VAlignStr));
			AppliedSettings += FString::Printf(TEXT("v_align=%s "), *VAlignStr);
		}

		if (Params->TryGetBoolField(TEXT("fill_empty_space"), bFillEmptySpace))
		{
			WrapSlot->SetFillEmptySpace(bFillEmptySpace);
			AppliedSettings += FString::Printf(TEXT("fill=%s "), bFillEmptySpace ? TEXT("true") : TEXT("false"));
		}
	}

	if (AppliedSettings.IsEmpty())
	{
		FString SlotType = Widget->Slot ? Widget->Slot->GetClass()->GetName() : TEXT("unknown");
		Result.Errors.Add(FString::Printf(TEXT("No slot properties were applied. Widget '%s' has slot type '%s'. Check that you're providing the correct properties for this slot type."), *WidgetName, *SlotType));
		return Result;
	}

	CompileAndMarkDirty(WidgetBP);

	Result.bSuccess = true;
	Result.ResultMessage = FString::Printf(TEXT("Configured slot on '%s': %s"), *WidgetName, *AppliedSettings.TrimEnd());
	Result.ModifiedAssets.Add(AssetPath);
	return Result;
}

// ============================================================================
// ExecuteSetWidgetProperty
// ============================================================================

FAutonomixActionResult FAutonomixWidgetActions::ExecuteSetWidgetProperty(const TSharedRef<FJsonObject>& Params, FAutonomixActionResult& Result)
{
	FString AssetPath     = Params->GetStringField(TEXT("asset_path"));
	FString WidgetName    = Params->GetStringField(TEXT("widget_name"));
	FString PropertyName  = Params->GetStringField(TEXT("property_name"));
	FString PropertyValue = Params->GetStringField(TEXT("property_value"));

	UWidgetBlueprint* WidgetBP = LoadWidgetBP(AssetPath, Result);
	if (!WidgetBP) return Result;

	UWidget* TargetWidget = FindWidgetByName(WidgetBP, WidgetName, Result);
	if (!TargetWidget) return Result;

	// Special handling for Text property on TextBlock — use SetText for proper FText
	if (PropertyName == TEXT("Text"))
	{
		UTextBlock* TextBlock = Cast<UTextBlock>(TargetWidget);
		if (TextBlock)
		{
			TextBlock->Modify();
			TextBlock->SetText(FText::FromString(PropertyValue));
			CompileAndMarkDirty(WidgetBP);

			Result.bSuccess = true;
			Result.ResultMessage = FString::Printf(TEXT("Set '%s.Text' = '%s' in '%s'."), *WidgetName, *PropertyValue, *AssetPath);
			Result.ModifiedAssets.Add(AssetPath);
			return Result;
		}

		// Also handle EditableTextBox Text
		UEditableTextBox* EditBox = Cast<UEditableTextBox>(TargetWidget);
		if (EditBox)
		{
			EditBox->Modify();
			EditBox->SetText(FText::FromString(PropertyValue));
			CompileAndMarkDirty(WidgetBP);

			Result.bSuccess = true;
			Result.ResultMessage = FString::Printf(TEXT("Set '%s.Text' = '%s' in '%s'."), *WidgetName, *PropertyValue, *AssetPath);
			Result.ModifiedAssets.Add(AssetPath);
			return Result;
		}
	}

	// Special handling for HintText on EditableTextBox
	if (PropertyName == TEXT("HintText"))
	{
		UEditableTextBox* EditBox = Cast<UEditableTextBox>(TargetWidget);
		if (EditBox)
		{
			EditBox->Modify();
			EditBox->SetHintText(FText::FromString(PropertyValue));
			CompileAndMarkDirty(WidgetBP);

			Result.bSuccess = true;
			Result.ResultMessage = FString::Printf(TEXT("Set '%s.HintText' = '%s' in '%s'."), *WidgetName, *PropertyValue, *AssetPath);
			Result.ModifiedAssets.Add(AssetPath);
			return Result;
		}
	}

	// Generic reflection path
	FProperty* Prop = TargetWidget->GetClass()->FindPropertyByName(FName(*PropertyName));
	if (!Prop)
	{
		Result.Errors.Add(FString::Printf(TEXT("Property '%s' not found on widget '%s' (%s). Property names are exact C++ names (case-sensitive). Common properties: Text, ColorAndOpacity, Font, Justification, Visibility, Padding, bIsEnabled, BackgroundColor, Brush, Percent, Value, IsFocusable, RenderOpacity, Clipping"), *PropertyName, *WidgetName, *TargetWidget->GetClass()->GetName()));
		return Result;
	}

	TargetWidget->Modify();
	void* PropAddr = Prop->ContainerPtrToValuePtr<void>(TargetWidget);
	Prop->ImportText_Direct(*PropertyValue, PropAddr, TargetWidget, PPF_None);

	UE_LOG(LogAutonomix, Log, TEXT("WidgetActions: Set property '%s' = '%s' on widget '%s'"), *PropertyName, *PropertyValue, *WidgetName);

	CompileAndMarkDirty(WidgetBP);

	Result.bSuccess = true;
	Result.ResultMessage = FString::Printf(TEXT("Set '%s.%s' = '%s' in '%s'."), *WidgetName, *PropertyName, *PropertyValue, *AssetPath);
	Result.ModifiedAssets.Add(AssetPath);
	return Result;
}

// ============================================================================
// ExecuteSetWidgetFont
// ============================================================================

FAutonomixActionResult FAutonomixWidgetActions::ExecuteSetWidgetFont(const TSharedRef<FJsonObject>& Params, FAutonomixActionResult& Result)
{
	FString AssetPath  = Params->GetStringField(TEXT("asset_path"));
	FString WidgetName = Params->GetStringField(TEXT("widget_name"));

	UWidgetBlueprint* WidgetBP = LoadWidgetBP(AssetPath, Result);
	if (!WidgetBP) return Result;

	UWidget* Widget = FindWidgetByName(WidgetBP, WidgetName, Result);
	if (!Widget) return Result;

	WidgetBP->Modify();
	Widget->Modify();

	// Get the current font info from the widget
	FSlateFontInfo FontInfo;

	UTextBlock* TextBlock = Cast<UTextBlock>(Widget);
	if (TextBlock)
	{
		FontInfo = TextBlock->GetFont();
	}
	else
	{
		// Try to get Font property via reflection
		FProperty* FontProp = Widget->GetClass()->FindPropertyByName(FName("Font"));
		if (FontProp)
		{
			FSlateFontInfo* FontPtr = FontProp->ContainerPtrToValuePtr<FSlateFontInfo>(Widget);
			if (FontPtr)
			{
				FontInfo = *FontPtr;
			}
		}
		else
		{
			Result.Errors.Add(FString::Printf(TEXT("Widget '%s' (%s) does not have a Font property. set_widget_font only works on text widgets: TextBlock, EditableTextBox, RichTextBlock, MultiLineEditableTextBox."), *WidgetName, *Widget->GetClass()->GetName()));
			return Result;
		}
	}

	// Apply font family
	FString FontFamily;
	if (Params->TryGetStringField(TEXT("font_family"), FontFamily) && !FontFamily.IsEmpty())
	{
		if (FontFamily.Equals(TEXT("Roboto"), ESearchCase::IgnoreCase))
		{
			// Use the engine's default Roboto font
			UObject* FontObj = LoadObject<UFont>(nullptr, TEXT("/Engine/EngineFonts/Roboto.Roboto"));
			if (FontObj)
			{
				FontInfo.FontObject = FontObj;
			}
		}
		else if (FontFamily.Equals(TEXT("DroidSansMono"), ESearchCase::IgnoreCase))
		{
			UObject* FontObj = LoadObject<UFont>(nullptr, TEXT("/Engine/EngineFonts/DroidSansMono.DroidSansMono"));
			if (FontObj)
			{
				FontInfo.FontObject = FontObj;
			}
		}
		else if (FontFamily.StartsWith(TEXT("/Game/")) || FontFamily.StartsWith(TEXT("/Engine/")))
		{
			// Asset path
			UObject* FontObj = LoadObject<UFont>(nullptr, *FontFamily);
			if (FontObj)
			{
				FontInfo.FontObject = FontObj;
			}
			else
			{
				Result.Warnings.Add(FString::Printf(TEXT("Font asset '%s' not found. Using default Roboto."), *FontFamily));
			}
		}
	}

	// Apply font size
	int32 FontSize = 12;
	if (Params->TryGetNumberField(TEXT("font_size"), FontSize))
	{
		FontInfo.Size = FontSize;
	}

	// Apply typeface
	FString Typeface;
	if (Params->TryGetStringField(TEXT("typeface"), Typeface) && !Typeface.IsEmpty())
	{
		FontInfo.TypefaceFontName = FName(*Typeface);
	}

	// Set the font back on the widget
	if (TextBlock)
	{
		TextBlock->SetFont(FontInfo);
	}
	else
	{
		FProperty* FontProp = Widget->GetClass()->FindPropertyByName(FName("Font"));
		if (FontProp)
		{
			FSlateFontInfo* FontPtr = FontProp->ContainerPtrToValuePtr<FSlateFontInfo>(Widget);
			if (FontPtr)
			{
				*FontPtr = FontInfo;
			}
		}
	}

	// Apply color (TextBlock only)
	FString ColorStr;
	if (TextBlock && Params->TryGetStringField(TEXT("color"), ColorStr))
	{
		FLinearColor Color;
		if (ParseLinearColor(ColorStr, Color))
		{
			TextBlock->SetColorAndOpacity(FSlateColor(Color));
		}
	}

	// Apply shadow
	FString ShadowOffsetStr;
	if (TextBlock && Params->TryGetStringField(TEXT("shadow_offset"), ShadowOffsetStr))
	{
		FVector2D ShadowOffset;
		if (ParseVector2D(ShadowOffsetStr, ShadowOffset))
		{
			TextBlock->SetShadowOffset(ShadowOffset);
		}
	}

	FString ShadowColorStr;
	if (TextBlock && Params->TryGetStringField(TEXT("shadow_color"), ShadowColorStr))
	{
		FLinearColor ShadowColor;
		if (ParseLinearColor(ShadowColorStr, ShadowColor))
		{
			TextBlock->SetShadowColorAndOpacity(ShadowColor);
		}
	}

	CompileAndMarkDirty(WidgetBP);

	Result.bSuccess = true;
	Result.ResultMessage = FString::Printf(TEXT("Set font on '%s': size=%d%s%s."),
		*WidgetName, static_cast<int32>(FontInfo.Size),
		!Typeface.IsEmpty() ? *FString::Printf(TEXT(", typeface=%s"), *Typeface) : TEXT(""),
		!FontFamily.IsEmpty() ? *FString::Printf(TEXT(", family=%s"), *FontFamily) : TEXT(""));
	Result.ModifiedAssets.Add(AssetPath);
	return Result;
}

// ============================================================================
// ExecuteSetWidgetBrush
// ============================================================================

FAutonomixActionResult FAutonomixWidgetActions::ExecuteSetWidgetBrush(const TSharedRef<FJsonObject>& Params, FAutonomixActionResult& Result)
{
	FString AssetPath   = Params->GetStringField(TEXT("asset_path"));
	FString WidgetName  = Params->GetStringField(TEXT("widget_name"));

	UWidgetBlueprint* WidgetBP = LoadWidgetBP(AssetPath, Result);
	if (!WidgetBP) return Result;

	UWidget* Widget = FindWidgetByName(WidgetBP, WidgetName, Result);
	if (!Widget) return Result;

	WidgetBP->Modify();
	Widget->Modify();

	FString BrushTarget = TEXT("Brush");
	Params->TryGetStringField(TEXT("brush_target"), BrushTarget);

	// Build the brush
	FSlateBrush NewBrush;

	// Load texture if provided
	FString TexturePath;
	if (Params->TryGetStringField(TEXT("texture_path"), TexturePath) && !TexturePath.IsEmpty())
	{
		UTexture2D* Texture = LoadObject<UTexture2D>(nullptr, *TexturePath);
		if (Texture)
		{
			NewBrush.SetResourceObject(Texture);
		}
		else
		{
			Result.Warnings.Add(FString::Printf(TEXT("Texture '%s' not found. Creating solid color brush instead."), *TexturePath));
		}
	}

	// Set tint color
	FString TintColorStr;
	if (Params->TryGetStringField(TEXT("tint_color"), TintColorStr))
	{
		FLinearColor TintColor;
		if (ParseLinearColor(TintColorStr, TintColor))
		{
			NewBrush.TintColor = FSlateColor(TintColor);
		}
	}

	// Set image size
	FString ImageSizeStr;
	if (Params->TryGetStringField(TEXT("image_size"), ImageSizeStr))
	{
		FVector2D ImageSize;
		// Parse "(X=64,Y=64)" format
		FString Clean = ImageSizeStr;
		Clean.ReplaceInline(TEXT("("), TEXT(""));
		Clean.ReplaceInline(TEXT(")"), TEXT(""));
		Clean.ReplaceInline(TEXT("X="), TEXT(""));
		Clean.ReplaceInline(TEXT("Y="), TEXT(""));
		if (ParseVector2D(Clean, ImageSize))
		{
			NewBrush.ImageSize = ImageSize;
		}
	}

	// Set draw type
	FString DrawAsStr;
	if (Params->TryGetStringField(TEXT("draw_as"), DrawAsStr))
	{
		if (DrawAsStr.Equals(TEXT("Box"), ESearchCase::IgnoreCase))
			NewBrush.DrawAs = ESlateBrushDrawType::Box;
		else if (DrawAsStr.Equals(TEXT("Border"), ESearchCase::IgnoreCase))
			NewBrush.DrawAs = ESlateBrushDrawType::Border;
		else if (DrawAsStr.Equals(TEXT("Image"), ESearchCase::IgnoreCase))
			NewBrush.DrawAs = ESlateBrushDrawType::Image;
		else if (DrawAsStr.Equals(TEXT("NoDrawType"), ESearchCase::IgnoreCase))
			NewBrush.DrawAs = ESlateBrushDrawType::NoDrawType;
	}

	// Set 9-slice margin
	FString MarginStr;
	if (Params->TryGetStringField(TEXT("margin"), MarginStr))
	{
		FMargin BrushMargin;
		if (ParseMargin(MarginStr, BrushMargin))
		{
			NewBrush.Margin = BrushMargin;
		}
	}

	// Apply brush to the correct target
	bool bApplied = false;

	// === Image widget ===
	UImage* ImageWidget = Cast<UImage>(Widget);
	if (ImageWidget && BrushTarget.Equals(TEXT("Brush"), ESearchCase::IgnoreCase))
	{
		ImageWidget->SetBrush(NewBrush);
		bApplied = true;
	}

	// === Button widget ===
	UButton* ButtonWidget = Cast<UButton>(Widget);
	if (ButtonWidget)
	{
		FButtonStyle ButtonStyle = ButtonWidget->GetStyle();
		
		if (BrushTarget.Equals(TEXT("Normal"), ESearchCase::IgnoreCase))
		{
			ButtonStyle.Normal = NewBrush;
			bApplied = true;
		}
		else if (BrushTarget.Equals(TEXT("Hovered"), ESearchCase::IgnoreCase))
		{
			ButtonStyle.Hovered = NewBrush;
			bApplied = true;
		}
		else if (BrushTarget.Equals(TEXT("Pressed"), ESearchCase::IgnoreCase))
		{
			ButtonStyle.Pressed = NewBrush;
			bApplied = true;
		}
		else if (BrushTarget.Equals(TEXT("Disabled"), ESearchCase::IgnoreCase))
		{
			ButtonStyle.Disabled = NewBrush;
			bApplied = true;
		}
		
		if (bApplied)
		{
			ButtonWidget->SetStyle(ButtonStyle);
		}
	}

	// === Border widget ===
	UBorder* BorderWidget = Cast<UBorder>(Widget);
	if (BorderWidget && BrushTarget.Equals(TEXT("Background"), ESearchCase::IgnoreCase))
	{
		BorderWidget->SetBrush(NewBrush);
		bApplied = true;
	}

	// === ProgressBar widget ===
	UProgressBar* PBWidget = Cast<UProgressBar>(Widget);
	if (PBWidget && BrushTarget.Equals(TEXT("FillImage"), ESearchCase::IgnoreCase))
	{
		FProgressBarStyle PBStyle = PBWidget->GetWidgetStyle();
		PBStyle.FillImage = NewBrush;
		PBWidget->SetWidgetStyle(PBStyle);
		bApplied = true;
	}

	if (!bApplied)
	{
		// Try generic reflection as fallback
		FProperty* BrushProp = Widget->GetClass()->FindPropertyByName(FName(*BrushTarget));
		if (BrushProp)
		{
			FSlateBrush* BrushPtr = BrushProp->ContainerPtrToValuePtr<FSlateBrush>(Widget);
			if (BrushPtr)
			{
				*BrushPtr = NewBrush;
				bApplied = true;
			}
		}

		if (!bApplied)
		{
			Result.Errors.Add(FString::Printf(TEXT("Could not apply brush to '%s.%s'. Widget type '%s' doesn't support brush_target '%s'. For Image use 'Brush', for Button use 'Normal'/'Hovered'/'Pressed'/'Disabled', for Border use 'Background', for ProgressBar use 'FillImage'."), *WidgetName, *BrushTarget, *Widget->GetClass()->GetName(), *BrushTarget));
			return Result;
		}
	}

	CompileAndMarkDirty(WidgetBP);

	Result.bSuccess = true;
	Result.ResultMessage = FString::Printf(TEXT("Set brush '%s' on '%s' in '%s'.%s"),
		*BrushTarget, *WidgetName, *AssetPath,
		TexturePath.IsEmpty() ? TEXT(" (solid color brush)") : *FString::Printf(TEXT(" (texture: %s)"), *TexturePath));
	Result.ModifiedAssets.Add(AssetPath);
	return Result;
}

// ============================================================================
// ExecuteBindWidgetEvent
// ============================================================================

FAutonomixActionResult FAutonomixWidgetActions::ExecuteBindWidgetEvent(const TSharedRef<FJsonObject>& Params, FAutonomixActionResult& Result)
{
	FString AssetPath  = Params->GetStringField(TEXT("asset_path"));
	FString WidgetName = Params->GetStringField(TEXT("widget_name"));
	FString EventName  = Params->GetStringField(TEXT("event_name"));

	UWidgetBlueprint* WidgetBP = LoadWidgetBP(AssetPath, Result);
	if (!WidgetBP) return Result;

	UWidget* Widget = FindWidgetByName(WidgetBP, WidgetName, Result);
	if (!Widget) return Result;

	// Find the delegate property on the widget class
	FMulticastDelegateProperty* DelegateProp = CastField<FMulticastDelegateProperty>(
		Widget->GetClass()->FindPropertyByName(FName(*EventName))
	);

	if (!DelegateProp)
	{
		// Build list of available events for this widget type
		FString AvailableEvents;
		for (TFieldIterator<FMulticastDelegateProperty> It(Widget->GetClass()); It; ++It)
		{
			if (!AvailableEvents.IsEmpty()) AvailableEvents += TEXT(", ");
			AvailableEvents += It->GetName();
		}
		Result.Errors.Add(FString::Printf(TEXT("Event '%s' not found on widget '%s' (%s). Available events: [%s]"), *EventName, *WidgetName, *Widget->GetClass()->GetName(), *AvailableEvents));
		return Result;
	}

	// Get the EventGraph
	UEdGraph* EventGraph = nullptr;
	for (UEdGraph* Graph : WidgetBP->UbergraphPages)
	{
		if (Graph->GetFName() == UEdGraphSchema_K2::GN_EventGraph)
		{
			EventGraph = Graph;
			break;
		}
	}

	if (!EventGraph && WidgetBP->UbergraphPages.Num() > 0)
	{
		EventGraph = WidgetBP->UbergraphPages[0];
	}

	if (!EventGraph)
	{
		Result.Errors.Add(TEXT("Widget Blueprint has no EventGraph. This is unexpected — try recompiling the Widget Blueprint first."));
		return Result;
	}

	WidgetBP->Modify();
	EventGraph->Modify();

	// Create a K2Node_ComponentBoundEvent
	UK2Node_ComponentBoundEvent* EventNode = NewObject<UK2Node_ComponentBoundEvent>(EventGraph);
	EventNode->DelegatePropertyName = DelegateProp->GetFName();
	EventNode->DelegateOwnerClass = Widget->GetClass();
	EventNode->ComponentPropertyName = FName(*WidgetName);

	// Position the node (find an empty area)
	int32 MaxY = 0;
	for (UEdGraphNode* Node : EventGraph->Nodes)
	{
		if (Node)
		{
			MaxY = FMath::Max(MaxY, Node->NodePosY + 200);
		}
	}
	EventNode->NodePosX = 0;
	EventNode->NodePosY = MaxY + 100;

	EventGraph->AddNode(EventNode, false, false);
	EventNode->CreateNewGuid();
	EventNode->PostPlacedNewNode();
	EventNode->AllocateDefaultPins();

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(WidgetBP);
	CompileAndMarkDirty(WidgetBP);

	FString NodeName = EventNode->GetName();
	Result.bSuccess = true;
	Result.ResultMessage = FString::Printf(
		TEXT("Bound event '%s' on widget '%s' in '%s'. Created event node '%s' at position (%d, %d). Use inject_blueprint_nodes_t3d to wire logic from this event node's exec output pin."),
		*EventName, *WidgetName, *AssetPath, *NodeName, EventNode->NodePosX, EventNode->NodePosY);
	Result.ModifiedAssets.Add(AssetPath);
	return Result;
}

// ============================================================================
// ExecuteRemoveWidget
// ============================================================================

FAutonomixActionResult FAutonomixWidgetActions::ExecuteRemoveWidget(const TSharedRef<FJsonObject>& Params, FAutonomixActionResult& Result)
{
	FString AssetPath  = Params->GetStringField(TEXT("asset_path"));
	FString WidgetName = Params->GetStringField(TEXT("widget_name"));

	UWidgetBlueprint* WidgetBP = LoadWidgetBP(AssetPath, Result);
	if (!WidgetBP) return Result;

	UWidget* Widget = FindWidgetByName(WidgetBP, WidgetName, Result);
	if (!Widget) return Result;

	WidgetBP->Modify();
	WidgetBP->WidgetTree->Modify();

	bool bWasRoot = (Widget == WidgetBP->WidgetTree->RootWidget);

	// If the widget has a parent panel, remove from it
	UPanelWidget* Parent = Widget->GetParent();
	if (Parent)
	{
		Parent->Modify();
		Parent->RemoveChild(Widget);
	}

	// If it was the root, clear the root
	if (bWasRoot)
	{
		WidgetBP->WidgetTree->RootWidget = nullptr;
		Result.Warnings.Add(TEXT("Removed the root widget. The widget tree is now empty. Use add_widget to add a new root."));
	}

	CompileAndMarkDirty(WidgetBP);

	Result.bSuccess = true;
	Result.ResultMessage = FString::Printf(TEXT("Removed widget '%s' from '%s'.%s"), *WidgetName, *AssetPath,
		bWasRoot ? TEXT(" (was root — tree is now empty)") : TEXT(""));
	Result.ModifiedAssets.Add(AssetPath);
	return Result;
}

// ============================================================================
// ExecuteGetWidgetTree
// ============================================================================

FAutonomixActionResult FAutonomixWidgetActions::ExecuteGetWidgetTree(const TSharedRef<FJsonObject>& Params, FAutonomixActionResult& Result)
{
	FString AssetPath = Params->GetStringField(TEXT("asset_path"));

	UWidgetBlueprint* WidgetBP = LoadWidgetBP(AssetPath, Result);
	if (!WidgetBP) return Result;

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

	UWidgetBlueprint* WidgetBP = LoadWidgetBP(AssetPath, Result);
	if (!WidgetBP) return Result;

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

	// Add widget tree summary to the result
	int32 WidgetCount = 0;
	FString RootName = TEXT("(none)");
	if (WidgetBP->WidgetTree)
	{
		TArray<UWidget*> AllWidgets;
		WidgetBP->WidgetTree->GetAllWidgets(AllWidgets);
		WidgetCount = AllWidgets.Num();
		if (WidgetBP->WidgetTree->RootWidget)
		{
			RootName = FString::Printf(TEXT("%s (%s)"), *WidgetBP->WidgetTree->RootWidget->GetName(), *WidgetBP->WidgetTree->RootWidget->GetClass()->GetName());
		}
	}

	Result.bSuccess = bOk;
	Result.ResultMessage = bOk
		? FString::Printf(TEXT("Widget Blueprint '%s' compiled successfully. %d widgets, root: %s."), *AssetPath, WidgetCount, *RootName)
		: FString::Printf(TEXT("Widget Blueprint '%s' compiled with ERRORS. %d widgets, root: %s."), *AssetPath, WidgetCount, *RootName);
	Result.ModifiedAssets.Add(AssetPath);
	return Result;
}
