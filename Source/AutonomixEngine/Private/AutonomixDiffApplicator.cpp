// Copyright Autonomix. All Rights Reserved.

#include "AutonomixDiffApplicator.h"
#include "Misc/Paths.h"

FAutonomixDiffApplicator::FAutonomixDiffApplicator()
{
}

// ============================================================================
// Public API
// ============================================================================

FAutonomixDiffApplyResult FAutonomixDiffApplicator::ApplyDiff(
	const FString& OriginalContent,
	const FString& DiffText,
	float FuzzyThreshold
) const
{
	TArray<FAutonomixSearchReplaceBlock> Blocks;
	if (!ParseDiffBlocks(DiffText, Blocks))
	{
		FAutonomixDiffApplyResult Failure;
		Failure.bSuccess = false;
		Failure.ErrorMessage = TEXT("Failed to parse diff blocks. Expected <<<<<<< SEARCH / ======= / >>>>>>> REPLACE format.");
		return Failure;
	}

	return ApplyBlocks(OriginalContent, Blocks, FuzzyThreshold);
}

FAutonomixDiffApplyResult FAutonomixDiffApplicator::ApplyBlocks(
	const FString& OriginalContent,
	const TArray<FAutonomixSearchReplaceBlock>& Blocks,
	float FuzzyThreshold
) const
{
	if (Blocks.Num() == 0)
	{
		FAutonomixDiffApplyResult Failure;
		Failure.bSuccess = false;
		Failure.ErrorMessage = TEXT("No search/replace blocks provided.");
		return Failure;
	}

	// Apply blocks sequentially — each block operates on the result of the previous
	FString RunningContent = OriginalContent;

	for (int32 i = 0; i < Blocks.Num(); i++)
	{
		FAutonomixDiffApplyResult BlockResult = ApplySingleBlock(RunningContent, Blocks[i], FuzzyThreshold);
		if (!BlockResult.bSuccess)
		{
			BlockResult.FailedBlockIndex = i;
			BlockResult.ErrorMessage = FString::Printf(
				TEXT("Block %d/%d failed: %s\nSearch content (first 200 chars):\n%s"),
				i + 1, Blocks.Num(),
				*BlockResult.ErrorMessage,
				*Blocks[i].SearchContent.Left(200)
			);
			return BlockResult;
		}
	}

	FAutonomixDiffApplyResult Success;
	Success.bSuccess = true;
	Success.ResultContent = RunningContent;
	return Success;
}

// ============================================================================
// Diff parsing
// ============================================================================

bool FAutonomixDiffApplicator::ParseDiffBlocks(const FString& DiffText, TArray<FAutonomixSearchReplaceBlock>& OutBlocks)
{
	OutBlocks.Empty();

	// Split into lines
	TArray<FString> Lines;
	DiffText.ParseIntoArrayLines(Lines, false);

	static const FString SearchMarker    = TEXT("<<<<<<< SEARCH");
	static const FString SepMarker       = TEXT("=======");
	static const FString ReplaceMarker   = TEXT(">>>>>>> REPLACE");
	static const FString LineHintPrefix  = TEXT(":start_line:");
	static const FString DashSep         = TEXT("-------");

	enum class EParseState { Outside, InSearch, InReplace };
	EParseState State = EParseState::Outside;

	FAutonomixSearchReplaceBlock CurrentBlock;
	TArray<FString> SearchLines;
	TArray<FString> ReplaceLines;

	for (const FString& Line : Lines)
	{
		const FString TrimmedLine = Line.TrimEnd();

		if (State == EParseState::Outside)
		{
			if (TrimmedLine == SearchMarker)
			{
				State = EParseState::InSearch;
				SearchLines.Empty();
				ReplaceLines.Empty();
				CurrentBlock = FAutonomixSearchReplaceBlock();
			}
		}
		else if (State == EParseState::InSearch)
		{
			if (TrimmedLine == SepMarker)
			{
				// Transition to replace section
				State = EParseState::InReplace;
			}
			else if (TrimmedLine.StartsWith(LineHintPrefix))
			{
				// Parse :start_line:N hint
				FString NumStr = TrimmedLine.Mid(LineHintPrefix.Len());
				NumStr.TrimStartAndEndInline();
				CurrentBlock.HintStartLine = FCString::Atoi(*NumStr);
			}
			else if (TrimmedLine == DashSep)
			{
				// Separator between hint and content — skip
			}
			else
			{
				SearchLines.Add(Line);  // Preserve original line (not trimmed)
			}
		}
		else if (State == EParseState::InReplace)
		{
			if (TrimmedLine == ReplaceMarker)
			{
				// End of block
				CurrentBlock.SearchContent = FString::Join(SearchLines, TEXT("\n"));
				CurrentBlock.ReplaceContent = FString::Join(ReplaceLines, TEXT("\n"));

				// Don't allow empty search blocks
				if (!CurrentBlock.SearchContent.IsEmpty())
				{
					OutBlocks.Add(CurrentBlock);
				}

				State = EParseState::Outside;
			}
			else
			{
				ReplaceLines.Add(Line);  // Preserve original line
			}
		}
	}

	return OutBlocks.Num() > 0;
}

