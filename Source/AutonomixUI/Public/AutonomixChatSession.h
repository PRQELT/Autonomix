// Copyright Autonomix. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Templates/SharedPointer.h"
#include "AutonomixTypes.h"
#include "AutonomixAutoApprovalHandler.h"

// Forward declarations
class IAutonomixLLMClient;
class FAutonomixActionRouter;
class FAutonomixToolSchemaRegistry;
class FAutonomixConversationManager;
class FAutonomixExecutionJournal;
class FAutonomixToolRepetitionDetector;
class FAutonomixFileContextTracker;
class FAutonomixContextManager;
class FAutonomixCheckpointManager;
struct FAutonomixToolCall;
struct FAutonomixMessage;
struct FAutonomixTokenUsage;
struct FAutonomixActionPlan;

DECLARE_MULTICAST_DELEGATE_OneParam(FOnToolRequiresApproval, const FAutonomixActionPlan& /*Plan*/);
DECLARE_MULTICAST_DELEGATE_OneParam(FOnAgentFinished, const FString& /*Reason*/);
DECLARE_MULTICAST_DELEGATE_OneParam(FOnTokenUsageUpdated, const FAutonomixTokenUsage& /*Usage*/);
DECLARE_MULTICAST_DELEGATE_OneParam(FOnMessageAdded, const FAutonomixMessage& /*Message*/);
DECLARE_MULTICAST_DELEGATE_TwoParams(FOnMessageUpdated, const FGuid& /*MessageId*/, const FString& /*DeltaText*/);
DECLARE_MULTICAST_DELEGATE_OneParam(FOnStatusUpdated, const FString& /*StatusText*/); // For progress overlay
DECLARE_MULTICAST_DELEGATE(FOnSessionCompletedContextManagement); // Trigger to resume after context condense

/**
 * FAutonomixChatSession
 * Manages the lifecycle of an AI conversation, handling the agentic loop, 
 * LLM streaming callbacks, tool execution queuing, and token usage tracking.
 */
class AUTONOMIXUI_API FAutonomixChatSession : public TSharedFromThis<FAutonomixChatSession>
{
public:
    FAutonomixChatSession();
    virtual ~FAutonomixChatSession();

    void Initialize(TSharedPtr<IAutonomixLLMClient> InLLMClient,
                    TSharedPtr<FAutonomixConversationManager> InConvManager,
                    TSharedPtr<FAutonomixActionRouter> InActionRouter,
                    TSharedPtr<FAutonomixExecutionJournal> InExecutionJournal,
                    TSharedPtr<FAutonomixToolRepetitionDetector> InToolRepetitionDetector,
                    TSharedPtr<FAutonomixFileContextTracker> InFileContextTracker,
                    TSharedPtr<FAutonomixContextManager> InContextManager,
                    TSharedPtr<FAutonomixToolSchemaRegistry> InToolSchemaRegistry,
                    TSharedPtr<FAutonomixCheckpointManager> InCheckpointManager);

    // Agentic Loop execution methods
    void ProcessToolCallQueue();
    void ContinueAgenticLoop();
    FString ExecuteToolCall(const FAutonomixToolCall& ToolCall, bool& bOutIsError);

    // LLM Callbacks
    void OnStreamingText(const FGuid& MessageId, const FString& DeltaText);
    void OnToolCallReceived(const FAutonomixToolCall& ToolCall);
    void OnMessageComplete(const FAutonomixMessage& Message);
    void OnRequestStarted();
    void OnRequestCompleted(bool bSuccess);
    void OnRequestCompletedPostContextManagement();

    // Approval Flow
    void OnToolCallsApproved(const FAutonomixActionPlan& Plan);
    void OnToolCallsRejected(const FAutonomixActionPlan& Plan);

    // Callbacks to request environment strings or actions from UI
    DECLARE_DELEGATE_RetVal(FString, FGetEnvironmentDetailsString);
    FGetEnvironmentDetailsString OnGetEnvironmentDetailsString;

    DECLARE_DELEGATE_RetVal(FString, FGetSystemPromptString);
    FGetSystemPromptString OnGetSystemPromptString;

    DECLARE_DELEGATE(FOnSaveTabsToDisk);
    FOnSaveTabsToDisk OnSaveTabsToDisk;

    // Meta-tool handlers
    DECLARE_DELEGATE_RetVal_OneParam(FString, FOnHandleUpdateTodoList, const FAutonomixToolCall&);
    FOnHandleUpdateTodoList OnHandleUpdateTodoList;

    DECLARE_DELEGATE_RetVal_OneParam(FString, FOnHandleAttemptCompletion, const FAutonomixToolCall&);
    FOnHandleAttemptCompletion OnHandleAttemptCompletion;

    DECLARE_DELEGATE_RetVal_OneParam(FString, FOnHandleSwitchMode, const FAutonomixToolCall&);
    FOnHandleSwitchMode OnHandleSwitchMode;

    // Delegates for UI state tracking
    FOnAgentFinished& GetOnAgentFinished() { return OnAgentFinished; }
    FOnToolRequiresApproval& GetOnToolRequiresApproval() { return OnToolRequiresApproval; }
    FOnTokenUsageUpdated& GetOnTokenUsageUpdated() { return OnTokenUsageUpdated; }
    FOnMessageAdded& GetOnMessageAdded() { return OnMessageAdded; }
    FOnMessageUpdated& GetOnMessageUpdated() { return OnMessageUpdated; }
    FOnStatusUpdated& GetOnStatusUpdated() { return OnStatusUpdated; }
    FOnSessionCompletedContextManagement& GetOnSessionCompletedContextManagement() { return OnSessionCompletedContextManagement; }

