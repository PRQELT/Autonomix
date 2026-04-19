// Copyright Autonomix. All Rights Reserved.

#include "Diagnostics/AutonomixDiagnosticsActions.h"
#include "AutonomixCoreModule.h"
#include "Logging/MessageLog.h"
#include "Logging/TokenizedMessage.h"
#include "Misc/OutputDeviceRedirector.h"
#include "Misc/OutputDeviceHelper.h"

#define LOCTEXT_NAMESPACE "AutonomixDiagnosticsActions"

/**
 * Custom output device that captures log messages into a buffer.
 * Attached temporarily during read_message_log to capture recent entries.
 */
class FAutonomixLogCapture : public FOutputDevice
{
public:
	struct FLogEntry
	{
		FString Category;
		FString Message;
		ELogVerbosity::Type Verbosity;
		double Time;
	};

	TArray<FLogEntry> Entries;
	int32 MaxEntries = 500;

	virtual void Serialize(const TCHAR* V, ELogVerbosity::Type Verbosity, const FName& Category) override
	{
		if (Entries.Num() < MaxEntries)
		{
			FLogEntry Entry;
			Entry.Category = Category.ToString();
			Entry.Message = V;
			Entry.Verbosity = Verbosity;
			Entry.Time = FPlatformTime::Seconds();
			Entries.Add(MoveTemp(Entry));
		}
	}
};

// Static log capture instance — always listening
static TSharedPtr<FAutonomixLogCapture> GAutonomixLogCapture;

// ============================================================================
// Lifecycle
// ============================================================================

FAutonomixDiagnosticsActions::FAutonomixDiagnosticsActions()
{
	// Create and attach the log capture device so it accumulates messages
	if (!GAutonomixLogCapture.IsValid())
	{
		GAutonomixLogCapture = MakeShared<FAutonomixLogCapture>();
		GLog->AddOutputDevice(GAutonomixLogCapture.Get());
	}
}

FAutonomixDiagnosticsActions::~FAutonomixDiagnosticsActions()
{
	// We intentionally do NOT remove the device here — it's a singleton
	// that persists for the editor session.
}

// ============================================================================
// IAutonomixActionExecutor Interface
// ============================================================================

FName FAutonomixDiagnosticsActions::GetActionName() const { return FName(TEXT("Diagnostics")); }
FText FAutonomixDiagnosticsActions::GetDisplayName() const { return LOCTEXT("DisplayName", "Diagnostics & Message Log"); }
EAutonomixActionCategory FAutonomixDiagnosticsActions::GetCategory() const { return EAutonomixActionCategory::General; }
EAutonomixRiskLevel FAutonomixDiagnosticsActions::GetDefaultRiskLevel() const { return EAutonomixRiskLevel::Low; }
bool FAutonomixDiagnosticsActions::CanUndo() const { return false; }
bool FAutonomixDiagnosticsActions::UndoAction() { return false; }

TArray<FString> FAutonomixDiagnosticsActions::GetSupportedToolNames() const
{
	return { TEXT("read_message_log") };
}

bool FAutonomixDiagnosticsActions::ValidateParams(const TSharedRef<FJsonObject>& Params, TArray<FString>& OutErrors) const
{
	return true; // All params optional
}

FAutonomixActionPlan FAutonomixDiagnosticsActions::PreviewAction(const TSharedRef<FJsonObject>& Params)
{
	FAutonomixActionPlan Plan;
	Plan.Summary = TEXT("Read recent Output Log entries (read-only)");
	Plan.MaxRiskLevel = EAutonomixRiskLevel::Low;
	FAutonomixAction Action;
	Action.Description = Plan.Summary;
	Action.Category = EAutonomixActionCategory::General;
	Action.RiskLevel = EAutonomixRiskLevel::Low;
	Plan.Actions.Add(Action);
	return Plan;
}

FAutonomixActionResult FAutonomixDiagnosticsActions::ExecuteAction(const TSharedRef<FJsonObject>& Params)
{
	FAutonomixActionResult Result;
	Result.bSuccess = false;
	return ExecuteReadMessageLog(Params, Result);
}