// ============================================================================
// Single block application
// ============================================================================

FAutonomixDiffApplyResult FAutonomixDiffApplicator::ApplySingleBlock(
	FString& Content,
	const FAutonomixSearchReplaceBlock& Block,
	float FuzzyThreshold
) const
{
	FAutonomixDiffApplyResult Result;

	if (Block.SearchContent.IsEmpty())
	{
		// Empty search = insert at hint line (or append)
		if (Block.HintStartLine > 0)
		{
			TArray<FString> Lines;
			Content.ParseIntoArrayLines(Lines, false);

			int32 InsertAt = FMath::Clamp(Block.HintStartLine - 1, 0, Lines.Num());
			TArray<FString> ReplaceLines;
			Block.ReplaceContent.ParseIntoArrayLines(ReplaceLines, false);

			for (int32 i = ReplaceLines.Num() - 1; i >= 0; i--)
			{
				Lines.Insert(ReplaceLines[i], InsertAt);
			}

			Content = FString::Join(Lines, TEXT("\n"));
			Result.bSuccess = true;
		}
		else
		{
			Content += TEXT("\n") + Block.ReplaceContent;
			Result.bSuccess = true;
		}
		return Result;
	}

	// Try exact match first (fast path)
	const FString NormSearch = NormalizeForComparison(Block.SearchContent);
	const FString NormContent = NormalizeForComparison(Content);

	int32 ExactIdx = NormContent.Find(NormSearch, ESearchCase::CaseSensitive);
	if (ExactIdx != INDEX_NONE)
	{
		// Find the actual position in the original content
		// (NormContent and Content have same structure, just normalized whitespace)
		int32 ActualIdx = Content.Find(Block.SearchContent, ESearchCase::CaseSensitive);
		if (ActualIdx == INDEX_NONE)
		{
			// Normalized match but not exact — fall back to normalized replacement
			ActualIdx = ExactIdx;
			Content = Content.Left(ActualIdx) + Block.ReplaceContent + Content.Mid(ActualIdx + Block.SearchContent.Len());
		}
		else
		{
			Content = Content.Left(ActualIdx) + Block.ReplaceContent + Content.Mid(ActualIdx + Block.SearchContent.Len());
		}

		Result.bSuccess = true;
		Result.BestSimilarityScore = 1.0f;
		return Result;
	}

	// Exact match failed — try fuzzy matching
	TArray<FString> Lines;
	Content.ParseIntoArrayLines(Lines, false);

	TArray<FString> SearchLines;
	Block.SearchContent.ParseIntoArrayLines(SearchLines, false);
	const int32 SearchLen = SearchLines.Num();

	if (SearchLen == 0)
	{
		Result.bSuccess = false;
		Result.ErrorMessage = TEXT("Search content produced no lines after splitting.");
		return Result;
	}

	// Determine search window
	int32 WindowStart = 0;
	int32 WindowEnd = Lines.Num();

	if (Block.HintStartLine > 0)
	{
		int32 HintIdx = Block.HintStartLine - 1;  // Convert to 0-based
		WindowStart = FMath::Max(0, HintIdx - BufferLines);
		WindowEnd = FMath::Min(Lines.Num(), HintIdx + BufferLines + SearchLen);
	}

	float BestScore = 0.0f;
	int32 BestLine = -1;
	FString BestContent;

	if (!FindBestMatch(Lines, Block.SearchContent, Block.HintStartLine > 0 ? Block.HintStartLine - 1 : Lines.Num() / 2,
		WindowStart, WindowEnd, FuzzyThreshold, BestScore, BestLine, BestContent))
	{
		// If windowed search failed and we had a hint, try full file search
		if (Block.HintStartLine > 0)
		{
			FindBestMatch(Lines, Block.SearchContent, Lines.Num() / 2,
				0, Lines.Num(), FuzzyThreshold, BestScore, BestLine, BestContent);
		}
	}

	if (BestLine >= 0 && BestScore >= FuzzyThreshold)
	{
		// Replace the matched region
		TArray<FString> NewLines;
		NewLines.Append(Lines.GetData(), BestLine);

		TArray<FString> ReplaceLines;
		Block.ReplaceContent.ParseIntoArrayLines(ReplaceLines, false);
		NewLines.Append(ReplaceLines);

		NewLines.Append(Lines.GetData() + BestLine + SearchLen, Lines.Num() - BestLine - SearchLen);

		Content = FString::Join(NewLines, TEXT("\n"));

		// Preserve trailing newline if original had one
		if (Lines.Num() > 0 && (Content.EndsWith(TEXT("\n")) != (Lines.Last().IsEmpty())))
		{
			// Keep consistent
		}

		Result.bSuccess = true;
		Result.BestSimilarityScore = BestScore;
		Result.MatchedLine = BestLine + 1;  // Convert to 1-based
	}
	else
	{
		Result.bSuccess = false;
		Result.BestSimilarityScore = BestScore;
		Result.ErrorMessage = FString::Printf(
			TEXT("Could not find a match for the search block (best similarity: %.1f%%, threshold: %.1f%%).\n"
			     "The file content may have changed since the search was generated."),
			BestScore * 100.0f,
			FuzzyThreshold * 100.0f
		);
	}

	return Result;
}

