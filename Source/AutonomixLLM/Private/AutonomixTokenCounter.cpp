// Copyright Autonomix. All Rights Reserved.

#include "AutonomixTokenCounter.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

int32 FAutonomixTokenCounter::EstimateTokens(const FString& Text)
{
	if (Text.IsEmpty()) return 0;
	return FMath::Max(1, Text.Len() / CharsPerToken);
}

int32 FAutonomixTokenCounter::EstimateTokens(const TArray<FAutonomixMessage>& Messages)
{
	int32 Total = 0;
	for (const FAutonomixMessage& Msg : Messages)
	{
		Total += EstimateTokens(Msg.Content);

		// If content blocks JSON is present, count that too (may contain more data)
		if (!Msg.ContentBlocksJson.IsEmpty())
		{
			Total += EstimateTokens(Msg.ContentBlocksJson);
		}
		else
		{
			// Content block overhead per message
			Total += MessageOverheadTokens;
		}
	}
	return Total;
}

int32 FAutonomixTokenCounter::EstimateTokens(const TSharedPtr<FJsonObject>& Json)
{
	if (!Json.IsValid()) return 0;

	FString Serialized;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Serialized);
	FJsonSerializer::Serialize(Json.ToSharedRef(), Writer);

	return EstimateTokens(Serialized);
}

int32 FAutonomixTokenCounter::EstimateTokens(const TArray<TSharedPtr<FJsonValue>>& JsonArray)
{
	if (JsonArray.Num() == 0) return 0;

	FString Serialized;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Serialized);
	FJsonSerializer::Serialize(JsonArray, Writer);

	return EstimateTokens(Serialized);
}

int32 FAutonomixTokenCounter::GetContextWindowTokens(bool bExtended)
{
	return bExtended ? 1000000 : 200000;
}

float FAutonomixTokenCounter::GetContextUsagePercent(int32 UsedTokens, int32 TotalWindowTokens)
{
	if (TotalWindowTokens <= 0) return 0.0f;
	return FMath::Clamp(100.0f * static_cast<float>(UsedTokens) / static_cast<float>(TotalWindowTokens), 0.0f, 100.0f);
}
