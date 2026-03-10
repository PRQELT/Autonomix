// Copyright Autonomix. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AutonomixTypes.h"

class AUTONOMIXENGINE_API FAutonomixErrorFeedback
{
public:
	FAutonomixErrorFeedback();
	~FAutonomixErrorFeedback();

	/** Format compilation errors for AI consumption */
	FString FormatCompilationErrors(const TArray<FString>& Errors);
	/** Format build errors for AI consumption */
	FString FormatBuildErrors(const FString& BuildOutput);
	/** Check if we should retry based on error count */
	bool ShouldRetry(const FGuid& ActionId) const;
	/** Record a retry attempt */
	void RecordRetry(const FGuid& ActionId);
	/** Reset retry count for an action */
	void ResetRetries(const FGuid& ActionId);
	/** Get current retry count */
	int32 GetRetryCount(const FGuid& ActionId) const;

	int32 MaxRetries = 3;

private:
	TMap<FGuid, int32> RetryCountMap;
};
