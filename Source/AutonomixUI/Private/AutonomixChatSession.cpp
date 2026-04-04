// Copyright Autonomix. All Rights Reserved.

#include "AutonomixChatSession.h"
#include "AutonomixConversationManager.h"
#include "AutonomixActionRouter.h"
#include "AutonomixExecutionJournal.h"
#include "AutonomixToolRepetitionDetector.h"
#include "AutonomixFileContextTracker.h"
#include "AutonomixContextManager.h"
#include "AutonomixToolSchemaRegistry.h"
#include "AutonomixCheckpointManager.h"
#include "AutonomixSettings.h"
#include "AutonomixCoreModule.h"
#include "AutonomixAutoApprovalHandler.h"
#include "Misc/MessageDialog.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"


FAutonomixChatSession::FAutonomixChatSession()
{
}

FAutonomixChatSession::~FAutonomixChatSession()
{
}

void FAutonomixChatSession::Initialize(TSharedPtr<IAutonomixLLMClient> InLLMClient,
									   TSharedPtr<FAutonomixConversationManager> InConvManager,
									   TSharedPtr<FAutonomixActionRouter> InActionRouter,
									   TSharedPtr<FAutonomixExecutionJournal> InExecutionJournal,
									   TSharedPtr<FAutonomixToolRepetitionDetector> InToolRepetitionDetector,
									   TSharedPtr<FAutonomixFileContextTracker> InFileContextTracker,
									   TSharedPtr<FAutonomixContextManager> InContextManager,
									   TSharedPtr<FAutonomixToolSchemaRegistry> InToolSchemaRegistry,
									   TSharedPtr<FAutonomixCheckpointManager> InCheckpointManager)
{
	LLMClient = InLLMClient;
	ConversationManager = InConvManager;
	ActionRouter = InActionRouter;
	ExecutionJournal = InExecutionJournal;
	ToolRepetitionDetector = InToolRepetitionDetector;
	FileContextTracker = InFileContextTracker;
	ContextManager = InContextManager;
	ToolSchemaRegistry = InToolSchemaRegistry;
	CheckpointManager = InCheckpointManager;
}

void FAutonomixChatSession::ProcessToolCallQueue()
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
			// Show prompt to user
			FAutonomixActionPlan DummyPlan;
			OnToolRequiresApproval.Broadcast(DummyPlan);
			return; // Wait for user approval before continuing
		}
	}

	// Phase 5.2: Create a checkpoint snapshot before executing this tool batch
	if (Settings && Settings->bEnableAutoBackup && CheckpointManager.IsValid())
	{
		FAutonomixCheckpoint CP;
		const FString Description = FString::Printf(TEXT("Before tool batch %d"), AgenticLoopCount);
		CheckpointManager->SaveCheckpoint(Description, AgenticLoopCount, CP);
		// Should also do FAutonomixBackupManager things here if implemented
	}

	bInAgenticLoop = true;
	bStopRequested = false;
	AgenticLoopCount++;

	// Record this batch in auto-approval tracking
	AutoApprovalHandler.RecordBatch(LastRequestCost);

	UE_LOG(LogAutonomix, Log, TEXT("MainPanel: Processing %d tool calls (loop %d)"),
		ToolCallQueue.Num(), AgenticLoopCount);

	TArray<FAutonomixToolCall> ActiveToolCalls = MoveTemp(ToolCallQueue);
	ToolCallQueue.Empty();
	for (const FAutonomixToolCall& ToolCall : ActiveToolCalls)
	{
		// Check if stop was requested between tool calls
		if (bStopRequested)
		{
			UE_LOG(LogAutonomix, Log, TEXT("ChatSession: Stop requested — aborting remaining %d tool call(s)."),
				ActiveToolCalls.Num());
			break;
		}

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
					OnMessageAdded.Broadcast(StopMsg);
					// Notify stop
					OnAgentFinished.Broadcast(TEXT("Task stopped: AI repetition loop detected."));
					return;
				}
				// User chose to allow one more try — continue
			}
		}

		bool bIsAttemptCompletion = (ToolCall.ToolName == TEXT("attempt_completion"));

		FAutonomixMessage ToolMsg;

		if (!bIsAttemptCompletion)
		{
			// Add a collapsible "Executing" system message; body will be filled with the result below
			FString ExecutingParamsStr;
			if (ToolCall.InputParams.IsValid())
			{
				TSharedRef<TJsonWriter<>> ParamsWriter = TJsonWriterFactory<>::Create(&ExecutingParamsStr);
				FJsonSerializer::Serialize(ToolCall.InputParams.ToSharedRef(), ParamsWriter);
			}
			ToolMsg = FAutonomixMessage(EAutonomixMessageRole::Assistant,
				FString::Printf(TEXT("🔧 Executing: %s\n%s\n\n"), *ToolCall.ToolName, *ExecutingParamsStr));
			ToolMsg.bIsCollapsible = true;
			OnMessageAdded.Broadcast(ToolMsg);
		}

		bool bIsError = false;
		FString ResultContent = ExecuteToolCall(ToolCall, bIsError);

		FAutonomixMessage& ToolResultMsg = ConversationManager->AddToolResultMessage(
			ToolCall.ToolUseId, ResultContent, bIsError);

		if (bIsError)
		{
			ToolResultMsg.ToolName = TEXT("error");
		}

		if (!bIsAttemptCompletion)
		{
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
			OnMessageUpdated.Broadcast(ToolMsg.MessageId, ResultToAppend);
		}
	}

	ActiveToolCalls.Empty();
	OnSaveTabsToDisk.ExecuteIfBound();

	if (!bInAgenticLoop || bStopRequested)
	{
		UE_LOG(LogAutonomix, Log, TEXT("MainPanel: Agentic loop was terminated (%s). Not continuing."),
			bStopRequested ? TEXT("stop requested") : TEXT("attempt_completion"));
		bStopRequested = false;
		bInAgenticLoop = false;
		bIsProcessing = false;
		OnStatusUpdated.Broadcast(TEXT(""));
		return;
	}

	ContinueAgenticLoop();
}

