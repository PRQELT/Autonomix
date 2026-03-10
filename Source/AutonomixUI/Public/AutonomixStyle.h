// Copyright Autonomix. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Styling/SlateStyle.h"

/**
 * Manages Slate style definitions for the Autonomix UI.
 * Handles icons, brushes, text styles, and color palette.
 */
class AUTONOMIXUI_API FAutonomixStyle
{
public:
	static void Initialize();
	static void Shutdown();
	static void ReloadTextures();
	static const ISlateStyle& Get();
	static FName GetStyleSetName();

	/** Get the plugin icon brush */
	static const FSlateBrush* GetPluginIcon();

private:
	static TSharedRef<FSlateStyleSet> Create();
	static TSharedPtr<FSlateStyleSet> StyleInstance;
};
