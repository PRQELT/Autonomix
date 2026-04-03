// Copyright Autonomix. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "AutonomixTypes.h"
#include "AutonomixCostTracker.h"
#include "AutonomixAutoApprovalHandler.h"

class SAutonomixChatView;
class SAutonomixInputArea;
class SAutonomixPlanPreview;
class SAutonomixProgress;
class SAutonomixTodoList;
class SButton;
class SHorizontalBox;
class IAutonomixLLMClient;
class FAutonomixClaudeClient;  // Still needed for OnContextWindowExceeded downcast
class FAutonomixConversationManager;
class FAutonomixToolSchemaRegistry;
class FAutonomixActionRouter;
class FAutonomixExecutionJournal;
class FAutonomixEditorContextCapture;
class FAutonomixContextGatherer;
class FAutonomixContextManager;
class FAutonomixBackupManager;

// Phase 1 additions
class FAutonomixToolRepetitionDetector;
class FAutonomixIgnoreController;
class FAutonomixFileContextTracker;

// Phase 2 additions
class FAutonomixEnvironmentDetails;
class FAutonomixDiffApplicator;

// Phase 3 additions
class FAutonomixCheckpointManager;
class FAutonomixReferenceParser;
class FAutonomixTaskDelegation;

// Phase 4 additions
class FAutonomixTaskHistory;
class FAutonomixSlashCommandRegistry;
class FAutonomixSkillsManager;
class FAutonomixMCPClient;

// Second-pass additions (tree-sitter equivalent)
class FAutonomixCodeStructureParser;

// New UI widgets (from Roo-Code comparison)
class SAutonomixContextBar;
class SAutonomixCheckpointPanel;
class SAutonomixHistoryPanel;
class SAutonomixFollowUpBar;
class SAutonomixFileChangesPanel;

/**
 * Root widget for the Autonomix editor panel.
 *
 * ORCHESTRATES THE FULL AGENTIC LOOP:
 * 1. User types prompt
 * 2. Privacy disclosure check (first use only)
 * 3. Build system prompt with project context + editor context
 * 4. Send to Claude API with tool schemas
 * 5. Stream response tokens to chat view in real-time
 * 6. On tool_use: compute pre-hash -> execute tool -> compute post-hash -> record in journal -> add tool_result
 * 7. Re-send conversation to Claude (loop back to step 4)
 * 8. Repeat until Claude responds with only text (no tool_use)
 *
 * v3.0: Added context management (auto-condense + truncation) via FAutonomixContextManager.
 * Context usage percentage is displayed in the header.
 */
class AUTONOMIXUI_API SAutonomixMainPanel : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SAutonomixMainPanel) {}
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs);
    virtual ~SAutonomixMainPanel();

