// Copyright Autonomix. All Rights Reserved.

#include "AutonomixExecutionJournal.h"
#include "AutonomixCoreModule.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/SecureHash.h"
#include "Misc/PackageName.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

FAutonomixExecutionJournal::FAutonomixExecutionJournal()
	: bDirty(false)
{
}

FAutonomixExecutionJournal::~FAutonomixExecutionJournal()
{
	if (bDirty)
	{
		FlushToDisk();
	}
}

void FAutonomixExecutionJournal::RecordExecution(const FAutonomixActionExecutionRecord& Record)
{
	SessionRecords.Add(Record);
	bDirty = true;

	UE_LOG(LogAutonomix, Log, TEXT("ExecutionJournal: Recorded %s — %s (success=%s, preHash=%s, postHash=%s)"),
		*Record.ToolName, *Record.ResultMessage.Left(100),
		Record.bSuccess ? TEXT("true") : TEXT("false"),
		*Record.PreStateHash.Left(8),
		*Record.PostStateHash.Left(8));

	// Auto-flush every 10 records
	if (SessionRecords.Num() % 10 == 0)
	{
		FlushToDisk();
	}
}

void FAutonomixExecutionJournal::FlushToDisk()
{
	FString FilePath = GetJournalFilePath();

	// Ensure directory exists
	FString Dir = FPaths::GetPath(FilePath);
	IFileManager::Get().MakeDirectory(*Dir, true);

	// Build JSON array of all session records
	TArray<TSharedPtr<FJsonValue>> RecordsArray;
	for (const FAutonomixActionExecutionRecord& Record : SessionRecords)
	{
		TSharedPtr<FJsonObject> RecordJson = RecordToJson(Record);
		RecordsArray.Add(MakeShared<FJsonValueObject>(RecordJson));
	}

	// Wrap in root object
	TSharedPtr<FJsonObject> RootObj = MakeShared<FJsonObject>();
	RootObj->SetStringField(TEXT("session_start"), SessionRecords.Num() > 0
		? SessionRecords[0].Timestamp.ToIso8601() : FDateTime::UtcNow().ToIso8601());
	RootObj->SetNumberField(TEXT("record_count"), SessionRecords.Num());
	RootObj->SetArrayField(TEXT("records"), RecordsArray);

	// Serialize to string
	FString OutputString;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutputString);
	FJsonSerializer::Serialize(RootObj.ToSharedRef(), Writer);

	// Write to file
	FFileHelper::SaveStringToFile(OutputString, *FilePath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
	bDirty = false;

	UE_LOG(LogAutonomix, Log, TEXT("ExecutionJournal: Flushed %d records to %s"), SessionRecords.Num(), *FilePath);
}

bool FAutonomixExecutionJournal::LoadFromDisk()
{
	FString FilePath = GetJournalFilePath();
	if (!FPaths::FileExists(FilePath)) return false;

	FString JsonString;
	if (!FFileHelper::LoadFileToString(JsonString, *FilePath)) return false;

	TSharedPtr<FJsonObject> RootObj;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);
	if (!FJsonSerializer::Deserialize(Reader, RootObj) || !RootObj.IsValid()) return false;

	UE_LOG(LogAutonomix, Log, TEXT("ExecutionJournal: Loaded journal with %d records"),
		RootObj->GetIntegerField(TEXT("record_count")));
	return true;
}

FString FAutonomixExecutionJournal::GetJournalFilePath() const
{
	return FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Autonomix"),
		TEXT("ExecutionLog_") + FDateTime::UtcNow().ToString(TEXT("%Y%m%d")) + TEXT(".json"));
}

void FAutonomixExecutionJournal::ClearSession()
{
	SessionRecords.Empty();
	bDirty = false;
}

FString FAutonomixExecutionJournal::BuildRecentActionsSummary(int32 Count) const
{
	FString Summary = TEXT("=== Recent Actions ===\n");
	int32 Start = FMath::Max(0, SessionRecords.Num() - Count);

	for (int32 i = Start; i < SessionRecords.Num(); ++i)
	{
		const FAutonomixActionExecutionRecord& Record = SessionRecords[i];
		Summary += FString::Printf(TEXT("[%s] %s: %s (%s)\n"),
			*Record.Timestamp.ToString(TEXT("%H:%M:%S")),
			*Record.ToolName,
			*Record.ResultMessage.Left(80),
			Record.bSuccess ? TEXT("OK") : TEXT("FAIL"));
	}

	return Summary;
}

// ============================================================================
// State Hashing for Deterministic Verification (ChatGPT enterprise-grade fix)
// ============================================================================

FString FAutonomixExecutionJournal::ComputeFileHash(const FString& FilePath)
{
	if (!FPaths::FileExists(FilePath))
	{
		return TEXT("FILE_NOT_FOUND");
	}

	TArray<uint8> FileData;
	if (!FFileHelper::LoadFileToArray(FileData, *FilePath))
	{
		return TEXT("HASH_FAILED");
	}

	// Compute SHA-1 (via FSHA1::HashBuffer)
	FSHAHash Hash;
	FSHA1::HashBuffer(FileData.GetData(), FileData.Num(), Hash.Hash);
	return Hash.ToString();
}

FString FAutonomixExecutionJournal::ComputeAssetHash(const FString& AssetPath)
{
	// Convert content path to disk file path
	FString PackageFilename;
	if (FPackageName::TryConvertLongPackageNameToFilename(AssetPath, PackageFilename, FPackageName::GetAssetPackageExtension()))
	{
		return ComputeFileHash(PackageFilename);
	}
	return TEXT("NO_DISK_PATH");
}

TSharedPtr<FJsonObject> FAutonomixExecutionJournal::RecordToJson(const FAutonomixActionExecutionRecord& Record) const
{
	TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
	Obj->SetStringField(TEXT("record_id"), Record.RecordId.ToString());
	Obj->SetStringField(TEXT("tool_name"), Record.ToolName);
	Obj->SetStringField(TEXT("tool_use_id"), Record.ToolUseId);
	Obj->SetStringField(TEXT("input_json"), Record.InputJson);
	Obj->SetStringField(TEXT("result_message"), Record.ResultMessage);
	Obj->SetBoolField(TEXT("success"), Record.bSuccess);
	Obj->SetBoolField(TEXT("is_error"), Record.bIsError);
	Obj->SetStringField(TEXT("timestamp"), Record.Timestamp.ToIso8601());
	Obj->SetNumberField(TEXT("execution_time_seconds"), Record.ExecutionTimeSeconds);

	// State hashing for integrity verification
	if (!Record.PreStateHash.IsEmpty())
	{
		Obj->SetStringField(TEXT("pre_state_hash"), Record.PreStateHash);
	}
	if (!Record.PostStateHash.IsEmpty())
	{
		Obj->SetStringField(TEXT("post_state_hash"), Record.PostStateHash);
	}

	TArray<TSharedPtr<FJsonValue>> FilesArray;
	for (const FString& F : Record.ModifiedFiles) FilesArray.Add(MakeShared<FJsonValueString>(F));
	Obj->SetArrayField(TEXT("modified_files"), FilesArray);

	TArray<TSharedPtr<FJsonValue>> AssetsArray;
	for (const FString& A : Record.ModifiedAssets) AssetsArray.Add(MakeShared<FJsonValueString>(A));
	Obj->SetArrayField(TEXT("modified_assets"), AssetsArray);

	return Obj;
}
