// Copyright Autonomix. All Rights Reserved.

#include "Material/AutonomixMaterialActions.h"
#include "AutonomixCoreModule.h"

#include "Materials/Material.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Materials/MaterialExpressionTextureSample.h"
#include "Materials/MaterialExpressionConstant.h"
#include "Materials/MaterialExpressionConstant3Vector.h"
#include "Materials/MaterialExpressionMultiply.h"
#include "Materials/MaterialExpressionLinearInterpolate.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#include "Materials/MaterialExpressionTextureCoordinate.h"
#include "MaterialEditingLibrary.h"
#include "AssetToolsModule.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Factories/MaterialFactoryNew.h"
#include "Factories/MaterialInstanceConstantFactoryNew.h"
#include "UObject/SavePackage.h"
#include "Misc/PackageName.h"
#include "ScopedTransaction.h"

FAutonomixMaterialActions::FAutonomixMaterialActions() {}
FAutonomixMaterialActions::~FAutonomixMaterialActions() {}

FName FAutonomixMaterialActions::GetActionName() const { return FName(TEXT("Material")); }
FText FAutonomixMaterialActions::GetDisplayName() const { return FText::FromString(TEXT("Material Actions")); }
EAutonomixActionCategory FAutonomixMaterialActions::GetCategory() const { return EAutonomixActionCategory::Material; }
EAutonomixRiskLevel FAutonomixMaterialActions::GetDefaultRiskLevel() const { return EAutonomixRiskLevel::Medium; }
bool FAutonomixMaterialActions::CanUndo() const { return true; }
bool FAutonomixMaterialActions::UndoAction() { return false; }

TArray<FString> FAutonomixMaterialActions::GetSupportedToolNames() const
{
	return {
		TEXT("create_material"),
		TEXT("create_material_instance"),
		TEXT("add_material_expression"),
		TEXT("connect_material_property")
	};
}

bool FAutonomixMaterialActions::ValidateParams(const TSharedRef<FJsonObject>& Params, TArray<FString>& OutErrors) const
{
	if (!Params->HasField(TEXT("asset_path")))
	{
		OutErrors.Add(TEXT("Missing required field: asset_path"));
		return false;
	}
	return true;
}

FAutonomixActionPlan FAutonomixMaterialActions::PreviewAction(const TSharedRef<FJsonObject>& Params)
{
	FAutonomixActionPlan Plan;
	FString AssetPath = Params->GetStringField(TEXT("asset_path"));
	Plan.Summary = FString::Printf(TEXT("Material operation at %s"), *AssetPath);
	FAutonomixAction Action;
	Action.Description = Plan.Summary;
	Action.Category = EAutonomixActionCategory::Material;
	Action.RiskLevel = EAutonomixRiskLevel::Low;
	Action.AffectedAssets.Add(AssetPath);
	Plan.Actions.Add(Action);
	return Plan;
}

FAutonomixActionResult FAutonomixMaterialActions::ExecuteAction(const TSharedRef<FJsonObject>& Params)
{
	// Wrap all Material mutations in FScopedTransaction for Ctrl+Z undo
	FScopedTransaction Transaction(FText::FromString(TEXT("Autonomix Material Action")));

	FAutonomixActionResult Result;
	Result.bSuccess = false;

	if (Params->HasField(TEXT("parent_material")))
	{
		return ExecuteCreateMaterialInstance(Params, Result);
	}
	else if (Params->HasField(TEXT("expressions")))
	{
		return ExecuteCreateMaterial(Params, Result);
	}

	// Default: create a basic material
	return ExecuteCreateMaterial(Params, Result);
}

