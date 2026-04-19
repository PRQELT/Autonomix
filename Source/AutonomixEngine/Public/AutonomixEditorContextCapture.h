// Copyright Autonomix. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AutonomixTypes.h"

/**
 * Captures real-time editor context: selected actors, active level,
 * open editor windows, viewport camera. Injected into the system prompt
 * so Claude knows what the user is looking at.
 */
class AUTONOMIXENGINE_API FAutonomixEditorContextCapture
{
public:
	FAutonomixEditorContextCapture();
	~FAutonomixEditorContextCapture();

	/** Build a snapshot of the current editor state */
	FAutonomixEditorContext CaptureContext();

	/** Build a context string suitable for the system prompt */
	FString BuildContextString();
};
