// Copyright Autonomix. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Framework/Commands/Commands.h"

/**
 * Editor commands for the Autonomix plugin.
 * Registers keyboard shortcuts and menu commands.
 */
class AUTONOMIXUI_API FAutonomixCommands : public TCommands<FAutonomixCommands>
{
public:
	FAutonomixCommands();

	virtual void RegisterCommands() override;

	/** Command to open the Autonomix chat panel */
	TSharedPtr<FUICommandInfo> OpenAutonomixPanel;

	/** Command to send the current prompt */
	TSharedPtr<FUICommandInfo> SendPrompt;

	/** Command to cancel the current AI request */
	TSharedPtr<FUICommandInfo> CancelRequest;

	/** Command to clear the conversation */
	TSharedPtr<FUICommandInfo> ClearConversation;
};
