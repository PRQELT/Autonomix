// Copyright Autonomix. All Rights Reserved.

#include "Widgets/SAutonomixMainPanel.h"
#include "Widgets/SAutonomixChatView.h"
#include "Widgets/SAutonomixInputArea.h"
#include "Widgets/SAutonomixPlanPreview.h"
#include "Widgets/SAutonomixProgress.h"
#include "Widgets/SAutonomixTodoList.h"
#include "AutonomixClaudeClient.h"          // Kept for OnContextWindowExceeded downcast
#include "AutonomixInterfaces.h"
#include "AutonomixLLMClientFactory.h"
#include "AutonomixConversationManager.h"
#include "AutonomixToolSchemaRegistry.h"
#include "AutonomixActionRouter.h"
#include "AutonomixExecutionJournal.h"
#include "AutonomixEditorContextCapture.h"
#include "AutonomixContextGatherer.h"
#include "AutonomixContextManager.h"
#include "AutonomixContextCondenser.h"
#include "AutonomixTokenCounter.h"
#include "AutonomixCostTracker.h"
#include "AutonomixAutoApprovalHandler.h"
#include "AutonomixBackupManager.h"
#include "AutonomixSettings.h"
#include "AutonomixCoreModule.h"

// Phase 1: Safety & Reliability
#include "AutonomixToolRepetitionDetector.h"
#include "AutonomixIgnoreController.h"
#include "AutonomixFileContextTracker.h"
#include "AutonomixSafetyGate.h"

// Phase 2: Developer Productivity
#include "AutonomixEnvironmentDetails.h"
#include "AutonomixDiffApplicator.h"

// Phase 3: Power Features
#include "AutonomixCheckpointManager.h"
#include "AutonomixReferenceParser.h"
#include "AutonomixTaskDelegation.h"

// Phase 4: Advanced Infrastructure
#include "AutonomixTaskHistory.h"
#include "AutonomixSlashCommandRegistry.h"
#include "AutonomixSkillsManager.h"
#include "AutonomixMCPClient.h"

// Second pass: Code structure (tree-sitter equivalent for UE)
#include "AutonomixCodeStructureParser.h"

// New UI widgets
#include "Widgets/SAutonomixContextBar.h"
#include "Widgets/SAutonomixCheckpointPanel.h"
#include "Widgets/SAutonomixHistoryPanel.h"
#include "Widgets/SAutonomixFollowUpBar.h"
#include "Widgets/SAutonomixFileChangesPanel.h"

// Action executors
#include "Blueprint/AutonomixBlueprintActions.h"
#include "Material/AutonomixMaterialActions.h"
#include "Cpp/AutonomixCppActions.h"
#include "Mesh/AutonomixMeshActions.h"
#include "Level/AutonomixLevelActions.h"
#include "Settings/AutonomixSettingsActions.h"
#ifdef WITH_AUTONOMIX_PRO
#include "Build/AutonomixBuildActions.h"
#endif
#include "Performance/AutonomixPerformanceActions.h"
#include "SourceControl/AutonomixSourceControlActions.h"
#include "Context/AutonomixContextActions.h"
#include "Input/AutonomixInputActions.h"
#include "Animation/AutonomixAnimationActions.h"
#include "Widget/AutonomixWidgetActions.h"
#include "PCG/AutonomixPCGActions.h"

// v1.1: New tool executors
#include "Python/AutonomixPythonActions.h"
#include "Viewport/AutonomixViewportActions.h"
#include "DataTable/AutonomixDataTableActions.h"
#include "Diagnostics/AutonomixDiagnosticsActions.h"
#include "BehaviorTree/AutonomixBehaviorTreeActions.h"
#include "Sequencer/AutonomixSequencerActions.h"
#include "PIE/AutonomixPIEActions.h"
#include "Validation/AutonomixValidationActions.h"
#include "GAS/AutonomixGASActions.h"

#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Text/STextBlock.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Guid.h"
#include "Misc/Paths.h"
#include "Misc/MessageDialog.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace
{
	static const TCHAR* TabsManifestFileName = TEXT("tabs_manifest.json");

	static TSharedPtr<FJsonObject> TokenUsageToJson(const FAutonomixTokenUsage& Usage)
	{
		TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetNumberField(TEXT("input_tokens"), Usage.InputTokens);
		Obj->SetNumberField(TEXT("output_tokens"), Usage.OutputTokens);
		Obj->SetNumberField(TEXT("cache_creation_input_tokens"), Usage.CacheCreationInputTokens);
		Obj->SetNumberField(TEXT("cache_read_input_tokens"), Usage.CacheReadInputTokens);
		return Obj;
	}

	static FAutonomixTokenUsage TokenUsageFromJson(const TSharedPtr<FJsonObject>& Obj)
	{
		FAutonomixTokenUsage Usage;
		if (!Obj.IsValid())
		{
			return Usage;
		}

		Obj->TryGetNumberField(TEXT("input_tokens"), Usage.InputTokens);
		Obj->TryGetNumberField(TEXT("output_tokens"), Usage.OutputTokens);
		Obj->TryGetNumberField(TEXT("cache_creation_input_tokens"), Usage.CacheCreationInputTokens);
		Obj->TryGetNumberField(TEXT("cache_read_input_tokens"), Usage.CacheReadInputTokens);
		return Usage;
	}
}

void SAutonomixMainPanel::Construct(const FArguments& InArgs)
{
	InitializeBackend();

	ChildSlot
	[
		SNew(SVerticalBox)

		// Header bar with model info + security mode badge + context usage + stop button
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(8.0f, 4.0f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(FText::FromString(TEXT("Autonomix AI Assistant")))
				.Font(FCoreStyle::GetDefaultFontStyle("Bold", 14))
			]
			// Security mode badge
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(4.0f, 0.0f)
			[
				SNew(STextBlock)
				.Text_Lambda([this]()
				{
					const UAutonomixDeveloperSettings* Settings = UAutonomixDeveloperSettings::Get();
					if (!Settings) return FText::GetEmpty();
					switch (Settings->SecurityMode)
					{
					case EAutonomixSecurityMode::Sandbox: return FText::FromString(TEXT("🔒 Sandbox"));
					case EAutonomixSecurityMode::Advanced: return FText::FromString(TEXT("⚡ Advanced"));
					case EAutonomixSecurityMode::Developer: return FText::FromString(TEXT("🔓 Developer"));
					default: return FText::GetEmpty();
					}
				})
				.ColorAndOpacity_Lambda([this]() -> FSlateColor
				{
					const UAutonomixDeveloperSettings* Settings = UAutonomixDeveloperSettings::Get();
					if (!Settings) return FSlateColor(FLinearColor::White);
					switch (Settings->SecurityMode)
					{
					case EAutonomixSecurityMode::Sandbox: return FSlateColor(FLinearColor(0.2f, 0.8f, 0.2f));
					case EAutonomixSecurityMode::Advanced: return FSlateColor(FLinearColor(1.0f, 0.8f, 0.0f));
					case EAutonomixSecurityMode::Developer: return FSlateColor(FLinearColor(1.0f, 0.3f, 0.3f));
					default: return FSlateColor(FLinearColor::White);
					}
				})
			]
			// Model + token info + session cost
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(4.0f, 0.0f)
			[
				SNew(STextBlock)
				.Text_Lambda([this]()
				{
					const UAutonomixDeveloperSettings* Settings = UAutonomixDeveloperSettings::Get();
					if (Settings && Settings->IsActiveProviderApiKeySet())
					{
						FString ModelName = Settings->GetModelDisplayName();
						FString TokenInfo = FString::Printf(TEXT(" [%d tokens]"),
							SessionTokenUsage.InputTokens + SessionTokenUsage.OutputTokens);
						FString CostInfo;
						if (Settings->bShowPerRequestCost && CostTracker.GetSessionTotalCost() > 0.0f)
						{
							CostInfo = FString::Printf(TEXT(" | %s session"),
								*FAutonomixCostTracker::FormatCost(CostTracker.GetSessionTotalCost()));
						}
						return FText::FromString(ModelName + TokenInfo + CostInfo);
					}
					return FText::FromString(TEXT("⚠ No API Key Set"));
				})
				.ColorAndOpacity(FSlateColor(FLinearColor(0.6f, 0.6f, 0.6f)))
			]
			// Context window usage percentage
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(4.0f, 0.0f)
			[
				SNew(STextBlock)
				.Text_Lambda([this]()
				{
					if (ContextUsagePercent <= 0.0f) return FText::GetEmpty();
					return FText::FromString(FString::Printf(TEXT("ctx: %.0f%%"), ContextUsagePercent));
				})
				.ColorAndOpacity_Lambda([this]() -> FSlateColor
				{
					// Green < 60%, Yellow 60-80%, Red > 80%
					if (ContextUsagePercent >= 80.0f)
						return FSlateColor(FLinearColor(1.0f, 0.3f, 0.3f));
					if (ContextUsagePercent >= 60.0f)
						return FSlateColor(FLinearColor(1.0f, 0.8f, 0.0f));
					return FSlateColor(FLinearColor(0.3f, 0.9f, 0.3f));
				})
				.Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
			]
			// Condense Context button -- visible when context usage is significant
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(4.0f, 0.0f)
			[
				SAssignNew(CondenseButton, SButton)
				.Text(FText::FromString(TEXT("📦 Condense")))
				.ToolTipText(FText::FromString(TEXT("Manually condense the conversation context to free up token space")))
				.OnClicked_Raw(this, &SAutonomixMainPanel::OnCondenseContextClicked)
				.IsEnabled_Lambda([this]() { return !bIsProcessing && ContextUsagePercent > 0.0f; })
			]
			// Stop button -- visible when processing
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(8.0f, 0.0f)
			[
				SAssignNew(StopButton, SButton)
				.Text(FText::FromString(TEXT("⏹ Stop")))
				.OnClicked_Raw(this, &SAutonomixMainPanel::OnStopClicked)
				.IsEnabled_Lambda([this]() { return bIsProcessing; })
				.Visibility_Lambda([this]() { return bIsProcessing ? EVisibility::Visible : EVisibility::Collapsed; })
			]
			// External API badge
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(4.0f, 0.0f)
			[
				SNew(STextBlock)
				.Text(FText::FromString(TEXT("🌐 External API Active")))
				.ColorAndOpacity(FSlateColor(FLinearColor(0.5f, 0.5f, 0.8f)))
				.Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
			]
		]

		// Conversation tabs
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(8.0f, 2.0f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			[
				SAssignNew(TabButtonContainer, SHorizontalBox)
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(4.0f, 0.0f)
			[
				SNew(SButton)
				.Text(FText::FromString(TEXT("+ New Tab")))
				.ToolTipText(FText::FromString(TEXT("Create a new conversation tab")))
				.OnClicked_Raw(this, &SAutonomixMainPanel::OnAddTabClicked)
				.IsEnabled_Lambda([this]() { return !bIsProcessing; })
			]
		]

		// Separator
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(SSeparator)
		]

		// Todo list (collapsible, hidden when empty)
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(8.0f, 2.0f)
		[
			SAssignNew(TodoListWidget, SAutonomixTodoList)
		]

		// Chat history area (fills available space)
		+ SVerticalBox::Slot()
		.FillHeight(1.0f)
		[
			SAssignNew(ChatView, SAutonomixChatView)
		]

		// Progress overlay (hidden by default)
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SAssignNew(ProgressOverlay, SAutonomixProgress)
		]

		// Plan preview (hidden by default -- only shown when tool calls need approval)
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SAssignNew(PlanPreview, SAutonomixPlanPreview)
			.OnPlanApproved_Raw(this, &SAutonomixMainPanel::OnToolCallsApproved)
			.OnPlanRejected_Raw(this, &SAutonomixMainPanel::OnToolCallsRejected)
		]

		// Separator
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(SSeparator)
		]

		// Input area at the bottom
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(8.0f, 4.0f)
		[
			SAssignNew(InputArea, SAutonomixInputArea)
			.OnPromptSubmitted_Raw(this, &SAutonomixMainPanel::OnPromptSubmitted)
		]
	];

	LoadRuntimeStateFromActiveTab();
	RefreshTabStrip();
	RenderActiveConversation();
}

SAutonomixMainPanel::~SAutonomixMainPanel()
{
	SaveTabsToDisk();

	if (LLMClient.IsValid())
	{
		LLMClient->CancelRequest();
		LLMClient->OnStreamingText().RemoveAll(this);
		LLMClient->OnToolCallReceived().RemoveAll(this);
		LLMClient->OnMessageComplete().RemoveAll(this);
		LLMClient->OnRequestStarted().RemoveAll(this);
		LLMClient->OnRequestCompleted().RemoveAll(this);
		LLMClient->OnErrorReceived().RemoveAll(this);
		LLMClient->OnTokenUsageUpdated().RemoveAll(this);
	}

	if (ExecutionJournal.IsValid())
	{
		ExecutionJournal->FlushToDisk();
	}
}

void SAutonomixMainPanel::InitializeBackend()
{
	// Create the LLM client via factory (supports all 10 providers) and bind delegates.
	// ConfigureClientFromSettings() handles both creation and delegate binding.
	ConfigureClientFromSettings();

	ToolSchemaRegistry = MakeShared<FAutonomixToolSchemaRegistry>();
	ToolSchemaRegistry->LoadAllSchemas();

	ActionRouter = MakeShared<FAutonomixActionRouter>();
	RegisterExecutors();

	// Sync schemas with registered executors: disable any schema that has no
	// backend executor (e.g. python_tools.json loaded but bEnablePythonTools=false).
	// This prevents the LLM from calling tools that would produce "No executor registered" errors.
	if (ToolSchemaRegistry.IsValid() && ActionRouter.IsValid())
	{
		ToolSchemaRegistry->SyncWithRegisteredTools(ActionRouter->GetRegisteredToolNames());
	}

	ExecutionJournal = MakeShared<FAutonomixExecutionJournal>();
	EditorContextCapture = MakeShared<FAutonomixEditorContextCapture>();
	ContextGatherer = MakeShared<FAutonomixContextGatherer>();

	// Phase 1: Initialize ignore controller (loads .autonomixignore from project root)
	IgnoreController = MakeShared<FAutonomixIgnoreController>();
	IgnoreController->Initialize(FPaths::ProjectDir());

	// Phase 1: Initialize file context tracker (detects externally modified files)
	FileContextTracker = MakeShared<FAutonomixFileContextTracker>();
	FileContextTracker->Initialize(FPaths::ProjectDir());

	// Phase 1: Initialize tool repetition detector
	ToolRepetitionDetector = MakeShared<FAutonomixToolRepetitionDetector>();

	// Phase 2: Initialize environment details builder
	EnvironmentDetails = MakeShared<FAutonomixEnvironmentDetails>();
	EnvironmentDetails->SetFileContextTracker(FileContextTracker.Get());
	EnvironmentDetails->SetIgnoreController(IgnoreController.Get());

	// Phase 2: Initialize fuzzy diff applicator
	DiffApplicator = MakeShared<FAutonomixDiffApplicator>();

	// Restore tab sessions and bind active tab conversation/context pointers.
	LoadTabsFromDisk();
	if (ConversationTabs.Num() == 0)
	{
		CreateNewTab();
	}
	else
	{
		if (!ConversationTabs.IsValidIndex(ActiveTabIndex))
		{
			ActiveTabIndex = 0;
		}
		LoadRuntimeStateFromActiveTab();
	}

	// Backup manager: per-iteration asset checkpoints before tool execution
	BackupManager = MakeShared<FAutonomixBackupManager>();
	const UAutonomixDeveloperSettings* Settings = UAutonomixDeveloperSettings::Get();
	if (Settings)
	{
		BackupManager->MaxBackupCount = Settings->MaxBackupCount;
	}

	// Phase 3: Git checkpoint manager
	CheckpointManager = MakeShared<FAutonomixCheckpointManager>();
	const FString SessionId = FGuid::NewGuid().ToString(EGuidFormats::Short);
	CheckpointManager->Initialize(SessionId, FPaths::ProjectDir());

	// Phase 3: @Reference parser
	ReferenceParser = MakeShared<FAutonomixReferenceParser>();
	ReferenceParser->ProjectRoot = FPaths::ProjectDir();
	ReferenceParser->SetIgnoreController(IgnoreController.Get());

	// Phase 3: Task delegation manager
	TaskDelegation = MakeShared<FAutonomixTaskDelegation>();
	TaskDelegation->OnSubTaskCompleted.BindSP(this, &SAutonomixMainPanel::OnSubTaskCompleted);

	// Phase 4: Task history
	TaskHistory = MakeShared<FAutonomixTaskHistory>();
	TaskHistory->Initialize();

	// Phase 4: Slash command registry
	SlashCommandRegistry = MakeShared<FAutonomixSlashCommandRegistry>();
	SlashCommandRegistry->Initialize();

	// Phase 4: Skills manager
	SkillsManager = MakeShared<FAutonomixSkillsManager>();
	SkillsManager->Initialize();

	// Phase 4: MCP client (loads config if present, no-op if no servers configured)
	MCPClient = MakeShared<FAutonomixMCPClient>();
	MCPClient->LoadConfigFromDisk();

	// Second pass: Code structure parser (tree-sitter equivalent)
	CodeStructureParser = MakeShared<FAutonomixCodeStructureParser>();

	// New UI widgets — initialized here, added to layout in Construct()
	ContextBar = SNew(SAutonomixContextBar)
		.OnCondenseClicked_Lambda([this]() { OnCondenseContextClicked(); })
		.OnModeClicked_Lambda([this]()
		{
			// Cycle through modes
			int32 CurrentIdx = (int32)CurrentAgentMode;
			int32 NextIdx = (CurrentIdx + 1) % 7;  // 7 modes: General..Orchestrator
			ApplyAgentMode((EAutonomixAgentMode)NextIdx);
		});

	CheckpointPanel = SNew(SAutonomixCheckpointPanel)
		.OnRestoreCheckpoint(this, &SAutonomixMainPanel::OnRestoreCheckpoint)
		.OnViewDiff(this, &SAutonomixMainPanel::OnViewCheckpointDiff);

	HistoryPanel = SNew(SAutonomixHistoryPanel)
		.OnLoadTask(this, &SAutonomixMainPanel::OnLoadHistoryTask)
		.OnDeleteTask(this, &SAutonomixMainPanel::OnDeleteHistoryTask);

	FollowUpBar = SNew(SAutonomixFollowUpBar)
		.OnFollowUpSelected(this, &SAutonomixMainPanel::OnFollowUpSelected);

	FileChangesPanel = SNew(SAutonomixFileChangesPanel);

	// Initialize history
	if (TaskHistory.IsValid())
	{
		HistoryPanel->RefreshHistory(TaskHistory->GetHistory());
	}

	UE_LOG(LogAutonomix, Log,
		TEXT("MainPanel: Backend initialized. %d tool schemas, %d executors, %d skills, %d slash commands."),
		ToolSchemaRegistry->GetToolCount(),
		ActionRouter->GetRegisteredExecutorNames().Num(),
		SkillsManager->GetSkillCount(),
		SlashCommandRegistry->GetAllCommands().Num()
	);
}

