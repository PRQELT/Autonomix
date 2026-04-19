// Copyright Autonomix. All Rights Reserved.

#include "Widgets/SAutonomixTodoList.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/SBoxPanel.h"

void SAutonomixTodoList::Construct(const FArguments& InArgs)
{
	ChildSlot
	[
		SAssignNew(TodoContainer, SVerticalBox)
	];
}

void SAutonomixTodoList::SetTodos(const TArray<FAutonomixTodoItem>& InTodos)
{
	Todos = InTodos;
	RebuildList();
}

TArray<FAutonomixTodoItem> SAutonomixTodoList::ParseMarkdownChecklist(const FString& MarkdownText)
{
	TArray<FAutonomixTodoItem> Items;
	TArray<FString> Lines;
	MarkdownText.ParseIntoArrayLines(Lines);

	for (const FString& Line : Lines)
	{
		FString Trimmed = Line.TrimStartAndEnd();
		if (Trimmed.IsEmpty()) continue;

		// Strip markdown bullet prefixes: "- ", "* ", "+ ", "1. ", "2. ", etc.
		// Claude typically sends "- [x] Task" or "* [x] Task"
		if (Trimmed.Len() >= 2)
		{
			TCHAR First = Trimmed[0];
			if ((First == TEXT('-') || First == TEXT('*') || First == TEXT('+')) && Trimmed[1] == TEXT(' '))
			{
				Trimmed = Trimmed.Mid(2).TrimStart();
			}
			else if (FChar::IsDigit(First))
			{
				// Handle numbered lists like "1. [x] Task", "12. [x] Task"
				int32 DotIdx = INDEX_NONE;
				for (int32 c = 1; c < Trimmed.Len(); c++)
				{
					if (Trimmed[c] == TEXT('.'))
					{
						DotIdx = c;
						break;
					}
					if (!FChar::IsDigit(Trimmed[c])) break;
				}
				if (DotIdx != INDEX_NONE && DotIdx + 1 < Trimmed.Len() && Trimmed[DotIdx + 1] == TEXT(' '))
				{
					Trimmed = Trimmed.Mid(DotIdx + 2).TrimStart();
				}
			}
		}

		EAutonomixTodoStatus Status = EAutonomixTodoStatus::Pending;
		FString Content;

		if (Trimmed.StartsWith(TEXT("[x]")) || Trimmed.StartsWith(TEXT("[X]")))
		{
			Status = EAutonomixTodoStatus::Completed;
			Content = Trimmed.Mid(3).TrimStartAndEnd();
		}
		else if (Trimmed.StartsWith(TEXT("[-]")))
		{
			Status = EAutonomixTodoStatus::InProgress;
			Content = Trimmed.Mid(3).TrimStartAndEnd();
		}
		else if (Trimmed.StartsWith(TEXT("[ ]")))
		{
			Status = EAutonomixTodoStatus::Pending;
			Content = Trimmed.Mid(3).TrimStartAndEnd();
		}
		else
		{
			// Not a checklist line, treat as pending todo content
			Content = Trimmed;
		}

		if (!Content.IsEmpty())
		{
			Items.Add(FAutonomixTodoItem(Content, Status));
		}
	}

	return Items;
}

const FAutonomixTodoItem* SAutonomixTodoList::GetMostImportantTodo() const
{
	// First in_progress
	for (const FAutonomixTodoItem& Item : Todos)
	{
		if (Item.Status == EAutonomixTodoStatus::InProgress) return &Item;
	}
	// Then first pending
	for (const FAutonomixTodoItem& Item : Todos)
	{
		if (Item.Status == EAutonomixTodoStatus::Pending) return &Item;
	}
	return nullptr;
}

