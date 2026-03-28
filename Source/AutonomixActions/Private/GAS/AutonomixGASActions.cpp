// Copyright Autonomix. All Rights Reserved.

#include "GAS/AutonomixGASActions.h"
#include "AutonomixCoreModule.h"
#include "AutonomixSettings.h"
#include "AbilitySystemComponent.h"
#include "GameplayEffect.h"
#include "GameplayAbilitySpec.h"
#include "Abilities/GameplayAbility.h"
#include "GameplayTagsManager.h"
#include "GameplayTagsEditorModule.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "Factories/BlueprintFactory.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Engine/SimpleConstructionScript.h"
#include "Engine/SCS_Node.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Editor.h"
#include "UObject/UnrealType.h"

#define LOCTEXT_NAMESPACE "AutonomixGASActions"

// ============================================================================
// Helper: Set a protected UPROPERTY on a CDO via reflection
// ============================================================================

namespace AutonomixGASReflection
{
	/** Set an enum property by name on an object via reflection */
	template<typename EnumType>
	bool SetEnumProperty(UObject* Object, const FString& PropertyName, EnumType Value)
	{
		if (!Object) return false;
		FProperty* Prop = Object->GetClass()->FindPropertyByName(FName(*PropertyName));
		if (!Prop) return false;

		FEnumProperty* EnumProp = CastField<FEnumProperty>(Prop);
		FByteProperty* ByteProp = CastField<FByteProperty>(Prop);

		if (EnumProp)
		{
			void* ValuePtr = EnumProp->ContainerPtrToValuePtr<void>(Object);
			EnumProp->GetUnderlyingProperty()->SetIntPropertyValue(ValuePtr, static_cast<int64>(Value));
			return true;
		}
		else if (ByteProp)
		{
			ByteProp->SetPropertyValue_InContainer(Object, static_cast<uint8>(Value));
			return true;
		}
		return false;
	}

	/** Set a TSubclassOf<> property by name on an object via reflection */
	bool SetClassProperty(UObject* Object, const FString& PropertyName, UClass* ClassValue)
	{
		if (!Object) return false;
		FClassProperty* Prop = CastField<FClassProperty>(Object->GetClass()->FindPropertyByName(FName(*PropertyName)));
		if (!Prop) return false;
		Prop->SetObjectPropertyValue_InContainer(Object, ClassValue);
		return true;
	}

	/** Add a tag to a FGameplayTagContainer property on an object via reflection */
	bool AddTagToContainer(UObject* Object, const FString& PropertyName, FGameplayTag Tag)
	{
		if (!Object || !Tag.IsValid()) return false;
		FProperty* Prop = Object->GetClass()->FindPropertyByName(FName(*PropertyName));
		if (!Prop) return false;
		FStructProperty* StructProp = CastField<FStructProperty>(Prop);
		if (!StructProp) return false;
		FGameplayTagContainer* Container = StructProp->ContainerPtrToValuePtr<FGameplayTagContainer>(Object);
		if (!Container) return false;
		Container->AddTag(Tag);
		return true;
	}
}

// ============================================================================
// Lifecycle
// ============================================================================

FAutonomixGASActions::FAutonomixGASActions() {}
FAutonomixGASActions::~FAutonomixGASActions() {}

// ============================================================================
// IAutonomixActionExecutor Interface
// ============================================================================

FName FAutonomixGASActions::GetActionName() const { return FName(TEXT("GAS")); }
FText FAutonomixGASActions::GetDisplayName() const { return LOCTEXT("DisplayName", "Gameplay Ability System"); }
EAutonomixActionCategory FAutonomixGASActions::GetCategory() const { return EAutonomixActionCategory::Blueprint; }
EAutonomixRiskLevel FAutonomixGASActions::GetDefaultRiskLevel() const { return EAutonomixRiskLevel::Medium; }
bool FAutonomixGASActions::CanUndo() const { return true; }
bool FAutonomixGASActions::UndoAction() { return GEditor && GEditor->UndoTransaction(); }

TArray<FString> FAutonomixGASActions::GetSupportedToolNames() const
{
	return {
		TEXT("gas_register_tags"),
		TEXT("gas_create_attribute_set"),
		TEXT("gas_setup_asc"),
		TEXT("gas_create_effect"),
		TEXT("gas_create_ability")
	};
}

