// Copyright Autonomix. All Rights Reserved.

#include "Input/AutonomixInputActions.h"
#include "AutonomixCoreModule.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "InputModifiers.h"
#include "InputTriggers.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "UObject/SavePackage.h"
#include "Misc/PackageName.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "ScopedTransaction.h"

FAutonomixInputActions::FAutonomixInputActions() {}
FAutonomixInputActions::~FAutonomixInputActions() {}
FName FAutonomixInputActions::GetActionName() const { return FName(TEXT("Input")); }
FText FAutonomixInputActions::GetDisplayName() const { return FText::FromString(TEXT("Enhanced Input Actions")); }
EAutonomixActionCategory FAutonomixInputActions::GetCategory() const { return EAutonomixActionCategory::Settings; }
EAutonomixRiskLevel FAutonomixInputActions::GetDefaultRiskLevel() const { return EAutonomixRiskLevel::Medium; }
bool FAutonomixInputActions::CanUndo() const { return false; }
bool FAutonomixInputActions::UndoAction() { return false; }

TArray<FString> FAutonomixInputActions::GetSupportedToolNames() const
{
	return {
		TEXT("create_input_action"),
		TEXT("create_input_mapping_context"),
		TEXT("add_input_mapping")
	};
}

bool FAutonomixInputActions::ValidateParams(const TSharedRef<FJsonObject>& Params, TArray<FString>& OutErrors) const
{
	return true;
}

FAutonomixActionPlan FAutonomixInputActions::PreviewAction(const TSharedRef<FJsonObject>& Params)
{
	FAutonomixActionPlan Plan;
	Plan.Summary = TEXT("Enhanced Input asset operation");
	Plan.MaxRiskLevel = EAutonomixRiskLevel::Medium;
	FAutonomixAction Action;
	Action.Description = Plan.Summary;
	Action.Category = EAutonomixActionCategory::Settings;
	Action.RiskLevel = EAutonomixRiskLevel::Medium;
	Plan.Actions.Add(Action);
	return Plan;
}

// Helper: resolve EInputActionValueType from string
static EInputActionValueType ParseValueType(const FString& TypeStr)
{
	if (TypeStr.Equals(TEXT("Axis1D"), ESearchCase::IgnoreCase) || TypeStr.Equals(TEXT("Float"), ESearchCase::IgnoreCase))
		return EInputActionValueType::Axis1D;
	if (TypeStr.Equals(TEXT("Axis2D"), ESearchCase::IgnoreCase) || TypeStr.Equals(TEXT("Vector2D"), ESearchCase::IgnoreCase))
		return EInputActionValueType::Axis2D;
	if (TypeStr.Equals(TEXT("Axis3D"), ESearchCase::IgnoreCase) || TypeStr.Equals(TEXT("Vector"), ESearchCase::IgnoreCase))
		return EInputActionValueType::Axis3D;
	// Default: Boolean/Digital
	return EInputActionValueType::Boolean;
}

// Helper: save a newly created asset package
static bool SaveAssetPackage(UPackage* Package, UObject* Asset, const FString& AssetPath)
{
	Asset->MarkPackageDirty();
	FAssetRegistryModule::AssetCreated(Asset);

	FString PackageFileName = FPackageName::LongPackageNameToFilename(
		AssetPath, FPackageName::GetAssetPackageExtension());

	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
	return UPackage::SavePackage(Package, Asset, *PackageFileName, SaveArgs);
}

// Helper: find FKey from string name
static FKey ParseKeyName(const FString& KeyName)
{
	// Use TCHAR* constructor to avoid most-vexing-parse with FName()
	FKey Key(*KeyName);
	if (!Key.IsValid())
	{
		UE_LOG(LogAutonomix, Warning, TEXT("InputActions: Key '%s' not recognized, using as-is"), *KeyName);
	}
	return Key;
}