SAutonomixMainPanel::FAutonomixConversationTabState* SAutonomixMainPanel::GetActiveTabState()
{
	return ConversationTabs.IsValidIndex(ActiveTabIndex) ? &ConversationTabs[ActiveTabIndex] : nullptr;
}

const SAutonomixMainPanel::FAutonomixConversationTabState* SAutonomixMainPanel::GetActiveTabState() const
{
	return ConversationTabs.IsValidIndex(ActiveTabIndex) ? &ConversationTabs[ActiveTabIndex] : nullptr;
}

FString SAutonomixMainPanel::GetTabsSessionDir()
{
	return FPaths::Combine(FAutonomixConversationManager::GetConversationSaveDir(), TEXT("Tabs"));
}

FString SAutonomixMainPanel::GetTabsManifestPath()
{
	return FPaths::Combine(GetTabsSessionDir(), TabsManifestFileName);
}

FString SAutonomixMainPanel::MakeTabConversationFileName(const FString& TabId)
{
	return FString::Printf(TEXT("tab_%s.json"), *TabId);
}

FString SAutonomixMainPanel::MakeDefaultTabTitle(int32 TabNumber)
{
	return FString::Printf(TEXT("Task %d"), TabNumber);
}

bool SAutonomixMainPanel::TryParseDefaultTabNumber(const FString& Title, int32& OutNumber)
{
	const FString Prefix = TEXT("Task ");
	if (!Title.StartsWith(Prefix, ESearchCase::IgnoreCase))
	{
		return false;
	}

	const FString NumberStr = Title.Mid(Prefix.Len()).TrimStartAndEnd();
	if (NumberStr.IsEmpty() || !NumberStr.IsNumeric())
	{
		return false;
	}

	OutNumber = FCString::Atoi(*NumberStr);
	return OutNumber > 0;
}

int32 SAutonomixMainPanel::GetNextAvailableTabNumber() const
{
	TSet<int32> UsedNumbers;
	for (const FAutonomixConversationTabState& Tab : ConversationTabs)
	{
		int32 ParsedNumber = 0;
		if (TryParseDefaultTabNumber(Tab.Title, ParsedNumber))
		{
			UsedNumbers.Add(ParsedNumber);
		}
	}

	int32 Candidate = 1;
	while (UsedNumbers.Contains(Candidate))
	{
		++Candidate;
	}

	return Candidate;
}

void SAutonomixMainPanel::SyncRuntimeStateToActiveTab()
{
	FAutonomixConversationTabState* ActiveTab = GetActiveTabState();
	if (!ActiveTab)
	{
		return;
	}

	ActiveTab->SessionTokenUsage = SessionTokenUsage;
	ActiveTab->LastResponseTokenUsage = LastResponseTokenUsage;
	ActiveTab->ContextUsagePercent = ContextUsagePercent;
	ActiveTab->LastRequestCost = LastRequestCost;
	ActiveTab->CostTracker = CostTracker;
	ActiveTab->AutoApprovalHandler = AutoApprovalHandler;
	ActiveTab->bInAgenticLoop = bInAgenticLoop;
	ActiveTab->AgenticLoopCount = AgenticLoopCount;
	ActiveTab->ConsecutiveNoToolCount = ConsecutiveNoToolCount;

	if (TodoListWidget.IsValid())
	{
		ActiveTab->Todos = TodoListWidget->GetTodos();
	}
}

void SAutonomixMainPanel::LoadRuntimeStateFromActiveTab()
{
	FAutonomixConversationTabState* ActiveTab = GetActiveTabState();
	if (!ActiveTab)
	{
		return;
	}

	if (!ActiveTab->ConversationManager.IsValid())
	{
		ActiveTab->ConversationManager = MakeShared<FAutonomixConversationManager>();
	}
	if (!ActiveTab->ContextManager.IsValid())
	{
		ActiveTab->ContextManager = MakeShared<FAutonomixContextManager>(LLMClient, ActiveTab->ConversationManager);
	}

	ConversationManager = ActiveTab->ConversationManager;
	ContextManager = ActiveTab->ContextManager;
	SessionTokenUsage = ActiveTab->SessionTokenUsage;
	LastResponseTokenUsage = ActiveTab->LastResponseTokenUsage;
	ContextUsagePercent = ActiveTab->ContextUsagePercent;
	LastRequestCost = ActiveTab->LastRequestCost;
	CostTracker = ActiveTab->CostTracker;
	AutoApprovalHandler = ActiveTab->AutoApprovalHandler;
	bInAgenticLoop = ActiveTab->bInAgenticLoop;
	AgenticLoopCount = ActiveTab->AgenticLoopCount;
	ConsecutiveNoToolCount = ActiveTab->ConsecutiveNoToolCount;
	ToolCallQueue.Empty();
	CurrentStreamingMessageId.Invalidate();

	if (TodoListWidget.IsValid())
	{
		TodoListWidget->SetTodos(ActiveTab->Todos);
	}
}

void SAutonomixMainPanel::RenderActiveConversation()
{
	if (!ChatView.IsValid())
	{
		return;
	}

	ChatView->ClearMessages();

	if (!ConversationManager.IsValid())
	{
		return;
	}

	const TArray<FAutonomixMessage>& History = ConversationManager->GetHistory();
	for (const FAutonomixMessage& Msg : History)
	{
		if (Msg.bIsStreaming)
		{
			continue;
		}

		// Skip tool result messages — they contain raw API round-trip data
		// (T3D readback, JSON tool output, etc.) that is not user-facing.
		// During live sessions these flow through the agentic loop invisibly;
		// on conversation reload they should not be rendered as chat messages.
		if (Msg.Role == EAutonomixMessageRole::ToolResult)
		{
			continue;
		}

		// Skip messages hidden by condensation or truncation
		if (!Msg.CondenseParent.IsEmpty() || !Msg.TruncationParent.IsEmpty())
		{
			continue;
		}

		ChatView->AddMessage(Msg);
	}

	if (History.Num() == 0)
	{
		FAutonomixMessage WelcomeMsg(EAutonomixMessageRole::System,
			TEXT("Welcome to Autonomix. Configure your API key in Project Settings > Plugins > Autonomix, then start chatting to create and manage your UE project."));
		ChatView->AddMessage(WelcomeMsg);
	}
}

void SAutonomixMainPanel::RefreshTabStrip()
{
	if (!TabButtonContainer.IsValid())
	{
		return;
	}

	TabButtonContainer->ClearChildren();

	for (int32 TabIndex = 0; TabIndex < ConversationTabs.Num(); ++TabIndex)
	{
		TabButtonContainer->AddSlot()
		.AutoWidth()
		.Padding(0.0f, 0.0f, 4.0f, 0.0f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			[
				SNew(SButton)
				.Text_Lambda([this, TabIndex]()
				{
					if (!ConversationTabs.IsValidIndex(TabIndex))
					{
						return FText::GetEmpty();
					}

					const FAutonomixConversationTabState& Tab = ConversationTabs[TabIndex];
					const int32 MessageCount = Tab.ConversationManager.IsValid()
						? Tab.ConversationManager->GetMessageCount()
						: 0;
					return FText::FromString(FString::Printf(TEXT("%s (%d)"), *Tab.Title, MessageCount));
				})
				.ToolTipText_Lambda([this, TabIndex]()
				{
					if (!ConversationTabs.IsValidIndex(TabIndex))
					{
						return FText::GetEmpty();
					}
					return FText::FromString(ConversationTabs[TabIndex].Title);
				})
				.ButtonColorAndOpacity_Lambda([this, TabIndex]()
				{
					return (TabIndex == ActiveTabIndex)
						? FLinearColor(0.20f, 0.48f, 0.82f)
						: FLinearColor(0.20f, 0.20f, 0.24f);
				})
				.OnClicked_Lambda([this, TabIndex]()
				{
					SwitchToTab(TabIndex);
					return FReply::Handled();
				})
				.IsEnabled_Lambda([this]() { return !bIsProcessing; })
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(2.0f, 0.0f, 0.0f, 0.0f)
			[
				SNew(SButton)
				.Text(FText::FromString(TEXT("x")))
				.ToolTipText(FText::FromString(TEXT("Close tab")))
				.OnClicked_Lambda([this, TabIndex]()
				{
					CloseTab(TabIndex);
					return FReply::Handled();
				})
				.IsEnabled_Lambda([this]() { return !bIsProcessing; })
				.Visibility_Lambda([this]()
				{
					return ConversationTabs.Num() > 1 ? EVisibility::Visible : EVisibility::Collapsed;
				})
			]
		];
	}
}

void SAutonomixMainPanel::CreateNewTab(const FString& InTitle, bool bMakeActive)
{
	if (!LLMClient.IsValid())
	{
		return;
	}

	FAutonomixConversationTabState NewTab;
	NewTab.TabId = FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphens);
	if (InTitle.IsEmpty())
	{
		const int32 NewTabNumber = GetNextAvailableTabNumber();
		NewTab.Title = MakeDefaultTabTitle(NewTabNumber);
		NextTabNumber = FMath::Max(NextTabNumber, NewTabNumber + 1);
	}
	else
	{
		NewTab.Title = InTitle;
	}
	NewTab.ConversationManager = MakeShared<FAutonomixConversationManager>();
	NewTab.ContextManager = MakeShared<FAutonomixContextManager>(LLMClient, NewTab.ConversationManager);
	NewTab.AutoApprovalHandler.Reset();
	NewTab.CostTracker.Reset();

	ConversationTabs.Add(MoveTemp(NewTab));

	if (bMakeActive)
	{
		SyncRuntimeStateToActiveTab();
		ActiveTabIndex = ConversationTabs.Num() - 1;
		LoadRuntimeStateFromActiveTab();
		if (PlanPreview.IsValid())
		{
			PlanPreview->HidePlan();
		}
		if (ProgressOverlay.IsValid())
		{
			ProgressOverlay->HideProgress();
		}
		RenderActiveConversation();
		if (InputArea.IsValid())
		{
			InputArea->FocusInput();
		}
	}

	RefreshTabStrip();
	SaveTabsToDisk();
}

void SAutonomixMainPanel::CloseTab(int32 TabIndex)
{
	if (!ConversationTabs.IsValidIndex(TabIndex))
	{
		return;
	}

	if (bIsProcessing)
	{
		if (ChatView.IsValid())
		{
			FAutonomixMessage BusyMsg(EAutonomixMessageRole::System,
				TEXT("Wait for the current request to finish before closing a tab."));
			ChatView->AddMessage(BusyMsg);
		}
		return;
	}

	if (ConversationTabs.Num() <= 1)
	{
		if (ChatView.IsValid())
		{
			FAutonomixMessage InfoMsg(EAutonomixMessageRole::System,
				TEXT("You must keep at least one tab open."));
			ChatView->AddMessage(InfoMsg);
		}
		return;
	}

	SyncRuntimeStateToActiveTab();

	const FString ClosedTabId = ConversationTabs[TabIndex].TabId;
	const FString ClosedConversationPath = FPaths::Combine(
		GetTabsSessionDir(),
		MakeTabConversationFileName(ClosedTabId));

	ConversationTabs.RemoveAt(TabIndex);

	if (TabIndex == ActiveTabIndex)
	{
		ActiveTabIndex = FMath::Clamp(TabIndex, 0, ConversationTabs.Num() - 1);
		LoadRuntimeStateFromActiveTab();

		if (PlanPreview.IsValid())
		{
			PlanPreview->HidePlan();
		}
		if (ProgressOverlay.IsValid())
		{
			ProgressOverlay->HideProgress();
		}

		RenderActiveConversation();
		if (InputArea.IsValid())
		{
			InputArea->FocusInput();
		}
	}
	else if (TabIndex < ActiveTabIndex)
	{
		ActiveTabIndex--;
	}

	IFileManager::Get().Delete(*ClosedConversationPath, false, true, true);
	RefreshTabStrip();
	SaveTabsToDisk();
}

void SAutonomixMainPanel::SwitchToTab(int32 TabIndex)
{
	if (!ConversationTabs.IsValidIndex(TabIndex) || TabIndex == ActiveTabIndex)
	{
		return;
	}

	if (bIsProcessing)
	{
		if (ChatView.IsValid())
		{
			FAutonomixMessage BusyMsg(EAutonomixMessageRole::System,
				TEXT("A request is in progress. Wait for completion before switching tabs."));
			ChatView->AddMessage(BusyMsg);
		}
		return;
	}

	SyncRuntimeStateToActiveTab();
	ActiveTabIndex = TabIndex;
	LoadRuntimeStateFromActiveTab();
	if (PlanPreview.IsValid())
	{
		PlanPreview->HidePlan();
	}
	if (ProgressOverlay.IsValid())
	{
		ProgressOverlay->HideProgress();
	}
	RefreshTabStrip();
	RenderActiveConversation();
	if (InputArea.IsValid())
	{
		InputArea->FocusInput();
	}
	SaveTabsToDisk();
}

FReply SAutonomixMainPanel::OnAddTabClicked()
{
	if (bIsProcessing)
	{
		if (ChatView.IsValid())
		{
			FAutonomixMessage BusyMsg(EAutonomixMessageRole::System,
				TEXT("Wait for the current request to finish before creating a new tab."));
			ChatView->AddMessage(BusyMsg);
		}
		return FReply::Handled();
	}

	CreateNewTab();
	return FReply::Handled();
}

