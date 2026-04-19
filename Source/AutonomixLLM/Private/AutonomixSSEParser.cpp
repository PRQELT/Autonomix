// Copyright Autonomix. All Rights Reserved.

#include "AutonomixSSEParser.h"
#include "AutonomixCoreModule.h"
#include "Serialization/JsonSerializer.h"

FAutonomixSSEParser::FAutonomixSSEParser()
{
}

FAutonomixSSEParser::~FAutonomixSSEParser()
{
}

void FAutonomixSSEParser::Reset()
{
	LineBuffer.Empty();
	CurrentEventType.Empty();
	CurrentData.Empty();
}

void FAutonomixSSEParser::ProcessChunk(const FString& RawData, TArray<FAutonomixSSEEvent>& OutEvents)
{
	// Append to existing buffer
	LineBuffer += RawData;

	// Process complete lines
	FString RemainingBuffer;
	FString Line;

	while (LineBuffer.Split(TEXT("\n"), &Line, &RemainingBuffer))
	{
		Line.TrimEndInline();
		ProcessLine(Line, OutEvents);
		LineBuffer = RemainingBuffer;
	}
}

void FAutonomixSSEParser::ProcessLine(const FString& Line, TArray<FAutonomixSSEEvent>& OutEvents)
{
	if (Line.IsEmpty())
	{
		// Empty line = end of event, flush
		FlushCurrentEvent(OutEvents);
		return;
	}

	if (Line.StartsWith(TEXT("event:")))
	{
		CurrentEventType = Line.Mid(6).TrimStartAndEnd();
	}
	else if (Line.StartsWith(TEXT("data:")))
	{
		FString DataValue = Line.Mid(5).TrimStartAndEnd();
		if (!CurrentData.IsEmpty())
		{
			CurrentData += TEXT("\n");
		}
		CurrentData += DataValue;
	}
	else if (Line.StartsWith(TEXT(":")))
	{
		// Comment line, ignore (SSE spec)
	}
}

void FAutonomixSSEParser::FlushCurrentEvent(TArray<FAutonomixSSEEvent>& OutEvents)
{
	if (CurrentData.IsEmpty() && CurrentEventType.IsEmpty())
	{
		return;
	}

	FAutonomixSSEEvent Event = ParseEvent(CurrentEventType, CurrentData);
	OutEvents.Add(Event);

	CurrentEventType.Empty();
	CurrentData.Empty();
}

FAutonomixSSEEvent FAutonomixSSEParser::ParseEvent(const FString& EventType, const FString& DataPayload)
{
	FAutonomixSSEEvent Event;
	Event.Type = StringToEventType(EventType);
	Event.RawData = DataPayload;

	// Try to parse JSON
	if (!DataPayload.IsEmpty())
	{
		TSharedPtr<FJsonObject> JsonObj;
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(DataPayload);
		if (FJsonSerializer::Deserialize(Reader, JsonObj) && JsonObj.IsValid())
		{
			Event.JsonData = JsonObj;

			// Extract content block index if present
			if (JsonObj->HasField(TEXT("index")))
			{
				Event.ContentBlockIndex = JsonObj->GetIntegerField(TEXT("index"));
			}
		}
	}

	return Event;
}

EAutonomixSSEEventType FAutonomixSSEParser::StringToEventType(const FString& EventTypeString)
{
	if (EventTypeString == TEXT("message_start")) return EAutonomixSSEEventType::MessageStart;
	if (EventTypeString == TEXT("content_block_start")) return EAutonomixSSEEventType::ContentBlockStart;
	if (EventTypeString == TEXT("content_block_delta")) return EAutonomixSSEEventType::ContentBlockDelta;
	if (EventTypeString == TEXT("content_block_stop")) return EAutonomixSSEEventType::ContentBlockStop;
	if (EventTypeString == TEXT("message_delta")) return EAutonomixSSEEventType::MessageDelta;
	if (EventTypeString == TEXT("message_stop")) return EAutonomixSSEEventType::MessageStop;
	if (EventTypeString == TEXT("ping")) return EAutonomixSSEEventType::Ping;
	if (EventTypeString == TEXT("error")) return EAutonomixSSEEventType::Error;
	return EAutonomixSSEEventType::Unknown;
}