void FAutonomixChatSession::StopAgenticLoop()
{
	UE_LOG(LogAutonomix, Log, TEXT("ChatSession: StopAgenticLoop() called. bInAgenticLoop=%d, bIsProcessing=%d"),
		bInAgenticLoop, bIsProcessing);
	
	bStopRequested = true;
	bInAgenticLoop = false;
	bIsProcessing = false;
	AgenticLoopCount = 0;
	ConsecutiveNoToolCount = 0;
	ToolCallQueue.Empty();

	OnStatusUpdated.Broadcast(TEXT(""));
	OnAgentFinished.Broadcast(TEXT("Stopped by user."));
}

void FAutonomixChatSession::ContinueAgenticLoop()
{
	// Check stop flag before making a new API call
	if (bStopRequested)
	{
		UE_LOG(LogAutonomix, Log, TEXT("ChatSession: ContinueAgenticLoop() aborted — stop was requested."));
		bStopRequested = false;
		bInAgenticLoop = false;
		bIsProcessing = false;
		OnStatusUpdated.Broadcast(TEXT(""));
		return;
	}

	const UAutonomixDeveloperSettings* Settings = UAutonomixDeveloperSettings::Get();
	if (!Settings) return;

	FAutonomixMessage StreamingMsg(EAutonomixMessageRole::Assistant, TEXT(""));
	StreamingMsg.bIsStreaming = true;
	CurrentStreamingMessageId = StreamingMsg.MessageId;
	OnMessageAdded.Broadcast(StreamingMsg);

	// Phase 2: Use mode-filtered schemas (local provider: essential set only)
	const UAutonomixDeveloperSettings* LoopSettings = UAutonomixDeveloperSettings::Get();
	const bool bIsLocalLoop = LoopSettings &&
		(LoopSettings->ActiveProvider == EAutonomixProvider::Ollama ||
			LoopSettings->ActiveProvider == EAutonomixProvider::LMStudio);

	TArray<TSharedPtr<FJsonObject>> ToolSchemas;
	if (ToolSchemaRegistry.IsValid())
	{
		// Phase 3: Two-tier tool loading — Tier 1 for cloud, essential for local
		ToolSchemas = bIsLocalLoop
			? ToolSchemaRegistry->GetEssentialSchemas()
			: ToolSchemaRegistry->GetTier1Schemas();
	}

	// Use GetEffectiveHistory() -- respects condense/truncation tags
	TArray<FAutonomixMessage> EffectiveHistory = ConversationManager->GetEffectiveHistory();

	// Phase 2: Build per-message environment details and inject into last message
	// This appends fresh editor state (open files, selected actors, errors, etc.)
	// to each API call without growing the static system prompt.
	FString EnvDetails;
	if (OnGetEnvironmentDetailsString.IsBound())
	{
		EnvDetails = OnGetEnvironmentDetailsString.Execute();
	}
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
	FString SystemPrompt;
	if (OnGetSystemPromptString.IsBound())
	{
		SystemPrompt = OnGetSystemPromptString.Execute();
	}

	LLMClient->SendMessage(
		EffectiveHistory,
		SystemPrompt,
		ToolSchemas
	);

	UE_LOG(LogAutonomix, Log, TEXT("MainPanel: Agentic loop iteration %d -- re-sending conversation to Claude (%d effective messages, %d schemas)."),
		AgenticLoopCount,
		EffectiveHistory.Num(),
		ToolSchemas.Num());
}