void SAutonomixMainPanel::LoadTabsFromDisk()
{
	ConversationTabs.Empty();
	ActiveTabIndex = INDEX_NONE;
	NextTabNumber = 1;

	const FString ManifestPath = GetTabsManifestPath();
	if (!FPaths::FileExists(ManifestPath))
	{
		return;
	}

	FString JsonString;
	if (!FFileHelper::LoadFileToString(JsonString, *ManifestPath))
	{
		UE_LOG(LogAutonomix, Warning, TEXT("MainPanel: Failed to read tab manifest: %s"), *ManifestPath);
		return;
	}

	TSharedPtr<FJsonObject> Root;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
	{
		UE_LOG(LogAutonomix, Warning, TEXT("MainPanel: Failed to parse tab manifest JSON: %s"), *ManifestPath);
		return;
	}

	Root->TryGetNumberField(TEXT("next_tab_number"), NextTabNumber);
	NextTabNumber = FMath::Max(NextTabNumber, 1);

	FString ActiveTabId;
	Root->TryGetStringField(TEXT("active_tab_id"), ActiveTabId);

	const TArray<TSharedPtr<FJsonValue>>* TabsArray = nullptr;
	if (!Root->TryGetArrayField(TEXT("tabs"), TabsArray) || !TabsArray)
	{
		return;
	}

	for (const TSharedPtr<FJsonValue>& TabValue : *TabsArray)
	{
		const TSharedPtr<FJsonObject>* TabObj = nullptr;
		if (!TabValue->TryGetObject(TabObj) || !TabObj || !(*TabObj).IsValid())
		{
			continue;
		}

		FAutonomixConversationTabState TabState;
		(*TabObj)->TryGetStringField(TEXT("id"), TabState.TabId);
		(*TabObj)->TryGetStringField(TEXT("title"), TabState.Title);

		if (TabState.TabId.IsEmpty())
		{
			continue;
		}
		if (TabState.Title.IsEmpty())
		{
			TabState.Title = MakeDefaultTabTitle(GetNextAvailableTabNumber());
		}

		TabState.ConversationManager = MakeShared<FAutonomixConversationManager>();

		FString ConversationFileName;
		(*TabObj)->TryGetStringField(TEXT("conversation_file"), ConversationFileName);
		if (ConversationFileName.IsEmpty())
		{
			ConversationFileName = MakeTabConversationFileName(TabState.TabId);
		}

		const FString ConversationPath = FPaths::Combine(GetTabsSessionDir(), ConversationFileName);
		TabState.ConversationManager->LoadSession(ConversationPath);
		TabState.ContextManager = MakeShared<FAutonomixContextManager>(LLMClient, TabState.ConversationManager);

		const TSharedPtr<FJsonObject>* SessionUsageObj = nullptr;
		if ((*TabObj)->TryGetObjectField(TEXT("session_token_usage"), SessionUsageObj))
		{
			TabState.SessionTokenUsage = TokenUsageFromJson(*SessionUsageObj);
		}

		const TSharedPtr<FJsonObject>* LastUsageObj = nullptr;
		if ((*TabObj)->TryGetObjectField(TEXT("last_response_token_usage"), LastUsageObj))
		{
			TabState.LastResponseTokenUsage = TokenUsageFromJson(*LastUsageObj);
		}

		(*TabObj)->TryGetNumberField(TEXT("context_usage_percent"), TabState.ContextUsagePercent);
		(*TabObj)->TryGetNumberField(TEXT("last_request_cost"), TabState.LastRequestCost);

		const TArray<TSharedPtr<FJsonValue>>* TodosArray = nullptr;
		if ((*TabObj)->TryGetArrayField(TEXT("todos"), TodosArray) && TodosArray)
		{
			for (const TSharedPtr<FJsonValue>& TodoValue : *TodosArray)
			{
				const TSharedPtr<FJsonObject>* TodoObj = nullptr;
				if (!TodoValue->TryGetObject(TodoObj) || !TodoObj || !(*TodoObj).IsValid())
				{
					continue;
				}

				FAutonomixTodoItem Todo;
				(*TodoObj)->TryGetStringField(TEXT("id"), Todo.Id);
				(*TodoObj)->TryGetStringField(TEXT("content"), Todo.Content);

				FString StatusStr;
				(*TodoObj)->TryGetStringField(TEXT("status"), StatusStr);
				Todo.Status = FAutonomixTodoItem::ParseStatus(StatusStr);

				if (!Todo.Content.IsEmpty())
				{
					TabState.Todos.Add(Todo);
				}
			}
		}

		TabState.AutoApprovalHandler.Reset();
		TabState.CostTracker.Reset();

		ConversationTabs.Add(MoveTemp(TabState));
	}

	if (ConversationTabs.Num() == 0)
	{
		return;
	}

	ActiveTabIndex = 0;
	if (!ActiveTabId.IsEmpty())
	{
		for (int32 i = 0; i < ConversationTabs.Num(); ++i)
		{
			if (ConversationTabs[i].TabId == ActiveTabId)
			{
				ActiveTabIndex = i;
				break;
			}
		}
	}

	NextTabNumber = FMath::Max(NextTabNumber, ConversationTabs.Num() + 1);
}