FAutonomixActionResult FAutonomixMaterialActions::ExecuteCreateMaterial(const TSharedRef<FJsonObject>& Params, FAutonomixActionResult& Result)
{
	FString AssetPath = Params->GetStringField(TEXT("asset_path"));
	FString PackagePath = FPackageName::GetLongPackagePath(AssetPath);
	FString AssetName = FPackageName::GetShortName(AssetPath);

	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
	UMaterialFactoryNew* Factory = NewObject<UMaterialFactoryNew>();

	UObject* NewAsset = AssetTools.CreateAsset(AssetName, PackagePath, UMaterial::StaticClass(), Factory);
	UMaterial* NewMaterial = Cast<UMaterial>(NewAsset);

	if (!NewMaterial)
	{
		Result.Errors.Add(FString::Printf(TEXT("Failed to create Material at %s"), *AssetPath));
		return Result;
	}

	// CRITICAL: Modify() before structural changes for undo support
	NewMaterial->Modify();

	// Process expressions if provided
	const TArray<TSharedPtr<FJsonValue>>* ExpressionsArray = nullptr;
	if (Params->TryGetArrayField(TEXT("expressions"), ExpressionsArray))
	{
		for (const TSharedPtr<FJsonValue>& ExprValue : *ExpressionsArray)
		{
			const TSharedPtr<FJsonObject>& ExprObj = ExprValue->AsObject();
			if (!ExprObj.IsValid()) continue;

			FString ExprType = ExprObj->GetStringField(TEXT("type"));
			int32 PosX = 0, PosY = 0;
			ExprObj->TryGetNumberField(TEXT("x"), PosX);
			ExprObj->TryGetNumberField(TEXT("y"), PosY);

			UMaterialExpression* Expression = nullptr;

			if (ExprType == TEXT("TextureSample"))
			{
				Expression = UMaterialEditingLibrary::CreateMaterialExpression(NewMaterial, UMaterialExpressionTextureSample::StaticClass(), PosX, PosY);
			}
			else if (ExprType == TEXT("Constant"))
			{
				Expression = UMaterialEditingLibrary::CreateMaterialExpression(NewMaterial, UMaterialExpressionConstant::StaticClass(), PosX, PosY);
				if (Expression)
				{
					double Value = 0;
					if (ExprObj->TryGetNumberField(TEXT("value"), Value))
					{
						Cast<UMaterialExpressionConstant>(Expression)->R = Value;
					}
				}
			}
			else if (ExprType == TEXT("Constant3Vector") || ExprType == TEXT("Color"))
			{
				Expression = UMaterialEditingLibrary::CreateMaterialExpression(NewMaterial, UMaterialExpressionConstant3Vector::StaticClass(), PosX, PosY);
			}
			else if (ExprType == TEXT("Multiply"))
			{
				Expression = UMaterialEditingLibrary::CreateMaterialExpression(NewMaterial, UMaterialExpressionMultiply::StaticClass(), PosX, PosY);
			}
			else if (ExprType == TEXT("Lerp") || ExprType == TEXT("LinearInterpolate"))
			{
				Expression = UMaterialEditingLibrary::CreateMaterialExpression(NewMaterial, UMaterialExpressionLinearInterpolate::StaticClass(), PosX, PosY);
			}
			else if (ExprType == TEXT("ScalarParameter"))
			{
				Expression = UMaterialEditingLibrary::CreateMaterialExpression(NewMaterial, UMaterialExpressionScalarParameter::StaticClass(), PosX, PosY);
				if (Expression)
				{
					FString ParamName;
					if (ExprObj->TryGetStringField(TEXT("parameter_name"), ParamName))
					{
						Cast<UMaterialExpressionScalarParameter>(Expression)->ParameterName = FName(*ParamName);
					}
				}
			}
			else if (ExprType == TEXT("VectorParameter"))
			{
				Expression = UMaterialEditingLibrary::CreateMaterialExpression(NewMaterial, UMaterialExpressionVectorParameter::StaticClass(), PosX, PosY);
				if (Expression)
				{
					FString ParamName;
					if (ExprObj->TryGetStringField(TEXT("parameter_name"), ParamName))
					{
						Cast<UMaterialExpressionVectorParameter>(Expression)->ParameterName = FName(*ParamName);
					}
				}
			}
			else if (ExprType == TEXT("TextureCoordinate"))
			{
				Expression = UMaterialEditingLibrary::CreateMaterialExpression(NewMaterial, UMaterialExpressionTextureCoordinate::StaticClass(), PosX, PosY);
			}

			if (Expression)
			{
				FString ConnectTo;
				if (ExprObj->TryGetStringField(TEXT("connect_to"), ConnectTo))
				{
					EMaterialProperty MatProp = MP_BaseColor;
					if (ConnectTo == TEXT("BaseColor")) MatProp = MP_BaseColor;
					else if (ConnectTo == TEXT("Metallic")) MatProp = MP_Metallic;
					else if (ConnectTo == TEXT("Specular")) MatProp = MP_Specular;
					else if (ConnectTo == TEXT("Roughness")) MatProp = MP_Roughness;
					else if (ConnectTo == TEXT("EmissiveColor")) MatProp = MP_EmissiveColor;
					else if (ConnectTo == TEXT("Normal")) MatProp = MP_Normal;
					else if (ConnectTo == TEXT("Opacity")) MatProp = MP_Opacity;
					else if (ConnectTo == TEXT("OpacityMask")) MatProp = MP_OpacityMask;

					UMaterialEditingLibrary::ConnectMaterialProperty(Expression, TEXT(""), MatProp);
				}

				UE_LOG(LogAutonomix, Log, TEXT("MaterialActions: Added expression %s at (%d, %d)"), *ExprType, PosX, PosY);
			}
			else
			{
				Result.Warnings.Add(FString::Printf(TEXT("Unknown expression type: %s"), *ExprType));
			}
		}
	}

	UMaterialEditingLibrary::RecompileMaterial(NewMaterial);

	UPackage* Package = NewMaterial->GetOutermost();
	Package->MarkPackageDirty();
	FString PackageFilename;
	if (FPackageName::TryConvertLongPackageNameToFilename(Package->GetName(), PackageFilename, FPackageName::GetAssetPackageExtension()))
	{
		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Standalone;
		UPackage::SavePackage(Package, NewMaterial, *PackageFilename, SaveArgs);
	}
	FAssetRegistryModule::AssetCreated(NewMaterial);

	Result.bSuccess = true;
	Result.ResultMessage = FString::Printf(TEXT("Created Material '%s'"), *AssetName);
	Result.ModifiedAssets.Add(AssetPath);
	return Result;
}

