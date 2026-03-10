// Copyright Autonomix. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/**
 * A single parsed code declaration from a UE source file.
 */
struct AUTONOMIXENGINE_API FAutonomixCodeDeclaration
{
	/** Line number (1-based) */
	int32 LineNumber = 0;

	/** End line number for multi-line declarations */
	int32 EndLineNumber = 0;

	/** The declaration text (signature only, body stripped) */
	FString Declaration;

	/** Category: Class, Function, Property, Enum, Struct, Interface */
	FString Category;
};

/**
 * Result of parsing a single file's code structure.
 */
struct AUTONOMIXENGINE_API FAutonomixFileStructure
{
	/** Project-relative file path */
	FString RelativePath;

	/** All parsed declarations in order */
	TArray<FAutonomixCodeDeclaration> Declarations;

	/** Whether parsing succeeded */
	bool bSuccess = false;

	/** Error message if parsing failed */
	FString ErrorMessage;

	/**
	 * Format as a "folded" (signatures only) view.
	 * Matches Roo Code's foldedFileContext.ts output format:
	 * "1--15 | class AMyActor : public AActor"
	 * "20--25 | void BeginPlay() override"
	 */
	FString ToFoldedString() const;
};

/**
 * Parses UE C++ and Blueprint header files to extract code structure.
 *
 * This is Autonomix's equivalent of Roo Code's tree-sitter service
 * (`src/services/tree-sitter/index.ts`).
 *
 * Instead of tree-sitter (which requires a native binary), we use regex
 * patterns to extract UE-specific declarations. This approach is simpler
 * and covers 95% of the value for UE projects.
 *
 * EXTRACTED DECLARATIONS:
 *   - UCLASS() declarations and class signatures
 *   - USTRUCT() declarations
 *   - UENUM() declarations
 *   - UINTERFACE() declarations
 *   - UPROPERTY() member variables
 *   - UFUNCTION() function signatures (strips body)
 *   - Regular member function declarations (in class body)
 *   - #define macros
 *   - typedef / using declarations
 *
 * USAGE (by AutonomixContextCondenser after condensation):
 *   TArray<FString> FilesRead = FileContextTracker->GetTrackedFilePaths();
 *   for (const FString& File : FilesRead) {
 *       FAutonomixFileStructure Structure = Parser.ParseFile(File);
 *       // Inject Structure.ToFoldedString() into system prompt as <system-reminder>
 *   }
 *
 * OUTPUT FORMAT (Roo Code compatible):
 *   ## File Context: Source/MyActor.h
 *   1--5 | UCLASS(BlueprintType)
 *   6--45 | class AMyActor : public AActor
 *   10--10 | UPROPERTY(EditAnywhere) float Health
 *   15--18 | UFUNCTION(BlueprintCallable) void Jump()
 */
class AUTONOMIXENGINE_API FAutonomixCodeStructureParser
{
public:
	FAutonomixCodeStructureParser();

	/** Minimum number of lines a function body must have to be included (default: 3) */
	int32 MinBodyLines = 3;

	/** Maximum characters per declaration line (truncate longer lines) */
	int32 MaxDeclarationLineLength = 200;

	/**
	 * Parse a single file and extract code structure.
	 *
	 * @param AbsolutePath  Absolute path to the file
	 * @param RelativePath  Project-relative path (for display)
	 * @return Parsed structure with all declarations
	 */
	FAutonomixFileStructure ParseFile(const FString& AbsolutePath, const FString& RelativePath) const;

	/**
	 * Parse multiple files and generate a combined folded context string.
	 * Used to inject code structure after context condensation.
	 *
	 * @param FilePaths     Array of (AbsPath, RelPath) pairs
	 * @param MaxCharacters Maximum total characters (default: 50000)
	 * @return Combined folded context in <file-context> blocks
	 */
	FString GenerateFoldedContext(
		const TArray<TPair<FString, FString>>& FilePaths,
		int32 MaxCharacters = 50000
	) const;

	/**
	 * Check if a file type is supported for parsing.
	 * Supported: .h, .cpp, .cs (C# for build rules)
	 */
	static bool IsSupportedFileType(const FString& FilePath);

private:
	/** Parse a C++ header/source file */
	FAutonomixFileStructure ParseCppFile(const FString& Content, const FString& RelativePath) const;

	/** Extract a function signature (removes body if present) */
	FString ExtractFunctionSignature(const FString& Line) const;

	/** Check if a line is a UCLASS/USTRUCT/UENUM macro line */
	static bool IsUEMacroLine(const FString& Line);

	/** Check if a line starts a class/struct body */
	static bool IsClassDeclarationLine(const FString& Line);

	/** Check if a line is a UPROPERTY member */
	static bool IsUPropertyLine(const FString& Line);

	/** Check if a line is a UFUNCTION member */
	static bool IsUFunctionLine(const FString& Line);

	/** Check if a line is a regular function declaration */
	static bool IsFunctionDeclarationLine(const FString& Line);

	/** Truncate a line if too long */
	FString TruncateDeclaration(const FString& Line) const;
};