FString FAutonomixChatSession::ExecuteToolCall(const FAutonomixToolCall& ToolCall, bool& bOutIsError)
{
	bOutIsError = false;
	FDateTime StartTime = FDateTime::UtcNow();

	// Meta tools are temporarily executed locally without UI components hooks
	// or through the ActionRouter directly. Since we don't have the widget ptrs anymore,
	// they should be implemented inside FAutonomixChatSession or registered to ActionRouter

	// ---- Meta-tool: update_todo_list (handled locally, not routed to action executors) ----
	if (ToolCall.ToolName == TEXT("update_todo_list"))
	{
		if (OnHandleUpdateTodoList.IsBound())
		{
			return OnHandleUpdateTodoList.Execute(ToolCall);
		}
		return TEXT("");
	}

	if (ToolCall.ToolName == TEXT("attempt_completion"))
	{
		bInAgenticLoop = false; // Stop the loop
		OnAgentFinished.Broadcast(TEXT("Task completed."));

		if (OnHandleAttemptCompletion.IsBound())
		{
			return OnHandleAttemptCompletion.Execute(ToolCall);
		}

		return TEXT("");
	}

	// ---- Phase 2 Meta-tool: switch_mode ----
	if (ToolCall.ToolName == TEXT("switch_mode"))
	{
		if (OnHandleSwitchMode.IsBound())
		{
			return OnHandleSwitchMode.Execute(ToolCall);
		}
		return TEXT("");
	}

	// ---- Phase 3 Meta-tool: new_task (task delegation) ----
	if (ToolCall.ToolName == TEXT("new_task"))
	{
		return TEXT("");
	}

	// ---- Phase 4 Meta-tool: skill ----
	if (ToolCall.ToolName == TEXT("skill"))
	{
		return TEXT("");
	}

	// ---- Discovery Meta-tools: get_tool_info / list_tools_in_category (Phase 3 token optimization) ----
	if (ToolCall.ToolName == TEXT("get_tool_info"))
	{
		if (ToolSchemaRegistry.IsValid() && ToolCall.InputParams.IsValid())
		{
			FString RequestedTool;
			ToolCall.InputParams->TryGetStringField(TEXT("tool_name"), RequestedTool);
			if (RequestedTool.IsEmpty())
			{
				bOutIsError = true;
				return TEXT("Error: 'tool_name' parameter is required. Example: get_tool_info({\"tool_name\": \"create_material\"})");
			}
			return ToolSchemaRegistry->GetToolInfoString(RequestedTool);
		}
		return TEXT("Error: ToolSchemaRegistry not available.");
	}

	if (ToolCall.ToolName == TEXT("list_tools_in_category"))
	{
		if (ToolSchemaRegistry.IsValid() && ToolCall.InputParams.IsValid())
		{
			FString Category;
			ToolCall.InputParams->TryGetStringField(TEXT("category"), Category);
			if (Category.IsEmpty())
			{
				bOutIsError = true;
				return TEXT("Error: 'category' parameter is required. Example: list_tools_in_category({\"category\": \"material\"})");
			}
			return ToolSchemaRegistry->ListToolsInCategoryString(Category);
		}
		return TEXT("Error: ToolSchemaRegistry not available.");
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

void FAutonomixChatSession::OnStreamingText(const FGuid& MessageId, const FString& DeltaText)
{
	OnMessageUpdated.Broadcast(MessageId, DeltaText);
}

void FAutonomixChatSession::OnToolCallReceived(const FAutonomixToolCall& ToolCall)
{
	ToolCallQueue.Add(ToolCall);
}

void FAutonomixChatSession::OnMessageComplete(const FAutonomixMessage& Message)
{
	if (ConversationManager.IsValid())
	{
		ConversationManager->AddAssistantMessageFull(Message);
	}
}

void FAutonomixChatSession::OnRequestStarted()
{
	bIsProcessing = true;
	FString StatusText = bInAgenticLoop
		? FString::Printf(TEXT("Executing tools... (iteration %d)"), AgenticLoopCount)
		: TEXT("Thinking...");
	OnStatusUpdated.Broadcast(StatusText);
	OnRequestStartedDelegate.Broadcast();
}

void FAutonomixChatSession::OnRequestCompleted(bool bSuccess)
{
	OnRequestCompletedDelegate.Broadcast(bSuccess);
	if (!bSuccess)
	{
		bIsProcessing = false;
		bInAgenticLoop = false;

		OnAgentFinished.Broadcast(TEXT("API Request failed."));
		OnStatusUpdated.Broadcast(TEXT("")); // Hide progress
		return;
	}

	// ---- Context Management: Run after each successful API response ----
	// This checks if we need to condense or truncate before processing tool calls.
	// We do this asynchronously and continue when done.
	if (ContextManager.IsValid() && !ContextManager->IsManaging())
	{
		FString SystemPrompt;
		if (OnGetSystemPromptString.IsBound())
		{
			SystemPrompt = OnGetSystemPromptString.Execute();
		}

		// Capture for async lambda
		TSharedPtr<FAutonomixChatSession> ThisSession = SharedThis(this);

		ContextManager->ManageContext(SystemPrompt, LastResponseTokenUsage,
			[ThisSession](const FAutonomixContextManagementResult& CtxResult)
			{
				if (!ThisSession.IsValid()) return;

				if (CtxResult.bDidCondense)
				{
					FAutonomixMessage CtxMsg(EAutonomixMessageRole::System,
						FString::Printf(TEXT("📦 Context condensed (was %.0f%% full). Summary created."),
							CtxResult.ContextPercent));
					ThisSession->OnMessageAdded.Broadcast(CtxMsg);

					UE_LOG(LogAutonomix, Log,
						TEXT("MainPanel: Context condensed. Was %.0f%%, now ~%d tokens."),
						CtxResult.ContextPercent, CtxResult.NewContextTokens);
				}
				else if (CtxResult.bDidTruncate)
				{
					FAutonomixMessage CtxMsg(EAutonomixMessageRole::System,
						FString::Printf(TEXT("✂ Context truncated: %d old messages hidden (was %.0f%% full)."),
							CtxResult.MessagesRemoved, CtxResult.ContextPercent));
					ThisSession->OnMessageAdded.Broadcast(CtxMsg);

					UE_LOG(LogAutonomix, Log,
						TEXT("MainPanel: Context truncated. Removed %d messages. Was %.0f%% full."),
						CtxResult.MessagesRemoved, CtxResult.ContextPercent);
				}

				// UI update logic has been moved out of this class
				ThisSession->OnSaveTabsToDisk.ExecuteIfBound();

				// Tell UI to regenerate context/UI
				ThisSession->OnSessionCompletedContextManagement.Broadcast();

				// Now continue with tool call processing
				ThisSession->OnRequestCompletedPostContextManagement();
			});
		return; // Wait for context management to complete
	}

	// No context manager -- proceed directly
	OnRequestCompletedPostContextManagement();
}

void FAutonomixChatSession::OnRequestCompletedPostContextManagement()
{
	// AGENTIC LOOP: If tool calls were received, check approval flow
	if (ToolCallQueue.Num() > 0)
	{
		const UAutonomixDeveloperSettings* Settings = UAutonomixDeveloperSettings::Get();

		// Build a quick action plan from tool calls for display and risk evaluation
		FAutonomixActionPlan Plan;
		Plan.Summary = FString::Printf(TEXT("%d tool(s) pending execution"), ToolCallQueue.Num());

		bool bAllReadOnly = true;
		static const TArray<FString> ReadOnlyToolPrefixes = {
			TEXT("get_"), TEXT("read_"), TEXT("list_"), TEXT("search_"),
			TEXT("find_"), TEXT("query_"), TEXT("show_"), TEXT("describe_")
		};

		for (FAutonomixToolCall& TC : ToolCallQueue)
		{
			if (ActionRouter.IsValid())
			{
				TSharedPtr<IAutonomixActionExecutor> Executor = ActionRouter->FindExecutorForTool(TC.ToolName);
				if (Executor.IsValid())
				{
					TC.Category = Executor->GetCategory();
				}
			}

			FAutonomixAction Action;
			Action.Description = FString::Printf(TEXT("🔧 %s"), *TC.ToolName);
			Action.Category = TC.Category;

			// Assign risk based on category
			switch (TC.Category)
			{
			case EAutonomixActionCategory::Cpp:
			case EAutonomixActionCategory::Build:
			case EAutonomixActionCategory::Settings:
				Action.RiskLevel = EAutonomixRiskLevel::High;
				break;
			case EAutonomixActionCategory::SourceControl:
			case EAutonomixActionCategory::FileSystem:
				Action.RiskLevel = EAutonomixRiskLevel::Medium;
				break;
			default:
				Action.RiskLevel = EAutonomixRiskLevel::Low;
				break;
			}

			Action.ToolCall = TC;
			Plan.Actions.Add(Action);

			if (Action.RiskLevel > Plan.MaxRiskLevel)
			{
				Plan.MaxRiskLevel = Action.RiskLevel;
			}

			// Check Read-Only status
			if (bAllReadOnly)
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
				// Meta-tools are safe
				if (TC.ToolName == TEXT("update_todo_list") || TC.ToolName == TEXT("switch_mode") || TC.ToolName == TEXT("attempt_completion"))
				{
					bIsReadOnly = true;
				}
				if (!bIsReadOnly)
				{
					bAllReadOnly = false;
				}
			}
		}

		bool bAutoApprove = false;

		bool bOnlyMetaTools = true;
		for (const FAutonomixToolCall& TC : ToolCallQueue)
		{
			if (TC.ToolName != TEXT("attempt_completion") && TC.ToolName != TEXT("switch_mode") && TC.ToolName != TEXT("update_todo_list"))
			{
				bOnlyMetaTools = false;
				break;
			}
		}

		if (bOnlyMetaTools)
		{
			UE_LOG(LogAutonomix, Log, TEXT("ChatSession: Auto-approving %d tool calls (only safe meta-tools pending)"), ToolCallQueue.Num());
			bAutoApprove = true;
		}
		else if (Settings)
		{
			if (Settings->bAutoApproveAllTools)
			{
				UE_LOG(LogAutonomix, Log, TEXT("ChatSession: Auto-approving %d tool calls (bAutoApproveAllTools=true)"), ToolCallQueue.Num());
				bAutoApprove = true;
			}
			else if (Settings->bAutoApproveLowRisk && Plan.MaxRiskLevel == EAutonomixRiskLevel::Low)
			{
				UE_LOG(LogAutonomix, Log, TEXT("ChatSession: Auto-approving %d tool calls (bAutoApproveLowRisk=true and MaxRiskLevel=Low)"), ToolCallQueue.Num());
				bAutoApprove = true;
			}
			else if (Settings->bAutoApproveReadOnlyTools && bAllReadOnly)
			{
				UE_LOG(LogAutonomix, Log, TEXT("ChatSession: Auto-approving %d read-only tool calls (bAutoApproveReadOnlyTools=true)"), ToolCallQueue.Num());
				bAutoApprove = true;
			}
		}

		if (bAutoApprove)
		{
			// We will hook this into FAutonomixAutoApprovalHandler limit checking
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

			ProcessToolCallQueue();
			return;
		}

		OnToolRequiresApproval.Broadcast(Plan);
	}
	else
	{
		// No tool calls received.
		// Roo Code approach: track consecutive no-tool responses.
		// On the FIRST no-tool response during agentic loop, nudge ONCE.
		// On subsequent no-tool responses, ask user or stop.

		if (bInAgenticLoop)
		{
			ConsecutiveNoToolCount++;

			if (ConsecutiveNoToolCount == 1)
			{
				// First no-tool response: nudge once
				ConversationManager->AddUserMessage(
					TEXT("[AUTONOMIX SYSTEM] You did not use any tools in your last response. ")
					TEXT("IMPORTANT: You MUST use a tool to continue. If the task is complete, ")
					TEXT("call the attempt_completion tool with a summary of what was accomplished. ")
					TEXT("If there is more work to do, use the appropriate tool to continue. ")
					TEXT("Do NOT respond with plain text only — you MUST call a tool."));

				OnSaveTabsToDisk.ExecuteIfBound();
				ContinueAgenticLoop();
				return;
			}

			if (ConsecutiveNoToolCount >= MaxConsecutiveNoToolResponses)
			{
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
					ConversationManager->AddUserMessage(
						TEXT("[AUTONOMIX SYSTEM] Please continue the task. If it is complete, ")
						TEXT("you MUST call attempt_completion. Do not respond with text only."));
					OnSaveTabsToDisk.ExecuteIfBound();
					ContinueAgenticLoop();
					return;
				}
			}
		}

		// Task is done (either not in agentic loop, or user chose to end/max nudges)
		bIsProcessing = false;
		bInAgenticLoop = false;
		AgenticLoopCount = 0;
		ConsecutiveNoToolCount = 0;

		OnStatusUpdated.Broadcast(TEXT(""));
		OnAgentFinished.Broadcast(TEXT("Task ended (no tools returned)."));
		OnSaveTabsToDisk.ExecuteIfBound();
	}
}