bool FAutonomixGASActions::ValidateParams(const TSharedRef<FJsonObject>& Params, TArray<FString>& OutErrors) const
{
	return true;
}

FAutonomixActionPlan FAutonomixGASActions::PreviewAction(const TSharedRef<FJsonObject>& Params)
{
	FAutonomixActionPlan Plan;
	FString Action;
	Params->TryGetStringField(TEXT("action"), Action);
	if (Action.IsEmpty()) Params->TryGetStringField(TEXT("tool_name"), Action);

	if (Action == TEXT("gas_register_tags"))
		Plan.Summary = TEXT("Register gameplay tags in DefaultGameplayTags.ini");
	else if (Action == TEXT("gas_create_attribute_set"))
		Plan.Summary = TEXT("Generate C++ AttributeSet class");
	else if (Action == TEXT("gas_setup_asc"))
		Plan.Summary = TEXT("Add AbilitySystemComponent to Blueprint actor");
	else if (Action == TEXT("gas_create_effect"))
		Plan.Summary = TEXT("Create GameplayEffect Blueprint");
	else if (Action == TEXT("gas_create_ability"))
		Plan.Summary = TEXT("Create GameplayAbility Blueprint");
	else
		Plan.Summary = TEXT("GAS operation");

	Plan.MaxRiskLevel = EAutonomixRiskLevel::Medium;
	FAutonomixAction A;
	A.Description = Plan.Summary;
	A.Category = EAutonomixActionCategory::Blueprint;
	A.RiskLevel = EAutonomixRiskLevel::Medium;
	Plan.Actions.Add(A);
	return Plan;
}

FAutonomixActionResult FAutonomixGASActions::ExecuteAction(const TSharedRef<FJsonObject>& Params)
{
	FAutonomixActionResult Result;
	Result.bSuccess = false;

	FString Action;
	if (!Params->TryGetStringField(TEXT("action"), Action) || Action.IsEmpty())
		Params->TryGetStringField(TEXT("tool_name"), Action);

	if (Action == TEXT("gas_register_tags"))
		return ExecuteRegisterTags(Params, Result);
	else if (Action == TEXT("gas_create_attribute_set"))
		return ExecuteCreateAttributeSet(Params, Result);
	else if (Action == TEXT("gas_setup_asc"))
		return ExecuteSetupASC(Params, Result);
	else if (Action == TEXT("gas_create_effect"))
		return ExecuteCreateEffect(Params, Result);
	else if (Action == TEXT("gas_create_ability"))
		return ExecuteCreateAbility(Params, Result);

	Result.Errors.Add(TEXT("Unknown GAS action. Use gas_register_tags, gas_create_attribute_set, gas_setup_asc, gas_create_effect, or gas_create_ability."));
	return Result;
}

// ============================================================================
// gas_register_tags
// ============================================================================

FAutonomixActionResult FAutonomixGASActions::ExecuteRegisterTags(
	const TSharedRef<FJsonObject>& Params, FAutonomixActionResult& Result)
{
	const TArray<TSharedPtr<FJsonValue>>* TagsArray;
	if (!Params->TryGetArrayField(TEXT("tags"), TagsArray) || TagsArray->Num() == 0)
	{
		Result.Errors.Add(TEXT("Missing required field: 'tags' — array of {tag, comment} objects"));
		return Result;
	}

	IGameplayTagsEditorModule& TagsEditor = IGameplayTagsEditorModule::Get();
	int32 TagsAdded = 0;
	FString Report;

	for (const TSharedPtr<FJsonValue>& TagVal : *TagsArray)
	{
		const TSharedPtr<FJsonObject>* TagObjPtr;
		if (!TagVal->TryGetObject(TagObjPtr)) continue;
		const TSharedPtr<FJsonObject>& TagObj = *TagObjPtr;

		FString TagName, Comment;
		TagObj->TryGetStringField(TEXT("tag"), TagName);
		TagObj->TryGetStringField(TEXT("comment"), Comment);

		if (TagName.IsEmpty()) continue;

		FGameplayTag ExistingTag = UGameplayTagsManager::Get().RequestGameplayTag(FName(*TagName), false);
		if (ExistingTag.IsValid())
		{
			Report += FString::Printf(TEXT("  [SKIP] '%s' (already exists)\n"), *TagName);
			continue;
		}

		TagsEditor.AddNewGameplayTagToINI(TagName, Comment.IsEmpty() ? TEXT("Added by Autonomix") : Comment);
		Report += FString::Printf(TEXT("  [ADD] '%s' — %s\n"), *TagName, *Comment);
		TagsAdded++;
	}

	Result.bSuccess = true;
	Result.ResultMessage = FString::Printf(TEXT("Registered %d gameplay tag(s):\n%s"), TagsAdded, *Report);
	return Result;
}