void SAutonomixMainPanel::SaveTabsToDisk()
{
	SyncRuntimeStateToActiveTab();

	if (ConversationTabs.Num() == 0)
	{
		return;
	}

	const FString TabsDir = GetTabsSessionDir();
	IFileManager::Get().MakeDirectory(*TabsDir, true);

	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("format_version"), TEXT("1.0.0"));
	Root->SetStringField(TEXT("saved_at"), FDateTime::UtcNow().ToIso8601());
	Root->SetNumberField(TEXT("next_tab_number"), NextTabNumber);

	const FAutonomixConversationTabState* ActiveTab = GetActiveTabState();
	Root->SetStringField(TEXT("active_tab_id"), ActiveTab ? ActiveTab->TabId : TEXT(""));

	TArray<TSharedPtr<FJsonValue>> TabsArray;
	for (const FAutonomixConversationTabState& TabState : ConversationTabs)
	{
		if (!TabState.ConversationManager.IsValid())
		{
			continue;
		}

		const FString ConversationFileName = MakeTabConversationFileName(TabState.TabId);
		const FString ConversationPath = FPaths::Combine(TabsDir, ConversationFileName);
		TabState.ConversationManager->SaveSession(ConversationPath);

		TSharedPtr<FJsonObject> TabObj = MakeShared<FJsonObject>();
		TabObj->SetStringField(TEXT("id"), TabState.TabId);
		TabObj->SetStringField(TEXT("title"), TabState.Title);
		TabObj->SetStringField(TEXT("conversation_file"), ConversationFileName);
		TabObj->SetObjectField(TEXT("session_token_usage"), TokenUsageToJson(TabState.SessionTokenUsage));
		TabObj->SetObjectField(TEXT("last_response_token_usage"), TokenUsageToJson(TabState.LastResponseTokenUsage));
		TabObj->SetNumberField(TEXT("context_usage_percent"), TabState.ContextUsagePercent);
		TabObj->SetNumberField(TEXT("last_request_cost"), TabState.LastRequestCost);

		TArray<TSharedPtr<FJsonValue>> TodosArray;
		for (const FAutonomixTodoItem& Todo : TabState.Todos)
		{
			TSharedPtr<FJsonObject> TodoObj = MakeShared<FJsonObject>();
			TodoObj->SetStringField(TEXT("id"), Todo.Id);
			TodoObj->SetStringField(TEXT("content"), Todo.Content);
			TodoObj->SetStringField(TEXT("status"), FAutonomixTodoItem::StatusToString(Todo.Status));
			TodosArray.Add(MakeShared<FJsonValueObject>(TodoObj));
		}
		TabObj->SetArrayField(TEXT("todos"), TodosArray);

		TabsArray.Add(MakeShared<FJsonValueObject>(TabObj));
	}

	Root->SetArrayField(TEXT("tabs"), TabsArray);

	FString OutString;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutString);
	FJsonSerializer::Serialize(Root.ToSharedRef(), Writer);

	const FString ManifestPath = GetTabsManifestPath();
	if (!FFileHelper::SaveStringToFile(OutString, *ManifestPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
	{
		UE_LOG(LogAutonomix, Warning, TEXT("MainPanel: Failed to save tab manifest: %s"), *ManifestPath);
	}
}

void SAutonomixMainPanel::RegisterExecutors()
{
	const UAutonomixDeveloperSettings* Settings = UAutonomixDeveloperSettings::Get();

	if (Settings && Settings->bEnableBlueprintTools)
		ActionRouter->RegisterExecutor(MakeShared<FAutonomixBlueprintActions>());
	if (Settings && Settings->bEnableMaterialTools)
		ActionRouter->RegisterExecutor(MakeShared<FAutonomixMaterialActions>());
	if (Settings && Settings->bEnableCppTools)
		ActionRouter->RegisterExecutor(MakeShared<FAutonomixCppActions>());
	if (Settings && Settings->bEnableImportTools)
		ActionRouter->RegisterExecutor(MakeShared<FAutonomixMeshActions>());
	if (Settings && Settings->bEnableLevelTools)
		ActionRouter->RegisterExecutor(MakeShared<FAutonomixLevelActions>());
	if (Settings && Settings->bEnableSettingsTools)
		ActionRouter->RegisterExecutor(MakeShared<FAutonomixSettingsActions>());
#ifdef WITH_AUTONOMIX_PRO
	if (Settings && Settings->bEnableBuildTools)
		ActionRouter->RegisterExecutor(MakeShared<FAutonomixBuildActions>());
#endif
	if (Settings && Settings->bEnablePerformanceTools)
		ActionRouter->RegisterExecutor(MakeShared<FAutonomixPerformanceActions>());

	// Context-as-tools: always registered (read-only, safe in all security modes)
	ActionRouter->RegisterExecutor(MakeShared<FAutonomixContextActions>());

	ActionRouter->RegisterExecutor(MakeShared<FAutonomixSourceControlActions>());

	// Enhanced Input asset tools: always registered (needed to fulfil zero-manual-steps for input setup)
	ActionRouter->RegisterExecutor(MakeShared<FAutonomixInputActions>());

	// Animation, Widget, and PCG tools — always registered (gated by blueprint tools being common prerequisite)
	if (Settings && Settings->bEnableBlueprintTools)
	{
		ActionRouter->RegisterExecutor(MakeShared<FAutonomixAnimationActions>());
		ActionRouter->RegisterExecutor(MakeShared<FAutonomixWidgetActions>());
		ActionRouter->RegisterExecutor(MakeShared<FAutonomixPCGActions>());
	}

	// ====================================================================
	// v1.1: New tool executors
	// ====================================================================

	// Python scripting — opt-in, requires Developer mode
	if (Settings && Settings->bEnablePythonTools)
		ActionRouter->RegisterExecutor(MakeShared<FAutonomixPythonActions>());

	// Viewport capture (multimodal vision) — read-only, safe in all modes
	if (Settings && Settings->bEnableViewportCapture)
		ActionRouter->RegisterExecutor(MakeShared<FAutonomixViewportActions>());

	// DataTable tools — standard asset creation
	if (Settings && Settings->bEnableDataTableTools)
		ActionRouter->RegisterExecutor(MakeShared<FAutonomixDataTableActions>());

	// Diagnostics (read_message_log) — always registered (read-only, safe)
	ActionRouter->RegisterExecutor(MakeShared<FAutonomixDiagnosticsActions>());

	// Behavior Tree / AI tools
	if (Settings && Settings->bEnableBehaviorTreeTools)
		ActionRouter->RegisterExecutor(MakeShared<FAutonomixBehaviorTreeActions>());

	// Sequencer / Cinematics tools
	if (Settings && Settings->bEnableSequencerTools)
		ActionRouter->RegisterExecutor(MakeShared<FAutonomixSequencerActions>());

	// PIE automation — opt-in, requires Developer mode
	if (Settings && Settings->bEnablePIETools)
		ActionRouter->RegisterExecutor(MakeShared<FAutonomixPIEActions>());

	// Validation & Testing — always registered (read-only, safe in all modes)
	ActionRouter->RegisterExecutor(MakeShared<FAutonomixValidationActions>());

	// Gameplay Ability System (GAS) tools
	if (Settings && Settings->bEnableGASTools)
		ActionRouter->RegisterExecutor(MakeShared<FAutonomixGASActions>());
}

void SAutonomixMainPanel::ConfigureClientFromSettings()
{
	const UAutonomixDeveloperSettings* Settings = UAutonomixDeveloperSettings::Get();
	if (!Settings) return;

	// Unbind from the old client before recreating (avoids dangling delegate handles)
	if (LLMClient.IsValid())
	{
		LLMClient->OnStreamingText().RemoveAll(this);
		LLMClient->OnToolCallReceived().RemoveAll(this);
		LLMClient->OnMessageComplete().RemoveAll(this);
		LLMClient->OnRequestStarted().RemoveAll(this);
		LLMClient->OnRequestCompleted().RemoveAll(this);
		LLMClient->OnErrorReceived().RemoveAll(this);
		LLMClient->OnTokenUsageUpdated().RemoveAll(this);
	}

	// Create the correct client for the active provider via factory.
	// This supports all 10 providers (Anthropic, OpenAI, Gemini, DeepSeek, etc.)
	LLMClient = FAutonomixLLMClientFactory::CreateClient();

	if (!LLMClient.IsValid())
	{
		UE_LOG(LogAutonomix, Error,
			TEXT("MainPanel: FAutonomixLLMClientFactory::CreateClient() returned null. "
			     "Check that the active provider's API key is set in Project Settings > Autonomix."));
		return;
	}

	// Bind standard delegates to the new client
	LLMClient->OnStreamingText().AddSP(this, &SAutonomixMainPanel::OnStreamingText);
	LLMClient->OnToolCallReceived().AddSP(this, &SAutonomixMainPanel::OnToolCallReceived);
	LLMClient->OnMessageComplete().AddSP(this, &SAutonomixMainPanel::OnMessageComplete);
	LLMClient->OnRequestStarted().AddSP(this, &SAutonomixMainPanel::OnRequestStarted);
	LLMClient->OnRequestCompleted().AddSP(this, &SAutonomixMainPanel::OnRequestCompleted);
	LLMClient->OnErrorReceived().AddSP(this, &SAutonomixMainPanel::OnErrorReceived);
	LLMClient->OnTokenUsageUpdated().AddSP(this, &SAutonomixMainPanel::OnTokenUsageUpdated);

	// Phase 1: Bind context window exceeded delegate (Anthropic-only feature).
	// For non-Anthropic providers, OnContextWindowExceeded is not available —
	// those providers handle context length differently (errors bubble up as HTTP errors).
	if (FAutonomixClaudeClient* Claude = ClaudeClientPtr())
	{
		Claude->OnContextWindowExceeded.BindSP(this, &SAutonomixMainPanel::HandleContextWindowExceeded);
	}

	UE_LOG(LogAutonomix, Log, TEXT("MainPanel: LLM client created: %s"),
		*FAutonomixLLMClientFactory::GetActiveProviderDisplayName());
}

FAutonomixClaudeClient* SAutonomixMainPanel::ClaudeClientPtr() const
{
	// Use the virtual AsClaudeClient() method — safe typed downcast without dynamic_cast.
	// UE compiles with /GR- (RTTI disabled), so dynamic_cast is forbidden.
	// StaticCastSharedPtr is also unsafe (reinterpret-style, crashes for non-Claude providers).
	// The virtual dispatch pattern:
	//   - FAutonomixClaudeClient::AsClaudeClient() returns this
	//   - All other providers inherit IAutonomixLLMClient::AsClaudeClient() which returns nullptr
	if (!LLMClient.IsValid()) return nullptr;
	return LLMClient->AsClaudeClient();
}

int32 SAutonomixMainPanel::GetContextWindowTokens() const
{
	const UAutonomixDeveloperSettings* Settings = UAutonomixDeveloperSettings::Get();
	bool bExtended = Settings && Settings->ContextWindow == EAutonomixContextWindow::Extended_1M;
	return FAutonomixTokenCounter::GetContextWindowTokens(bExtended);
}

// ============================================================================
// Privacy Disclosure Check (Marketplace requirement)
// ============================================================================

bool SAutonomixMainPanel::CheckPrivacyDisclosure()
{
	// Use GetMutableDefault so we can call SaveConfig() without const_cast.
	UAutonomixDeveloperSettings* Settings = GetMutableDefault<UAutonomixDeveloperSettings>();
	if (!Settings) return false;

	if (Settings->bHasAcceptedPrivacyDisclosure)
	{
		return true; // Already accepted
	}

	// Show mandatory first-launch privacy disclosure — provider-agnostic.
	// Autonomix supports 10+ providers; the disclosure must be generic.
	FString ProviderName = FAutonomixLLMClientFactory::GetActiveProviderDisplayName();
	FText Title = FText::FromString(TEXT("Autonomix Privacy Disclosure"));
	FText Message = FText::FromString(FString::Printf(
		TEXT("Autonomix connects to external AI APIs to provide AI assistance.\n")
		TEXT("Currently configured provider: %s\n\n")
		TEXT("- Your prompts and project context are sent to the selected AI provider for processing.\n")
		TEXT("- No data is stored by the Autonomix plugin itself.\n")
		TEXT("- Data handling is governed by your AI provider's privacy policy.\n\n")
		TEXT("By clicking OK, you acknowledge this data handling and agree to proceed.\n")
		TEXT("You can review this at any time in Project Settings > Plugins > Autonomix."),
		*ProviderName
	));

	EAppReturnType::Type Result = FMessageDialog::Open(EAppMsgType::OkCancel, Message, Title);

	if (Result == EAppReturnType::Ok)
	{
		Settings->bHasAcceptedPrivacyDisclosure = true;
		Settings->SaveConfig();
		return true;
	}

	return false; // User declined
}

// ============================================================================
// User Input -> LLM API (supports all providers: Anthropic, OpenAI, Gemini, etc.)
// ============================================================================

void SAutonomixMainPanel::OnPromptSubmitted(const FString& PromptText)
{
	// Concurrency guard: if already processing, queue the message instead of rejecting
	if (bIsProcessing)
	{
		PendingMessageQueue.Add(PromptText);
		FAutonomixMessage QueueMsg(EAutonomixMessageRole::System,
			FString::Printf(TEXT("⏳ Message queued (position %d). Will start when current task completes."),
				PendingMessageQueue.Num()));
		ChatView->AddMessage(QueueMsg);
		UE_LOG(LogAutonomix, Log, TEXT("MainPanel: Message queued (queue size: %d)"), PendingMessageQueue.Num());
		return;
	}

	// Re-read settings in case API key changed
	ConfigureClientFromSettings();

	const UAutonomixDeveloperSettings* Settings = UAutonomixDeveloperSettings::Get();
	if (!Settings || !Settings->IsActiveProviderApiKeySet())
	{
		FString ProviderName = FAutonomixLLMClientFactory::GetActiveProviderDisplayName();
		FAutonomixMessage ErrorMsg(EAutonomixMessageRole::Error,
			FString::Printf(
				TEXT("API key not configured for active provider (%s). "
				     "Go to Project Settings > Plugins > Autonomix to set your API key."),
				*ProviderName));
		ChatView->AddMessage(ErrorMsg);
		return;
	}

	// Privacy disclosure check (first use only)
	if (!CheckPrivacyDisclosure())
	{
		FAutonomixMessage PrivacyMsg(EAutonomixMessageRole::System,
			TEXT("Privacy disclosure was declined. Autonomix cannot send data to the API without your consent."));
		ChatView->AddMessage(PrivacyMsg);
		return;
	}

	// Reset agentic loop state
	bInAgenticLoop = false;
	AgenticLoopCount = 0;
	ConsecutiveNoToolCount = 0;
	ToolCallQueue.Empty();

	// Phase 4: Check for slash commands — expand if found
	FString ProcessedPrompt = PromptText;
	if (SlashCommandRegistry.IsValid() && SlashCommandRegistry->IsSlashCommand(PromptText))
	{
		FString ExpandedPrompt;
		EAutonomixAgentMode SuggestedMode = EAutonomixAgentMode::General;

		if (SlashCommandRegistry->ExpandSlashCommand(PromptText, ExpandedPrompt, SuggestedMode))
		{
			ProcessedPrompt = ExpandedPrompt;

			if (SuggestedMode != EAutonomixAgentMode::General && SuggestedMode != CurrentAgentMode)
			{
				ApplyAgentMode(SuggestedMode);
			}

			FAutonomixMessage SlashMsg(EAutonomixMessageRole::System,
				FString::Printf(TEXT("💬 Slash command expanded → %s"), *PromptText.Left(50)));
			ChatView->AddMessage(SlashMsg);
		}
	}

	// Phase 3: Resolve @references in the user input
	if (ReferenceParser.IsValid())
	{
		FAutonomixParseReferencesResult RefResult = ReferenceParser->ParseAndResolve(ProcessedPrompt);
		if (RefResult.ResolvedReferences.Num() > 0)
		{
			// Prepend resolved reference content to the processed prompt
			FString RefContent;
			for (const FAutonomixResolvedReference& Ref : RefResult.ResolvedReferences)
			{
				if (Ref.bSuccess)
				{
					RefContent += Ref.Content + TEXT("\n\n");
				}
			}
			ProcessedPrompt = RefContent + TEXT("---\n\n") + RefResult.ProcessedText;

			FAutonomixMessage RefMsg(EAutonomixMessageRole::System,
				FString::Printf(TEXT("📎 Resolved %d reference(s) from input"), RefResult.ResolvedReferences.Num()));
			ChatView->AddMessage(RefMsg);
		}
	}

	// Add user message to conversation and UI
	FAutonomixMessage& UserMsg = ConversationManager->AddUserMessage(ProcessedPrompt);
	ChatView->AddMessage(UserMsg);
	SaveTabsToDisk();

	// Create a placeholder streaming message for the assistant
	FAutonomixMessage StreamingMsg(EAutonomixMessageRole::Assistant, TEXT(""));
	StreamingMsg.bIsStreaming = true;
	CurrentStreamingMessageId = StreamingMsg.MessageId;
	ChatView->AddMessage(StreamingMsg);

	// Build system prompt and send using effective history (condense/truncate aware)
	FString SystemPrompt = BuildSystemPrompt();

	// Phase 2: Use mode-filtered schemas
	// CRITICAL FIX (local providers): Local models (Ollama/LMStudio) cannot handle 90+ tools.
	// Use GetEssentialSchemas() which returns ~15 core tools (~750 tokens) instead of
	// the full set (~5,000 tokens). Without this, local models respond with
	// "I can't create files" because tools overwhelm the context window.
	const UAutonomixDeveloperSettings* ProviderSettings = UAutonomixDeveloperSettings::Get();
	const bool bIsLocalProvider = ProviderSettings &&
		(ProviderSettings->ActiveProvider == EAutonomixProvider::Ollama ||
		 ProviderSettings->ActiveProvider == EAutonomixProvider::LMStudio);

	TArray<TSharedPtr<FJsonObject>> ToolSchemas;
	if (ToolSchemaRegistry.IsValid())
	{
		ToolSchemas = bIsLocalProvider
			? ToolSchemaRegistry->GetEssentialSchemas()
			: ToolSchemaRegistry->GetSchemasForMode(CurrentAgentMode);
	}

	// Phase 4: Inject MCP tool schemas if available
	if (MCPClient.IsValid())
	{
		TArray<TSharedPtr<FJsonObject>> MCPSchemas = MCPClient->GetToolSchemasForAPI();
		ToolSchemas.Append(MCPSchemas);
	}

	// Use GetEffectiveHistory() instead of GetPrunedHistory() --
	// This respects condense/truncation tags for a proper "fresh start" after condensation
	TArray<FAutonomixMessage> EffectiveHistory = ConversationManager->GetEffectiveHistory();

	LLMClient->SendMessage(
		EffectiveHistory,
		SystemPrompt,
		ToolSchemas
	);
}

// ============================================================================
// Stop Button
// ============================================================================

FReply SAutonomixMainPanel::OnStopClicked()
{
	if (LLMClient.IsValid())
	{
		LLMClient->CancelRequest();
	}

	bIsProcessing = false;
	bInAgenticLoop = false;
	AgenticLoopCount = 0;
	ConsecutiveNoToolCount = 0;
	ToolCallQueue.Empty();

	FAutonomixMessage StopMsg(EAutonomixMessageRole::System, TEXT("⏹ Request cancelled by user."));
	ChatView->AddMessage(StopMsg);

	if (InputArea.IsValid())
	{
		InputArea->SetSendEnabled(true);
		InputArea->FocusInput();
	}
	if (ProgressOverlay.IsValid())
	{
		ProgressOverlay->HideProgress();
	}
	if (PlanPreview.IsValid())
	{
		PlanPreview->HidePlan();
	}

	return FReply::Handled();
}

// ============================================================================
// Streaming Callbacks
// ============================================================================

void SAutonomixMainPanel::OnStreamingText(const FGuid& MessageId, const FString& DeltaText)
{
	if (ChatView.IsValid())
	{
		ChatView->UpdateStreamingMessage(CurrentStreamingMessageId, DeltaText, EAutonomixMessageRole::Assistant);
	}
}

void SAutonomixMainPanel::OnToolCallReceived(const FAutonomixToolCall& ToolCall)
{
	UE_LOG(LogAutonomix, Log, TEXT("MainPanel: Tool call queued: %s (id: %s)"),
		*ToolCall.ToolName, *ToolCall.ToolUseId);

	ToolCallQueue.Add(ToolCall);
}

void SAutonomixMainPanel::OnMessageComplete(const FAutonomixMessage& Message)
{
	// CRITICAL FIX: Use AddAssistantMessageFull to preserve ContentBlocksJson.
	// Without this, tool_use blocks are lost and subsequent tool_result messages
	// reference non-existent tool_use_ids, causing Claude to reject with HTTP 400
	// "unexpected tool_use_id found in 'tool_result' blocks".
	ConversationManager->AddAssistantMessageFull(Message);
	SaveTabsToDisk();
}

void SAutonomixMainPanel::OnRequestStarted()
{
	bIsProcessing = true;
	if (InputArea.IsValid())
	{
		InputArea->SetSendEnabled(false);
	}
	if (ProgressOverlay.IsValid())
	{
		FString StatusText = bInAgenticLoop
			? FString::Printf(TEXT("Executing tools... (iteration %d)"), AgenticLoopCount)
			: TEXT("Thinking...");
		ProgressOverlay->ShowProgress(StatusText);
	}
}

void SAutonomixMainPanel::OnRequestCompleted(bool bSuccess)
{
	if (!bSuccess)
	{
		bIsProcessing = false;
		bInAgenticLoop = false;
		if (InputArea.IsValid())
		{
			InputArea->SetSendEnabled(true);
			InputArea->FocusInput();
		}
		if (ProgressOverlay.IsValid())
		{
			ProgressOverlay->HideProgress();
		}
		return;
	}

	// ---- Context Management: Run after each successful API response ----
	// This checks if we need to condense or truncate before processing tool calls.
	// We do this asynchronously and continue when done.
	if (ContextManager.IsValid() && !ContextManager->IsManaging())
	{
		FString SystemPrompt = BuildSystemPrompt();

		// Capture for async lambda
		TSharedPtr<SAutonomixMainPanel> ThisWidget = SharedThis(this);

		ContextManager->ManageContext(SystemPrompt, LastResponseTokenUsage,
			[ThisWidget](const FAutonomixContextManagementResult& CtxResult)
			{
				if (!ThisWidget.IsValid()) return;

				if (CtxResult.bDidCondense)
				{
					FAutonomixMessage CtxMsg(EAutonomixMessageRole::System,
						FString::Printf(TEXT("📦 Context condensed (was %.0f%% full). Summary created."),
							CtxResult.ContextPercent));
					ThisWidget->ChatView->AddMessage(CtxMsg);

					UE_LOG(LogAutonomix, Log,
						TEXT("MainPanel: Context condensed. Was %.0f%%, now ~%d tokens."),
						CtxResult.ContextPercent, CtxResult.NewContextTokens);
				}
				else if (CtxResult.bDidTruncate)
				{
					FAutonomixMessage CtxMsg(EAutonomixMessageRole::System,
						FString::Printf(TEXT("✂ Context truncated: %d old messages hidden (was %.0f%% full)."),
							CtxResult.MessagesRemoved, CtxResult.ContextPercent));
					ThisWidget->ChatView->AddMessage(CtxMsg);

					UE_LOG(LogAutonomix, Log,
						TEXT("MainPanel: Context truncated. Removed %d messages. Was %.0f%% full."),
						CtxResult.MessagesRemoved, CtxResult.ContextPercent);
				}

				// Update context usage percentage for header display
				if (CtxResult.PrevContextTokens > 0)
				{
					const int32 WindowTokens = ThisWidget->GetContextWindowTokens();
					ThisWidget->ContextUsagePercent = FAutonomixTokenCounter::GetContextUsagePercent(
						CtxResult.PrevContextTokens, WindowTokens);
				}

				ThisWidget->SaveTabsToDisk();

				// Update code structure context after condensation
				// (regenerates folded file signatures like Roo Code's <system-reminder> blocks)
				ThisWidget->UpdateCodeStructureContext();

				// Now continue with tool call processing
				ThisWidget->OnRequestCompletedPostContextManagement();
			});
		return; // Wait for context management to complete
	}

	// No context manager -- proceed directly
	OnRequestCompletedPostContextManagement();
}

void SAutonomixMainPanel::OnRequestCompletedPostContextManagement()
{
	// AGENTIC LOOP: If tool calls were received, check approval flow
	if (ToolCallQueue.Num() > 0)
	{
		const UAutonomixDeveloperSettings* Settings = UAutonomixDeveloperSettings::Get();

		// Auto-approve: skip the approval panel and execute immediately
		if (Settings && Settings->bAutoApproveAllTools)
		{
			UE_LOG(LogAutonomix, Log, TEXT("MainPanel: Auto-approving %d tool calls (bAutoApproveAllTools=true)"), ToolCallQueue.Num());
			ProcessToolCallQueue();
			return;
		}

		// Per-category auto-approval: auto-approve if ALL queued tools are read-only
		// and bAutoApproveReadOnlyTools is enabled (Roo Code: category-based approval)
		if (Settings && Settings->bAutoApproveReadOnlyTools)
		{
			static const TArray<FString> ReadOnlyToolPrefixes = {
				TEXT("get_"), TEXT("read_"), TEXT("list_"), TEXT("search_"),
				TEXT("find_"), TEXT("query_"), TEXT("show_"), TEXT("describe_")
			};

			bool bAllReadOnly = true;
			for (const FAutonomixToolCall& TC : ToolCallQueue)
			{
				bool bIsReadOnly = false;
				for (const FString& Prefix : ReadOnlyToolPrefixes)
				{
					if (TC.ToolName.StartsWith(Prefix, ESearchCase::IgnoreCase))
					{
						bIsReadOnly = true;
						break;
					}
				}
				// Meta-tools are always safe
				if (TC.ToolName == TEXT("update_todo_list") || TC.ToolName == TEXT("switch_mode"))
				{
					bIsReadOnly = true;
				}
				if (!bIsReadOnly)
				{
					bAllReadOnly = false;
					break;
				}
			}

			if (bAllReadOnly)
			{
				UE_LOG(LogAutonomix, Log,
					TEXT("MainPanel: Auto-approving %d read-only tool calls (bAutoApproveReadOnlyTools=true)"),
					ToolCallQueue.Num());
				ProcessToolCallQueue();
				return;
			}
		}

		// Show the approval panel for the user to review
		if (PlanPreview.IsValid())
		{
			if (ProgressOverlay.IsValid())
			{
				ProgressOverlay->ShowProgress(TEXT("Awaiting tool approval..."));
			}

			FAutonomixMessage ApprovalMsg(EAutonomixMessageRole::System,
				FString::Printf(TEXT("🔧 %d tool call(s) pending approval. Review and approve/reject below."), ToolCallQueue.Num()));
			ChatView->AddMessage(ApprovalMsg);

			PlanPreview->ShowToolCalls(ToolCallQueue);
		}
		else
		{
			// Fallback: no plan preview widget, auto-execute
			ProcessToolCallQueue();
		}
		return;
	}

	// No tool calls received.
	// Roo Code approach: track consecutive no-tool responses.
	// On the FIRST no-tool response during agentic loop, nudge ONCE.
	// On subsequent no-tool responses, stop — do not nudge infinitely.
	// The AI should use attempt_completion to formally end the task.

	if (bInAgenticLoop)
	{
		ConsecutiveNoToolCount++;

		if (ConsecutiveNoToolCount == 1)
		{
			// First no-tool response: nudge once with instructions
			// Roo Code: inject a reminder about attempt_completion
			FAutonomixMessage& NudgeMsg = ConversationManager->AddUserMessage(
				TEXT("[AUTONOMIX SYSTEM] You did not use any tools in your last response. ")
				TEXT("IMPORTANT: You MUST use a tool to continue. If the task is complete, ")
				TEXT("call the attempt_completion tool with a summary of what was accomplished. ")
				TEXT("If there is more work to do, use the appropriate tool to continue. ")
				TEXT("Do NOT respond with plain text only — you MUST call a tool."));

			SaveTabsToDisk();
			ContinueAgenticLoop();
			return;
		}

		if (ConsecutiveNoToolCount >= MaxConsecutiveNoToolResponses)
		{
			// Repeated no-tool responses — ask user rather than infinite nudge
			FText Title = FText::FromString(TEXT("Autonomix — AI Stopped Using Tools"));
			FText Message = FText::FromString(FString::Printf(
				TEXT("The AI has responded %d times without using any tools.\n\n")
				TEXT("This may mean:\n")
				TEXT("  • The task is done (but it forgot to call attempt_completion)\n")
				TEXT("  • The AI is stuck and needs guidance\n\n")
				TEXT("YES — Continue the task\n")
				TEXT("NO — End the task now"),
				ConsecutiveNoToolCount));

			EAppReturnType::Type Result = FMessageDialog::Open(EAppMsgType::YesNo, Message, Title);

			if (Result == EAppReturnType::Yes)
			{
				ConsecutiveNoToolCount = 0;
				FAutonomixMessage& NudgeMsg = ConversationManager->AddUserMessage(
					TEXT("[AUTONOMIX SYSTEM] Please continue the task. If it is complete, ")
					TEXT("you MUST call attempt_completion. Do not respond with text only."));
				SaveTabsToDisk();
				ContinueAgenticLoop();
				return;
			}
			// else: fall through to end the task
		}
		else
		{
			// Between 1 and max — stop immediately, don't loop
			// The single nudge above already fired; if still no tools, just stop gracefully
		}
	}

	// Task is done (either not in agentic loop, or user chose to end)
	bIsProcessing = false;
	bInAgenticLoop = false;
	AgenticLoopCount = 0;
	ConsecutiveNoToolCount = 0;

	if (InputArea.IsValid())
	{
		InputArea->SetSendEnabled(true);
		InputArea->FocusInput();
	}
	if (ProgressOverlay.IsValid())
	{
		ProgressOverlay->HideProgress();
	}

	SaveTabsToDisk();
}

void SAutonomixMainPanel::OnErrorReceived(const FAutonomixHTTPError& Error)
{
	FAutonomixMessage ErrorMsg(EAutonomixMessageRole::Error, Error.UserFriendlyMessage);
	ChatView->AddMessage(ErrorMsg);
}

void SAutonomixMainPanel::OnTokenUsageUpdated(const FAutonomixTokenUsage& Usage)
{
	// Update live UI after each token update
	UpdateLiveUI();

	SessionTokenUsage.InputTokens += Usage.InputTokens;
	SessionTokenUsage.OutputTokens += Usage.OutputTokens;

	// Store last response usage for context management
	LastResponseTokenUsage = Usage;

	// Update context usage percentage for header display
	if (Usage.InputTokens > 0)
	{
		const int32 WindowTokens = GetContextWindowTokens();
		ContextUsagePercent = FAutonomixTokenCounter::GetContextUsagePercent(
			Usage.InputTokens, WindowTokens);
	}

	// Phase 4.1: Calculate per-request cost and accumulate session total
	const UAutonomixDeveloperSettings* Settings = UAutonomixDeveloperSettings::Get();
	if (Settings)
	{
		FAutonomixRequestCost RequestCost = FAutonomixCostTracker::CalculateRequestCost(
			Settings->ClaudeModel, Usage);
		CostTracker.AddRequestCost(RequestCost);
		LastRequestCost = RequestCost.TotalCost;

		// Show cost in chat if enabled
		if (Settings->bShowPerRequestCost && RequestCost.TotalCost > 0.0f)
		{
			UE_LOG(LogAutonomix, Log, TEXT("MainPanel: Request cost: %s (session total: %s)"),
				*FAutonomixCostTracker::FormatCost(RequestCost.TotalCost),
				*FAutonomixCostTracker::FormatCost(CostTracker.GetSessionTotalCost()));
		}
	}

	UE_LOG(LogAutonomix, Log, TEXT("MainPanel: Token usage -- input: %d, output: %d, session total: %d, ctx: %.1f%%"),
		Usage.InputTokens, Usage.OutputTokens, SessionTokenUsage.TotalTokens(), ContextUsagePercent);
	SaveTabsToDisk();
}

// ============================================================================
// AGENTIC LOOP: Tool Execution -> Result -> Re-send
// ============================================================================

void SAutonomixMainPanel::ProcessToolCallQueue()
{
	// Roo Code approach: NO hard iteration limit.
	// The loop continues until:
	//   1. Claude responds with text only (no tool_use) — task is done
	//   2. Auto-approval limits (cost + request count) are exceeded — asks user
	//   3. User clicks Stop
	//   4. Context window fills up (handled by context management)
	// The only safety net is the auto-approval handler which pauses to ask.

	// Reset consecutive no-tool counter since we have tool calls
	ConsecutiveNoToolCount = 0;

	// Phase 3.1: Check auto-approval limits (request count + cost cap)
	const UAutonomixDeveloperSettings* Settings = UAutonomixDeveloperSettings::Get();
	if (Settings)
	{
		FAutonomixAutoApprovalCheck Check = AutoApprovalHandler.CheckLimits(
			Settings->MaxConsecutiveAutoApprovedRequests,
			Settings->MaxAutoApprovedCostDollars,
			LastRequestCost);

		if (Check.bRequiresApproval)
		{
			HandleAutoApprovalLimitReached(Check);
			return; // Wait for user approval before continuing
		}
	}

	// Phase 5.2: Create a checkpoint snapshot before executing this tool batch
	if (Settings && Settings->bEnableAutoBackup)
	{
		CreateCheckpointForToolBatch(ToolCallQueue, AgenticLoopCount);
	}

	bInAgenticLoop = true;
	AgenticLoopCount++;

	// Record this batch in auto-approval tracking
	AutoApprovalHandler.RecordBatch(LastRequestCost);

	UE_LOG(LogAutonomix, Log, TEXT("MainPanel: Processing %d tool calls (loop %d)"),
		ToolCallQueue.Num(), AgenticLoopCount);

	TArray<FAutonomixToolCall> ActiveToolCalls = MoveTemp(ToolCallQueue);
	ToolCallQueue.Empty();
	for (const FAutonomixToolCall& ToolCall : ActiveToolCalls)
	{
		// Phase 1: Check for tool repetition (identical consecutive calls)
		if (ToolRepetitionDetector.IsValid())
		{
			FAutonomixToolRepetitionCheck RepCheck = ToolRepetitionDetector->Check(ToolCall);
			if (!RepCheck.bAllowExecution)
			{
				// Block this tool call — show warning dialog
				const FText Title = FText::FromString(TEXT("Autonomix — Repetition Loop Detected"));
				const FText Msg = FText::FromString(RepCheck.WarningMessage);
				EAppReturnType::Type UserResponse = FMessageDialog::Open(EAppMsgType::YesNo, Msg, Title);

				if (UserResponse != EAppReturnType::Yes)
				{
					// User chose to stop
					ActiveToolCalls.Empty();
					bIsProcessing = false;
					bInAgenticLoop = false;
					AgenticLoopCount = 0;
					FAutonomixMessage StopMsg(EAutonomixMessageRole::System,
						TEXT("⏹ Task stopped: AI repetition loop detected."));
					ChatView->AddMessage(StopMsg);
					if (InputArea.IsValid()) { InputArea->SetSendEnabled(true); InputArea->FocusInput(); }
					if (ProgressOverlay.IsValid()) ProgressOverlay->HideProgress();
					return;
				}
				// User chose to allow one more try — continue
			}
		}

		// Add a collapsible "Executing" system message; body will be filled with the result below
		FString ExecutingParamsStr;
		if (ToolCall.InputParams.IsValid())
		{
			TSharedRef<TJsonWriter<>> ParamsWriter = TJsonWriterFactory<>::Create(&ExecutingParamsStr);
			FJsonSerializer::Serialize(ToolCall.InputParams.ToSharedRef(), ParamsWriter);
		}
		FAutonomixMessage ToolMsg(EAutonomixMessageRole::Assistant,
			FString::Printf(TEXT("🔧 Executing: %s\n%s\n\n"), *ToolCall.ToolName, *ExecutingParamsStr));
		ToolMsg.bIsCollapsible = true;
		ChatView->AddMessage(ToolMsg);

		bool bIsError = false;
		FString ResultContent = ExecuteToolCall(ToolCall, bIsError);

		FAutonomixMessage& ToolResultMsg = ConversationManager->AddToolResultMessage(
			ToolCall.ToolUseId, ResultContent, bIsError);

		if (bIsError)
		{
			ToolResultMsg.ToolName = TEXT("error");
		}

		// Append the full result into the previously added executing message (no truncation)
		FString ResultToAppend = ResultContent;
		if (bIsError)
		{
			ResultToAppend = FString::Printf(TEXT("❌ %s"), *ResultContent);
		}
		else
		{
			ResultToAppend = FString::Printf(TEXT("✅ %s"), *ResultContent);
		}
		ChatView->UpdateStreamingMessage(ToolMsg.MessageId, ResultToAppend);
	}

	ActiveToolCalls.Empty();
	SaveTabsToDisk();

	// CRITICAL: Check if attempt_completion stopped the loop during tool execution.
	// If bInAgenticLoop was set to false by HandleAttemptCompletion, do NOT
	// call ContinueAgenticLoop — the task is done.
	if (!bInAgenticLoop)
	{
		UE_LOG(LogAutonomix, Log, TEXT("MainPanel: Agentic loop was terminated (attempt_completion). Not continuing."));
		return;
	}

	ContinueAgenticLoop();
}

FString SAutonomixMainPanel::ExecuteToolCall(const FAutonomixToolCall& ToolCall, bool& bOutIsError)
{
	bOutIsError = false;
	FDateTime StartTime = FDateTime::UtcNow();

	// ---- Meta-tool: update_todo_list (handled locally, not routed to action executors) ----
	if (ToolCall.ToolName == TEXT("update_todo_list"))
	{
		return HandleUpdateTodoList(ToolCall);
	}

	// ---- CRITICAL: attempt_completion — terminates the agentic loop ----
	// This is how the AI signals "I'm done". Must be handled BEFORE routing
	// to action executors. Returns the result to the conversation and stops
	// the agentic loop cleanly.
	if (ToolCall.ToolName == TEXT("attempt_completion"))
	{
		return HandleAttemptCompletion(ToolCall);
	}

	// ---- Phase 2 Meta-tool: switch_mode ----
	if (ToolCall.ToolName == TEXT("switch_mode"))
	{
		return HandleSwitchMode(ToolCall);
	}

	// ---- Phase 3 Meta-tool: new_task (task delegation) ----
	if (ToolCall.ToolName == TEXT("new_task"))
	{
		return HandleNewTask(ToolCall);
	}

	// ---- Phase 4 Meta-tool: skill ----
	if (ToolCall.ToolName == TEXT("skill"))
	{
		return HandleSkillTool(ToolCall);
	}

	// Check security mode
	const UAutonomixDeveloperSettings* Settings = UAutonomixDeveloperSettings::Get();
	TSharedPtr<IAutonomixActionExecutor> Executor = ActionRouter->FindExecutorForTool(ToolCall.ToolName);
	if (Executor.IsValid() && Settings)
	{
		if (!Settings->IsToolCategoryAllowed(Executor->GetCategory()))
		{
			bOutIsError = true;
			FString ErrorMsg = FString::Printf(
				TEXT("Tool '%s' is blocked by current security mode (%s). Change security mode in settings to use this tool."),
				*ToolCall.ToolName,
				Settings->SecurityMode == EAutonomixSecurityMode::Sandbox ? TEXT("Sandbox") : TEXT("Advanced"));

			FAutonomixActionExecutionRecord Record;
			Record.ToolName = ToolCall.ToolName;
			Record.ToolUseId = ToolCall.ToolUseId;
			Record.bSuccess = false;
			Record.bIsError = true;
			Record.ResultMessage = ErrorMsg;
			if (ExecutionJournal.IsValid()) ExecutionJournal->RecordExecution(Record);

			return ErrorMsg;
		}
	}

	// CRITICAL FIX (ChatGPT): Compute pre-state hash for deterministic verification
	FString PreHash;
	if (ToolCall.InputParams.IsValid())
	{
		FString AssetPath;
		if (ToolCall.InputParams->TryGetStringField(TEXT("asset_path"), AssetPath))
		{
			PreHash = FAutonomixExecutionJournal::ComputeAssetHash(AssetPath);
		}
		else
		{
			FString FilePath;
			if (ToolCall.InputParams->TryGetStringField(TEXT("file_path"), FilePath))
			{
				PreHash = FAutonomixExecutionJournal::ComputeFileHash(FilePath);
			}
		}
	}

	// Route and execute
	FAutonomixActionResult Result = ActionRouter->RouteToolCall(ToolCall);

	FDateTime EndTime = FDateTime::UtcNow();
	float ElapsedSeconds = (EndTime - StartTime).GetTotalSeconds();

	// Compute post-state hash
	FString PostHash;
	if (ToolCall.InputParams.IsValid())
	{
		FString AssetPath;
		if (ToolCall.InputParams->TryGetStringField(TEXT("asset_path"), AssetPath))
		{
			PostHash = FAutonomixExecutionJournal::ComputeAssetHash(AssetPath);
		}
		else
		{
			FString FilePath;
			if (ToolCall.InputParams->TryGetStringField(TEXT("file_path"), FilePath))
			{
				PostHash = FAutonomixExecutionJournal::ComputeFileHash(FilePath);
			}
		}
	}

	// Build result content for Claude
	FString ResultContent;
	if (Result.bSuccess)
	{
		ResultContent = Result.ResultMessage;
		if (Result.ModifiedAssets.Num() > 0)
		{
			ResultContent += TEXT("\nModified assets: ") + FString::Join(Result.ModifiedAssets, TEXT(", "));
		}
		if (Result.ModifiedPaths.Num() > 0)
		{
			ResultContent += TEXT("\nModified files: ") + FString::Join(Result.ModifiedPaths, TEXT(", "));

			// Phase 1: Track files modified by Autonomix (prevents false stale detection)
			if (FileContextTracker.IsValid())
			{
				for (const FString& ModifiedPath : Result.ModifiedPaths)
				{
					// Convert to relative path
					FString RelPath = ModifiedPath;
					FPaths::MakePathRelativeTo(RelPath, *FPaths::ProjectDir());
					FileContextTracker->OnFileEditedByAutonomix(RelPath);
				}
			}
		}
		if (Result.Warnings.Num() > 0)
		{
			ResultContent += TEXT("\nWarnings: ") + FString::Join(Result.Warnings, TEXT("; "));
		}
	}
	else
	{
		bOutIsError = true;
		ResultContent = TEXT("EXECUTION FAILED: ") + FString::Join(Result.Errors, TEXT("; "));
	}

	// Record in execution journal with state hashes
	FAutonomixActionExecutionRecord Record;
	Record.ToolName = ToolCall.ToolName;
	Record.ToolUseId = ToolCall.ToolUseId;
	Record.bSuccess = Result.bSuccess;
	Record.bIsError = !Result.bSuccess;
	Record.ResultMessage = ResultContent;
	Record.ModifiedFiles = Result.ModifiedPaths;
	Record.ModifiedAssets = Result.ModifiedAssets;
	Record.BackupPaths = Result.BackupPaths;
	Record.ExecutionTimeSeconds = ElapsedSeconds;
	Record.PreStateHash = PreHash;
	Record.PostStateHash = PostHash;

	if (ToolCall.InputParams.IsValid())
	{
		FString InputStr;
		TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&InputStr);
		FJsonSerializer::Serialize(ToolCall.InputParams.ToSharedRef(), Writer);
		Record.InputJson = InputStr;
	}

	if (ExecutionJournal.IsValid())
	{
		ExecutionJournal->RecordExecution(Record);
	}

	return ResultContent;
}