void FAutonomixChatSession::OnToolCallsRejected(const FAutonomixActionPlan& Plan)
{
	// Send rejection as tool_result errors so Claude knows the tools were not executed
	TArray<FAutonomixToolCall> ActiveToolCalls = MoveTemp(ToolCallQueue);
	for (const FAutonomixToolCall& ToolCall : ActiveToolCalls)
	{
		FAutonomixMessage& ToolResultMsg = ConversationManager->AddToolResultMessage(
			ToolCall.ToolUseId,
			TEXT("Tool execution was rejected by the user. Do not retry this action unless explicitly asked."),
			true /* bIsError */);
		ToolResultMsg.ToolName = TEXT("error");
	}

	ActiveToolCalls.Empty();

	// End agentic loop
	bInAgenticLoop = false;
	bIsProcessing = false;
	OnStatusUpdated.Broadcast(TEXT(""));
	OnAgentFinished.Broadcast(TEXT("Execution rejected by user."));
}

void FAutonomixChatSession::HandleAutoApprovalLimitReached(const FAutonomixAutoApprovalCheck& Check)
{
	FText Title = FText::FromString(TEXT("Autonomix — Approval Required"));
	FText Message = FText::FromString(Check.ApprovalReason);

	EAppReturnType::Type Result = FMessageDialog::Open(EAppMsgType::YesNo, Message, Title);

	if (Result == EAppReturnType::Yes)
	{
		AutoApprovalHandler.ResetBaseline();

		FAutonomixMessage InfoMsg(EAutonomixMessageRole::System,
			TEXT("✅ Continuation approved. Auto-approval counters reset."));
		OnMessageAdded.Broadcast(InfoMsg);

		ProcessToolCallQueue();
	}
	else
	{
		ToolCallQueue.Empty();
		bIsProcessing = false;
		bInAgenticLoop = false;
		AgenticLoopCount = 0;
		OnStatusUpdated.Broadcast(TEXT(""));
		OnAgentFinished.Broadcast(TEXT("Execution rejected by user."));
	}
}