FAutonomixActionResult FAutonomixInputActions::ExecuteAction(const TSharedRef<FJsonObject>& Params)
{
	FScopedTransaction Transaction(FText::FromString(TEXT("Autonomix Enhanced Input Action")));

	// Dispatch by _tool_name (injected by ActionRouter with underscore prefix)
	FString ToolName;
	Params->TryGetStringField(TEXT("_tool_name"), ToolName);

	if (ToolName == TEXT("create_input_action"))
		return ExecuteCreateInputAction(Params);
	else if (ToolName == TEXT("create_input_mapping_context"))
		return ExecuteCreateInputMappingContext(Params);
	else if (ToolName == TEXT("add_input_mapping"))
		return ExecuteAddInputMapping(Params);

	FAutonomixActionResult Result;
	Result.bSuccess = false;
	Result.Errors.Add(FString::Printf(TEXT("Unknown tool: %s"), *ToolName));
	return Result;
}

// ============================================================
// create_input_action
// ============================================================
FAutonomixActionResult FAutonomixInputActions::ExecuteCreateInputAction(const TSharedRef<FJsonObject>& Params)
{
	FAutonomixActionResult Result;
	Result.bSuccess = false;

	FString AssetPath;
	Params->TryGetStringField(TEXT("asset_path"), AssetPath);
	if (AssetPath.IsEmpty())
	{
		Result.Errors.Add(TEXT("Missing required field: 'asset_path'"));
		return Result;
	}

	FString ValueTypeStr;
	Params->TryGetStringField(TEXT("value_type"), ValueTypeStr);

	// Check if asset already exists
	if (FindObject<UInputAction>(nullptr, *AssetPath))
	{
		Result.bSuccess = true;
		Result.ResultMessage = FString::Printf(TEXT("Input Action already exists at %s"), *AssetPath);
		return Result;
	}

	// Extract package path and asset name
	FString PackagePath = AssetPath;
	FString AssetName = FPackageName::GetLongPackageAssetName(AssetPath);

	UPackage* Package = CreatePackage(*PackagePath);
	if (!Package)
	{
		Result.Errors.Add(FString::Printf(TEXT("Failed to create package: %s"), *PackagePath));
		return Result;
	}

	UInputAction* NewAction = NewObject<UInputAction>(Package, FName(*AssetName),
		RF_Public | RF_Standalone | RF_Transactional);
	if (!NewAction)
	{
		Result.Errors.Add(TEXT("Failed to create UInputAction object"));
		return Result;
	}

	// Set value type
	NewAction->ValueType = ParseValueType(ValueTypeStr);

	// Optional: consume input
	bool bConsumeInput = true;
	if (Params->HasField(TEXT("consume_input")))
	{
		bConsumeInput = Params->GetBoolField(TEXT("consume_input"));
	}
	NewAction->bConsumeInput = bConsumeInput;

	// Save
	if (!SaveAssetPackage(Package, NewAction, PackagePath))
	{
		Result.Errors.Add(FString::Printf(TEXT("Failed to save Input Action to disk: %s"), *PackagePath));
		return Result;
	}

	Result.bSuccess = true;
	Result.ResultMessage = FString::Printf(
		TEXT("Created Input Action '%s' (ValueType=%s) at %s"),
		*AssetName,
		ValueTypeStr.IsEmpty() ? TEXT("Boolean") : *ValueTypeStr,
		*AssetPath);
	Result.ModifiedPaths.Add(AssetPath);

	UE_LOG(LogAutonomix, Log, TEXT("InputActions: Created IA '%s' at %s"), *AssetName, *AssetPath);
	return Result;
}

// ============================================================
// create_input_mapping_context
// ============================================================
FAutonomixActionResult FAutonomixInputActions::ExecuteCreateInputMappingContext(const TSharedRef<FJsonObject>& Params)
{
	FAutonomixActionResult Result;
	Result.bSuccess = false;

	FString AssetPath;
	Params->TryGetStringField(TEXT("asset_path"), AssetPath);
	if (AssetPath.IsEmpty())
	{
		Result.Errors.Add(TEXT("Missing required field: 'asset_path'"));
		return Result;
	}

	// Check if asset already exists
	if (FindObject<UInputMappingContext>(nullptr, *AssetPath))
	{
		Result.bSuccess = true;
		Result.ResultMessage = FString::Printf(TEXT("Input Mapping Context already exists at %s"), *AssetPath);
		return Result;
	}

	FString AssetName = FPackageName::GetLongPackageAssetName(AssetPath);

	UPackage* Package = CreatePackage(*AssetPath);
	if (!Package)
	{
		Result.Errors.Add(FString::Printf(TEXT("Failed to create package: %s"), *AssetPath));
		return Result;
	}

	UInputMappingContext* NewIMC = NewObject<UInputMappingContext>(Package, FName(*AssetName),
		RF_Public | RF_Standalone | RF_Transactional);
	if (!NewIMC)
	{
		Result.Errors.Add(TEXT("Failed to create UInputMappingContext object"));
		return Result;
	}

	// Save
	if (!SaveAssetPackage(Package, NewIMC, AssetPath))
	{
		Result.Errors.Add(FString::Printf(TEXT("Failed to save IMC to disk: %s"), *AssetPath));
		return Result;
	}

	Result.bSuccess = true;
	Result.ResultMessage = FString::Printf(
		TEXT("Created Input Mapping Context '%s' at %s"),
		*AssetName, *AssetPath);
	Result.ModifiedPaths.Add(AssetPath);

	UE_LOG(LogAutonomix, Log, TEXT("InputActions: Created IMC '%s' at %s"), *AssetName, *AssetPath);
	return Result;
}

