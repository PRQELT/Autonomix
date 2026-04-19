// Copyright Autonomix. All Rights Reserved.

#include "Widgets/SAutonomixMessage.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Text/SMultiLineEditableText.h"
#include "Widgets/Layout/SExpandableArea.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"

// ============================================================================
// Static helpers
// ============================================================================

bool SAutonomixMessage::DetectToolCallFromContent(const FString& Content)
{
	// Match the "🔧 Executing:" prefix used by the tool execution display format
	// The wrench emoji is U+1F527 which is encoded as a surrogate pair in UTF-16
	return Content.StartsWith(TEXT("\U0001F527")) || Content.Contains(TEXT("Executing:"));
}

FString SAutonomixMessage::ExtractToolNameFromContent(const FString& Content)
{
	// Format: "🔧 Executing: tool_name\n..."
	// Find "Executing: " and extract the tool name until newline or end
	int32 ExecIdx = Content.Find(TEXT("Executing: "));
	if (ExecIdx != INDEX_NONE)
	{
		int32 NameStart = ExecIdx + 11; // len("Executing: ")
		int32 NewlineIdx;
		if (Content.FindChar('\n', NewlineIdx) && NewlineIdx > NameStart)
		{
			return Content.Mid(NameStart, NewlineIdx - NameStart).TrimStartAndEnd();
		}
		return Content.Mid(NameStart).TrimStartAndEnd();
	}
	return TEXT("tool_call");
}

// ============================================================================
// Construct
// ============================================================================

