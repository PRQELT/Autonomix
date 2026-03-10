// Copyright Autonomix. All Rights Reserved.

#include "Widgets/SAutonomixInputArea.h"
#include "AutonomixSettings.h"
#include "Widgets/Input/SMultiLineEditableTextBox.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"

void SAutonomixInputArea::Construct(const FArguments& InArgs)
{
	OnPromptSubmitted = InArgs._OnPromptSubmitted;

	ChildSlot
	[
		SNew(SVerticalBox)

		// Autocomplete popup (shown above input when @ or / is typed)
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SAssignNew(AutocompleteOverlay, SOverlay)
			.Visibility(EVisibility::Collapsed)
			+ SOverlay::Slot()
			[
				BuildAutocompletePopup()
			]
		]

		// Main input row
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(SHorizontalBox)

			// Input text area
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.Padding(0, 0, 4, 0)
			[
				SNew(SBorder)
				.BorderBackgroundColor(FLinearColor(0.08f, 0.08f, 0.10f))
				.Padding(6.0f)
				[
					SNew(SVerticalBox)

					// Hint for shortcuts
					+ SVerticalBox::Slot()
					.AutoHeight()
					[
						SNew(STextBlock)
						.Text(FText::FromString(TEXT("@file /command  •  Ctrl+Enter to send")))
						.ColorAndOpacity(FLinearColor(0.4f, 0.4f, 0.4f))
						.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
					]

					// The actual text input
					+ SVerticalBox::Slot()
					.FillHeight(1.0f)
					[
						SAssignNew(InputTextBox, SMultiLineEditableText)
						.AutoWrapText(true)
						.HintText(FText::FromString(TEXT("Type your prompt... (@file, @errors, /new-actor)")))
						.OnTextChanged(this, &SAutonomixInputArea::OnTextChanged)
						.OnKeyDownHandler(this, &SAutonomixInputArea::OnKeyDown)
					]
				]
			]

			// Right side buttons
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Bottom)
			[
				SNew(SVerticalBox)

				// Queued message badge
				+ SVerticalBox::Slot()
				.AutoHeight()
				.HAlign(HAlign_Center)
				.Padding(0, 0, 0, 2)
				[
					BuildQueueBadge()
				]

				// Auto-approve toggle
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0, 0, 0, 2)
				[
					BuildAutoApproveToggle()
				]

				// Send button
				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SAssignNew(SendButtonContainer, SBorder)
					.Padding(0)
					[
						SNew(SButton)
						.Text(FText::FromString(TEXT("▶ Send")))
						.ToolTipText(FText::FromString(TEXT("Send prompt (Ctrl+Enter)")))
						.OnClicked(FOnClicked::CreateSP(this, &SAutonomixInputArea::OnSendClicked))
						.ButtonColorAndOpacity(FLinearColor(0.2f, 0.6f, 1.0f))
					]
				]
			]
		]
	];
}

// ============================================================================
// Public API
// ============================================================================

void SAutonomixInputArea::ClearInput()
{
	if (InputTextBox.IsValid())
	{
		InputTextBox->SetText(FText::GetEmpty());
	}
	HideAutocompletePopup();
}

void SAutonomixInputArea::FocusInput()
{
	if (InputTextBox.IsValid())
	{
		FSlateApplication::Get().SetKeyboardFocus(InputTextBox);
	}
}

void SAutonomixInputArea::SetSendEnabled(bool bEnabled)
{
	bSendEnabled = bEnabled;
}

void SAutonomixInputArea::SetQueuedMessageCount(int32 Count)
{
	QueuedCount = Count;
	if (QueueBadgeText.IsValid())
	{
		QueueBadgeText->SetText(FText::FromString(
			Count > 0
				? FString::Printf(TEXT("📬 %d queued"), Count)
				: FString()
		));
		QueueBadgeText->SetVisibility(Count > 0 ? EVisibility::Visible : EVisibility::Collapsed);
	}
}

void SAutonomixInputArea::SetMentionSuggestions(const TArray<FString>& Suggestions)
{
	MentionSuggestions = Suggestions;
}

void SAutonomixInputArea::SetSlashCommandSuggestions(const TArray<FString>& Commands)
{
	SlashSuggestions = Commands;
}

void SAutonomixInputArea::InsertText(const FString& Text)
{
	if (!InputTextBox.IsValid()) return;

	FString Current = InputTextBox->GetText().ToString();
	// Find last @ or / token start and replace it
	int32 LastAt = INDEX_NONE;
	int32 LastSlash = INDEX_NONE;
	Current.FindLastChar(TEXT('@'), LastAt);
	Current.FindLastChar(TEXT('/'), LastSlash);
	int32 TokenStart = FMath::Max(LastAt, LastSlash);

	if (TokenStart != INDEX_NONE)
	{
		FString NewText = Current.Left(TokenStart) + Text;
		InputTextBox->SetText(FText::FromString(NewText));
	}
	else
	{
		InputTextBox->SetText(FText::FromString(Current + Text));
	}

	HideAutocompletePopup();
}