void SAutonomixTodoList::RebuildList()
{
	if (!TodoContainer.IsValid()) return;

	TodoContainer->ClearChildren();

	if (Todos.Num() == 0)
	{
		SetVisibility(EVisibility::Collapsed);
		return;
	}

	SetVisibility(EVisibility::Visible);

	int32 CompletedCount = 0;
	for (const FAutonomixTodoItem& Item : Todos)
	{
		if (Item.Status == EAutonomixTodoStatus::Completed) CompletedCount++;
	}
	bool bAllComplete = (CompletedCount == Todos.Num());

	// --- Header row: clickable to expand/collapse ---
	TodoContainer->AddSlot()
	.AutoHeight()
	.Padding(0.0f, 2.0f)
	[
		SNew(SButton)
		.ButtonStyle(FCoreStyle::Get(), "NoBorder")
		.OnClicked_Lambda([this]()
		{
			bIsExpanded = !bIsExpanded;
			RebuildList();
			return FReply::Handled();
		})
		.Content()
		[
			SNew(SHorizontalBox)

			// Icon
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(0.0f, 0.0f, 4.0f, 0.0f)
			[
				SNew(STextBlock)
				.Text(FText::FromString(bAllComplete ? TEXT("✅") : TEXT("📋")))
				.Font(FCoreStyle::GetDefaultFontStyle("Regular", 11))
			]

			// Summary text
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text_Lambda([this, CompletedCount, bAllComplete]()
				{
					if (bAllComplete)
					{
						return FText::FromString(FString::Printf(TEXT("All %d tasks complete"), CompletedCount));
					}

					const FAutonomixTodoItem* Current = GetMostImportantTodo();
					if (!bIsExpanded && Current)
					{
						return FText::FromString(Current->Content);
					}

					return FText::FromString(FString::Printf(TEXT("Tasks: %d/%d completed"),
						CompletedCount, Todos.Num()));
				})
				.Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
				.ColorAndOpacity(FSlateColor(bAllComplete
					? FLinearColor(0.3f, 0.9f, 0.3f)
					: FLinearColor(0.8f, 0.8f, 0.8f)))
			]

			// Progress fraction
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(4.0f, 0.0f)
			[
				SNew(STextBlock)
				.Text(FText::FromString(FString::Printf(TEXT("%d/%d"), CompletedCount, Todos.Num())))
				.Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
				.ColorAndOpacity(FSlateColor(FLinearColor(0.5f, 0.5f, 0.5f)))
			]

			// Expand/collapse indicator
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(FText::FromString(bIsExpanded ? TEXT("▼") : TEXT("▶")))
				.Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
				.ColorAndOpacity(FSlateColor(FLinearColor(0.5f, 0.5f, 0.5f)))
			]
		]
	];

	// --- Expanded list ---
	if (bIsExpanded)
	{
		TSharedPtr<SScrollBox> ScrollBox;

		TodoContainer->AddSlot()
		.AutoHeight()
		.MaxHeight(300.0f)
		.Padding(8.0f, 0.0f, 0.0f, 4.0f)
		[
			SAssignNew(ScrollBox, SScrollBox)
		];

		for (int32 i = 0; i < Todos.Num(); i++)
		{
			const FAutonomixTodoItem& Item = Todos[i];

			FString StatusIcon;
			FLinearColor TextColor;
			switch (Item.Status)
			{
			case EAutonomixTodoStatus::Completed:
				StatusIcon = TEXT("✅");
				TextColor = FLinearColor(0.3f, 0.8f, 0.3f);
				break;
			case EAutonomixTodoStatus::InProgress:
				StatusIcon = TEXT("🔄");
				TextColor = FLinearColor(1.0f, 0.85f, 0.0f);
				break;
			default: // Pending
				StatusIcon = TEXT("⬜");
				TextColor = FLinearColor(0.45f, 0.45f, 0.45f);
				break;
			}

			ScrollBox->AddSlot()
			.Padding(0.0f, 1.0f)
			[
				SNew(SHorizontalBox)

				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Top)
				.Padding(0.0f, 0.0f, 4.0f, 0.0f)
				[
					SNew(STextBlock)
					.Text(FText::FromString(StatusIcon))
					.Font(FCoreStyle::GetDefaultFontStyle("Regular", 10))
				]

				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				.VAlign(VAlign_Top)
				[
					SNew(STextBlock)
					.Text(FText::FromString(Item.Content))
					.Font(FCoreStyle::GetDefaultFontStyle("Regular", 10))
					.ColorAndOpacity(FSlateColor(TextColor))
					.AutoWrapText(true)
				]
			];
		}
	}
}