// ============================================================================
// Fuzzy matching
// ============================================================================

bool FAutonomixDiffApplicator::FindBestMatch(
	const TArray<FString>& Lines,
	const FString& SearchChunk,
	int32 HintLine,
	int32 WindowStart,
	int32 WindowEnd,
	float Threshold,
	float& OutScore,
	int32& OutMatchLine,
	FString& OutMatchContent
) const
{
	TArray<FString> SearchLines;
	SearchChunk.ParseIntoArrayLines(SearchLines, false);
	const int32 SearchLen = SearchLines.Num();

	OutScore = 0.0f;
	OutMatchLine = -1;
	OutMatchContent.Empty();

	if (WindowEnd - WindowStart < SearchLen)
	{
		return false;
	}

	const int32 MaxStart = FMath::Min(WindowEnd - SearchLen, Lines.Num() - SearchLen);
	const int32 MinStart = FMath::Max(WindowStart, 0);

	if (MaxStart < MinStart)
	{
		return false;
	}

	// Middle-out search: start at hint, expand outward
	const int32 MidPoint = FMath::Clamp(HintLine, MinStart, MaxStart);
	int32 LeftIdx = MidPoint;
	int32 RightIdx = MidPoint + 1;

	while (LeftIdx >= MinStart || RightIdx <= MaxStart)
	{
		auto CheckLine = [&](int32 Idx)
		{
			if (Idx < MinStart || Idx > MaxStart) return;

			// Extract chunk from file at this position
			TArray<FString> Chunk;
			for (int32 j = Idx; j < Idx + SearchLen && j < Lines.Num(); j++)
			{
				Chunk.Add(Lines[j]);
			}
			FString ChunkStr = FString::Join(Chunk, TEXT("\n"));
			float Score = ComputeSimilarity(ChunkStr, SearchChunk);

			if (Score > OutScore)
			{
				OutScore = Score;
				OutMatchLine = Idx;
				OutMatchContent = ChunkStr;

				if (Score >= Threshold)
				{
					return; // Early exit on exact-enough match
				}
			}
		};

		if (LeftIdx >= MinStart)
		{
			CheckLine(LeftIdx);
			LeftIdx--;
		}

		if (RightIdx <= MaxStart)
		{
			CheckLine(RightIdx);
			RightIdx++;
		}

		// Early exit if we found a perfect match
		if (OutScore >= 1.0f)
		{
			break;
		}
	}

	return OutMatchLine >= 0 && OutScore >= Threshold;
}