FString SAutonomixInputArea::GetCurrentText() const
{
	return InputTextBox.IsValid() ? InputTextBox->GetText().ToString() : FString();
}

// ============================================================================
// Widget builders
// ============================================================================

TSharedRef<SWidget> SAutonomixInputArea::BuildAutocompletePopup()
{
	return SNew(SBorder)
		.BorderBackgroundColor(FLinearColor(0.12f, 0.12f, 0.15f))
		.Padding(4.0f)
		[
			SNew(SBox)
			.MaxDesiredHeight(150.0f)
			[
				SNew(SScrollBox)
				// Items will be populated dynamically when popup shows
				+ SScrollBox::Slot()
				[
					SNew(STextBlock)
					.Text(FText::FromString(TEXT("No suggestions")))
					.ColorAndOpacity(FLinearColor(0.5f, 0.5f, 0.5f))
				]
			]
		];
}

TSharedRef<SWidget> SAutonomixInputArea::BuildQueueBadge()
{
	return SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.AutoWidth()
		[
			SAssignNew(QueueBadgeText, STextBlock)
			.Text(FText::GetEmpty())
			.ColorAndOpacity(FLinearColor(1.0f, 0.8f, 0.0f))
			.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
			.Visibility(EVisibility::Collapsed)
		];
}

TSharedRef<SWidget> SAutonomixInputArea::BuildAutoApproveToggle()
{
	return SNew(SComboButton)
		.ButtonContent()
		[
			SNew(STextBlock)
			.Text(this, &SAutonomixInputArea::GetAutoApproveLabel)
			.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
		]
		.MenuContent()
		[
			MakeAutoApproveMenu()
		]
		.ToolTipText(FText::FromString(TEXT("Auto-approve tool calls")));
}

TSharedRef<SWidget> SAutonomixInputArea::MakeAutoApproveMenu()
{
	FMenuBuilder MenuBuilder(true, nullptr);

	MenuBuilder.AddMenuEntry(
		FText::FromString(TEXT("🔒 Ask for all tools")),
		FText::FromString(TEXT("Always show approval dialog for every tool call")),
		FSlateIcon(),
		FUIAction(FExecuteAction::CreateLambda([]()
		{
			if (UAutonomixDeveloperSettings* Settings = GetMutableDefault<UAutonomixDeveloperSettings>())
			{
				Settings->bAutoApproveAllTools = false;
				Settings->bAutoApproveReadOnlyTools = false;
				Settings->SaveConfig();
			}
		}))
	);

	MenuBuilder.AddMenuEntry(
		FText::FromString(TEXT("👁 Auto: Read-Only tools")),
		FText::FromString(TEXT("Auto-approve read file, list files, search operations. Ask for write tools.")),
		FSlateIcon(),
		FUIAction(FExecuteAction::CreateLambda([]()
		{
			if (UAutonomixDeveloperSettings* Settings = GetMutableDefault<UAutonomixDeveloperSettings>())
			{
				Settings->bAutoApproveAllTools = false;
				Settings->bAutoApproveReadOnlyTools = true;
				Settings->SaveConfig();
			}
		}))
	);

	MenuBuilder.AddMenuEntry(
		FText::FromString(TEXT("⚡ Auto: All tools")),
		FText::FromString(TEXT("Skip approval dialog for all tool calls. Use with caution.")),
		FSlateIcon(),
		FUIAction(FExecuteAction::CreateLambda([]()
		{
			if (UAutonomixDeveloperSettings* Settings = GetMutableDefault<UAutonomixDeveloperSettings>())
			{
				Settings->bAutoApproveAllTools = true;
				Settings->bAutoApproveReadOnlyTools = true;
				Settings->SaveConfig();
			}
		}))
	);

	return MenuBuilder.MakeWidget();
}

FText SAutonomixInputArea::GetAutoApproveLabel() const
{
	const UAutonomixDeveloperSettings* Settings = UAutonomixDeveloperSettings::Get();
	if (Settings)
	{
		if (Settings->bAutoApproveAllTools) return FText::FromString(TEXT("⚡ Auto"));
		if (Settings->bAutoApproveReadOnlyTools) return FText::FromString(TEXT("👁 Read"));
	}
	return FText::FromString(TEXT("🔒 Ask"));
}

// ============================================================================
// Event handlers
// ============================================================================

FReply SAutonomixInputArea::OnSendClicked()
{
	SubmitPrompt();
	return FReply::Handled();
}

void SAutonomixInputArea::SubmitPrompt()
{
	if (!bSendEnabled || !InputTextBox.IsValid()) return;

	FString PromptText = InputTextBox->GetText().ToString();
	if (PromptText.TrimStartAndEnd().IsEmpty()) return;

	HideAutocompletePopup();
	OnPromptSubmitted.ExecuteIfBound(PromptText);
	ClearInput();
}