private:
    /** Per-tab state for multi-conversation sessions. */
    struct FAutonomixConversationTabState
    {
        FString TabId;
        FString Title;
        TSharedPtr<FAutonomixConversationManager> ConversationManager;
        TSharedPtr<FAutonomixContextManager> ContextManager;
        FAutonomixTokenUsage SessionTokenUsage;
        FAutonomixTokenUsage LastResponseTokenUsage;
        float ContextUsagePercent = 0.0f;
        float LastRequestCost = 0.0f;
        FAutonomixCostTracker CostTracker;
        TSharedPtr<class FAutonomixChatSession> ChatSession;
        TArray<FAutonomixTodoItem> Todos;

        // Phase 2: per-tab agent mode
        EAutonomixAgentMode AgentMode = EAutonomixAgentMode::General;
    };

    // ---- Sub-widgets ----
    TSharedPtr<SHorizontalBox> TabButtonContainer;
    TSharedPtr<SAutonomixChatView> ChatView;
    TSharedPtr<SAutonomixInputArea> InputArea;
    TSharedPtr<SAutonomixPlanPreview> PlanPreview;
    TSharedPtr<SAutonomixProgress> ProgressOverlay;
    TSharedPtr<SAutonomixTodoList> TodoListWidget;
    TSharedPtr<SButton> StopButton;
    TSharedPtr<SButton> CondenseButton;

    // ---- Backend components ----
    // LLMClient holds the active provider (Anthropic/OpenAI/Gemini/etc.) created by FAutonomixLLMClientFactory.
    // Use ClaudeClientPtr() helper when Claude-specific APIs (OnContextWindowExceeded) are needed.
    TSharedPtr<IAutonomixLLMClient> LLMClient;
    TSharedPtr<FAutonomixConversationManager> ConversationManager;
    TSharedPtr<FAutonomixToolSchemaRegistry> ToolSchemaRegistry;
    TSharedPtr<FAutonomixActionRouter> ActionRouter;
    TSharedPtr<FAutonomixExecutionJournal> ExecutionJournal;
    TSharedPtr<FAutonomixEditorContextCapture> EditorContextCapture;
    TSharedPtr<FAutonomixContextGatherer> ContextGatherer;
    TSharedPtr<FAutonomixContextManager> ContextManager;
    TSharedPtr<FAutonomixBackupManager> BackupManager;

    // ---- Phase 1: Safety & Reliability ----

    /** Detects consecutive identical tool calls to prevent infinite loops */
    TSharedPtr<FAutonomixToolRepetitionDetector> ToolRepetitionDetector;

    /** Manages .autonomixignore file — blocks AI from accessing excluded paths */
    TSharedPtr<FAutonomixIgnoreController> IgnoreController;

    /** Tracks files the AI has read; detects when user edits them externally */
    TSharedPtr<FAutonomixFileContextTracker> FileContextTracker;

    // ---- Phase 2: Developer Productivity ----

    /** Builds fresh per-message environment details (open files, errors, selection, etc.) */
    TSharedPtr<FAutonomixEnvironmentDetails> EnvironmentDetails;

    /** Applies fuzzy multi-search-replace diffs with Levenshtein matching */
    TSharedPtr<FAutonomixDiffApplicator> DiffApplicator;

    // ---- Cost & Auto-Approval Tracking ----

    /** Tracks per-request and session costs */
    FAutonomixCostTracker CostTracker;

    /** Tracks consecutive auto-approved requests and cumulative cost */
    FAutonomixAutoApprovalHandler AutoApprovalHandler;

    /** Cost of the last completed API request */
    float LastRequestCost = 0.0f;

    

    // ---- Phase 2: Agent Mode (per panel, reflects active tab) ----

    /** Current agent mode for the active tab.
     *  Determines: tool schema set, system prompt role, write permissions. */
    EAutonomixAgentMode CurrentAgentMode = EAutonomixAgentMode::General;

    // ---- Phase 3: Checkpoint Manager ----

    /** Git-based shadow checkpoint system — saves state before each tool batch */
    TSharedPtr<FAutonomixCheckpointManager> CheckpointManager;

    /** @Reference parser — resolves @file, @errors, @selection, etc. in user input */
    TSharedPtr<FAutonomixReferenceParser> ReferenceParser;

    /** Task delegation — manages AI-initiated sub-tasks via new_task tool */
    TSharedPtr<FAutonomixTaskDelegation> TaskDelegation;

    // ---- Phase 4: Advanced Features ----

    /** Task history — persists all conversation sessions with metadata */
    TSharedPtr<FAutonomixTaskHistory> TaskHistory;

    /** Slash command registry — /new-actor, /fix-errors, etc. */
    TSharedPtr<FAutonomixSlashCommandRegistry> SlashCommandRegistry;

    /** Skills manager — loadable step-by-step instruction guides for Claude */
    TSharedPtr<FAutonomixSkillsManager> SkillsManager;

    /** MCP client — connects to external tool servers */
    TSharedPtr<FAutonomixMCPClient> MCPClient;

    /** Code structure parser — extracts UE class/function signatures for folded context */
    TSharedPtr<FAutonomixCodeStructureParser> CodeStructureParser;

    /** Cached folded code structure context (injected into system prompt after condensation) */
    FString CachedCodeStructureContext;

    /** Update the cached code structure context from tracked files */
    void UpdateCodeStructureContext();

    // ---- New UI widgets (Roo-Code parity) ----

    /** Context bar: colored progress bar + token/cost + mode selector + Condense button */
    TSharedPtr<SAutonomixContextBar> ContextBar;

    /** Checkpoint timeline panel (toggled by header button) */
    TSharedPtr<SAutonomixCheckpointPanel> CheckpointPanel;

    /** Task history browser panel (toggled by header button) */
    TSharedPtr<SAutonomixHistoryPanel> HistoryPanel;

    /** Follow-up suggestions bar (shown after attempt_completion) */
    TSharedPtr<SAutonomixFollowUpBar> FollowUpBar;

    /** File changes panel: lists all files modified during session */
    TSharedPtr<SAutonomixFileChangesPanel> FileChangesPanel;

    /** Whether the checkpoint panel is visible */
    bool bShowCheckpointPanel = false;

    /** Whether the history panel is visible */
    bool bShowHistoryPanel = false;

    /** Whether the file changes panel is visible */
    bool bShowFileChangesPanel = false;

    // ---- New UI event handlers ----

    /** Toggle checkpoint panel visibility */
    FReply OnCheckpointsPanelToggled();

    /** Toggle history panel visibility */
    FReply OnHistoryPanelToggled();

    /** Toggle file changes panel visibility */
    FReply OnFileChangesPanelToggled();

    /** Called when user selects a follow-up suggestion */
    void OnFollowUpSelected(const FString& SuggestionText);

    /** Called when user clicks Restore on a checkpoint */
    void OnRestoreCheckpoint(const FString& CommitHash);

    /** Called when user clicks Diff on a checkpoint */
    void OnViewCheckpointDiff(const FString& CommitHash);

    /** Called when user clicks Resume on a history item */
    void OnLoadHistoryTask(const FString& TabId);

    /** Called when user clicks Delete on a history item */
    void OnDeleteHistoryTask(const FString& TabId);

    /** Update all live UI elements (context bar, file changes, etc.) */
    void UpdateLiveUI();

    /** Token usage tracking */
    FAutonomixTokenUsage SessionTokenUsage;

    /** Last reported token usage (for context management) */
    FAutonomixTokenUsage LastResponseTokenUsage;

    /** Context usage percentage (0-100), updated after each API response */
    float ContextUsagePercent = 0.0f;

    // ---- Event handlers ----

    /** Called when the user submits a prompt via the input area */
    void OnPromptSubmitted(const FString& PromptText);

    

    // ---- Delegates from ChatSession ----
    void OnMessageAdded(const FAutonomixMessage& Message);
    void OnMessageUpdated(const FGuid& MessageId, const FString& DeltaText);
    void OnStatusUpdated(const FString& StatusText);
    void OnAgentFinished(const FString& Reason);
    void OnToolRequiresApproval(const FAutonomixActionPlan& Plan);
    void OnRequestStarted();
    void OnRequestCompleted(bool bSuccess);

    /** Called when an HTTP error is received */
    void OnErrorReceived(const FAutonomixHTTPError& Error);

    /** Called when token usage is updated */
    void OnTokenUsageUpdated(const FAutonomixTokenUsage& Usage);

    /** Called when the Stop button is clicked */
    FReply OnStopClicked();

    /** Called when the Condense Context button is clicked */
    FReply OnCondenseContextClicked();

    // ---- Todo / Task Management ----

    /** Handle the update_todo_list meta-tool: parse markdown checklist, update widget.
     *  Returns the tool_result string. */
    FString HandleUpdateTodoList(const FAutonomixToolCall& ToolCall);

    // ---- Phase 2: Mode switching ----

    /**
     * Handle the attempt_completion tool — TERMINATES the agentic loop.
     * This is the PRIMARY way Claude signals "I'm done with this task."
     * Displays the result to the user and stops processing.
     * Returns the tool_result string sent back to Claude (for context).
     */
    FString HandleAttemptCompletion(const FAutonomixToolCall& ToolCall);

    /** Handle the switch_mode meta-tool: change active tab's agent mode.
     *  Returns the tool_result string. */
    FString HandleSwitchMode(const FAutonomixToolCall& ToolCall);

    /** Apply a mode change to the active tab and update UI + system prompt */
    void ApplyAgentMode(EAutonomixAgentMode NewMode);

    /** Get display name for a mode (for UI display) */
    static FString GetModeDisplayName(EAutonomixAgentMode Mode);

    // ---- Phase 1+2: Context window overflow handling ----

    /** Called by ClaudeClient when context window is exceeded.
     *  Trims history by ContextWindowForcedKeepFraction and retries. */
    void HandleContextWindowExceeded(int32 RetryCount);

    // ---- Phase 3: Meta-tool handlers ----

    /** Handle the new_task tool: spawn a child tab and delegate to it */
    FString HandleNewTask(const FAutonomixToolCall& ToolCall);

    /** Handle the skill tool: load a skill document and inject as context */
    FString HandleSkillTool(const FAutonomixToolCall& ToolCall);

    /** Parse @references in user input and resolve to content blocks */
    FString ResolveReferencesInInput(FString& InOutUserInput) const;

    // ---- Phase 4: Sub-task completion callback ----

    /** Called when a child tab's task completes (via attempt_completion). */
    void OnSubTaskCompleted(const FString& SubTaskId, bool bSuccess, const FString& ResultMessage);

    // ---- Agentic Loop Core ----
    