// ============================================================================
// String utilities
// ============================================================================

float FAutonomixDiffApplicator::ComputeSimilarity(const FString& A, const FString& B)
{
	if (A.IsEmpty() && B.IsEmpty()) return 1.0f;
	if (A.IsEmpty() || B.IsEmpty()) return 0.0f;

	const FString NormA = NormalizeForComparison(A);
	const FString NormB = NormalizeForComparison(B);

	if (NormA == NormB) return 1.0f;

	const int32 Dist = LevenshteinDistance(NormA, NormB);
	const int32 MaxLen = FMath::Max(NormA.Len(), NormB.Len());

	if (MaxLen == 0) return 1.0f;

	return 1.0f - (float)Dist / (float)MaxLen;
}

int32 FAutonomixDiffApplicator::LevenshteinDistance(const FString& A, const FString& B)
{
	const int32 LenA = A.Len();
	const int32 LenB = B.Len();

	if (LenA == 0) return LenB;
	if (LenB == 0) return LenA;

	// Space-optimized: use two rows
	TArray<int32> PrevRow, CurrRow;
	PrevRow.SetNumUninitialized(LenB + 1);
	CurrRow.SetNumUninitialized(LenB + 1);

	for (int32 j = 0; j <= LenB; j++) PrevRow[j] = j;

	for (int32 i = 1; i <= LenA; i++)
	{
		CurrRow[0] = i;
		for (int32 j = 1; j <= LenB; j++)
		{
			int32 Cost = (A[i - 1] == B[j - 1]) ? 0 : 1;
			CurrRow[j] = FMath::Min3(
				CurrRow[j - 1] + 1,     // Insert
				PrevRow[j] + 1,          // Delete
				PrevRow[j - 1] + Cost    // Replace
			);
		}
		Swap(PrevRow, CurrRow);
	}

	return PrevRow[LenB];
}

FString FAutonomixDiffApplicator::NormalizeForComparison(const FString& Text)
{
	FString Result = Text;

	// Normalize line endings
	Result.ReplaceInline(TEXT("\r\n"), TEXT("\n"));
	Result.ReplaceInline(TEXT("\r"), TEXT("\n"));

	// Replace common smart quotes with straight quotes
	Result.ReplaceInline(TEXT("\u2018"), TEXT("'"));  // Left single quotation mark
	Result.ReplaceInline(TEXT("\u2019"), TEXT("'"));  // Right single quotation mark
	Result.ReplaceInline(TEXT("\u201C"), TEXT("\"")); // Left double quotation mark
	Result.ReplaceInline(TEXT("\u201D"), TEXT("\"")); // Right double quotation mark
	Result.ReplaceInline(TEXT("\u2014"), TEXT("--")); // Em dash
	Result.ReplaceInline(TEXT("\u2013"), TEXT("-"));  // En dash

	// Trim trailing whitespace on each line
	TArray<FString> Lines;
	Result.ParseIntoArrayLines(Lines, false);
	for (FString& Line : Lines)
	{
		Line.TrimEndInline();
	}
	Result = FString::Join(Lines, TEXT("\n"));

	return Result;
}
