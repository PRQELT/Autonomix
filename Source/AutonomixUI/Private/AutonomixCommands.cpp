// Copyright Autonomix. All Rights Reserved.

#include "AutonomixCommands.h"

#define LOCTEXT_NAMESPACE "FAutonomixCommands"

FAutonomixCommands::FAutonomixCommands()
	: TCommands<FAutonomixCommands>(
		TEXT("Autonomix"),
		NSLOCTEXT("Contexts", "Autonomix", "Autonomix Plugin"),
		NAME_None,
		TEXT("AutonomixStyle"))
{
}

void FAutonomixCommands::RegisterCommands()
{
	UI_COMMAND(OpenAutonomixPanel, "Autonomix", "Open the Autonomix AI assistant panel", EUserInterfaceActionType::Button, FInputChord(EModifierKey::Control | EModifierKey::Shift, EKeys::A));
	UI_COMMAND(SendPrompt, "Send", "Send the current prompt to the AI", EUserInterfaceActionType::Button, FInputChord());
	UI_COMMAND(CancelRequest, "Cancel", "Cancel the current AI request", EUserInterfaceActionType::Button, FInputChord(EKeys::Escape));
	UI_COMMAND(ClearConversation, "Clear", "Clear the conversation history", EUserInterfaceActionType::Button, FInputChord());
}

#undef LOCTEXT_NAMESPACE