    DECLARE_MULTICAST_DELEGATE(FOnRequestStartedDelegate);
    DECLARE_MULTICAST_DELEGATE_OneParam(FOnRequestCompletedDelegate, bool);

    FOnRequestStartedDelegate& GetOnRequestStarted() { return OnRequestStartedDelegate; }
    FOnRequestCompletedDelegate& GetOnRequestCompleted() { return OnRequestCompletedDelegate; }

    /** State-change delegate — UI binds to this for Stop/Send swap, input enable/disable, etc. */
    FOnConversationStateChanged& GetOnConversationStateChanged() { return OnConversationStateChanged; }

    // Getters
    bool IsInAgenticLoop() const { return bInAgenticLoop; }
    int32 GetAgenticLoopCount() const { return AgenticLoopCount; }
    float GetLastRequestCost() const { return LastRequestCost; }
    FAutonomixTokenUsage GetLastResponseTokenUsage() const { return LastResponseTokenUsage; }

    /** Get the current conversation state (single source of truth) */
    EConversationState GetConversationState() const { return CurrentState; }

    /** Backward-compat: returns true when Streaming or Cancelling */
    bool IsProcessing() const { return CurrentState == EConversationState::Streaming || CurrentState == EConversationState::Cancelling; }

    /** Transition to a new conversation state. Broadcasts OnConversationStateChanged. */
    void SetState(EConversationState NewState);

    // Setters
    void SetAgentMode(EAutonomixAgentMode NewMode) { CurrentAgentMode = NewMode; }

    /** Stop the agentic loop immediately — clears tool queue, resets flags.
     *  Called when the user clicks the Stop button. */
    void StopAgenticLoop();

    /**
     * Resume an interrupted task.
     *
     * Called when the user clicks "Continue Task" on a restored conversation.
     * This method:
     * 1. Injects synthetic tool_result messages for any orphaned tool_use blocks
     * 2. Injects a time-aware resumption prompt as a user message
     * 3. Restarts the agentic loop (sends the conversation to the LLM)
     *
     * The resumption prompt follows the "Discovery Hypothesis Pattern" — it forces
     * the AI to replan rather than blindly resume from the exact point of interruption.
     *
     * @param InterruptedAt  When the task was interrupted (for time-aware prompt)
     */
    void ResumeTask(const FDateTime& InterruptedAt);

    // ---- DynamicallyLoadedTools persistence (v4.0) ----

    /** Get the set of tools discovered via get_tool_info during this session */
    const TSet<FString>& GetDynamicallyLoadedTools() const { return DynamicallyLoadedTools; }

    /** Restore dynamically loaded tools from persisted state (called during tab load) */
    void SetDynamicallyLoadedTools(const TSet<FString>& InTools) { DynamicallyLoadedTools = InTools; }

private:
    // Dependencies
    TSharedPtr<IAutonomixLLMClient> LLMClient;
    TSharedPtr<FAutonomixConversationManager> ConversationManager;
    TSharedPtr<FAutonomixActionRouter> ActionRouter;
    TSharedPtr<FAutonomixExecutionJournal> ExecutionJournal;
    TSharedPtr<FAutonomixToolRepetitionDetector> ToolRepetitionDetector;
    TSharedPtr<FAutonomixFileContextTracker> FileContextTracker;
    TSharedPtr<FAutonomixToolSchemaRegistry> ToolSchemaRegistry;
    TSharedPtr<FAutonomixContextManager> ContextManager;
    TSharedPtr<FAutonomixCheckpointManager> CheckpointManager;

    // Conversation State (single source of truth)
    EConversationState CurrentState = EConversationState::Idle;

    // Loop State
    TArray<FAutonomixToolCall> ToolCallQueue;
    bool bInAgenticLoop = false;
    bool bStopRequested = false;  // Set by StopAgenticLoop(), checked during tool execution
    int32 AgenticLoopCount = 0;
    int32 ConsecutiveNoToolCount = 0;
    static const int32 MaxConsecutiveNoToolResponses = 3;
    float LastRequestCost = 0.0f;
    EAutonomixAgentMode CurrentAgentMode = EAutonomixAgentMode::General;

    /** Tools discovered via get_tool_info during the current session.
     *  These are injected into the tools array on subsequent API calls
     *  so that strict-mode providers (OpenAI Responses API) can actually
     *  call them. Fixes the "discovery loop" where the model repeatedly
     *  calls get_tool_info but can never invoke the discovered tool.
     *  Cleared on new conversation / tab switch. */
    TSet<FString> DynamicallyLoadedTools;
    FGuid CurrentStreamingMessageId;
    FAutonomixTokenUsage LastResponseTokenUsage;

    // Auto Approval
    FAutonomixAutoApprovalHandler AutoApprovalHandler;
    void HandleAutoApprovalLimitReached(const FAutonomixAutoApprovalCheck& Check);

    // Delegates
    FOnAgentFinished OnAgentFinished;
    FOnToolRequiresApproval OnToolRequiresApproval;
    FOnTokenUsageUpdated OnTokenUsageUpdated;
    FOnMessageAdded OnMessageAdded;
    FOnMessageUpdated OnMessageUpdated;
    FOnStatusUpdated OnStatusUpdated;
    FOnSessionCompletedContextManagement OnSessionCompletedContextManagement;
    FOnRequestStartedDelegate OnRequestStartedDelegate;
    FOnRequestCompletedDelegate OnRequestCompletedDelegate;
    FOnConversationStateChanged OnConversationStateChanged;
};
