// Copyright Autonomix. All Rights Reserved.

#include "AutonomixContextCondenser.h"
#include "AutonomixInterfaces.h"
#include "AutonomixConversationManager.h"
#include "AutonomixCoreModule.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Misc/Guid.h"

// ============================================================================
// Summary Prompt (ported from Roo Code condense/index.ts)
// ============================================================================

const FString FAutonomixContextCondenser::SummaryPrompt =
	TEXT("You are a helpful AI assistant tasked with summarizing conversations.\n\n")
	TEXT("CRITICAL: This is a summarization-only request. DO NOT call any tools or functions.\n")
	TEXT("Your ONLY task is to analyze the conversation and produce a text summary.\n")
	TEXT("Respond with text only - no tool calls will be processed.\n\n")
	TEXT("CRITICAL: This summarization request is a SYSTEM OPERATION, not a user message.\n")
	TEXT("When analyzing \"user requests\" and \"user intent\", completely EXCLUDE this summarization message.\n")
	TEXT("The \"most recent user request\" and \"next step\" must be based on what the user was doing BEFORE ")
	TEXT("this system message appeared.\n")
	TEXT("The goal is for work to continue seamlessly after condensation - as if it never happened.");

const FString FAutonomixContextCondenser::CondenseInstructions =
	TEXT("Please create a concise but comprehensive summary of our conversation so far. Include:\n")
	TEXT("1. The main task or goal the user is trying to accomplish\n")
	TEXT("2. Key decisions and actions taken\n")
	TEXT("3. Current state of the project/task\n")
	TEXT("4. Any important context needed to continue the work\n")
	TEXT("5. The most recent user request and what the next step should be\n\n")
	TEXT("Focus on information that will allow work to continue seamlessly.");

// ============================================================================
// Constructor / Destructor
// ============================================================================

FAutonomixContextCondenser::FAutonomixContextCondenser(
	TSharedPtr<IAutonomixLLMClient> InLLMClient,
	TSharedPtr<FAutonomixConversationManager> InConversationManager)
	: LLMClient(InLLMClient)
	, ConversationManager(InConversationManager)
	, bIsCondensing(false)
{
}

FAutonomixContextCondenser::~FAutonomixContextCondenser()
{
	// Unbind any active delegate handles
	if (LLMClient.IsValid())
	{
		if (StreamingHandle.IsValid())
			LLMClient->OnStreamingText().Remove(StreamingHandle);
		if (MessageCompleteHandle.IsValid())
			LLMClient->OnMessageComplete().Remove(MessageCompleteHandle);
		if (RequestCompletedHandle.IsValid())
			LLMClient->OnRequestCompleted().Remove(RequestCompletedHandle);
	}
}

// ============================================================================
// SummarizeConversation
// ============================================================================

