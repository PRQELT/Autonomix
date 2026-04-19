// Copyright Autonomix. All Rights Reserved.

#include "AutonomixToolRepetitionDetector.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

FAutonomixToolRepetitionDetector::FAutonomixToolRepetitionDetector(int32 InMaxRepetitions)
	: MaxRepetitions(FMath::Max(1, InMaxRepetitions))
	, ConsecutiveCount(0)
	, bLimitReached(false)
{
}

FAutonomixToolRepetitionCheck FAutonomixToolRepetitionDetector::Check(const FAutonomixToolCall& ToolCall)
{
	const FString Serialized = SerializeToolCall(ToolCall);

	// New distinct tool call — reset counter entirely
	if (Serialized != LastSerializedCall)
	{
		LastSerializedCall = Serialized;
		ConsecutiveCount = 1;
		bLimitReached = false;

		return { true, FString(), ConsecutiveCount };
	}

	// Same tool call as last time
	ConsecutiveCount++;

	// If limit was already triggered last time, allow ONE recovery attempt and reset
	if (bLimitReached)
	{
		// Reset everything — give Claude a fresh start
		Reset();
		LastSerializedCall = Serialized;
		ConsecutiveCount = 1;
		return { true, FString(), ConsecutiveCount };
	}

	// Check if we just hit the repetition limit
	if (ConsecutiveCount >= MaxRepetitions)
	{
		bLimitReached = true;

		const FString WarningMessage = FString::Printf(
			TEXT("Autonomix has detected that the AI is stuck in a repetition loop.\n\n"
				 "The tool '%s' has been called %d consecutive times with identical parameters.\n\n"
				 "This usually means the AI is unable to make progress. Options:\n"
				 "  • Provide feedback to guide the AI (e.g., what error is occurring)\n"
				 "  • Cancel and start a new conversation with more context\n"
				 "  • Allow Autonomix to try once more\n\n"
				 "Tool: %s\nConsecutive calls: %d"),
			*ToolCall.ToolName,
			ConsecutiveCount,
			*ToolCall.ToolName,
			ConsecutiveCount
		);

		return { false, WarningMessage, ConsecutiveCount };
	}

	// Below limit — allow
	return { true, FString(), ConsecutiveCount };
}

void FAutonomixToolRepetitionDetector::Reset()
{
	ConsecutiveCount = 0;
	LastSerializedCall.Empty();
	bLimitReached = false;
}

FString FAutonomixToolRepetitionDetector::SerializeToolCall(const FAutonomixToolCall& ToolCall) const
{
	// Canonical key: tool name + sorted JSON parameters
	// Sorting keys ensures {"a":1,"b":2} == {"b":2,"a":1}
	FString Result = ToolCall.ToolName + TEXT("|");

	if (ToolCall.InputParams.IsValid())
	{
		// Collect all fields and sort by key for canonical ordering
		TArray<FString> Keys;
		ToolCall.InputParams->Values.GetKeys(Keys);
		Keys.Sort();

		TArray<TPair<FString, FString>> SortedPairs;
		for (const FString& Key : Keys)
		{
			TSharedPtr<FJsonValue> Val = ToolCall.InputParams->Values.FindRef(Key);
			FString ValStr;
			if (Val.IsValid())
			{
				switch (Val->Type)
				{
				case EJson::String:
					ValStr = Val->AsString();
					break;
				case EJson::Number:
					ValStr = FString::SanitizeFloat(Val->AsNumber());
					break;
				case EJson::Boolean:
					ValStr = Val->AsBool() ? TEXT("true") : TEXT("false");
					break;
				case EJson::Null:
					ValStr = TEXT("null");
					break;
				default:
				{
					// For objects/arrays, serialize to JSON string
					TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&ValStr);
					FJsonSerializer::Serialize(Val.ToSharedRef(), TEXT(""), Writer);
					break;
				}
				}
			}
			SortedPairs.Add({ Key, ValStr });
		}

		for (const TPair<FString, FString>& Pair : SortedPairs)
		{
			Result += Pair.Key + TEXT("=") + Pair.Value + TEXT(";");
		}
	}

	return Result;
}