// ============================================================================
// gas_create_attribute_set
// ============================================================================

FAutonomixActionResult FAutonomixGASActions::ExecuteCreateAttributeSet(
	const TSharedRef<FJsonObject>& Params, FAutonomixActionResult& Result)
{
	FString ClassName;
	if (!Params->TryGetStringField(TEXT("class_name"), ClassName))
	{
		Result.Errors.Add(TEXT("Missing required field: 'class_name'"));
		return Result;
	}

	if (!ClassName.StartsWith(TEXT("U")))
		ClassName = TEXT("U") + ClassName;

	FString FileName = ClassName.Mid(1);

	const TArray<TSharedPtr<FJsonValue>>* AttributesArray;
	if (!Params->TryGetArrayField(TEXT("attributes"), AttributesArray) || AttributesArray->Num() == 0)
	{
		Result.Errors.Add(TEXT("Missing required field: 'attributes'"));
		return Result;
	}

	FString ModuleName;
	if (!Params->TryGetStringField(TEXT("module_name"), ModuleName))
	{
		FString ProjectName = FString(FApp::GetProjectName());
		ModuleName = ProjectName.ToUpper() + TEXT("_API");
	}

	// Build header
	FString Header;
	Header += TEXT("// Generated by Autonomix — Gameplay Ability System AttributeSet\n\n");
	Header += TEXT("#pragma once\n\n");
	Header += TEXT("#include \"CoreMinimal.h\"\n");
	Header += TEXT("#include \"AttributeSet.h\"\n");
	Header += TEXT("#include \"AbilitySystemComponent.h\"\n");
	Header += TEXT("#include \"Net/UnrealNetwork.h\"\n");
	Header += FString::Printf(TEXT("#include \"%s.generated.h\"\n\n"), *FileName);

	Header += TEXT("#define ATTRIBUTE_ACCESSORS(ClassName, PropertyName) \\\n");
	Header += TEXT("\tGAMEPLAYATTRIBUTE_PROPERTY_GETTER(ClassName, PropertyName) \\\n");
	Header += TEXT("\tGAMEPLAYATTRIBUTE_VALUE_GETTER(PropertyName) \\\n");
	Header += TEXT("\tGAMEPLAYATTRIBUTE_VALUE_SETTER(PropertyName) \\\n");
	Header += TEXT("\tGAMEPLAYATTRIBUTE_VALUE_INITTER(PropertyName)\n\n");

	Header += TEXT("UCLASS()\n");
	Header += FString::Printf(TEXT("class %s %s : public UAttributeSet\n"), *ModuleName, *ClassName);
	Header += TEXT("{\n\tGENERATED_BODY()\n\npublic:\n");
	Header += FString::Printf(TEXT("\t%s();\n\n"), *ClassName);
	Header += TEXT("\tvirtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;\n\n");

	TArray<FString> AttributeNames;
	for (const TSharedPtr<FJsonValue>& AttrVal : *AttributesArray)
	{
		const TSharedPtr<FJsonObject>* AttrObjPtr;
		if (!AttrVal->TryGetObject(AttrObjPtr)) continue;

		FString AttrName;
		(*AttrObjPtr)->TryGetStringField(TEXT("name"), AttrName);
		if (AttrName.IsEmpty()) continue;

		bool bReplicated = true;
		(*AttrObjPtr)->TryGetBoolField(TEXT("replicated"), bReplicated);

		AttributeNames.Add(AttrName);

		if (bReplicated)
			Header += FString::Printf(TEXT("\tUPROPERTY(BlueprintReadOnly, Category = \"Attributes\", ReplicatedUsing = OnRep_%s)\n"), *AttrName);
		else
			Header += TEXT("\tUPROPERTY(BlueprintReadOnly, Category = \"Attributes\")\n");

		Header += FString::Printf(TEXT("\tFGameplayAttributeData %s;\n"), *AttrName);
		Header += FString::Printf(TEXT("\tATTRIBUTE_ACCESSORS(%s, %s)\n\n"), *ClassName, *AttrName);

		if (bReplicated)
			Header += FString::Printf(TEXT("\tUFUNCTION()\n\tvirtual void OnRep_%s(const FGameplayAttributeData& Old%s);\n\n"), *AttrName, *AttrName);
	}
	Header += TEXT("};\n");

	// Build source
	FString Source;
	Source += FString::Printf(TEXT("#include \"%s.h\"\n#include \"Net/UnrealNetwork.h\"\n\n"), *FileName);
	Source += FString::Printf(TEXT("%s::%s() {\n"), *ClassName, *ClassName);
	for (const FString& A : AttributeNames)
		Source += FString::Printf(TEXT("\tInit%s(100.0f);\n"), *A);
	Source += TEXT("}\n\n");

	Source += FString::Printf(TEXT("void %s::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const {\n"), *ClassName);
	Source += TEXT("\tSuper::GetLifetimeReplicatedProps(OutLifetimeProps);\n");
	for (const FString& A : AttributeNames)
		Source += FString::Printf(TEXT("\tDOREPLIFETIME_CONDITION_NOTIFY(%s, %s, COND_None, REPNOTIFY_Always);\n"), *ClassName, *A);
	Source += TEXT("}\n\n");

	for (const FString& A : AttributeNames)
	{
		Source += FString::Printf(TEXT("void %s::OnRep_%s(const FGameplayAttributeData& Old%s) {\n"), *ClassName, *A, *A);
		Source += FString::Printf(TEXT("\tGAMEPLAYATTRIBUTE_REPNOTIFY(%s, %s, Old%s);\n}\n\n"), *ClassName, *A, *A);
	}

	// Write files
	FString SourceDir = FPaths::GameSourceDir();
	FString SubDir;
	Params->TryGetStringField(TEXT("source_directory"), SubDir);
	if (SubDir.IsEmpty()) SubDir = FString(FApp::GetProjectName());

	FString HeaderPath = FPaths::Combine(SourceDir, SubDir, FileName + TEXT(".h"));
	FString SourcePath = FPaths::Combine(SourceDir, SubDir, FileName + TEXT(".cpp"));

	FFileHelper::SaveStringToFile(Header, *HeaderPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
	FFileHelper::SaveStringToFile(Source, *SourcePath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);

	Result.bSuccess = true;
	Result.ModifiedPaths.Add(HeaderPath);
	Result.ModifiedPaths.Add(SourcePath);
	Result.ResultMessage = FString::Printf(
		TEXT("Created AttributeSet '%s' with %d attributes.\n  Header: %s\n  Source: %s\n\n"
			 "IMPORTANT: Run trigger_compile before using other GAS tools."), *ClassName, AttributeNames.Num(), *HeaderPath, *SourcePath);
	return Result;
}

// ============================================================================
// gas_setup_asc
// ============================================================================

FAutonomixActionResult FAutonomixGASActions::ExecuteSetupASC(
	const TSharedRef<FJsonObject>& Params, FAutonomixActionResult& Result)
{
	FString AssetPath;
	if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
	{
		Result.Errors.Add(TEXT("Missing required field: 'asset_path'"));
		return Result;
	}

	UBlueprint* Blueprint = LoadObject<UBlueprint>(nullptr, *AssetPath);
	if (!Blueprint)
	{
		Result.Errors.Add(FString::Printf(TEXT("Blueprint not found: '%s'"), *AssetPath));
		return Result;
	}

	USimpleConstructionScript* SCS = Blueprint->SimpleConstructionScript;
	if (!SCS)
	{
		Result.Errors.Add(TEXT("Blueprint has no SimpleConstructionScript."));
		return Result;
	}

	// Check if ASC already exists
	for (USCS_Node* Node : SCS->GetAllNodes())
	{
		if (Node && Node->ComponentClass && Node->ComponentClass->IsChildOf(UAbilitySystemComponent::StaticClass()))
		{
			Result.bSuccess = true;
			Result.ResultMessage = FString::Printf(TEXT("ASC already exists on '%s'. No changes."), *AssetPath);
			return Result;
		}
	}

	Blueprint->Modify();
	USCS_Node* ASCNode = SCS->CreateNode(UAbilitySystemComponent::StaticClass(), TEXT("AbilitySystemComp"));
	if (!ASCNode)
	{
		Result.Errors.Add(TEXT("Failed to create ASC SCS node."));
		return Result;
	}
	SCS->AddNode(ASCNode);

	FKismetEditorUtilities::CompileBlueprint(Blueprint, EBlueprintCompileOptions::SkipGarbageCollection);
	Blueprint->MarkPackageDirty();

	Result.bSuccess = true;
	Result.ModifiedAssets.Add(AssetPath);
	Result.ResultMessage = FString::Printf(
		TEXT("Added AbilitySystemComponent to '%s'.\n\n"
			 "NEXT: Implement IAbilitySystemInterface in C++ and call InitAbilityActorInfo."), *AssetPath);
	return Result;
}

// ============================================================================
// gas_create_effect
// ============================================================================

FAutonomixActionResult FAutonomixGASActions::ExecuteCreateEffect(
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
	UBlueprintFactory* Factory = NewObject<UBlueprintFactory>();
	Factory->ParentClass = UGameplayEffect::StaticClass();
	Factory->bSkipClassPicker = true;

	UObject* NewAsset = AssetTools.CreateAsset(AssetName, PackagePath, UBlueprint::StaticClass(), Factory);
	if (!NewAsset)
	{
		Result.Errors.Add(FString::Printf(TEXT("Failed to create GE at '%s'."), *AssetPath));
		return Result;
	}

	UBlueprint* GEBlueprint = Cast<UBlueprint>(NewAsset);
	if (!GEBlueprint || !GEBlueprint->GeneratedClass)
	{
		Result.Errors.Add(TEXT("Invalid GE Blueprint."));
		return Result;
	}

	UGameplayEffect* GECDO = Cast<UGameplayEffect>(GEBlueprint->GeneratedClass->GetDefaultObject());
	if (!GECDO) { Result.Errors.Add(TEXT("No GE CDO.")); return Result; }

	GECDO->Modify();
	FString Report;

	// Duration Policy (public property)
	FString DurationStr;
	Params->TryGetStringField(TEXT("duration_policy"), DurationStr);
	DurationStr = DurationStr.ToLower();

	if (DurationStr == TEXT("instant"))
	{
		GECDO->DurationPolicy = EGameplayEffectDurationType::Instant;
		Report += TEXT("Duration: Instant\n");
	}
	else if (DurationStr == TEXT("has_duration") || DurationStr == TEXT("duration"))
	{
		GECDO->DurationPolicy = EGameplayEffectDurationType::HasDuration;
		float Duration = 5.0f;
		Params->TryGetNumberField(TEXT("duration_seconds"), Duration);
		GECDO->DurationMagnitude = FScalableFloat(Duration);
		Report += FString::Printf(TEXT("Duration: %.1fs\n"), Duration);
	}
	else if (DurationStr == TEXT("infinite"))
	{
		GECDO->DurationPolicy = EGameplayEffectDurationType::Infinite;
		Report += TEXT("Duration: Infinite\n");
	}
	else
	{
		GECDO->DurationPolicy = EGameplayEffectDurationType::Instant;
		Report += TEXT("Duration: Instant (default)\n");
	}

	// Modifiers (public array)
	const TArray<TSharedPtr<FJsonValue>>* ModifiersArray;
	if (Params->TryGetArrayField(TEXT("modifiers"), ModifiersArray))
	{
		for (const TSharedPtr<FJsonValue>& ModVal : *ModifiersArray)
		{
			const TSharedPtr<FJsonObject>* ModObjPtr;
			if (!ModVal->TryGetObject(ModObjPtr)) continue;

			FGameplayModifierInfo Modifier;
			FString OpStr;
			(*ModObjPtr)->TryGetStringField(TEXT("operation"), OpStr);
			OpStr = OpStr.ToLower();

			if (OpStr == TEXT("add") || OpStr == TEXT("additive"))
				Modifier.ModifierOp = EGameplayModOp::Additive;
			else if (OpStr == TEXT("multiply"))
				Modifier.ModifierOp = EGameplayModOp::Multiplicitive;
			else if (OpStr == TEXT("divide"))
				Modifier.ModifierOp = EGameplayModOp::Division;
			else if (OpStr == TEXT("override"))
				Modifier.ModifierOp = EGameplayModOp::Override;
			else
				Modifier.ModifierOp = EGameplayModOp::Additive;

			float Magnitude = 0.0f;
			(*ModObjPtr)->TryGetNumberField(TEXT("magnitude"), Magnitude);
			Modifier.ModifierMagnitude = FScalableFloat(Magnitude);

			FString AttributeStr;
			(*ModObjPtr)->TryGetStringField(TEXT("attribute"), AttributeStr);

			GECDO->Modifiers.Add(Modifier);
			Report += FString::Printf(TEXT("Modifier: %s %.1f to %s\n"), *OpStr, Magnitude, *AttributeStr);
		}
	}

	// Tags: Use reflection to access InheritableOwnedTagsContainer (public on GE CDO)
	// For grant_tags on the GE itself:
	const TArray<TSharedPtr<FJsonValue>>* GrantTagsArray;
	if (Params->TryGetArrayField(TEXT("grant_tags"), GrantTagsArray))
	{
		for (const TSharedPtr<FJsonValue>& TagVal : *GrantTagsArray)
		{
			FString TagStr;
			if (TagVal->TryGetString(TagStr))
			{
				FGameplayTag Tag = FGameplayTag::RequestGameplayTag(FName(*TagStr), false);
				if (Tag.IsValid())
				{
					// Use reflection to set tags on the CDO — handles both pre and post 5.3 layouts
					AutonomixGASReflection::AddTagToContainer(GECDO, TEXT("InheritableOwnedTagsContainer"), Tag);
					Report += FString::Printf(TEXT("Owned tag: %s\n"), *TagStr);
				}
				else
				{
					Result.Warnings.Add(FString::Printf(TEXT("Tag '%s' not found. Register first."), *TagStr));
				}
			}
		}
	}

	// Asset tags via reflection
	const TArray<TSharedPtr<FJsonValue>>* AssetTagsArray;
	if (Params->TryGetArrayField(TEXT("asset_tags"), AssetTagsArray))
	{
		for (const TSharedPtr<FJsonValue>& TagVal : *AssetTagsArray)
		{
			FString TagStr;
			if (TagVal->TryGetString(TagStr))
			{
				FGameplayTag Tag = FGameplayTag::RequestGameplayTag(FName(*TagStr), false);
				if (Tag.IsValid())
				{
					AutonomixGASReflection::AddTagToContainer(GECDO, TEXT("InheritableGameplayEffectTags"), Tag);
					Report += FString::Printf(TEXT("Asset tag: %s\n"), *TagStr);
				}
			}
		}
	}

	FKismetEditorUtilities::CompileBlueprint(GEBlueprint, EBlueprintCompileOptions::SkipGarbageCollection);
	GEBlueprint->MarkPackageDirty();

	Result.bSuccess = true;
	Result.ModifiedAssets.Add(AssetPath);
	Result.ResultMessage = FString::Printf(TEXT("Created GameplayEffect '%s':\n%s"), *AssetPath, *Report);
	return Result;
}

// ============================================================================
// gas_create_ability
// ============================================================================

FAutonomixActionResult FAutonomixGASActions::ExecuteCreateAbility(
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
	UBlueprintFactory* Factory = NewObject<UBlueprintFactory>();
	Factory->ParentClass = UGameplayAbility::StaticClass();
	Factory->bSkipClassPicker = true;

	UObject* NewAsset = AssetTools.CreateAsset(AssetName, PackagePath, UBlueprint::StaticClass(), Factory);
	if (!NewAsset)
	{
		Result.Errors.Add(FString::Printf(TEXT("Failed to create GA at '%s'."), *AssetPath));
		return Result;
	}

	UBlueprint* GABlueprint = Cast<UBlueprint>(NewAsset);
	if (!GABlueprint || !GABlueprint->GeneratedClass)
	{
		Result.Errors.Add(TEXT("Invalid GA Blueprint."));
		return Result;
	}

	UGameplayAbility* GACDO = Cast<UGameplayAbility>(GABlueprint->GeneratedClass->GetDefaultObject());
	if (!GACDO) { Result.Errors.Add(TEXT("No GA CDO.")); return Result; }

	GACDO->Modify();
	FString Report;

	// Instancing Policy (protected — use reflection)
	FString InstancingStr;
	Params->TryGetStringField(TEXT("instancing_policy"), InstancingStr);
	InstancingStr = InstancingStr.ToLower();

	EGameplayAbilityInstancingPolicy::Type InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
	if (InstancingStr == TEXT("instanced_per_execution"))
		InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerExecution;
	else if (InstancingStr == TEXT("non_instanced"))
		InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor; // NonInstanced is deprecated

	AutonomixGASReflection::SetEnumProperty(GACDO, TEXT("InstancingPolicy"), InstancingPolicy);
	Report += FString::Printf(TEXT("Instancing: %s\n"), *InstancingStr);

	// Net Execution Policy (protected — use reflection)
	FString NetPolicyStr;
	Params->TryGetStringField(TEXT("net_execution_policy"), NetPolicyStr);
	NetPolicyStr = NetPolicyStr.ToLower();

	EGameplayAbilityNetExecutionPolicy::Type NetPolicy = EGameplayAbilityNetExecutionPolicy::LocalPredicted;
	if (NetPolicyStr == TEXT("server_only"))
		NetPolicy = EGameplayAbilityNetExecutionPolicy::ServerOnly;
	else if (NetPolicyStr == TEXT("local_only"))
		NetPolicy = EGameplayAbilityNetExecutionPolicy::LocalOnly;
	else if (NetPolicyStr == TEXT("server_initiated"))
		NetPolicy = EGameplayAbilityNetExecutionPolicy::ServerInitiated;

	AutonomixGASReflection::SetEnumProperty(GACDO, TEXT("NetExecutionPolicy"), NetPolicy);
	Report += FString::Printf(TEXT("Net: %s\n"), *NetPolicyStr);

	// Ability Tags (public on CDO)
	const TArray<TSharedPtr<FJsonValue>>* AbilityTagsArray;
	if (Params->TryGetArrayField(TEXT("ability_tags"), AbilityTagsArray))
	{
		for (const TSharedPtr<FJsonValue>& TagVal : *AbilityTagsArray)
		{
			FString TagStr;
			if (TagVal->TryGetString(TagStr))
			{
				FGameplayTag Tag = FGameplayTag::RequestGameplayTag(FName(*TagStr), false);
				if (Tag.IsValid())
				{
					AutonomixGASReflection::AddTagToContainer(GACDO, TEXT("AbilityTags"), Tag);
					Report += FString::Printf(TEXT("Ability tag: %s\n"), *TagStr);
				}
				else
				{
					Result.Warnings.Add(FString::Printf(TEXT("Tag '%s' not found."), *TagStr));
				}
			}
		}
	}

	// Cooldown GE class (protected — use reflection)
	FString CooldownGEPath;
	if (Params->TryGetStringField(TEXT("cooldown_effect"), CooldownGEPath))
	{
		UBlueprint* CooldownBP = LoadObject<UBlueprint>(nullptr, *CooldownGEPath);
		if (CooldownBP && CooldownBP->GeneratedClass)
		{
			AutonomixGASReflection::SetClassProperty(GACDO, TEXT("CooldownGameplayEffectClass"), CooldownBP->GeneratedClass);
			Report += FString::Printf(TEXT("Cooldown GE: %s\n"), *CooldownGEPath);
		}
	}

	// Cost GE class (protected — use reflection)
	FString CostGEPath;
	if (Params->TryGetStringField(TEXT("cost_effect"), CostGEPath))
	{
		UBlueprint* CostBP = LoadObject<UBlueprint>(nullptr, *CostGEPath);
		if (CostBP && CostBP->GeneratedClass)
		{
			AutonomixGASReflection::SetClassProperty(GACDO, TEXT("CostGameplayEffectClass"), CostBP->GeneratedClass);
			Report += FString::Printf(TEXT("Cost GE: %s\n"), *CostGEPath);
		}
	}

	FKismetEditorUtilities::CompileBlueprint(GABlueprint, EBlueprintCompileOptions::SkipGarbageCollection);
	GABlueprint->MarkPackageDirty();

	Result.bSuccess = true;
	Result.ModifiedAssets.Add(AssetPath);
	Result.ResultMessage = FString::Printf(
		TEXT("Created GameplayAbility '%s':\n%s\n"
			 "Use inject_blueprint_nodes_t3d to add the ability graph."), *AssetPath, *Report);
	return Result;
}

#undef LOCTEXT_NAMESPACE
