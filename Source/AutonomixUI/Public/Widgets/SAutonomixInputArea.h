// Copyright Autonomix. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "AutonomixTypes.h"  // for EConversationState

DECLARE_DELEGATE_OneParam(FOnAutonomixPromptSubmitted, const FString& /*PromptText*/);
DECLARE_DELEGATE(FOnAutonomixStopRequested);

/**
 * Multi-line input area with send button, @-mention autocomplete, and slash command autocomplete.
 *
 * Features added from Roo Code's ChatTextArea.tsx:
 *   - Ctrl+Enter to submit (instead of only clicking Send button)
 *   - @ key → floating mention autocomplete popup (file paths, @errors, @selection, etc.)
 *   - / key at start → slash command autocomplete popup (/new-actor, /fix-errors, etc.)
 *   - Queued message count badge shown when AI is processing
 *   - Auto-approve quick toggle dropdown (Ask All / Auto: Read-Only / Auto: All)
 */
class AUTONOMIXUI_API SAutonomixInputArea : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SAutonomixInputArea) {}
		SLATE_EVENT(FOnAutonomixPromptSubmitted, OnPromptSubmitted)
		SLATE_EVENT(FOnAutonomixStopRequested, OnStopRequested)
		/** Optional: array of @-mention suggestions (file paths, keywords) */
		SLATE_ATTRIBUTE(TArray<FString>, MentionSuggestions)
		/** Optional: count of queued messages waiting to be processed */
		SLATE_ATTRIBUTE(int32, QueuedMessageCount)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	/** Clear the input field */
	void ClearInput();

	/** Set focus to the input field */
	void FocusInput();

	/** Enable or disable the send button and keyboard submission (legacy — prefer SetConversationState) */
	void SetSendEnabled(bool bEnabled);

	/** Update the button state based on conversation state (Send vs Stop swap) */
	void SetConversationState(EConversationState NewState);

	/** Update the queued message count badge */
	void SetQueuedMessageCount(int32 Count);

	/** Set @-mention suggestions for the autocomplete popup */
	void SetMentionSuggestions(const TArray<FString>& Suggestions);

	/** Set slash command suggestions */
	void SetSlashCommandSuggestions(const TArray<FString>& Commands);

	/** Insert text at the current cursor position (used by mention/slash selection) */
	void InsertText(const FString& Text);

	/** Get current input text */
	FString GetCurrentText() const;

private:
	// ---- Core widget refs ----
	TSharedPtr<class SMultiLineEditableText> InputTextBox;
	TSharedPtr<class SBorder> SendButtonContainer;
	TSharedPtr<class SButton> SendButton;
	TSharedPtr<class SButton> StopButton;
	TSharedPtr<class STextBlock> QueueBadgeText;
	TSharedPtr<class SOverlay> AutocompleteOverlay;

	// ---- State ----
	FOnAutonomixPromptSubmitted OnPromptSubmitted;
	FOnAutonomixStopRequested OnStopRequested;
	EConversationState CurrentConversationState = EConversationState::Idle;
	bool bSendEnabled = true;
	int32 QueuedCount = 0;

	// ---- Autocomplete state ----
	bool bShowMentionPopup = false;
	bool bShowSlashPopup = false;
	TArray<FString> MentionSuggestions;
	TArray<FString> SlashSuggestions;
	int32 PopupSelectedIndex = 0;

	// ---- Widget helpers ----
	TSharedRef<SWidget> BuildAutocompletePopup();
	TSharedRef<SWidget> BuildQueueBadge();
	TSharedRef<SWidget> BuildAutoApproveToggle();

	// ---- Event handlers ----
	FReply OnSendClicked();
	FReply OnStopClicked();
	void SubmitPrompt();
	void OnTextChanged(const FText& NewText);
	FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& KeyEvent);

	// ---- Autocomplete logic ----
	void ShowMentionPopup(const FString& PartialText);
	void ShowSlashPopup(const FString& PartialText);
	void HideAutocompletePopup();
	void SelectPopupItem(int32 Index);
	FString GetCurrentPartialToken() const;

	// ---- Auto-approve toggle ----
	TSharedRef<SWidget> MakeAutoApproveMenu();
	FText GetAutoApproveLabel() const;
};
