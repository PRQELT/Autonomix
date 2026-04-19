// Copyright Autonomix. All Rights Reserved.

#include "AutonomixStyle.h"
#include "Styling/SlateStyleRegistry.h"
#include "Interfaces/IPluginManager.h"
#include "Styling/SlateTypes.h"
#include "Misc/Paths.h"

TSharedPtr<FSlateStyleSet> FAutonomixStyle::StyleInstance = nullptr;

void FAutonomixStyle::Initialize()
{
	if (!StyleInstance.IsValid())
	{
		StyleInstance = Create();
		FSlateStyleRegistry::RegisterSlateStyle(*StyleInstance);
	}
}

void FAutonomixStyle::Shutdown()
{
	FSlateStyleRegistry::UnRegisterSlateStyle(*StyleInstance);
	ensure(StyleInstance.IsUnique());
	StyleInstance.Reset();
}

void FAutonomixStyle::ReloadTextures()
{
	if (FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().GetRenderer()->ReloadTextureResources();
	}
}

const ISlateStyle& FAutonomixStyle::Get()
{
	return *StyleInstance;
}

FName FAutonomixStyle::GetStyleSetName()
{
	static FName StyleSetName(TEXT("AutonomixStyle"));
	return StyleSetName;
}

const FSlateBrush* FAutonomixStyle::GetPluginIcon()
{
	return StyleInstance->GetBrush(TEXT("Autonomix.PluginIcon"));
}

TSharedRef<FSlateStyleSet> FAutonomixStyle::Create()
{
	TSharedRef<FSlateStyleSet> Style = MakeShareable(new FSlateStyleSet(GetStyleSetName()));
	Style->SetContentRoot(FPaths::ProjectPluginsDir() / TEXT("Autonomix/Content/Icons"));

	// Placeholder brush — icons will be added later
	Style->Set("Autonomix.PluginIcon", new FSlateNoResource());

	return Style;
}