void SAutonomixMainPanel::ContinueAgenticLoop()
{
	const UAutonomixDeveloperSettings* Settings = UAutonomixDeveloperSettings::Get();
	if (!Settings) return;

	FAutonomixMessage StreamingMsg(EAutonomixMessageRole::Assistant, TEXT(""));
	StreamingMsg.bIsStreaming = true;
	CurrentStreamingMessageId = StreamingMsg.MessageId;
	ChatView->AddMessage(StreamingMsg);

	// Phase 2: Use mode-filtered schemas (local provider: essential set only)
	const UAutonomixDeveloperSettings* LoopSettings = UAutonomixDeveloperSettings::Get();
	const bool bIsLocalLoop = LoopSettings &&
		(LoopSettings->ActiveProvider == EAutonomixProvider::Ollama ||
		 LoopSettings->ActiveProvider == EAutonomixProvider::LMStudio);

	TArray<TSharedPtr<FJsonObject>> ToolSchemas;
	if (ToolSchemaRegistry.IsValid())
	{
		ToolSchemas = bIsLocalLoop
			? ToolSchemaRegistry->GetEssentialSchemas()
			: ToolSchemaRegistry->GetSchemasForMode(CurrentAgentMode);
	}

	// Use GetEffectiveHistory() -- respects condense/truncation tags
	TArray<FAutonomixMessage> EffectiveHistory = ConversationManager->GetEffectiveHistory();

	// Phase 2: Build per-message environment details and inject into last message
	// This appends fresh editor state (open files, selected actors, errors, etc.)
	// to each API call without growing the static system prompt.
	FString EnvDetails = BuildEnvironmentDetailsString();
	if (!EnvDetails.IsEmpty() && EffectiveHistory.Num() > 0)
	{
		// Append to the last message in the history
		FAutonomixMessage& LastMsg = EffectiveHistory.Last();
		if (LastMsg.Role == EAutonomixMessageRole::User ||
			LastMsg.Role == EAutonomixMessageRole::ToolResult)
		{
			if (!LastMsg.Content.IsEmpty())
			{
				LastMsg.Content += TEXT("\n\n");
			}
			LastMsg.Content += EnvDetails;
		}
	}

	// Phase 2: Mode-aware system prompt (includes role definition for current mode)
	FString SystemPrompt = BuildSystemPrompt();

	LLMClient->SendMessage(
		EffectiveHistory,
		SystemPrompt,
		ToolSchemas
	);

	UE_LOG(LogAutonomix, Log, TEXT("MainPanel: Agentic loop iteration %d (mode=%s) -- re-sending conversation to Claude (%d effective messages, %d schemas)."),
		AgenticLoopCount,
		*FAutonomixToolSchemaRegistry::GetModeDisplayName(CurrentAgentMode),
		EffectiveHistory.Num(),
		ToolSchemas.Num());
}

// ============================================================================
// Auto-Approval Limit Handling (Phase 3.1)
// ============================================================================

void SAutonomixMainPanel::HandleAutoApprovalLimitReached(const FAutonomixAutoApprovalCheck& Check)
{
	FText Title = FText::FromString(TEXT("Autonomix — Approval Required"));
	FText Message = FText::FromString(Check.ApprovalReason);

	EAppReturnType::Type Result = FMessageDialog::Open(EAppMsgType::YesNo, Message, Title);

	if (Result == EAppReturnType::Yes)
	{
		AutoApprovalHandler.ResetBaseline();

		FAutonomixMessage InfoMsg(EAutonomixMessageRole::System,
			TEXT("✅ Continuation approved. Auto-approval counters reset."));
		ChatView->AddMessage(InfoMsg);

		ProcessToolCallQueue();
	}
	else
	{
		ToolCallQueue.Empty();
		bIsProcessing = false;
		bInAgenticLoop = false;
		AgenticLoopCount = 0;

		FAutonomixMessage StopMsg(EAutonomixMessageRole::System,
			TEXT("⏹ Task stopped by user at auto-approval limit."));
		ChatView->AddMessage(StopMsg);

		if (InputArea.IsValid()) { InputArea->SetSendEnabled(true); InputArea->FocusInput(); }
		if (ProgressOverlay.IsValid()) ProgressOverlay->HideProgress();
	}
}