void FAutonomixContextCondenser::SummarizeConversation(
	const FString& SystemPrompt,
	TFunction<void(const FAutonomixCondenseResult&)> OnComplete,
	const FString& FoldedCodeContext)
{
	if (bIsCondensing)
	{
		UE_LOG(LogAutonomix, Warning, TEXT("ContextCondenser: Already condensing. Ignoring request."));
		FAutonomixCondenseResult FailResult;
		FailResult.bSuccess = false;
		FailResult.ErrorMessage = TEXT("Condensation already in progress.");
		OnComplete(FailResult);
		return;
	}

	if (!LLMClient.IsValid() || !ConversationManager.IsValid())
	{
		UE_LOG(LogAutonomix, Error, TEXT("ContextCondenser: Invalid LLM client or conversation manager."));
		FAutonomixCondenseResult FailResult;
		FailResult.bSuccess = false;
		FailResult.ErrorMessage = TEXT("Invalid client or conversation manager.");
		OnComplete(FailResult);
		return;
	}

	const TArray<FAutonomixMessage>& FullHistory = ConversationManager->GetHistory();
	if (FullHistory.Num() <= 1)
	{
		FAutonomixCondenseResult FailResult;
		FailResult.bSuccess = false;
		FailResult.ErrorMessage = TEXT("Not enough messages to condense.");
		OnComplete(FailResult);
		return;
	}

	bIsCondensing = true;
	AccumulatedSummary.Empty();
	PendingCallback = OnComplete;

	// Prepare messages: convert tool blocks to text + inject synthetic tool results
	TArray<FAutonomixMessage> MessagesToSummarize = ConvertToolBlocksToText(FullHistory);
	InjectSyntheticToolResults(MessagesToSummarize);

	// Add the condense instructions as the final user message
	// Ported from Roo Code: inject folded code context into the condensation
	// so that Claude's summary includes awareness of code structure.
	// This becomes a <file-context> block preserved in the summary message.
	FString InstructionsContent = CondenseInstructions;
	if (!FoldedCodeContext.IsEmpty())
	{
		InstructionsContent += TEXT("\n\n")
			TEXT("The following code structure context should be preserved in your summary.\n")
			TEXT("Include a brief note about the key files and their structure:\n\n")
			+ FoldedCodeContext;
	}

	FAutonomixMessage InstructionsMsg;
	InstructionsMsg.MessageId = FGuid::NewGuid();
	InstructionsMsg.Role = EAutonomixMessageRole::User;
	InstructionsMsg.Content = InstructionsContent;
	InstructionsMsg.Timestamp = FDateTime::UtcNow();
	MessagesToSummarize.Add(InstructionsMsg);

	// Bind delegates for the summarization response
	// We use temporary lambda captures to collect the summary text
	FGuid SummaryMessageId;
	TSharedPtr<FString> SummaryAccumulator = MakeShared<FString>();
	TSharedPtr<bool> bCompleted = MakeShared<bool>(false);

	StreamingHandle = LLMClient->OnStreamingText().AddLambda(
		[SummaryAccumulator](const FGuid& MsgId, const FString& DeltaText)
		{
			*SummaryAccumulator += DeltaText;
		});

	RequestCompletedHandle = LLMClient->OnRequestCompleted().AddLambda(
		[this, SummaryAccumulator, bCompleted](bool bSuccess)
		{
			if (*bCompleted) return;
			*bCompleted = true;

			// Unbind delegates
			if (LLMClient.IsValid())
			{
				LLMClient->OnStreamingText().Remove(StreamingHandle);
				LLMClient->OnMessageComplete().Remove(MessageCompleteHandle);
				LLMClient->OnRequestCompleted().Remove(RequestCompletedHandle);
			}

			bIsCondensing = false;

			FAutonomixCondenseResult Result;

			if (!bSuccess || SummaryAccumulator->IsEmpty())
			{
				Result.bSuccess = false;
				Result.ErrorMessage = TEXT("Condensation API call failed or returned empty summary.");
				UE_LOG(LogAutonomix, Error, TEXT("ContextCondenser: %s"), *Result.ErrorMessage);

				if (PendingCallback)
				{
					PendingCallback(Result);
					PendingCallback = nullptr;
				}
				return;
			}

			FString Summary = SummaryAccumulator->TrimStartAndEnd();
			Result.bSuccess = true;
			Result.Summary = Summary;

			// Generate a unique condense ID
			Result.CondenseId = FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphens);

			// Apply condensation to the conversation manager
			ApplyCondensation(Summary, Result);

			UE_LOG(LogAutonomix, Log,
				TEXT("ContextCondenser: Condensed %d messages. Summary length: %d chars. CondenseId: %s"),
				ConversationManager->GetMessageCount(), Summary.Len(), *Result.CondenseId);

			if (PendingCallback)
			{
				PendingCallback(Result);
				PendingCallback = nullptr;
			}
		});

	// Send the summarization request — no tool schemas (text-only)
	LLMClient->SendMessage(MessagesToSummarize, SummaryPrompt, TArray<TSharedPtr<FJsonObject>>());
}

// ============================================================================
// Convert tool blocks to text (for summarization without tools param)
// Ported from Roo Code's convertToolBlocksToText() in condense/index.ts
// ============================================================================

TArray<FAutonomixMessage> FAutonomixContextCondenser::ConvertToolBlocksToText(const TArray<FAutonomixMessage>& Messages)
{
	TArray<FAutonomixMessage> Result;

	for (const FAutonomixMessage& Msg : Messages)
	{
		FAutonomixMessage ConvertedMsg = Msg;

		if (Msg.Role == EAutonomixMessageRole::Assistant && !Msg.ContentBlocksJson.IsEmpty())
		{
			// Parse content blocks and convert tool_use to text
			TArray<TSharedPtr<FJsonValue>> ContentBlocks;
			TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Msg.ContentBlocksJson);
			if (FJsonSerializer::Deserialize(Reader, ContentBlocks))
			{
				FString TextContent;
				for (const TSharedPtr<FJsonValue>& Block : ContentBlocks)
				{
					const TSharedPtr<FJsonObject>* BlockObj = nullptr;
					if (!Block->TryGetObject(BlockObj)) continue;

					FString BlockType;
					(*BlockObj)->TryGetStringField(TEXT("type"), BlockType);

					if (BlockType == TEXT("text"))
					{
						FString Text;
						(*BlockObj)->TryGetStringField(TEXT("text"), Text);
						TextContent += Text;
					}
					else if (BlockType == TEXT("tool_use"))
					{
						FString ToolName;
						(*BlockObj)->TryGetStringField(TEXT("name"), ToolName);

						const TSharedPtr<FJsonObject>* InputObj = nullptr;
						FString InputStr;
						if ((*BlockObj)->TryGetObjectField(TEXT("input"), InputObj))
						{
							TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&InputStr);
							FJsonSerializer::Serialize((*InputObj).ToSharedRef(), Writer);
						}

						TextContent += FString::Printf(TEXT("\n[Tool Use: %s]\n%s\n"), *ToolName, *InputStr);
					}
				}
				ConvertedMsg.Content = TextContent;
				ConvertedMsg.ContentBlocksJson.Empty(); // Remove blocks so client sends as plain text
			}
		}
		else if (Msg.Role == EAutonomixMessageRole::ToolResult)
		{
			// Convert tool_result to text representation
			FString Prefix = TEXT("[Tool Result]\n");
			ConvertedMsg.Content = Prefix + Msg.Content;
		}

		Result.Add(ConvertedMsg);
	}

	return Result;
}