FAutonomixActionResult FAutonomixMaterialActions::ExecuteCreateMaterialInstance(const TSharedRef<FJsonObject>& Params, FAutonomixActionResult& Result)
{
	FString AssetPath = Params->GetStringField(TEXT("asset_path"));
	FString ParentPath = Params->GetStringField(TEXT("parent_material"));
	FString PackagePath = FPackageName::GetLongPackagePath(AssetPath);
	FString AssetName = FPackageName::GetShortName(AssetPath);

	UMaterial* ParentMaterial = LoadObject<UMaterial>(nullptr, *ParentPath);
	if (!ParentMaterial)
	{
		Result.Errors.Add(FString::Printf(TEXT("Parent material not found: %s"), *ParentPath));
		return Result;
	}

	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
	UMaterialInstanceConstantFactoryNew* Factory = NewObject<UMaterialInstanceConstantFactoryNew>();
	Factory->InitialParent = ParentMaterial;

	UObject* NewAsset = AssetTools.CreateAsset(AssetName, PackagePath, UMaterialInstanceConstant::StaticClass(), Factory);
	UMaterialInstanceConstant* NewMI = Cast<UMaterialInstanceConstant>(NewAsset);

	if (!NewMI)
	{
		Result.Errors.Add(FString::Printf(TEXT("Failed to create Material Instance at %s"), *AssetPath));
		return Result;
	}

	// CRITICAL: Modify() before changing parameters for undo support
	NewMI->Modify();

	// Set scalar parameters
	const TSharedPtr<FJsonObject>* ScalarsObj = nullptr;
	if (Params->TryGetObjectField(TEXT("scalar_parameters"), ScalarsObj))
	{
		for (const auto& Pair : (*ScalarsObj)->Values)
		{
			double Value = 0;
			if (Pair.Value->TryGetNumber(Value))
			{
				UMaterialEditingLibrary::SetMaterialInstanceScalarParameterValue(NewMI, FName(*Pair.Key), Value);
			}
		}
	}

	// Set vector parameters
	const TSharedPtr<FJsonObject>* VectorsObj = nullptr;
	if (Params->TryGetObjectField(TEXT("vector_parameters"), VectorsObj))
	{
		for (const auto& Pair : (*VectorsObj)->Values)
		{
			const TSharedPtr<FJsonObject>* VecObj = nullptr;
			if (Pair.Value->TryGetObject(VecObj))
			{
				double R = 0, G = 0, B = 0, A = 1;
				(*VecObj)->TryGetNumberField(TEXT("r"), R);
				(*VecObj)->TryGetNumberField(TEXT("g"), G);
				(*VecObj)->TryGetNumberField(TEXT("b"), B);
				(*VecObj)->TryGetNumberField(TEXT("a"), A);
				FLinearColor Color(R, G, B, A);
				UMaterialEditingLibrary::SetMaterialInstanceVectorParameterValue(NewMI, FName(*Pair.Key), Color);
			}
		}
	}

	NewMI->GetOutermost()->MarkPackageDirty();
	FAssetRegistryModule::AssetCreated(NewMI);

	Result.bSuccess = true;
	Result.ResultMessage = FString::Printf(TEXT("Created Material Instance '%s' from '%s'"), *AssetName, *ParentPath);
	Result.ModifiedAssets.Add(AssetPath);
	return Result;
}

FAutonomixActionResult FAutonomixMaterialActions::ExecuteAddMaterialExpression(const TSharedRef<FJsonObject>& Params, FAutonomixActionResult& Result)
{
	Result.Errors.Add(TEXT("Use create_material with expressions array instead."));
	return Result;
}