/** Build the per-message environment details string (injected at each API call) */
FString BuildEnvironmentDetailsString() const;

    // ---- Approval Flow ----

    /** Called when the user approves the pending tool actions */
    void OnToolCallsApproved(const FAutonomixActionPlan& Plan);

    /** Called when the user rejects the pending tool actions */
    void OnToolCallsRejected(const FAutonomixActionPlan& Plan);

    // ---- Auto-Approval Limit Handling ----

    /** Called when auto-approval limits (request count or cost) are exceeded.
     *  Shows a dialog asking the user to confirm continuation or cancel. */
    void HandleAutoApprovalLimitReached(const FAutonomixAutoApprovalCheck& Check);

    // ---- Asset Checkpoints (Phase 5.2) ----

    /** Create a checkpoint snapshot of all assets that will be affected by the pending tool calls.
     *  Called before each tool execution batch so the user can restore if needed. */
    void CreateCheckpointForToolBatch(const TArray<FAutonomixToolCall>& ToolCalls, int32 LoopIteration);

    /** Get all asset paths that will be affected by a set of tool calls */
    TArray<FString> GetAffectedPathsFromToolCalls(const TArray<FAutonomixToolCall>& ToolCalls) const;

    // ---- Helpers ----

    /** Build the system prompt with project context + editor context */
    FString BuildSystemPrompt() const;

    /** Initialize backend components with current settings */
    void InitializeBackend();

    /** Register all action executors with the router */
    void RegisterExecutors();

    /** Configure the LLM client from developer settings.
     *  Recreates the client when the active provider changes. */
    void ConfigureClientFromSettings();

    /**
     * Get a typed pointer to the active LLM client downcast as FAutonomixClaudeClient.
     * Returns nullptr for non-Anthropic providers that don't have context window exceeded support.
     * Used only for Anthropic-specific features like OnContextWindowExceeded.
     */
    FAutonomixClaudeClient* ClaudeClientPtr() const;

    /** Show the privacy disclosure dialog (first use only) */
    bool CheckPrivacyDisclosure();

    /** Get context window size in tokens from current settings */
    int32 GetContextWindowTokens() const;

    /** The ID of the message currently being streamed */
    FGuid CurrentStreamingMessageId;

    /** Whether the client is currently processing */
    bool bIsProcessing = false;

    /** The active chat session for the current tab */
    TSharedPtr<class FAutonomixChatSession> ActiveChatSession;

    // ---- Message Queue (Roo Code's MessageQueueService concept) ----

    /** Messages queued while bIsProcessing == true.
     *  When the current task finishes, the next queued message is auto-started.
     *  Prevents the "already processing, please wait" UX problem. */
    TArray<FString> PendingMessageQueue;

    /** Process the next queued message (called when a task completes) */
    void ProcessNextQueuedMessage();

    // ---- Multi-Tab Session State ----

    TArray<FAutonomixConversationTabState> ConversationTabs;
    int32 ActiveTabIndex = INDEX_NONE;
    int32 NextTabNumber = 1;

    FAutonomixConversationTabState* GetActiveTabState();
    const FAutonomixConversationTabState* GetActiveTabState() const;
    void CreateNewTab(const FString& InTitle = FString(), bool bMakeActive = true);
    void CloseTab(int32 TabIndex);
    void SwitchToTab(int32 TabIndex);
    void RefreshTabStrip();
    void RenderActiveConversation();
    void SyncRuntimeStateToActiveTab();
    void LoadRuntimeStateFromActiveTab();

    void LoadTabsFromDisk();
    void SaveTabsToDisk();
    static FString GetTabsSessionDir();
    static FString GetTabsManifestPath();
    static FString MakeTabConversationFileName(const FString& TabId);
    static FString MakeDefaultTabTitle(int32 TabNumber);
    static bool TryParseDefaultTabNumber(const FString& Title, int32& OutNumber);
    int32 GetNextAvailableTabNumber() const;

    FReply OnAddTabClicked();
};
