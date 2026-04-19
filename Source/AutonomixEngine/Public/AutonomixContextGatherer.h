// Copyright Autonomix. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AutonomixInterfaces.h"
#include "AutonomixProjectContext.h"

class AUTONOMIXENGINE_API FAutonomixContextGatherer : public IAutonomixContextGatherer
{
public:
	FAutonomixContextGatherer();
	virtual ~FAutonomixContextGatherer();

	virtual FString BuildContextString() override;
	virtual FString GetFileTreeString() override;
	virtual FString GetAssetSummaryString() override;
	virtual FString GetSettingsSnapshotString() override;
	virtual FString GetClassHierarchyString() override;
	virtual int32 EstimateTokenCount() const override;

	/** Build a full project context object */
	FAutonomixProjectContext BuildProjectContext();

private:
	FAutonomixProjectContext CachedContext;
	FDateTime LastCaptureTime;
	static const int32 CacheValiditySeconds = 30;
};