// ============================================================================
// read_message_log
// ============================================================================

FAutonomixActionResult FAutonomixDiagnosticsActions::ExecuteReadMessageLog(
	const TSharedRef<FJsonObject>& Params, FAutonomixActionResult& Result)
{
	if (!GAutonomixLogCapture.IsValid())
	{
		Result.Errors.Add(TEXT("Log capture device is not initialized."));
		return Result;
	}

	// Parse optional filters
	int32 MaxLines = 100;
	Params->TryGetNumberField(TEXT("max_lines"), MaxLines);
	MaxLines = FMath::Clamp(MaxLines, 1, 500);

	FString CategoryFilter;
	Params->TryGetStringField(TEXT("category_filter"), CategoryFilter);

	FString SeverityFilter;
	Params->TryGetStringField(TEXT("severity_filter"), SeverityFilter);

	bool bErrorsOnly = false;
	if (SeverityFilter.Equals(TEXT("Error"), ESearchCase::IgnoreCase))
		bErrorsOnly = true;

	bool bWarningsAndErrors = false;
	if (SeverityFilter.Equals(TEXT("Warning"), ESearchCase::IgnoreCase))
		bWarningsAndErrors = true;

	// Filter and format entries
	FString Output = TEXT("=== Output Log ===\n");
	int32 OutputCount = 0;
	int32 TotalErrors = 0;
	int32 TotalWarnings = 0;

	// Read from end (most recent first)
	const auto& Entries = GAutonomixLogCapture->Entries;
	int32 StartIdx = FMath::Max(0, Entries.Num() - MaxLines * 2); // Over-read to account for filtering

	for (int32 i = Entries.Num() - 1; i >= StartIdx && OutputCount < MaxLines; --i)
	{
		const auto& Entry = Entries[i];

		// Count stats
		if (Entry.Verbosity == ELogVerbosity::Error || Entry.Verbosity == ELogVerbosity::Fatal)
			TotalErrors++;
		if (Entry.Verbosity == ELogVerbosity::Warning)
			TotalWarnings++;

		// Apply category filter
		if (!CategoryFilter.IsEmpty() && !Entry.Category.Contains(CategoryFilter, ESearchCase::IgnoreCase))
			continue;

		// Apply severity filter
		if (bErrorsOnly && Entry.Verbosity != ELogVerbosity::Error && Entry.Verbosity != ELogVerbosity::Fatal)
			continue;
		if (bWarningsAndErrors && Entry.Verbosity != ELogVerbosity::Error
			&& Entry.Verbosity != ELogVerbosity::Fatal && Entry.Verbosity != ELogVerbosity::Warning)
			continue;

		// Format the entry
		FString Severity;
		switch (Entry.Verbosity)
		{
		case ELogVerbosity::Fatal:   Severity = TEXT("FATAL"); break;
		case ELogVerbosity::Error:   Severity = TEXT("ERROR"); break;
		case ELogVerbosity::Warning: Severity = TEXT("WARN "); break;
		default:                     Severity = TEXT("LOG  "); break;
		}

		// Truncate very long messages
		FString Msg = Entry.Message.Left(500);
		Output += FString::Printf(TEXT("[%s] %s: %s\n"), *Severity, *Entry.Category, *Msg);
		OutputCount++;
	}

	if (OutputCount == 0)
	{
		Output += TEXT("  (no matching log entries found)\n");
	}

	Output += FString::Printf(TEXT("\n--- Showing %d of %d total entries | %d errors, %d warnings ---\n"),
		OutputCount, Entries.Num(), TotalErrors, TotalWarnings);

	// Optionally clear after reading
	bool bClearAfterRead = false;
	Params->TryGetBoolField(TEXT("clear_after_read"), bClearAfterRead);
	if (bClearAfterRead)
	{
		GAutonomixLogCapture->Entries.Empty();
		Output += TEXT("(log buffer cleared)\n");
	}

	Result.bSuccess = true;
	Result.ResultMessage = Output;
	return Result;
}

#undef LOCTEXT_NAMESPACE