// ============================================================================
// Asset Checkpoints (Phase 5.2)
// ============================================================================

void SAutonomixMainPanel::CreateCheckpointForToolBatch(
	const TArray<FAutonomixToolCall>& ToolCalls, int32 LoopIteration)
{
	// Phase 3: Save git checkpoint before executing tool batch
	if (CheckpointManager.IsValid() && CheckpointManager->IsInitialized())
	{
		FAutonomixCheckpoint CP;
		const FString Description = FString::Printf(TEXT("Before tool batch %d"), LoopIteration);
		if (CheckpointManager->SaveCheckpoint(Description, LoopIteration, CP))
		{
			UE_LOG(LogAutonomix, Log,
				TEXT("MainPanel: Saved git checkpoint '%s' -> %s"),
				*Description, *CP.CommitHash.Left(8));
		}
	}

	// Also keep file-level backups (AutonomixBackupManager) for quick single-file restore
	if (!BackupManager.IsValid()) return;

	TArray<FString> AffectedPaths = GetAffectedPathsFromToolCalls(ToolCalls);
	if (AffectedPaths.IsEmpty()) return;

	TArray<FString> BackupPaths = BackupManager->BackupFiles(AffectedPaths);

	if (BackupPaths.Num() > 0)
	{
		UE_LOG(LogAutonomix, Log,
			TEXT("MainPanel: Checkpoint loop %d — backed up %d assets."),
			LoopIteration, AffectedPaths.Num());
	}
}

TArray<FString> SAutonomixMainPanel::GetAffectedPathsFromToolCalls(
	const TArray<FAutonomixToolCall>& ToolCalls) const
{
	TArray<FString> Paths;
	for (const FAutonomixToolCall& ToolCall : ToolCalls)
	{
		if (!ToolCall.InputParams.IsValid()) continue;
		FString AssetPath;
		if (ToolCall.InputParams->TryGetStringField(TEXT("asset_path"), AssetPath) && !AssetPath.IsEmpty())
			Paths.AddUnique(AssetPath);
		FString FilePath;
		if (ToolCall.InputParams->TryGetStringField(TEXT("file_path"), FilePath) && !FilePath.IsEmpty())
			Paths.AddUnique(FilePath);
		FString Path;
		if (ToolCall.InputParams->TryGetStringField(TEXT("path"), Path) && !Path.IsEmpty())
			Paths.AddUnique(Path);
	}
	return Paths;
}

// ============================================================================
// Approval Flow: User approves or rejects tool calls
// ============================================================================

void SAutonomixMainPanel::OnToolCallsApproved(const FAutonomixActionPlan& Plan)
{
	UE_LOG(LogAutonomix, Log, TEXT("MainPanel: User approved %d tool calls."), ToolCallQueue.Num());

	FAutonomixMessage ApprovedMsg(EAutonomixMessageRole::System,
		TEXT("✅ Tool calls approved by user. Executing..."));
	ChatView->AddMessage(ApprovedMsg);

	// Execute the queued tool calls
	ProcessToolCallQueue();
}

void SAutonomixMainPanel::OnToolCallsRejected(const FAutonomixActionPlan& Plan)
{
	UE_LOG(LogAutonomix, Log, TEXT("MainPanel: User rejected %d tool calls."), ToolCallQueue.Num());

	// Send rejection as tool_result errors so Claude knows the tools were not executed
	TArray<FAutonomixToolCall> ActiveToolCalls = MoveTemp(ToolCallQueue);
	ToolCallQueue.Empty();
	for (const FAutonomixToolCall& ToolCall : ActiveToolCalls)
	{
		FAutonomixMessage& ToolResultMsg = ConversationManager->AddToolResultMessage(
			ToolCall.ToolUseId,
			TEXT("Tool execution was rejected by the user. Do not retry this action unless explicitly asked."),
			true /* bIsError */);
		ToolResultMsg.ToolName = TEXT("error");
	}

	ActiveToolCalls.Empty();
	SaveTabsToDisk();

	FAutonomixMessage RejectedMsg(EAutonomixMessageRole::System,
		TEXT("❌ Tool calls rejected by user. The AI has been informed."));
	ChatView->AddMessage(RejectedMsg);

	// Continue the agentic loop so Claude can respond to the rejection
	ContinueAgenticLoop();
}

// ============================================================================
// System Prompt Construction
// ============================================================================

FString SAutonomixMainPanel::BuildSystemPrompt() const
{
	// =========================================================================
	// LOCAL PROVIDER FAST PATH — Condensed system prompt for Ollama/LMStudio
	//
	// Full system prompt = ~7,000+ tokens (role + tool use + guidelines + rules
	// with Blueprint workflow + objective + project context + code structure).
	// For a local model with 8K context, this alone fills the window, leaving
	// no room for user messages, tools, or responses.
	//
	// This condensed prompt is ~800 tokens — saving ~6,000+ tokens.
	// Combined with GetEssentialSchemas() (~750 tokens) and suppressed project
	// context, the total overhead drops from ~42,000 to ~1,800 tokens.
	// =========================================================================
	{
		const UAutonomixDeveloperSettings* LocalSettings = UAutonomixDeveloperSettings::Get();
		const bool bIsLocalProvider = LocalSettings &&
			(LocalSettings->ActiveProvider == EAutonomixProvider::Ollama ||
			 LocalSettings->ActiveProvider == EAutonomixProvider::LMStudio);

		if (bIsLocalProvider)
		{
			FString Prompt = TEXT(
				"You are Autonomix, an AI assistant for Unreal Engine. "
				"You have tools to read/write files, search assets, and modify Blueprints.\n\n"
				"CRITICAL RULES:\n"
				"- You MUST use tools to complete tasks. NEVER say 'I cannot' or 'I don't have access'.\n"
				"- You CAN create files (write_file), read files (read_file), edit files (apply_diff), "
				"list directories (list_directory), search files (search_files), and search assets (search_assets).\n"
				"- Use one tool per response. Wait for results before next step.\n"
				"- When done, call attempt_completion with a summary.\n"
				"- Read files before modifying them.\n"
				"- Follow UE5 conventions: UCLASS, UPROPERTY, UFUNCTION macros.\n\n"
				"TOOL FORMAT:\n"
				"To call a tool, respond with a tool_calls message containing the function name and arguments as JSON.\n"
				"Example: to read a file, call read_file with {\"file_path\": \"Source/MyGame/MyActor.h\"}\n"
				"Example: to write a file, call write_file with {\"file_path\": \"...\", \"content\": \"...\"}\n"
				"Example: to complete, call attempt_completion with {\"result\": \"Done: created MyActor.h\"}"
			);

			// Minimal project context: just project name and engine version
			if (ContextGatherer.IsValid())
			{
				FAutonomixProjectContext Ctx = ContextGatherer->BuildProjectContext();
				Prompt += FString::Printf(TEXT("\n\nPROJECT: %s (UE %s)\nRoot: %s"),
					*Ctx.ProjectName, *Ctx.EngineVersion, *Ctx.ProjectRootPath);

				// Add asset summary counts (single line) — much cheaper than full listings
				if (Ctx.AssetCountsByClass.Num() > 0)
				{
					Prompt += TEXT("\nAssets: ");
					bool bFirst = true;
					for (const auto& Pair : Ctx.AssetCountsByClass)
					{
						if (!bFirst) Prompt += TEXT(", ");
						Prompt += FString::Printf(TEXT("%s:%d"), *Pair.Key, Pair.Value);
						bFirst = false;
					}
				}
				if (Ctx.SourceTree.Num() > 0)
				{
					Prompt += FString::Printf(TEXT("\nSource files: %d"), Ctx.SourceTree.Num());
				}
			}

			UE_LOG(LogAutonomix, Log,
				TEXT("BuildSystemPrompt: LOCAL provider — using condensed prompt (~%d chars / ~%d est. tokens)"),
				Prompt.Len(), Prompt.Len() / 4);

			return Prompt;
		}
	}
	// =========================================================================
	// CLOUD PROVIDER PATH — Full system prompt (below)
	// =========================================================================

	// ---- Role definition (mode-specific) ----
	FString RoleDefinition;
	if (ToolSchemaRegistry.IsValid())
	{
		RoleDefinition = FAutonomixToolSchemaRegistry::GetModeRoleDefinition(CurrentAgentMode);
	}
	else
	{
		RoleDefinition = TEXT("You are Autonomix, an AI assistant embedded in the Unreal Engine Editor.");
	}

	// ---- TOOL USE section (from Roo Code's getSharedToolUseSection) ----
	const FString ToolUseSection = TEXT(
		"====\n\n"
		"TOOL USE\n\n"
		"You have access to a set of tools that are executed upon the user's approval. "
		"You MUST use a tool in every response when inside an agentic loop — do NOT respond with plain text only. "
		"To complete a task, call the appropriate tools one at a time. "
		"After each tool execution, you will receive the result. "
		"When the task is fully complete, you MUST call the attempt_completion tool to signal completion. "
		"NEVER end a task by responding with plain text — always use attempt_completion.\n\n"
		"CRITICAL RULE: If you have nothing more to do, call attempt_completion. "
		"If there is more work, call the appropriate work tool. "
		"There is NO valid reason to respond without calling a tool during an agentic task."
	);

	// ---- TOOL USE GUIDELINES (from Roo Code's getToolUseGuidelinesSection) ----
	const FString ToolUseGuidelinesSection = TEXT(
		"====\n\n"
		"TOOL USE GUIDELINES\n\n"
		"IMPORTANT RULES FOR EFFECTIVE TOOL USE:\n\n"
		"1. ANALYZE BEFORE ACTING: Before calling any tool, carefully analyze the current situation. "
		"Read file structure and existing code before making changes.\n\n"
		"2. ONE TOOL AT A TIME: Use one tool per response step. Wait for results before proceeding. "
		"Do not batch unrelated operations.\n\n"
		"3. GATHER BEFORE ASKING: Never ask the user for information you can get yourself with tools. "
		"Use read_file, list_files, and search_files to gather context.\n\n"
		"4. READ BEFORE WRITE: When modifying files, ALWAYS read current content first. "
		"Use apply_diff for targeted edits rather than full rewrites when possible.\n\n"
		"5. HANDLE ERRORS: If a tool call fails, analyze the error and try an alternative. "
		"Do NOT proceed if a prerequisite tool call failed.\n\n"
		"6. VERIFY CHANGES: After writing a file, read it back to confirm the change was applied correctly.\n\n"
		"7. COMPLETE CLEANLY: When all work is done, call attempt_completion with a clear summary. "
		"NEVER end with a text-only response during an agentic task."
	);

	// ---- OBJECTIVE section (from Roo Code's getObjectiveSection) ----
	const FString ObjectiveSection = TEXT(
		"====\n\n"
		"OBJECTIVE\n\n"
		"You accomplish a given task iteratively, breaking it down into clear steps and working through them methodically.\n\n"
		"1. Analyze the user's task and set clear, achievable goals. Prioritize in logical order.\n"
		"2. Work through goals sequentially using tools one at a time. Each goal = one distinct step.\n"
		"3. Before calling a tool, analyze what information you have. Use the most appropriate tool.\n"
		"4. Once the task is FULLY complete, call attempt_completion to present results to the user.\n"
		"5. The user may provide feedback which you can use to improve. Do NOT end responses with questions or offers for further assistance."
	);

	// ---- RULES section ----
	const FString RulesSection = TEXT(
		"====\n\n"
		"RULES\n\n"
		"- The project base directory is the Unreal Engine project root.\n"
		"- You cannot open files in external applications — use the provided tools only.\n"
		"- Do not ask for more information than necessary. Use tools to gather context.\n"
		"- When making changes, always consider the context in which code is used.\n"
		"- You are STRICTLY FORBIDDEN from starting messages with conversational openers like 'Great', 'Certainly', 'Okay', 'Sure'.\n"
		"- After completing a task, ALWAYS use attempt_completion — NEVER end with a text response only.\n"
		"- Every response during an agentic task MUST include at least one tool call.\n"
		"- Follow UE5 C++ conventions: UCLASS/UPROPERTY/UFUNCTION macros, TArray/TMap/FString over std::, check()/ensure() over assert().\n\n"

		"BLUEPRINT WORKFLOW (CRITICAL — follow this order):\n"
		"1. Call search_assets to find the Blueprint by name (e.g. query='BP_ThirdPerson', class_filter='Blueprint')\n"
		"2. Call get_blueprint_info with the asset_path from search results to see ALL nodes, pins, components, and variables\n"
		"3. BEFORE CREATING NEW NODES: Check if the needed nodes ALREADY EXIST in the graph. "
		"If they do, use connect_blueprint_pins to wire them together or set_node_pin_default to fix their values — do NOT create duplicates.\n"
		"4. If duplicate or unwanted nodes exist, use delete_blueprint_nodes to remove them BEFORE injecting replacements.\n"
		"5. Add logic nodes via inject_blueprint_nodes_t3d — include ALL connected nodes in ONE T3D block when possible (use LinkedTo references within the same T3D)\n"
		"6. After inject_blueprint_nodes_t3d, call get_blueprint_info AGAIN to see the actual internal node names that were created\n"
		"7. Use connect_blueprint_pins to wire nodes that were NOT connected in the T3D (e.g. connecting newly injected nodes to pre-existing event nodes)\n"
		"8. ALWAYS verify connections: call get_blueprint_info after connecting to confirm pins are wired\n\n"
		"NODE MANAGEMENT RULES:\n"
		"- NEVER inject nodes if equivalent nodes already exist — reconnect or fix existing ones instead\n"
		"- If you must replace nodes, DELETE the old ones first with delete_blueprint_nodes, then inject new ones\n"
		"- After any operation, call get_blueprint_info to verify the current state — do NOT assume success\n\n"
		"PIN CONNECTION RULES:\n"
		"- Execution flow: wire 'then' output to 'execute' input (white pins)\n"
		"- Data flow: wire data output pins (e.g. 'ReturnValue') to matching-type data input pins\n"
		"- The internal node names from get_blueprint_info are EXACT — use them verbatim in connect_blueprint_pins\n"
		"- Common pin names: 'then'/'execute' (exec flow), 'ReturnValue' (function output), 'self' (target), 'Value' (setter input)\n"
		"- NEVER leave nodes unconnected — every injected node must be wired into the execution flow\n\n"
		"T3D LIMITATIONS — use dedicated tools instead:\n"
		"- Enhanced Input nodes (K2Node_EnhancedInputAction) CANNOT be created via T3D — use add_enhanced_input_node tool instead\n"
		"- For Enhanced Input: search_assets for IA_ actions, then call add_enhanced_input_node, then connect_blueprint_pins to wire Triggered/Started output"
	);

	// ---- Project context ----
	FString ProjectContext;
	if (ContextGatherer.IsValid())
	{
		ProjectContext = ContextGatherer->BuildContextString();
	}

	// ---- Security mode ----
	FString SecurityInfo;
	const UAutonomixDeveloperSettings* Settings = UAutonomixDeveloperSettings::Get();
	if (Settings)
	{
		switch (Settings->SecurityMode)
		{
		case EAutonomixSecurityMode::Sandbox:
			SecurityInfo = TEXT("SECURITY MODE: Sandbox — Blueprints, Materials, Level editing allowed. C++, builds, and config edits DISABLED.");
			break;
		case EAutonomixSecurityMode::Advanced:
			SecurityInfo = TEXT("SECURITY MODE: Advanced — Full asset editing and C++ generation available. Builds DISABLED.");
			break;
		case EAutonomixSecurityMode::Developer:
			SecurityInfo = TEXT("SECURITY MODE: Developer — Full power. All tools including C++ and builds available.");
			break;
		}
	}

	// ---- Recent actions ----
	FString RecentActions;
	if (ExecutionJournal.IsValid() && ExecutionJournal->GetSessionRecordCount() > 0)
	{
		RecentActions = ExecutionJournal->BuildRecentActionsSummary(5);
	}

	// ---- Assemble final system prompt ----
	// Structure mirrors Roo Code: roleDefinition + toolUse + guidelines + capabilities + rules + objective
	FString SystemPrompt = RoleDefinition;
	SystemPrompt += TEXT("\n\n") + ToolUseSection;
	SystemPrompt += TEXT("\n\n") + ToolUseGuidelinesSection;
	if (!ProjectContext.IsEmpty()) SystemPrompt += TEXT("\n\n====\n\nPROJECT CONTEXT\n\n") + ProjectContext;
	if (!SecurityInfo.IsEmpty()) SystemPrompt += TEXT("\n\n") + SecurityInfo;
	if (!RecentActions.IsEmpty()) SystemPrompt += TEXT("\n\n") + RecentActions;

	// Inject folded code structure context if available
	// This survives context condensation (like Roo Code's <system-reminder> blocks)
	if (!CachedCodeStructureContext.IsEmpty())
	{
		SystemPrompt += TEXT("\n\n====\n\nCODE STRUCTURE (signatures only)\n\n");
		SystemPrompt += CachedCodeStructureContext;
	}

	SystemPrompt += TEXT("\n\n") + RulesSection;
	SystemPrompt += TEXT("\n\n") + ObjectiveSection;

	// Load any user-provided custom system prompt template (appended, not replaced)
	FString CustomPrompt;
	FString TemplatePath = FPaths::Combine(
		FPaths::ProjectPluginsDir(), TEXT("Autonomix"),
		TEXT("Resources"), TEXT("SystemPrompt"), TEXT("autonomix_system_prompt.txt"));
	if (FFileHelper::LoadFileToString(CustomPrompt, *TemplatePath) && !CustomPrompt.IsEmpty())
	{
		CustomPrompt = CustomPrompt.Replace(TEXT("{PROJECT_CONTEXT}"), *ProjectContext);
		SystemPrompt += TEXT("\n\n====\n\nCUSTOM INSTRUCTIONS\n\n") + CustomPrompt;
	}

	return SystemPrompt;
}

