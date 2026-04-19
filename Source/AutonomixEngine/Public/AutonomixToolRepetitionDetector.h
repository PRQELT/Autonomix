// Copyright Autonomix. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AutonomixTypes.h"

/**
 * Result of a tool repetition check.
 */
struct AUTONOMIXENGINE_API FAutonomixToolRepetitionCheck
{
	/** Whether the tool call should be allowed to execute */
	bool bAllowExecution = true;

	/** Warning message to show the user if execution is blocked. Empty if allowed. */
	FString WarningMessage;

	/** Number of consecutive identical calls detected (informational) */
	int32 ConsecutiveCount = 0;
};

/**
 * Detects consecutive identical tool calls from the AI.
 *
 * When Claude gets stuck in a loop (e.g., repeatedly calling read_file on the same
 * file, or applying the same diff that keeps failing), this detector catches it and
 * stops execution before wasting tokens and money.
 *
 * Ported from Roo Code's ToolRepetitionDetector.ts:
 *   - Serializes each tool call to canonical JSON for comparison
 *   - Counts consecutive identical calls (resets on any new distinct tool call)
 *   - Blocks execution when ConsecutiveCount >= MaxRepetitions
 *   - Allows one recovery attempt after blocking (bLimitReached flag)
 *
 * Integration: call Check() before ExecuteToolCall() in SAutonomixMainPanel.
 * If bAllowExecution == false, pause the agentic loop and present WarningMessage.
 */
class AUTONOMIXENGINE_API FAutonomixToolRepetitionDetector
{
public:
	/** Default: block after 3 consecutive identical calls (same as Roo Code default) */
	static constexpr int32 DefaultMaxRepetitions = 3;

	explicit FAutonomixToolRepetitionDetector(int32 InMaxRepetitions = DefaultMaxRepetitions);

	/**
	 * Check whether the given tool call should be allowed.
	 * Must be called for every tool call before execution.
	 *
	 * @param ToolCall  The tool call received from Claude
	 * @return          Check result — bAllowExecution=false means block + show WarningMessage
	 */
	FAutonomixToolRepetitionCheck Check(const FAutonomixToolCall& ToolCall);

	/** Reset all state. Call when the user intervenes or a new conversation starts. */
	void Reset();

	/** Current consecutive identical call count (for debug display) */
	int32 GetConsecutiveCount() const { return ConsecutiveCount; }

	/** Whether the limit has already been triggered this run */
	bool HasLimitBeenReached() const { return bLimitReached; }

private:
	/** Produce a canonical string representation of a tool call for comparison */
	FString SerializeToolCall(const FAutonomixToolCall& ToolCall) const;

	int32 MaxRepetitions;
	int32 ConsecutiveCount;
	FString LastSerializedCall;

	/** True once we have blocked this run of identical calls.
	 *  On the next call with the same params, we reset instead of re-blocking,
	 *  giving Claude one recovery opportunity. */
	bool bLimitReached;
};