// ============================================================
// add_input_mapping
// ============================================================
FAutonomixActionResult FAutonomixInputActions::ExecuteAddInputMapping(const TSharedRef<FJsonObject>& Params)
{
	FAutonomixActionResult Result;
	Result.bSuccess = false;

	FString IMCPath, IAPath, KeyName;
	Params->TryGetStringField(TEXT("mapping_context_path"), IMCPath);
	Params->TryGetStringField(TEXT("action_path"), IAPath);
	Params->TryGetStringField(TEXT("key"), KeyName);

	if (IMCPath.IsEmpty() || IAPath.IsEmpty() || KeyName.IsEmpty())
	{
		Result.Errors.Add(TEXT("Missing required fields: 'mapping_context_path', 'action_path', and 'key' are required."));
		return Result;
	}

	// Load assets
	UInputMappingContext* IMC = LoadObject<UInputMappingContext>(nullptr, *IMCPath);
	if (!IMC)
	{
		// Try with _C suffix stripped or added
		IMC = LoadObject<UInputMappingContext>(nullptr, *(IMCPath + TEXT(".") + FPackageName::GetLongPackageAssetName(IMCPath)));
		if (!IMC)
		{
			Result.Errors.Add(FString::Printf(TEXT("Could not load Input Mapping Context: %s"), *IMCPath));
			return Result;
		}
	}

	UInputAction* IA = LoadObject<UInputAction>(nullptr, *IAPath);
	if (!IA)
	{
		IA = LoadObject<UInputAction>(nullptr, *(IAPath + TEXT(".") + FPackageName::GetLongPackageAssetName(IAPath)));
		if (!IA)
		{
			Result.Errors.Add(FString::Printf(TEXT("Could not load Input Action: %s"), *IAPath));
			return Result;
		}
	}

	FKey Key = ParseKeyName(KeyName);

	// Add the mapping
	IMC->Modify();
	FEnhancedActionKeyMapping& Mapping = IMC->MapKey(IA, Key);

	// Parse optional modifiers
	const TArray<TSharedPtr<FJsonValue>>* ModifiersArray = nullptr;
	if (Params->TryGetArrayField(TEXT("modifiers"), ModifiersArray))
	{
		for (const TSharedPtr<FJsonValue>& ModVal : *ModifiersArray)
		{
			FString ModName = ModVal->AsString();

			if (ModName.Equals(TEXT("Negate"), ESearchCase::IgnoreCase))
			{
				Mapping.Modifiers.Add(NewObject<UInputModifierNegate>());
			}
			else if (ModName.Equals(TEXT("SwizzleInputAxisValues"), ESearchCase::IgnoreCase) ||
					 ModName.Equals(TEXT("Swizzle"), ESearchCase::IgnoreCase))
			{
				Mapping.Modifiers.Add(NewObject<UInputModifierSwizzleAxis>());
			}
			else if (ModName.Equals(TEXT("DeadZone"), ESearchCase::IgnoreCase))
			{
				Mapping.Modifiers.Add(NewObject<UInputModifierDeadZone>());
			}
			else if (ModName.Equals(TEXT("Scalar"), ESearchCase::IgnoreCase))
			{
				Mapping.Modifiers.Add(NewObject<UInputModifierScalar>());
			}
			else
			{
				UE_LOG(LogAutonomix, Warning, TEXT("InputActions: Unknown modifier '%s', skipping"), *ModName);
			}
		}
	}

	// Parse optional triggers
	const TArray<TSharedPtr<FJsonValue>>* TriggersArray = nullptr;
	if (Params->TryGetArrayField(TEXT("triggers"), TriggersArray))
	{
		for (const TSharedPtr<FJsonValue>& TrigVal : *TriggersArray)
		{
			FString TrigName = TrigVal->AsString();

			if (TrigName.Equals(TEXT("Pressed"), ESearchCase::IgnoreCase) ||
				TrigName.Equals(TEXT("Down"), ESearchCase::IgnoreCase))
			{
				Mapping.Triggers.Add(NewObject<UInputTriggerPressed>());
			}
			else if (TrigName.Equals(TEXT("Released"), ESearchCase::IgnoreCase))
			{
				Mapping.Triggers.Add(NewObject<UInputTriggerReleased>());
			}
			else if (TrigName.Equals(TEXT("Hold"), ESearchCase::IgnoreCase))
			{
				Mapping.Triggers.Add(NewObject<UInputTriggerHold>());
			}
			else if (TrigName.Equals(TEXT("Tap"), ESearchCase::IgnoreCase))
			{
				Mapping.Triggers.Add(NewObject<UInputTriggerTap>());
			}
			else if (TrigName.Equals(TEXT("Pulse"), ESearchCase::IgnoreCase))
			{
				Mapping.Triggers.Add(NewObject<UInputTriggerPulse>());
			}
			else if (!TrigName.Equals(TEXT("default"), ESearchCase::IgnoreCase) && !TrigName.IsEmpty())
			{
				UE_LOG(LogAutonomix, Warning, TEXT("InputActions: Unknown trigger '%s', skipping"), *TrigName);
			}
			// "default" and empty strings are silently skipped — we add a default below
		}
	}

	// CRITICAL FIX: UE5 requires every key mapping to have at least one Input Trigger.
	// "There cannot be a null Input Trigger on a key mapping" warnings break asset validation.
	// If no valid triggers were specified (or AI passed "default"), add UInputTriggerPressed.
	if (Mapping.Triggers.Num() == 0)
	{
		Mapping.Triggers.Add(NewObject<UInputTriggerPressed>());
		UE_LOG(LogAutonomix, Log, TEXT("InputActions: No triggers specified — defaulting to Pressed trigger."));
	}

	// Save the modified IMC
	IMC->MarkPackageDirty();
	UPackage* Package = IMC->GetOutermost();
	FString PackageFileName = FPackageName::LongPackageNameToFilename(
		Package->GetName(), FPackageName::GetAssetPackageExtension());

	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
	if (!UPackage::SavePackage(Package, IMC, *PackageFileName, SaveArgs))
	{
		Result.Errors.Add(FString::Printf(TEXT("Failed to save modified IMC to disk: %s"), *IMCPath));
		return Result;
	}

	// Build result description
	FString ModsDesc = TEXT("none");
	if (ModifiersArray && ModifiersArray->Num() > 0)
	{
		TArray<FString> ModNames;
		for (const TSharedPtr<FJsonValue>& M : *ModifiersArray)
			ModNames.Add(M->AsString());
		ModsDesc = FString::Join(ModNames, TEXT(", "));
	}

	FString TrigsDesc = TEXT("default");
	if (TriggersArray && TriggersArray->Num() > 0)
	{
		TArray<FString> TrigNames;
		for (const TSharedPtr<FJsonValue>& T : *TriggersArray)
			TrigNames.Add(T->AsString());
		TrigsDesc = FString::Join(TrigNames, TEXT(", "));
	}

	Result.bSuccess = true;
	Result.ResultMessage = FString::Printf(
		TEXT("Added mapping: %s → %s (Key=%s, Modifiers=[%s], Triggers=[%s])"),
		*IA->GetName(), *IMC->GetName(), *KeyName, *ModsDesc, *TrigsDesc);
	Result.ModifiedPaths.Add(IMCPath);

	UE_LOG(LogAutonomix, Log, TEXT("InputActions: Added mapping %s → %s (Key=%s)"),
		*IA->GetName(), *IMC->GetName(), *KeyName);
	return Result;
}