void SAutonomixMessage::Construct(const FArguments& InArgs)
{
	MessageData = InArgs._Message;

	// ---- Determine role label & color ----
	FLinearColor RoleColor = FLinearColor::White;
	FString RoleLabel;
	switch (MessageData.Role)
	{
	case EAutonomixMessageRole::User:      RoleLabel = TEXT("You"); RoleColor = FLinearColor(0.3f, 0.6f, 1.0f); break;
	case EAutonomixMessageRole::Assistant: RoleLabel = TEXT("Autonomix"); RoleColor = FLinearColor(0.2f, 0.9f, 0.4f); break;
	case EAutonomixMessageRole::System:    RoleLabel = TEXT("System"); RoleColor = FLinearColor(0.7f, 0.7f, 0.7f); break;
	case EAutonomixMessageRole::ToolResult:RoleLabel = TEXT("Tool"); RoleColor = FLinearColor(1.0f, 0.8f, 0.2f); break;
	case EAutonomixMessageRole::Error:     RoleLabel = TEXT("Error"); RoleColor = FLinearColor(1.0f, 0.2f, 0.2f); break;
	default: break;
	}

	// ---- Detect tool call messages ----
	bool bIsToolCall = IsToolCallMessage();
	bool bIsCollapsible = MessageData.bIsCollapsible && !bIsToolCall; // non-tool collapsible (thinking, etc.)
	bool bIsError = (MessageData.Role == EAutonomixMessageRole::Error);

	// ---- Prepare body text ----
	FString BodyText;
	if (bIsToolCall)
	{
		// For tool calls, body is everything after the first line
		int32 NewlineIndex;
		if (MessageData.Content.FindChar('\n', NewlineIndex))
		{
			BodyText = MessageData.Content.Mid(NewlineIndex + 1);
		}
		else
		{
			BodyText = TEXT("");
		}
	}
	else if (bIsCollapsible)
	{
		int32 NewlineIndex;
		if (MessageData.Content.FindChar('\n', NewlineIndex))
		{
			BodyText = MessageData.Content.Mid(NewlineIndex + 1);
		}
		else
		{
			BodyText = TEXT("");
		}
	}
	else if (bIsError)
	{
		// For error messages, we will prefix with bold "❌ Error:" in the header
		// but the body text is still the full content
		BodyText = MessageData.Content;
	}
	else
	{
		BodyText = MessageData.Content;
	}

	// ---- Main text widget ----
	TSharedRef<SMultiLineEditableText> MainTextWidget = SAssignNew(ContentTextBlock, SMultiLineEditableText)
		.Text(FText::FromString(BodyText))
		.AutoWrapText(true)
		.IsReadOnly(true);

	// ---- Build the body widget based on message type ----
	TSharedPtr<SWidget> BodyWidget;

	if (bIsToolCall)
	{
		// === TOOL CALL: Collapsible SExpandableArea ===
		FString ToolName = !MessageData.ToolName.IsEmpty()
			? MessageData.ToolName
			: ExtractToolNameFromContent(MessageData.Content);

		// Tool call header: 🔧 tool_name [status icon]
		TSharedRef<SHorizontalBox> ToolHeader = SNew(SHorizontalBox)
			// Wrench icon + tool name
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(FText::FromString(FString::Printf(TEXT("\U0001F527 %s"), *ToolName)))
				.ColorAndOpacity(FSlateColor(FLinearColor(0.9f, 0.9f, 0.9f)))
				.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
			]
			// Status icon (✅/❌ or spinner-like "...")
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(8.0f, 0.0f, 0.0f, 0.0f)
			.VAlign(VAlign_Center)
			[
				SAssignNew(ToolStatusIcon, STextBlock)
				.Text(FText::FromString(TEXT("\u2026")))  // "..." while in-progress
				.ColorAndOpacity(FSlateColor(FLinearColor(0.6f, 0.6f, 0.6f)))
				.Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
			];

		// Initially expanded while in-progress, will be collapsed on completion
		BodyWidget = SAssignNew(ToolExpandableArea, SExpandableArea)
			.InitiallyCollapsed(false) // expanded while streaming/in-progress
			.HeaderContent()
			[
				ToolHeader
			]
			.BodyContent()
			[
				SNew(SBox)
				.Padding(FMargin(12.0f, 4.0f, 4.0f, 4.0f))
				[
					MainTextWidget
				]
			];
	}
	else if (bIsCollapsible)
	{
		// === GENERIC COLLAPSIBLE (thinking blocks, etc.) ===
		BodyWidget = SNew(SExpandableArea)
			.InitiallyCollapsed(true)
			.HeaderContent()
			[
				SNew(STextBlock)
				.Text(this, &SAutonomixMessage::GetHeaderText)
				.ColorAndOpacity(FSlateColor(FLinearColor(0.6f, 0.6f, 0.6f)))
			]
			.BodyContent()
			[
				MainTextWidget
			];
	}
	else
	{
		// === NORMAL MESSAGE ===
		BodyWidget = MainTextWidget;
	}

	// ---- Build the main vertical box ----
	TSharedPtr<SVerticalBox> MainBox = SNew(SVerticalBox);

	if (InArgs._ShowRoleLabel)
	{
		if (bIsError)
		{
			// Error messages: Bold "❌ Error:" prefix in red
			MainBox->AddSlot()
				.AutoHeight()
				.Padding(0, 10, 0, 2)
				[
					SNew(STextBlock)
					.Text(FText::FromString(TEXT("\u274C Error")))
					.ColorAndOpacity(FSlateColor(FLinearColor(1.0f, 0.2f, 0.2f)))
					.Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
				];
		}
		else if (!bIsToolCall) // Tool calls have their own header inside the expandable area
		{
			MainBox->AddSlot()
				.AutoHeight()
				.Padding(0, 10, 0, 2)
				[
					SNew(STextBlock)
					.Text(FText::FromString(RoleLabel))
					.ColorAndOpacity(FSlateColor(RoleColor))
				];
		}
	}

	MainBox->AddSlot()
		.AutoHeight()
		.Padding(bIsToolCall ? 0.0f : 8.0f, 0, 0, 4)
		[
			BodyWidget.ToSharedRef()
		];

	// ---- Determine background color (error messages get red tint) ----
	FLinearColor BackgroundColor = FLinearColor(0.0f, 0.0f, 0.0f, 0.0f); // transparent by default
	if (bIsError)
	{
		BackgroundColor = FLinearColor(0.25f, 0.05f, 0.05f, 0.6f); // red tint
	}

	// ---- Wrap everything in a color-coded left border ----
	FLinearColor BorderColor = GetBorderColor();

	ChildSlot
	[
		SNew(SHorizontalBox)
		// Color-coded left border strip (3px wide)
		+ SHorizontalBox::Slot()
		.AutoWidth()
		[
			SAssignNew(LeftBorderWidget, SBorder)
			.BorderBackgroundColor(BorderColor)
			.Padding(0.0f)
			[
				SNew(SBox)
				.WidthOverride(3.0f)
			]
		]
		// Message content with optional background tint
		+ SHorizontalBox::Slot()
		.FillWidth(1.0f)
		[
			SNew(SBorder)
			.BorderBackgroundColor(BackgroundColor)
			.Padding(FMargin(4.0f, 0.0f, 0.0f, 0.0f))
			[
				MainBox.ToSharedRef()
			]
		]
	];
}

// ============================================================================
// AppendText — streaming token append
// ============================================================================