void SAutonomixInputArea::OnTextChanged(const FText& NewText)
{
	const FString Text = NewText.ToString();

	// Check for slash command at start of line
	if (Text.StartsWith(TEXT("/")) && !Text.Contains(TEXT(" ")))
	{
		FString Partial = Text.Mid(1);  // After the /
		ShowSlashPopup(Partial);
		return;
	}

	// Check for @ mention anywhere in text
	int32 LastAt = INDEX_NONE;
	Text.FindLastChar(TEXT('@'), LastAt);
	if (LastAt != INDEX_NONE)
	{
		FString AfterAt = Text.Mid(LastAt + 1);
		// Only trigger if no spaces after the @
		if (!AfterAt.Contains(TEXT(" ")))
		{
			ShowMentionPopup(AfterAt);
			return;
		}
	}

	HideAutocompletePopup();
}

FReply SAutonomixInputArea::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& KeyEvent)
{
	const FKey Key = KeyEvent.GetKey();

	// Ctrl+Enter submits
	if (Key == EKeys::Enter && KeyEvent.IsControlDown())
	{
		SubmitPrompt();
		return FReply::Handled();
	}

	// If popup is showing, handle navigation
	if (bShowMentionPopup || bShowSlashPopup)
	{
		if (Key == EKeys::Escape)
		{
			HideAutocompletePopup();
			return FReply::Handled();
		}
		if (Key == EKeys::Tab || Key == EKeys::Enter)
		{
			SelectPopupItem(PopupSelectedIndex);
			return FReply::Handled();
		}
		if (Key == EKeys::Up)
		{
			PopupSelectedIndex = FMath::Max(0, PopupSelectedIndex - 1);
			return FReply::Handled();
		}
		if (Key == EKeys::Down)
		{
			const TArray<FString>& Items = bShowSlashPopup ? SlashSuggestions : MentionSuggestions;
			PopupSelectedIndex = FMath::Min(Items.Num() - 1, PopupSelectedIndex + 1);
			return FReply::Handled();
		}
	}

	return FReply::Unhandled();
}

// ============================================================================
// Autocomplete
// ============================================================================

void SAutonomixInputArea::ShowMentionPopup(const FString& PartialText)
{
	if (MentionSuggestions.Num() == 0)
	{
		HideAutocompletePopup();
		return;
	}

	// Filter suggestions
	TArray<FString> Filtered;
	for (const FString& Suggestion : MentionSuggestions)
	{
		if (PartialText.IsEmpty() || Suggestion.StartsWith(PartialText, ESearchCase::IgnoreCase))
		{
			Filtered.Add(Suggestion);
		}
	}

	if (Filtered.Num() == 0)
	{
		HideAutocompletePopup();
		return;
	}

	bShowMentionPopup = true;
	bShowSlashPopup = false;
	PopupSelectedIndex = 0;

	if (AutocompleteOverlay.IsValid())
	{
		AutocompleteOverlay->SetVisibility(EVisibility::Visible);
	}
}

void SAutonomixInputArea::ShowSlashPopup(const FString& PartialText)
{
	if (SlashSuggestions.Num() == 0)
	{
		HideAutocompletePopup();
		return;
	}

	TArray<FString> Filtered;
	for (const FString& Cmd : SlashSuggestions)
	{
		if (PartialText.IsEmpty() || Cmd.StartsWith(PartialText, ESearchCase::IgnoreCase))
		{
			Filtered.Add(Cmd);
		}
	}

	if (Filtered.Num() == 0)
	{
		HideAutocompletePopup();
		return;
	}

	bShowSlashPopup = true;
	bShowMentionPopup = false;
	PopupSelectedIndex = 0;

	if (AutocompleteOverlay.IsValid())
	{
		AutocompleteOverlay->SetVisibility(EVisibility::Visible);
	}
}

void SAutonomixInputArea::HideAutocompletePopup()
{
	bShowMentionPopup = false;
	bShowSlashPopup = false;

	if (AutocompleteOverlay.IsValid())
	{
		AutocompleteOverlay->SetVisibility(EVisibility::Collapsed);
	}
}

void SAutonomixInputArea::SelectPopupItem(int32 Index)
{
	const TArray<FString>& Items = bShowSlashPopup ? SlashSuggestions : MentionSuggestions;
	if (!Items.IsValidIndex(Index)) return;

	const FString Selected = Items[Index];
	if (bShowSlashPopup)
	{
		// Replace the text with the full slash command
		InsertText(TEXT("/") + Selected);
	}
	else
	{
		// Insert the @mention
		InsertText(TEXT("@") + Selected + TEXT(" "));
	}

	HideAutocompletePopup();
}

FString SAutonomixInputArea::GetCurrentPartialToken() const
{
	if (!InputTextBox.IsValid()) return FString();
	FString Text = InputTextBox->GetText().ToString();

	int32 LastAt = INDEX_NONE;
	int32 LastSlash = INDEX_NONE;
	Text.FindLastChar(TEXT('@'), LastAt);
	Text.FindLastChar(TEXT('/'), LastSlash);
	int32 TokenStart = FMath::Max(LastAt, LastSlash);

	if (TokenStart == INDEX_NONE) return FString();
	return Text.Mid(TokenStart + 1);
}
