// Copyright Autonomix. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/**
 * A single search/replace block for multi-search-replace diff operations.
 */
struct AUTONOMIXENGINE_API FAutonomixSearchReplaceBlock
{
	/** The text to search for in the original file */
	FString SearchContent;

	/** The replacement text */
	FString ReplaceContent;

	/** Optional line hint for faster matching (0 = search entire file) */
	int32 HintStartLine = 0;

	/** Optional end line constraint (0 = no constraint) */
	int32 HintEndLine = 0;

	FAutonomixSearchReplaceBlock() = default;
	FAutonomixSearchReplaceBlock(const FString& InSearch, const FString& InReplace)
		: SearchContent(InSearch), ReplaceContent(InReplace) {}
};

/**
 * Result of applying a diff operation.
 */
struct AUTONOMIXENGINE_API FAutonomixDiffApplyResult
{
	/** Whether the operation succeeded */
	bool bSuccess = false;

	/** The resulting file content (only valid if bSuccess == true) */
	FString ResultContent;

	/** Best similarity score found (0.0-1.0); < threshold means failure */
	float BestSimilarityScore = 0.0f;

	/** Line number where the best match was found (-1 if not found) */
	int32 MatchedLine = -1;

	/** Human-readable error message (only valid if bSuccess == false) */
	FString ErrorMessage;

	/** Which block index failed (for multi-block operations) */
	int32 FailedBlockIndex = -1;
};

/**
 * Applies code edits using fuzzy multi-search-replace diff strategy.
 *
 * Ported and adapted from Roo Code's MultiSearchReplaceDiffStrategy.ts:
 *
 * ALGORITHM:
 *   1. Parse the diff into search/replace blocks (delimited by <<<<<<< SEARCH / ======= / >>>>>>> REPLACE)
 *   2. For each block, find the best matching region in the file using:
 *      a. Exact match first (O(n) scan)
 *      b. Fuzzy match via Levenshtein distance if exact fails
 *      c. "Middle-out" search starting from the hint line
 *   3. Apply all blocks in order (each block modifies the running result)
 *
 * WHY FUZZY MATCHING:
 *   UE C++ headers contain complex macros (UPROPERTY, UFUNCTION, GENERATED_BODY).
 *   Smart quote substitution, whitespace normalization, or minor rewording by
 *   Claude can cause exact matching to fail. Fuzzy matching prevents spurious
 *   failures when the search content is "close enough".
 *
 * SIMILARITY THRESHOLD:
 *   Default: 0.8 (80% similar). Below this threshold, the block fails.
 *   The threshold is configurable for stricter/looser matching.
 *
 * MULTI-BLOCK FORMAT (compatible with apply_diff tool):
 *   <<<<<<< SEARCH
 *   :start_line:42
 *   -------
 *   [original content]
 *   =======
 *   [replacement content]
 *   >>>>>>> REPLACE
 *
 * Usage:
 *   FAutonomixDiffApplicator Applicator;
 *   FAutonomixDiffApplyResult Result = Applicator.ApplyDiff(OriginalContent, DiffText);
 *   if (Result.bSuccess) { WriteFile(Result.ResultContent); }
 */
class AUTONOMIXENGINE_API FAutonomixDiffApplicator
{
public:
	/** Default fuzzy similarity threshold (0.0-1.0). Below this, a block fails. */
	static constexpr float DefaultFuzzyThreshold = 0.8f;

	/** Number of context lines to include in error diagnostics */
	static constexpr int32 BufferLines = 40;

	FAutonomixDiffApplicator();

	/**
	 * Apply a diff string in multi-search-replace format to original content.
	 * Parses the diff into blocks and applies them sequentially.
	 *
	 * @param OriginalContent  The current file content
	 * @param DiffText         Diff in <<<<<<< SEARCH / ======= / >>>>>>> REPLACE format
	 * @param FuzzyThreshold   Similarity threshold (default 0.8)
	 * @return                 Result with success/failure and result content
	 */
	FAutonomixDiffApplyResult ApplyDiff(
		const FString& OriginalContent,
		const FString& DiffText,
		float FuzzyThreshold = DefaultFuzzyThreshold
	) const;

	/**
	 * Apply pre-parsed search/replace blocks to original content.
	 *
	 * @param OriginalContent  The current file content
	 * @param Blocks           Pre-parsed search/replace blocks
	 * @param FuzzyThreshold   Similarity threshold (default 0.8)
	 * @return                 Result with success/failure and result content
	 */
	FAutonomixDiffApplyResult ApplyBlocks(
		const FString& OriginalContent,
		const TArray<FAutonomixSearchReplaceBlock>& Blocks,
		float FuzzyThreshold = DefaultFuzzyThreshold
	) const;

	/**
	 * Parse a diff string into search/replace blocks.
	 * Handles the <<<<<<< SEARCH / :start_line:N / ------- / ======= / >>>>>>> REPLACE format.
	 *
	 * @param DiffText  Raw diff string from Claude
	 * @param OutBlocks Parsed blocks
	 * @return true if at least one block was parsed successfully
	 */
	static bool ParseDiffBlocks(const FString& DiffText, TArray<FAutonomixSearchReplaceBlock>& OutBlocks);

	/**
	 * Compute string similarity using normalized Levenshtein distance.
	 * Returns 1.0 for identical strings, 0.0 for completely different strings.
	 *
	 * @param A  First string
	 * @param B  Second string
	 * @return   Similarity ratio [0.0, 1.0]
	 */
	static float ComputeSimilarity(const FString& A, const FString& B);

private:
	/**
	 * Apply a single search/replace block to content.
	 *
	 * @param Content          Running file content (modified in place on success)
	 * @param Block            The search/replace block
	 * @param FuzzyThreshold   Similarity threshold
	 * @return                 Result for this block
	 */
	FAutonomixDiffApplyResult ApplySingleBlock(
		FString& Content,
		const FAutonomixSearchReplaceBlock& Block,
		float FuzzyThreshold
	) const;

	/**
	 * Compute Levenshtein edit distance between two strings.
	 * Uses a space-optimized two-row DP implementation.
	 */
	static int32 LevenshteinDistance(const FString& A, const FString& B);

	/**
	 * Find the best match for SearchChunk in Lines using middle-out search.
	 * Starts at the hint line and expands outward to minimize search time
	 * when the hint is accurate.
	 *
	 * @param Lines          File split into lines
	 * @param SearchChunk    The content to search for (may be multi-line)
	 * @param HintLine       Starting search position (0-based line index)
	 * @param WindowStart    Start of allowed search window
	 * @param WindowEnd      End of allowed search window
	 * @param OutScore       Best similarity score found
	 * @param OutMatchLine   Best matching line index
	 * @param OutMatchContent The actual matched content
	 * @return               true if score >= threshold
	 */
	bool FindBestMatch(
		const TArray<FString>& Lines,
		const FString& SearchChunk,
		int32 HintLine,
		int32 WindowStart,
		int32 WindowEnd,
		float Threshold,
		float& OutScore,
		int32& OutMatchLine,
		FString& OutMatchContent
	) const;

	/**
	 * Normalize a string for comparison:
	 * - Trim trailing whitespace on each line
	 * - Normalize line endings to \n
	 * - Replace smart quotes with straight quotes
	 */
	static FString NormalizeForComparison(const FString& Text);
};