void SAutonomixMessage::AppendText(const FString& DeltaText)
{
	MessageData.Content += DeltaText;

	bool bIsToolCall = IsToolCallMessage();
	bool bIsCollapsible = MessageData.bIsCollapsible && !bIsToolCall;

	FString BodyText;
	if (bIsToolCall || bIsCollapsible)
	{
		int32 NewlineIndex;
		if (MessageData.Content.FindChar('\n', NewlineIndex))
		{
			BodyText = MessageData.Content.Mid(NewlineIndex + 1);
		}
		else
		{
			BodyText = TEXT("");
		}
	}
	else
	{
		BodyText = MessageData.Content;
	}

	if (ContentTextBlock.IsValid())
	{
		ContentTextBlock->SetText(FText::FromString(BodyText));
	}
}

// ============================================================================
// MarkToolCompleted — collapse and update status icon
// ============================================================================

void SAutonomixMessage::MarkToolCompleted(bool bSuccess)
{
	bToolCompleted = true;
	bToolSuccess = bSuccess;

	// Update status icon
	if (ToolStatusIcon.IsValid())
	{
		if (bSuccess)
		{
			ToolStatusIcon->SetText(FText::FromString(TEXT("\u2705"))); // ✅
			ToolStatusIcon->SetColorAndOpacity(FSlateColor(FLinearColor(0.2f, 0.9f, 0.2f)));
		}
		else
		{
			ToolStatusIcon->SetText(FText::FromString(TEXT("\u274C"))); // ❌
			ToolStatusIcon->SetColorAndOpacity(FSlateColor(FLinearColor(1.0f, 0.2f, 0.2f)));
		}
	}

	// Collapse the expandable area after completion
	if (ToolExpandableArea.IsValid())
	{
		ToolExpandableArea->SetExpanded(false);
	}

	// Update left border color based on success/failure
	if (LeftBorderWidget.IsValid())
	{
		if (bSuccess)
		{
			LeftBorderWidget->SetBorderBackgroundColor(FLinearColor(0.2f, 0.8f, 0.2f)); // green
		}
		else
		{
			LeftBorderWidget->SetBorderBackgroundColor(FLinearColor(1.0f, 0.2f, 0.2f)); // red
		}
	}
}

// ============================================================================
// IsToolCallMessage
// ============================================================================

bool SAutonomixMessage::IsToolCallMessage() const
{
	// Check explicit ToolName first, then detect from content
	if (!MessageData.ToolName.IsEmpty())
	{
		return true;
	}
	if (MessageData.Role == EAutonomixMessageRole::Assistant && DetectToolCallFromContent(MessageData.Content))
	{
		return true;
	}
	return false;
}

// ============================================================================
// GetBorderColor — color-coded left border based on message role
// ============================================================================

FLinearColor SAutonomixMessage::GetBorderColor() const
{
	switch (MessageData.Role)
	{
	case EAutonomixMessageRole::User:
		return FLinearColor(0.3f, 0.6f, 1.0f); // blue

	case EAutonomixMessageRole::Assistant:
		// Tool calls get a yellow border while in-progress, green/red after completion
		if (IsToolCallMessage())
		{
			if (bToolCompleted)
			{
				return bToolSuccess
					? FLinearColor(0.2f, 0.8f, 0.2f)  // green
					: FLinearColor(1.0f, 0.2f, 0.2f);  // red
			}
			return FLinearColor(1.0f, 0.8f, 0.2f); // yellow while in-progress
		}
		return FLinearColor(0.0f, 0.0f, 0.0f, 0.0f); // transparent (no border for assistant)

	case EAutonomixMessageRole::ToolResult:
		return FLinearColor(0.2f, 0.8f, 0.2f); // green (success by default; overridden on error)

	case EAutonomixMessageRole::Error:
		return FLinearColor(1.0f, 0.2f, 0.2f); // red

	case EAutonomixMessageRole::System:
		return FLinearColor(0.5f, 0.5f, 0.5f); // gray

	default:
		return FLinearColor(0.0f, 0.0f, 0.0f, 0.0f); // transparent
	}
}

// ============================================================================
// GetHeaderText — for non-tool collapsible messages
// ============================================================================

FText SAutonomixMessage::GetHeaderText() const
{
	FString FirstLine;
	int32 NewlineIndex;
	if (MessageData.Content.FindChar('\n', NewlineIndex))
	{
		FirstLine = MessageData.Content.Left(NewlineIndex);
	}
	else
	{
		FirstLine = MessageData.Content;
	}

	if (FirstLine.Len() > 180)
	{
		FirstLine = FirstLine.Left(180) + TEXT("...");
	}

	if (FirstLine.TrimStartAndEnd().IsEmpty())
	{
		FirstLine = TEXT("Details...");
	}

	return FText::FromString(FirstLine);
}
