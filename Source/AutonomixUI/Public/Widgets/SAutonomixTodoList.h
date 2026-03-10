// Copyright Autonomix. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "AutonomixTypes.h"

class SVerticalBox;

/**
 * Collapsible todo list widget displayed in the main panel.
 *
 * Modeled after Roo Code's TodoListDisplay component.
 * Shows a compact summary when collapsed (current in-progress item + progress count),
 * and a full checklist when expanded.
 *
 * Status icons:
 *   [ ] Pending   — dimmed
 *   [-] InProgress — yellow highlight
 *   [x] Completed  — green with checkmark
 */
class AUTONOMIXUI_API SAutonomixTodoList : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SAutonomixTodoList) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	/** Replace the entire todo list with new items. Called when the AI uses update_todo_list. */
	void SetTodos(const TArray<FAutonomixTodoItem>& InTodos);

	/** Get the current todo list */
	const TArray<FAutonomixTodoItem>& GetTodos() const { return Todos; }

	/** Returns true if there are any todos to display */
	bool HasTodos() const { return Todos.Num() > 0; }

	/** Parse a markdown checklist string into todo items.
	 *  Recognizes: [x] completed, [-] in_progress, [ ] pending */
	static TArray<FAutonomixTodoItem> ParseMarkdownChecklist(const FString& MarkdownText);

private:
	/** Rebuild the Slate widget tree from current Todos */
	void RebuildList();

	/** Get the most important todo for collapsed display (first in_progress, then first pending) */
	const FAutonomixTodoItem* GetMostImportantTodo() const;

	TArray<FAutonomixTodoItem> Todos;

	TSharedPtr<SVerticalBox> TodoContainer;
	TSharedPtr<SVerticalBox> ExpandedList;

	bool bIsExpanded = false;
};
