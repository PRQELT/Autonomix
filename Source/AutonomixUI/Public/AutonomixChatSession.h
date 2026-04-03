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

    // Getters
    bool IsInAgenticLoop() const { return bInAgenticLoop; }
    int32 GetAgenticLoopCount() const { return AgenticLoopCount; }
    float GetLastRequestCost() const { return LastRequestCost; }
    FAutonomixTokenUsage GetLastResponseTokenUsage() const { return LastResponseTokenUsage; }

    // Setters
    void SetAgentMode(EAutonomixAgentMode NewMode) { CurrentAgentMode = NewMode; }
    void SetProcessingState(bool bProcessing) { bIsProcessing = bProcessing; }
    void SetAgenticLoopState(bool bLooping) { bInAgenticLoop = bLooping; }

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

    // Loop State
    TArray<FAutonomixToolCall> ToolCallQueue;
    bool bInAgenticLoop = false;
    bool bIsProcessing = false;
    int32 AgenticLoopCount = 0;
    int32 ConsecutiveNoToolCount = 0;
    static const int32 MaxConsecutiveNoToolResponses = 3;
    float LastRequestCost = 0.0f;
    EAutonomixAgentMode CurrentAgentMode = EAutonomixAgentMode::General;
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
};
