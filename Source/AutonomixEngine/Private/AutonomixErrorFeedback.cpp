// Copyright Autonomix. All Rights Reserved.

#include "AutonomixErrorFeedback.h"

FAutonomixErrorFeedback::FAutonomixErrorFeedback() {}
FAutonomixErrorFeedback::~FAutonomixErrorFeedback() {}

FString FAutonomixErrorFeedback::FormatCompilationErrors(const TArray<FString>& Errors)
{
	FString Result = TEXT("Compilation Errors:\n");
	for (const FString& Error : Errors) { Result += FString::Printf(TEXT("- %s\n"), *Error); }
	return Result;
}

FString FAutonomixErrorFeedback::FormatBuildErrors(const FString& BuildOutput) { return FString::Printf(TEXT("Build Output:\n%s"), *BuildOutput); }
bool FAutonomixErrorFeedback::ShouldRetry(const FGuid& ActionId) const { return GetRetryCount(ActionId) < MaxRetries; }
void FAutonomixErrorFeedback::RecordRetry(const FGuid& ActionId) { RetryCountMap.FindOrAdd(ActionId)++; }
void FAutonomixErrorFeedback::ResetRetries(const FGuid& ActionId) { RetryCountMap.Remove(ActionId); }
int32 FAutonomixErrorFeedback::GetRetryCount(const FGuid& ActionId) const { const int32* Count = RetryCountMap.Find(ActionId); return Count ? *Count : 0; }