// ============================================================================
// Todo / Task Management
// ============================================================================

FString SAutonomixMainPanel::HandleUpdateTodoList(const FAutonomixToolCall& ToolCall)
{
	FString TodosMarkdown;
	if (ToolCall.InputParams.IsValid())
	{
		ToolCall.InputParams->TryGetStringField(TEXT("todos"), TodosMarkdown);
	}

	if (TodosMarkdown.IsEmpty())
	{
		return TEXT("Error: 'todos' parameter is required. Provide a markdown checklist.");
	}

	TArray<FAutonomixTodoItem> ParsedTodos = SAutonomixTodoList::ParseMarkdownChecklist(TodosMarkdown);

	if (ParsedTodos.Num() == 0)
	{
		return TEXT("Error: Could not parse any todo items from the provided checklist.");
	}

	// Update the UI widget on the game thread
	if (TodoListWidget.IsValid())
	{
		TodoListWidget->SetTodos(ParsedTodos);
	}
	if (FAutonomixConversationTabState* ActiveTab = GetActiveTabState())
	{
		ActiveTab->Todos = ParsedTodos;
	}
	SaveTabsToDisk();

	// Build a confirmation message
	int32 Completed = 0, InProgress = 0, Pending = 0;
	for (const FAutonomixTodoItem& Item : ParsedTodos)
	{
		switch (Item.Status)
		{
		case EAutonomixTodoStatus::Completed:  Completed++;  break;
		case EAutonomixTodoStatus::InProgress: InProgress++; break;
		default:                               Pending++;    break;
		}
	}

	FString Result = FString::Printf(
		TEXT("Todo list updated successfully. %d items total: %d completed, %d in progress, %d pending."),
		ParsedTodos.Num(), Completed, InProgress, Pending);

	UE_LOG(LogAutonomix, Log, TEXT("MainPanel: %s"), *Result);

	return Result;
}

// ============================================================================
// attempt_completion — Terminates the agentic loop
// ============================================================================

FString SAutonomixMainPanel::HandleAttemptCompletion(const FAutonomixToolCall& ToolCall)
{
	FString Result;
	if (ToolCall.InputParams.IsValid())
	{
		ToolCall.InputParams->TryGetStringField(TEXT("result"), Result);
	}

	if (Result.IsEmpty())
	{
		Result = TEXT("(Task completed — no result message provided)");
	}

	// Display the completion result prominently in the chat
	FAutonomixMessage CompletionMsg(EAutonomixMessageRole::System,
		FString::Printf(TEXT("✅ Task Complete\n\n%s"), *Result));
	ChatView->AddMessage(CompletionMsg);

	// Show follow-up suggestions (Roo Code's FollowUpSuggest.tsx)
	if (FollowUpBar.IsValid())
	{
		FollowUpBar->ShowSuggestionsForResult(Result);
	}

	// Record in history
	if (TaskHistory.IsValid())
	{
		FAutonomixTaskHistoryItem HistoryItem;
		const FAutonomixConversationTabState* ActiveTab = GetActiveTabState();
		if (ActiveTab)
		{
			HistoryItem.TabId = ActiveTab->TabId;
			HistoryItem.Title = ActiveTab->Title;
			HistoryItem.TotalTokenUsage = SessionTokenUsage;
			HistoryItem.TotalCostUSD = CostTracker.GetSessionTotalCost();
			HistoryItem.MessageCount = ConversationManager.IsValid()
				? ConversationManager->GetHistory().Num() : 0;
		}
		HistoryItem.LastActiveAt = FDateTime::UtcNow();
		TaskHistory->RecordTask(HistoryItem);
	}

	// CRITICAL: Stop the agentic loop immediately
	bIsProcessing = false;
	bInAgenticLoop = false;
	AgenticLoopCount = 0;
	ConsecutiveNoToolCount = 0;
	ToolCallQueue.Empty();

	// Re-enable input
	if (InputArea.IsValid())
	{
		InputArea->SetSendEnabled(true);
		InputArea->FocusInput();
	}
	if (ProgressOverlay.IsValid())
	{
		ProgressOverlay->HideProgress();
	}

	// Reset tool repetition detector for next task
	if (ToolRepetitionDetector.IsValid())
	{
		ToolRepetitionDetector->Reset();
	}

	SaveTabsToDisk();

	UE_LOG(LogAutonomix, Log, TEXT("MainPanel: Task completed via attempt_completion. Result: %s"),
		*Result.Left(200));

	// Process next queued message if one is waiting
	// Use a short delay to allow the UI to update first
	TSharedPtr<SAutonomixMainPanel> ThisWidget = SharedThis(this);
	FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateLambda([ThisWidget](float) -> bool
		{
			if (ThisWidget.IsValid())
			{
				ThisWidget->ProcessNextQueuedMessage();
			}
			return false;  // one-shot
		}),
		0.1f  // 100ms delay
	);

	// Return a brief acknowledgment so the tool_result is in the conversation history
	// (Claude will not be asked to respond again — loop is already stopped above)
	return TEXT("Task result delivered to user. Conversation ended.");
}

void SAutonomixMainPanel::ProcessNextQueuedMessage()
{
	if (PendingMessageQueue.Num() == 0 || bIsProcessing)
	{
		return;
	}

	const FString NextMessage = PendingMessageQueue[0];
	PendingMessageQueue.RemoveAt(0);

	if (!NextMessage.IsEmpty())
	{
		FAutonomixMessage QueueNotice(EAutonomixMessageRole::System,
			FString::Printf(TEXT("📬 Processing queued message (%d remaining in queue)"),
				PendingMessageQueue.Num()));
		ChatView->AddMessage(QueueNotice);

		UE_LOG(LogAutonomix, Log, TEXT("MainPanel: Processing queued message (%d remaining)"),
			PendingMessageQueue.Num());

		// Start the next task
		OnPromptSubmitted(NextMessage);
	}
}

// ============================================================================
// Phase 2: Mode Switching
// ============================================================================

FString SAutonomixMainPanel::HandleSwitchMode(const FAutonomixToolCall& ToolCall)
{
	FString ModeSlug;
	FString Reason;

	if (ToolCall.InputParams.IsValid())
	{
		ToolCall.InputParams->TryGetStringField(TEXT("mode_slug"), ModeSlug);
		ToolCall.InputParams->TryGetStringField(TEXT("reason"), Reason);
	}

	if (ModeSlug.IsEmpty())
	{
		return TEXT("Error: 'mode_slug' parameter is required. Valid values: general, blueprint, cpp_code, architect, debug, asset");
	}

	// Map mode slug to enum
	EAutonomixAgentMode NewMode = EAutonomixAgentMode::General;
	if (ModeSlug == TEXT("general"))             NewMode = EAutonomixAgentMode::General;
	else if (ModeSlug == TEXT("blueprint"))      NewMode = EAutonomixAgentMode::Blueprint;
	else if (ModeSlug == TEXT("cpp_code"))       NewMode = EAutonomixAgentMode::CppCode;
	else if (ModeSlug == TEXT("architect"))      NewMode = EAutonomixAgentMode::Architect;
	else if (ModeSlug == TEXT("debug"))          NewMode = EAutonomixAgentMode::Debug;
	else if (ModeSlug == TEXT("asset"))          NewMode = EAutonomixAgentMode::Asset;
	else if (ModeSlug == TEXT("orchestrator"))   NewMode = EAutonomixAgentMode::Orchestrator;
	else
	{
		return FString::Printf(
			TEXT("Error: Unknown mode_slug '%s'. Valid values: general, blueprint, cpp_code, architect, debug, asset, orchestrator"),
			*ModeSlug
		);
	}

	ApplyAgentMode(NewMode);

	FString Result = FString::Printf(
		TEXT("Mode switched to '%s'. %s"),
		*FAutonomixToolSchemaRegistry::GetModeDisplayName(NewMode),
		*Reason
	);

	UE_LOG(LogAutonomix, Log, TEXT("MainPanel: %s"), *Result);
	return Result;
}

void SAutonomixMainPanel::ApplyAgentMode(EAutonomixAgentMode NewMode)
{
	CurrentAgentMode = NewMode;

	// Update the active tab state
	if (FAutonomixConversationTabState* ActiveTab = GetActiveTabState())
	{
		ActiveTab->AgentMode = NewMode;
	}

	// Show mode change in chat
	FString ModeName = FAutonomixToolSchemaRegistry::GetModeDisplayName(NewMode);
	FString WhenToUse = FAutonomixToolSchemaRegistry::GetModeWhenToUse(NewMode);
	FAutonomixMessage ModeMsg(EAutonomixMessageRole::System,
		FString::Printf(TEXT("🔄 Mode switched to: %s — %s"), *ModeName, *WhenToUse));
	ChatView->AddMessage(ModeMsg);

	// Persist
	SaveTabsToDisk();

	UE_LOG(LogAutonomix, Log, TEXT("MainPanel: Agent mode changed to %s"), *ModeName);
}

FString SAutonomixMainPanel::GetModeDisplayName(EAutonomixAgentMode Mode)
{
	return FAutonomixToolSchemaRegistry::GetModeDisplayName(Mode);
}

// ============================================================================
// Phase 1: Context Window Overflow Handling
// ============================================================================

void SAutonomixMainPanel::HandleContextWindowExceeded(int32 RetryCount)
{
	// This handler is only called when using the Anthropic provider.
	FAutonomixClaudeClient* Claude = ClaudeClientPtr();
	if (!ConversationManager.IsValid() || !Claude)
	{
		return;
	}

	UE_LOG(LogAutonomix, Warning,
		TEXT("MainPanel: Context window exceeded. Applying forced reduction (retry %d/%d)."),
		RetryCount,
		FAutonomixClaudeClient::MaxContextWindowRetries
	);

	// Show info to user
	FString InfoText = FString::Printf(
		TEXT("⚠️ Context window exceeded. Removing oldest messages and retrying... (attempt %d/%d)"),
		RetryCount,
		FAutonomixClaudeClient::MaxContextWindowRetries
	);
	FAutonomixMessage InfoMsg(EAutonomixMessageRole::System, InfoText);
	ChatView->AddMessage(InfoMsg);

	// -----------------------------------------------------------------------
	// CRITICAL FIX: Use non-destructive TruncateConversation() + GetEffectiveHistory()
	// instead of raw index-based array trimming.
	//
	// The previous approach:
	//   1. Called GetEffectiveHistory() to get N messages
	//   2. Sliced the array by removing the first RemoveCount elements
	//   3. The resulting array could start with a user message containing tool_result
	//      blocks whose tool_use_id referenced an assistant message that was just removed
	//   4. Claude rejected the retry with HTTP 400:
	//      "messages.0.content.0: unexpected 'tool_use_id' found in 'tool_result' blocks"
	//
	// The correct approach (matching Roo Code's manageContext + getEffectiveApiHistory):
	//   1. TruncateConversation() tags oldest messages with TruncationParent (non-destructive)
	//   2. GetEffectiveHistory() filters out truncated messages AND orphaned tool_result blocks
	//      (the orphan filter collects all tool_use IDs from visible assistant messages,
	//       then removes tool_result blocks in user messages that reference unknown IDs)
	//   3. RetryWithTrimmedHistory() receives a clean, API-valid history
	// -----------------------------------------------------------------------

	// -----------------------------------------------------------------------
	// STEP 1: Strip base64 image data from tool_result messages.
	//
	// capture_viewport returns massive base64 strings (even at JPEG 512px,
	// it can be 30-80K chars = ~8-20K tokens). For long sessions or models
	// with smaller context windows (200K), a single viewport capture can
	// consume a huge fraction of the context. Stripping these first is far
	// more effective than removing messages, because the image data is
	// typically in RECENT messages (not in the oldest that truncation targets).
	// -----------------------------------------------------------------------
	{
		int32 ImagesStripped = 0;
		int64 CharsFreed = 0;
		TArray<FAutonomixMessage>& FullHistory = const_cast<TArray<FAutonomixMessage>&>(ConversationManager->GetHistory());
		for (FAutonomixMessage& Msg : FullHistory)
		{
			// Strip [IMAGE:base64:data:image/...;base64,...] blocks from tool results
			if (Msg.Content.Contains(TEXT("[IMAGE:base64:")))
			{
				int32 StartIdx = Msg.Content.Find(TEXT("[IMAGE:base64:"));
				int32 EndIdx = Msg.Content.Find(TEXT("]"), ESearchCase::IgnoreCase, ESearchDir::FromStart, StartIdx);
				if (StartIdx != INDEX_NONE && EndIdx != INDEX_NONE)
				{
					int32 OrigLen = Msg.Content.Len();
					FString Before = Msg.Content.Left(StartIdx);
					FString After = Msg.Content.Mid(EndIdx + 1);
					Msg.Content = Before + TEXT("[Image stripped to free context space — viewport was already analyzed]") + After;
					CharsFreed += (OrigLen - Msg.Content.Len());
					ImagesStripped++;
				}
			}
		}

		if (ImagesStripped > 0)
		{
			int32 TokensFreed = CharsFreed / 4; // ~4 chars per token
			UE_LOG(LogAutonomix, Log,
				TEXT("MainPanel: Stripped %d base64 images from history, freeing ~%lld chars (~%d tokens)."),
				ImagesStripped, CharsFreed, TokensFreed);
		}
	}

	// -----------------------------------------------------------------------
	// STEP 2: Apply non-destructive sliding window truncation on the full history.
	// Remove ~50% of visible messages (matching Roo Code's default TruncationFrac = 0.5).
	// This is more aggressive than the previous 25% to ensure we actually fit in the window.
	// -----------------------------------------------------------------------
	const int32 VisibleBefore = ConversationManager->GetEffectiveHistory().Num();
	const int32 MessagesRemoved = ConversationManager->TruncateConversation(0.5f);

	// Even if MessagesRemoved is 0, the image stripping above may have freed enough.
	// Always attempt the retry.
	{
		// GetEffectiveHistory() now returns the post-truncation view, AND filters orphaned
		// tool_result blocks whose tool_use_id was in the removed assistant messages.
		TArray<FAutonomixMessage> TrimmedHistory = ConversationManager->GetEffectiveHistory();

		UE_LOG(LogAutonomix, Log,
			TEXT("MainPanel: Context overflow — truncation removed %d messages. "
			     "History: %d → %d effective messages (retry %d/%d)."),
			MessagesRemoved, VisibleBefore, TrimmedHistory.Num(),
			RetryCount, FAutonomixClaudeClient::MaxContextWindowRetries
		);

		if (TrimmedHistory.Num() > 0)
		{
			// Persist the truncation state so it survives restarts
			SaveTabsToDisk();

			// Retry with the clean, orphan-free history
			Claude->RetryWithTrimmedHistory(TrimmedHistory);
		}
		else
		{
			// History is completely empty after all reduction
			UE_LOG(LogAutonomix, Error,
				TEXT("MainPanel: Cannot reduce history further (effective count=0). Context window error."));

			FAutonomixMessage ErrorMsg(EAutonomixMessageRole::System,
				TEXT("❌ Context window is full and cannot be reduced further. Please start a new conversation."));
			ChatView->AddMessage(ErrorMsg);

			bIsProcessing = false;
			bInAgenticLoop = false;
			if (InputArea.IsValid()) { InputArea->SetSendEnabled(true); InputArea->FocusInput(); }
			if (ProgressOverlay.IsValid()) ProgressOverlay->HideProgress();
		}
	}
}

// ============================================================================
// Phase 2: Per-Message Environment Details
// ============================================================================

FString SAutonomixMainPanel::BuildEnvironmentDetailsString() const
{
	if (!EnvironmentDetails.IsValid())
	{
		return FString();
	}

	const FAutonomixConversationTabState* ActiveTab = GetActiveTabState();
	TArray<FAutonomixTodoItem> CurrentTodos;
	FString TabTitle;
	if (ActiveTab)
	{
		CurrentTodos = ActiveTab->Todos;
		TabTitle = ActiveTab->Title;
	}

	return EnvironmentDetails->Build(
		ContextUsagePercent,
		CurrentTodos,
		TabTitle,
		AgenticLoopCount
	);
}

// ============================================================================
// Condense Context (Manual Trigger)
// ============================================================================