// ============================================================================
// Inject synthetic tool results for orphaned tool_calls
// Ported from Roo Code's injectSyntheticToolResults() in condense/index.ts
// ============================================================================

void FAutonomixContextCondenser::InjectSyntheticToolResults(TArray<FAutonomixMessage>& Messages)
{
	// Find all tool_use IDs in assistant messages
	TSet<FString> ToolCallIds;
	// Find all tool_result IDs in user messages
	TSet<FString> ToolResultIds;

	for (const FAutonomixMessage& Msg : Messages)
	{
		if (Msg.Role == EAutonomixMessageRole::Assistant && !Msg.ContentBlocksJson.IsEmpty())
		{
			TArray<TSharedPtr<FJsonValue>> ContentBlocks;
			TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Msg.ContentBlocksJson);
			if (FJsonSerializer::Deserialize(Reader, ContentBlocks))
			{
				for (const TSharedPtr<FJsonValue>& Block : ContentBlocks)
				{
					const TSharedPtr<FJsonObject>* BlockObj = nullptr;
					if (!Block->TryGetObject(BlockObj)) continue;

					FString BlockType;
					(*BlockObj)->TryGetStringField(TEXT("type"), BlockType);
					if (BlockType == TEXT("tool_use"))
					{
						FString Id;
						(*BlockObj)->TryGetStringField(TEXT("id"), Id);
						if (!Id.IsEmpty()) ToolCallIds.Add(Id);
					}
				}
			}
		}
		else if (Msg.Role == EAutonomixMessageRole::ToolResult)
		{
			if (!Msg.ToolUseId.IsEmpty()) ToolResultIds.Add(Msg.ToolUseId);
		}
	}

	// Find orphans (tool_calls without matching tool_results)
	TArray<FString> OrphanIds;
	for (const FString& Id : ToolCallIds)
	{
		if (!ToolResultIds.Contains(Id))
		{
			OrphanIds.Add(Id);
		}
	}

	if (OrphanIds.Num() == 0) return;

	// Inject synthetic tool_results for each orphan
	for (const FString& OrphanId : OrphanIds)
	{
		FAutonomixMessage SyntheticResult;
		SyntheticResult.MessageId = FGuid::NewGuid();
		SyntheticResult.Role = EAutonomixMessageRole::ToolResult;
		SyntheticResult.ToolUseId = OrphanId;
		SyntheticResult.Content = TEXT("Context condensation triggered. Tool execution deferred.");
		SyntheticResult.Timestamp = FDateTime::UtcNow();
		Messages.Add(SyntheticResult);

		UE_LOG(LogAutonomix, Log,
			TEXT("ContextCondenser: Injected synthetic tool_result for orphan tool_use id=%s"), *OrphanId);
	}
}

// ============================================================================
// Apply condensation to the conversation manager
// ============================================================================

void FAutonomixContextCondenser::ApplyCondensation(const FString& Summary, const FAutonomixCondenseResult& Result)
{
	if (!ConversationManager.IsValid()) return;

	TArray<FAutonomixMessage>& History = const_cast<TArray<FAutonomixMessage>&>(ConversationManager->GetHistory());

	// Tag ALL existing messages with CondenseParent = Result.CondenseId
	// (unless they already have a CondenseParent from a previous condensation)
	for (FAutonomixMessage& Msg : History)
	{
		if (Msg.CondenseParent.IsEmpty() && !Msg.bIsSummary)
		{
			Msg.CondenseParent = Result.CondenseId;
		}
	}

	// Insert the summary message at the end
	FAutonomixMessage SummaryMsg;
	SummaryMsg.MessageId = FGuid::NewGuid();
	SummaryMsg.Role = EAutonomixMessageRole::User;
	SummaryMsg.Content = FString::Printf(TEXT("## Conversation Summary\n%s"), *Summary);
	SummaryMsg.Timestamp = FDateTime::UtcNow();
	SummaryMsg.bIsSummary = true;
	SummaryMsg.CondenseId = Result.CondenseId;

	History.Add(SummaryMsg);
}