FReply SAutonomixMainPanel::OnCondenseContextClicked()
{
	if (bIsProcessing)
	{
		FAutonomixMessage BusyMsg(EAutonomixMessageRole::System,
			TEXT("⏳ Cannot condense while a request is in progress."));
		ChatView->AddMessage(BusyMsg);
		return FReply::Handled();
	}

	if (!ContextManager.IsValid() || !LLMClient.IsValid())
	{
		FAutonomixMessage ErrorMsg(EAutonomixMessageRole::Error,
			TEXT("Context manager not initialized. Cannot condense."));
		ChatView->AddMessage(ErrorMsg);
		return FReply::Handled();
	}

	// Show progress
	FAutonomixMessage StartMsg(EAutonomixMessageRole::System,
		TEXT("📦 Condensing context... This sends the conversation to Claude for summarization."));
	ChatView->AddMessage(StartMsg);

	if (ProgressOverlay.IsValid())
	{
		ProgressOverlay->ShowProgress(TEXT("Condensing context..."));
	}

	// Create the condenser and trigger summarization
	TSharedPtr<FAutonomixContextCondenser> Condenser = MakeShared<FAutonomixContextCondenser>(
		LLMClient, ConversationManager);

	FString SystemPrompt = BuildSystemPrompt();

	// Capture for async lambda
	TSharedPtr<SAutonomixMainPanel> ThisWidget = SharedThis(this);
	TSharedPtr<FAutonomixContextCondenser> CondenserRef = Condenser; // prevent GC

	// Pass folded code structure context so it's preserved in the condensed summary
	// (Roo Code's foldedFileContext.ts pattern — code signatures survive condensation)
	const FString FoldedCtx = CachedCodeStructureContext;
	Condenser->SummarizeConversation(SystemPrompt,
		[ThisWidget, CondenserRef](const FAutonomixCondenseResult& Result)
		{
			if (!ThisWidget.IsValid()) return;

			if (ThisWidget->ProgressOverlay.IsValid())
			{
				ThisWidget->ProgressOverlay->HideProgress();
			}

			if (Result.bSuccess)
			{
				FAutonomixMessage SuccessMsg(EAutonomixMessageRole::System,
					FString::Printf(TEXT("📦 Context condensed successfully! New context: ~%d tokens. Summary:\n%s"),
						Result.NewContextTokens, *Result.Summary.Left(300)));
				ThisWidget->ChatView->AddMessage(SuccessMsg);

				// Update context usage
				const int32 WindowTokens = ThisWidget->GetContextWindowTokens();
				if (WindowTokens > 0)
				{
					ThisWidget->ContextUsagePercent =
						(float(Result.NewContextTokens) / float(WindowTokens)) * 100.0f;
				}

				UE_LOG(LogAutonomix, Log,
					TEXT("MainPanel: Manual condense succeeded. ~%d tokens remaining."),
					Result.NewContextTokens);
				ThisWidget->SaveTabsToDisk();
			}
			else
			{
				FAutonomixMessage ErrorMsg(EAutonomixMessageRole::Error,
					FString::Printf(TEXT("❌ Context condensation failed: %s"), *Result.ErrorMessage));
				ThisWidget->ChatView->AddMessage(ErrorMsg);

				UE_LOG(LogAutonomix, Warning,
					TEXT("MainPanel: Manual condense failed: %s"), *Result.ErrorMessage);
			}
		});

	return FReply::Handled();
}

// ============================================================================
// Phase 3: Task Delegation (new_task tool)
// ============================================================================

FString SAutonomixMainPanel::HandleNewTask(const FAutonomixToolCall& ToolCall)
{
	if (!TaskDelegation.IsValid())
	{
		return TEXT("Error: Task delegation system not initialized.");
	}

	FString ModeSlug;
	FString Message;
	FString InitialTodos;

	if (ToolCall.InputParams.IsValid())
	{
		ToolCall.InputParams->TryGetStringField(TEXT("mode"), ModeSlug);
		ToolCall.InputParams->TryGetStringField(TEXT("message"), Message);
		ToolCall.InputParams->TryGetStringField(TEXT("todos"), InitialTodos);
	}

	if (Message.IsEmpty())
	{
		return TEXT("Error: 'message' parameter is required for new_task.");
	}

	// Map mode slug to enum
	EAutonomixAgentMode ChildMode = EAutonomixAgentMode::General;
	if (!ModeSlug.IsEmpty())
	{
		if (ModeSlug == TEXT("blueprint"))      ChildMode = EAutonomixAgentMode::Blueprint;
		else if (ModeSlug == TEXT("cpp_code"))  ChildMode = EAutonomixAgentMode::CppCode;
		else if (ModeSlug == TEXT("architect")) ChildMode = EAutonomixAgentMode::Architect;
		else if (ModeSlug == TEXT("debug"))     ChildMode = EAutonomixAgentMode::Debug;
		else if (ModeSlug == TEXT("asset"))     ChildMode = EAutonomixAgentMode::Asset;
	}

	const FAutonomixConversationTabState* ActiveTab = GetActiveTabState();
	if (!ActiveTab)
	{
		return TEXT("Error: No active tab to delegate from.");
	}

	// Check nesting depth limit
	if (TaskDelegation->GetNestingDepth(ActiveTab->TabId) >= FAutonomixTaskDelegation::MaxNestingDepth)
	{
		return FString::Printf(
			TEXT("Error: Maximum task nesting depth (%d) reached. Cannot create further sub-tasks."),
			FAutonomixTaskDelegation::MaxNestingDepth
		);
	}

	// Create sub-task record
	FAutonomixSubTask SubTask;
	if (!TaskDelegation->CreateSubTask(ActiveTab->TabId, ChildMode, Message, InitialTodos, SubTask))
	{
		return TEXT("Error: Failed to create sub-task. The active tab may already have a pending child task.");
	}

	// Create a new tab for the child task
	FString ModeName = FAutonomixToolSchemaRegistry::GetModeDisplayName(ChildMode);
	const FString ChildTitle = FString::Printf(TEXT("Sub-task [%s]: %s"), *ModeName, *Message.Left(40));
	CreateNewTab(ChildTitle, false);  // Don't switch focus yet

	// Get the new tab's ID and update the sub-task
	if (ConversationTabs.Num() > 0)
	{
		FAutonomixConversationTabState& ChildTab = ConversationTabs.Last();
		ChildTab.AgentMode = ChildMode;

		// Link child tab to the sub-task via SetChildTabId
		TaskDelegation->SetChildTabId(SubTask.SubTaskId, ChildTab.TabId);

		// Show notification
		FAutonomixMessage InfoMsg(EAutonomixMessageRole::System,
			FString::Printf(TEXT("🔀 Delegated to sub-task [%s]: %s"), *ModeName, *Message.Left(80)));
		ChatView->AddMessage(InfoMsg);

		SaveTabsToDisk();

		return FString::Printf(
			TEXT("Sub-task created successfully in '%s' mode.\n"
			     "Task ID: %s\n"
			     "The sub-task is running in a separate conversation tab.\n"
			     "Wait for the sub-task to complete and report back."),
			*ModeName,
			*SubTask.SubTaskId
		);
	}

	return TEXT("Error: Failed to create child tab for sub-task.");
}

void SAutonomixMainPanel::OnSubTaskCompleted(
	const FString& SubTaskId,
	bool bSuccess,
	const FString& ResultMessage
)
{
	// Find which parent tab was waiting for this sub-task
	const FAutonomixSubTask* SubTask = TaskDelegation.IsValid()
		? TaskDelegation->GetSubTask(SubTaskId)
		: nullptr;

	if (!SubTask)
	{
		UE_LOG(LogAutonomix, Warning, TEXT("MainPanel: OnSubTaskCompleted — sub-task %s not found"), *SubTaskId);
		return;
	}

	// Find the parent tab
	for (int32 i = 0; i < ConversationTabs.Num(); i++)
	{
		if (ConversationTabs[i].TabId == SubTask->ParentTabId)
		{
			// Show completion notification on the parent tab's chat view
			// (We need to switch to that tab first, or handle it from wherever we are)
			const FString StatusIcon = bSuccess ? TEXT("✅") : TEXT("❌");
			const FString NotifText = FString::Printf(
				TEXT("%s Sub-task completed (%s)\n\nResult: %s"),
				*StatusIcon,
				bSuccess ? TEXT("success") : TEXT("failure"),
				*ResultMessage.Left(500)
			);

			FAutonomixMessage CompletionMsg(EAutonomixMessageRole::System, NotifText);
			ChatView->AddMessage(CompletionMsg);

			// If we are on the parent tab's context, inject the result into the conversation
			// and continue the parent's agentic loop
			if (ActiveTabIndex == i && ConversationManager.IsValid())
			{
				FString ToolResultContent = FString::Printf(
					TEXT("Sub-task '%s' completed.\nSuccess: %s\nResult: %s"),
					*SubTaskId,
					bSuccess ? TEXT("true") : TEXT("false"),
					*ResultMessage
				);

				// Inject as a system message to inform the parent AI
				ConversationManager->AddUserMessage(ToolResultContent);
				ContinueAgenticLoop();
			}

			break;
		}
	}
}

FString SAutonomixMainPanel::ResolveReferencesInInput(FString& InOutUserInput) const
{
	if (!ReferenceParser.IsValid())
	{
		return FString();
	}

	FAutonomixParseReferencesResult Result = ReferenceParser->ParseAndResolve(InOutUserInput);
	InOutUserInput = Result.ProcessedText;

	FString ContentBlock;
	for (const FAutonomixResolvedReference& Ref : Result.ResolvedReferences)
	{
		if (Ref.bSuccess && !Ref.Content.IsEmpty())
		{
			ContentBlock += Ref.Content + TEXT("\n\n");
		}
	}
	return ContentBlock;
}

// ============================================================================
// Second Pass: Code Structure Context (tree-sitter equivalent)
// ============================================================================

void SAutonomixMainPanel::UpdateCodeStructureContext()
{
	if (!CodeStructureParser.IsValid() || !FileContextTracker.IsValid())
	{
		return;
	}

	// Get all files the AI has read during this session (via GetTrackedPaths)
	const TArray<FString> TrackedRelPaths = FileContextTracker->GetTrackedPaths();
	if (TrackedRelPaths.Num() == 0)
	{
		CachedCodeStructureContext.Empty();
		return;
	}

	// Collect (AbsPath, RelPath) pairs for tracked files that are C++/CS
	const FString ProjectRoot = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir());
	TArray<TPair<FString, FString>> FilePairs;

	const int32 MaxFiles = 30;  // Limit to prevent excessive token usage
	int32 Count = 0;
	for (const FString& RelPath : TrackedRelPaths)
	{
		if (Count >= MaxFiles) break;
		if (!FAutonomixCodeStructureParser::IsSupportedFileType(RelPath)) continue;
		if (IgnoreController.IsValid() && IgnoreController->IsPathIgnored(RelPath)) continue;

		const FString AbsPath = FPaths::Combine(ProjectRoot, RelPath);
		if (FPaths::FileExists(AbsPath))
		{
			FilePairs.Add({ AbsPath, RelPath });
			Count++;
		}
	}

	if (FilePairs.Num() == 0)
	{
		CachedCodeStructureContext.Empty();
		return;
	}

	// Generate folded context (max 30KB to leave room for conversation)
	CachedCodeStructureContext = CodeStructureParser->GenerateFoldedContext(FilePairs, 30000);

	UE_LOG(LogAutonomix, Log,
		TEXT("MainPanel: Generated code structure context for %d files (%d chars)"),
		FilePairs.Num(), CachedCodeStructureContext.Len());
}

// ============================================================================
// New UI Handlers (Roo-Code parity widgets)
// ============================================================================

FReply SAutonomixMainPanel::OnCheckpointsPanelToggled()
{
	bShowCheckpointPanel = !bShowCheckpointPanel;
	if (bShowCheckpointPanel && CheckpointPanel.IsValid() && CheckpointManager.IsValid())
	{
		CheckpointPanel->RefreshCheckpoints(CheckpointManager->GetAllCheckpoints());
	}
	return FReply::Handled();
}

FReply SAutonomixMainPanel::OnHistoryPanelToggled()
{
	bShowHistoryPanel = !bShowHistoryPanel;
	if (bShowHistoryPanel && HistoryPanel.IsValid() && TaskHistory.IsValid())
	{
		HistoryPanel->RefreshHistory(TaskHistory->GetHistory());
	}
	return FReply::Handled();
}

FReply SAutonomixMainPanel::OnFileChangesPanelToggled()
{
	bShowFileChangesPanel = !bShowFileChangesPanel;
	return FReply::Handled();
}

void SAutonomixMainPanel::OnFollowUpSelected(const FString& SuggestionText)
{
	if (FollowUpBar.IsValid()) FollowUpBar->Hide();
	OnPromptSubmitted(SuggestionText);
}

void SAutonomixMainPanel::OnRestoreCheckpoint(const FString& CommitHash)
{
	if (!CheckpointManager.IsValid()) return;
	const FText Title = FText::FromString(TEXT("Autonomix — Restore Checkpoint"));
	const FText Msg = FText::FromString(FString::Printf(
		TEXT("Restore project files to checkpoint %s?\n\nThis will overwrite current file changes. Conversation history is NOT affected."),
		*CommitHash.Left(8)));
	if (FMessageDialog::Open(EAppMsgType::YesNo, Msg, Title) != EAppReturnType::Yes) return;
	TArray<FString> RestoredFiles;
	if (CheckpointManager->RestoreToCheckpoint(CommitHash, RestoredFiles))
	{
		ChatView->AddMessage(FAutonomixMessage(EAutonomixMessageRole::System,
			FString::Printf(TEXT("✅ Restored to checkpoint %s. %d files restored."), *CommitHash.Left(8), RestoredFiles.Num())));
	}
}

void SAutonomixMainPanel::OnViewCheckpointDiff(const FString& CommitHash)
{
	if (!CheckpointManager.IsValid()) return;
	FAutonomixCheckpointDiff Diff = CheckpointManager->GetDiff(
		CheckpointManager->GetInitialCommitHash(), CommitHash, TEXT("checkpoint"));
	if (Diff.bSuccess)
	{
		// Show diff summary in chat (full diff viewer integration deferred to layout)
		FString Summary = FString::Printf(
			TEXT("📊 Checkpoint diff to %s:\n%d files changed\n\n```diff\n%s\n```"),
			*CommitHash.Left(8),
			Diff.ChangedFiles.Num(),
			*Diff.DiffText.Left(2000)
		);
		ChatView->AddMessage(FAutonomixMessage(EAutonomixMessageRole::System, Summary));
	}
	else
	{
		ChatView->AddMessage(FAutonomixMessage(EAutonomixMessageRole::Error,
			FString::Printf(TEXT("Failed to get diff: %s"), *Diff.ErrorMessage)));
	}
}

void SAutonomixMainPanel::OnLoadHistoryTask(const FString& TabId)
{
	if (!TaskHistory.IsValid()) return;
	const FAutonomixTaskHistoryItem* Item = TaskHistory->GetTask(TabId);
	if (!Item) return;
	CreateNewTab(Item->Title);
	ChatView->AddMessage(FAutonomixMessage(EAutonomixMessageRole::System,
		FString::Printf(TEXT("📋 Loaded task: %s"), *Item->Title)));
}

void SAutonomixMainPanel::OnDeleteHistoryTask(const FString& TabId)
{
	if (!TaskHistory.IsValid()) return;
	TaskHistory->RemoveTask(TabId);
	if (HistoryPanel.IsValid() && TaskHistory.IsValid())
	{
		HistoryPanel->RefreshHistory(TaskHistory->GetHistory());
	}
}

void SAutonomixMainPanel::UpdateLiveUI()
{
	if (ContextBar.IsValid())
	{
		const int32 MaxTok = GetContextWindowTokens();
		const float Fraction = (MaxTok > 0)
			? FMath::Clamp(ContextUsagePercent / 100.0f, 0.0f, 1.0f)
			: 0.0f;
		ContextBar->UpdateStats(
			Fraction,
			SessionTokenUsage.InputTokens,
			SessionTokenUsage.OutputTokens,
			MaxTok,
			LastRequestCost
		);
		ContextBar->SetMode(FAutonomixToolSchemaRegistry::GetModeDisplayName(CurrentAgentMode));
	}

	// Update input area queued message badge
	if (InputArea.IsValid())
	{
		InputArea->SetQueuedMessageCount(PendingMessageQueue.Num());
	}

	// Refresh checkpoint panel if visible
	if (bShowCheckpointPanel && CheckpointPanel.IsValid() && CheckpointManager.IsValid())
	{
		CheckpointPanel->RefreshCheckpoints(CheckpointManager->GetAllCheckpoints());
	}
}

// ============================================================================
// Phase 4: Skill Tool Handler
// ============================================================================

FString SAutonomixMainPanel::HandleSkillTool(const FAutonomixToolCall& ToolCall)
{
	if (!SkillsManager.IsValid())
	{
		return TEXT("Error: Skills system not initialized.");
	}

	FString SkillName;
	FString Args;

	if (ToolCall.InputParams.IsValid())
	{
		ToolCall.InputParams->TryGetStringField(TEXT("skill"), SkillName);
		ToolCall.InputParams->TryGetStringField(TEXT("args"), Args);
	}

	if (SkillName.IsEmpty())
	{
		return TEXT("Error: 'skill' parameter is required.");
	}

	return SkillsManager->HandleSkillToolCall(SkillName, Args);
}
