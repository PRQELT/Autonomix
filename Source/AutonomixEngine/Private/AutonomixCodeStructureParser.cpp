// Copyright Autonomix. All Rights Reserved.

#include "AutonomixCodeStructureParser.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"

FAutonomixCodeStructureParser::FAutonomixCodeStructureParser()
{
}

// ============================================================================
// FAutonomixFileStructure
// ============================================================================

FString FAutonomixFileStructure::ToFoldedString() const
{
	if (!bSuccess || Declarations.Num() == 0)
	{
		return FString();
	}

	FString Result = FString::Printf(TEXT("## File Context: %s\n"), *RelativePath);

	for (const FAutonomixCodeDeclaration& Decl : Declarations)
	{
		if (Decl.EndLineNumber > Decl.LineNumber)
		{
			Result += FString::Printf(TEXT("%d--%d | %s\n"),
				Decl.LineNumber, Decl.EndLineNumber, *Decl.Declaration);
		}
		else
		{
			Result += FString::Printf(TEXT("%d | %s\n"),
				Decl.LineNumber, *Decl.Declaration);
		}
	}

	return Result;
}

// ============================================================================
// FAutonomixCodeStructureParser
// ============================================================================

bool FAutonomixCodeStructureParser::IsSupportedFileType(const FString& FilePath)
{
	FString Ext = FPaths::GetExtension(FilePath).ToLower();
	return Ext == TEXT("h") || Ext == TEXT("cpp") || Ext == TEXT("cs");
}

FAutonomixFileStructure FAutonomixCodeStructureParser::ParseFile(
	const FString& AbsolutePath,
	const FString& RelativePath) const
{
	FAutonomixFileStructure Result;
	Result.RelativePath = RelativePath;

	if (!IsSupportedFileType(AbsolutePath))
	{
		Result.bSuccess = false;
		Result.ErrorMessage = FString::Printf(TEXT("Unsupported file type: %s"),
			*FPaths::GetExtension(AbsolutePath));
		return Result;
	}

	if (!FPaths::FileExists(AbsolutePath))
	{
		Result.bSuccess = false;
		Result.ErrorMessage = TEXT("File does not exist");
		return Result;
	}

	FString Content;
	if (!FFileHelper::LoadFileToString(Content, *AbsolutePath))
	{
		Result.bSuccess = false;
		Result.ErrorMessage = TEXT("Failed to read file");
		return Result;
	}

	return ParseCppFile(Content, RelativePath);
}

FAutonomixFileStructure FAutonomixCodeStructureParser::ParseCppFile(
	const FString& Content,
	const FString& RelativePath) const
{
	FAutonomixFileStructure Result;
	Result.RelativePath = RelativePath;
	Result.bSuccess = true;

	TArray<FString> Lines;
	Content.ParseIntoArrayLines(Lines, false);

	bool bInBlockComment = false;
	int32 BraceDepth = 0;
	bool bPrevWasUEMacro = false;
	bool bPrevWasUProperty = false;
	bool bPrevWasUFunction = false;
	FString PendingUEMacroLine;
	int32 PendingMacroLineNum = 0;

	for (int32 i = 0; i < Lines.Num(); i++)
	{
		const FString& Line = Lines[i];
		const FString TrimmedLine = Line.TrimStartAndEnd();
		const int32 LineNum = i + 1;

		// Skip empty lines
		if (TrimmedLine.IsEmpty()) continue;

		// Track block comments
		if (!bInBlockComment && TrimmedLine.Contains(TEXT("/*")))
		{
			if (!TrimmedLine.Contains(TEXT("*/")))
			{
				bInBlockComment = true;
			}
			continue;
		}
		if (bInBlockComment)
		{
			if (TrimmedLine.Contains(TEXT("*/")))
			{
				bInBlockComment = false;
			}
			continue;
		}

		// Skip single-line comments
		if (TrimmedLine.StartsWith(TEXT("//"))) continue;

		// Skip preprocessor includes and pragmas (keep #define)
		if (TrimmedLine.StartsWith(TEXT("#include")) ||
			TrimmedLine.StartsWith(TEXT("#pragma")))
		{
			continue;
		}

		// Track braces for nesting
		for (TCHAR Ch : TrimmedLine)
		{
			if (Ch == TEXT('{')) BraceDepth++;
			else if (Ch == TEXT('}')) BraceDepth = FMath::Max(0, BraceDepth - 1);
		}

		// UCLASS / USTRUCT / UENUM / UINTERFACE macros
		if (IsUEMacroLine(TrimmedLine))
		{
			bPrevWasUEMacro = true;
			bPrevWasUProperty = false;
			bPrevWasUFunction = false;
			PendingUEMacroLine = TrimmedLine;
			PendingMacroLineNum = LineNum;
			continue;
		}

		// Class/struct declaration following a UE macro
		if (bPrevWasUEMacro && IsClassDeclarationLine(TrimmedLine))
		{
			FAutonomixCodeDeclaration Decl;
			Decl.LineNumber = PendingMacroLineNum;
			Decl.EndLineNumber = LineNum;
			Decl.Declaration = TruncateDeclaration(PendingUEMacroLine + TEXT(" | ") + TrimmedLine.Replace(TEXT("{"), TEXT("{")));
			Decl.Category = TEXT("Class");
			Result.Declarations.Add(Decl);
			bPrevWasUEMacro = false;
			continue;
		}

		// UPROPERTY declarations
		if (IsUPropertyLine(TrimmedLine))
		{
			bPrevWasUProperty = true;
			bPrevWasUEMacro = false;
			bPrevWasUFunction = false;
			PendingUEMacroLine = TrimmedLine;
			PendingMacroLineNum = LineNum;
			continue;
		}

		// Member variable following UPROPERTY
		if (bPrevWasUProperty && BraceDepth >= 1)
		{
			FString Combined = PendingUEMacroLine + TEXT(" ") + TrimmedLine;
			FAutonomixCodeDeclaration Decl;
			Decl.LineNumber = PendingMacroLineNum;
			Decl.EndLineNumber = LineNum;
			Decl.Declaration = TruncateDeclaration(Combined);
			Decl.Category = TEXT("Property");
			Result.Declarations.Add(Decl);
			bPrevWasUProperty = false;
			continue;
		}

		// UFUNCTION declarations
		if (IsUFunctionLine(TrimmedLine))
		{
			bPrevWasUFunction = true;
			bPrevWasUEMacro = false;
			bPrevWasUProperty = false;
			PendingUEMacroLine = TrimmedLine;
			PendingMacroLineNum = LineNum;
			continue;
		}

		// Function signature following UFUNCTION
		if (bPrevWasUFunction && BraceDepth >= 1)
		{
			FString Sig = ExtractFunctionSignature(TrimmedLine);
			FAutonomixCodeDeclaration Decl;
			Decl.LineNumber = PendingMacroLineNum;
			Decl.EndLineNumber = LineNum;
			Decl.Declaration = TruncateDeclaration(PendingUEMacroLine + TEXT(" ") + Sig);
			Decl.Category = TEXT("Function");
			Result.Declarations.Add(Decl);
			bPrevWasUFunction = false;
			continue;
		}

		// Regular (non-UE) function declarations in class body (depth >= 1)
		if (BraceDepth >= 1 && IsFunctionDeclarationLine(TrimmedLine))
		{
			FString Sig = ExtractFunctionSignature(TrimmedLine);
			if (!Sig.IsEmpty())
			{
				FAutonomixCodeDeclaration Decl;
				Decl.LineNumber = LineNum;
				Decl.EndLineNumber = LineNum;
				Decl.Declaration = TruncateDeclaration(Sig);
				Decl.Category = TEXT("Function");
				Result.Declarations.Add(Decl);
			}
		}

		bPrevWasUEMacro = false;
		bPrevWasUProperty = false;
		bPrevWasUFunction = false;
	}

	return Result;
}

FString FAutonomixCodeStructureParser::GenerateFoldedContext(
	const TArray<TPair<FString, FString>>& FilePaths,
	int32 MaxCharacters) const
{
	FString Combined;
	int32 TotalChars = 0;
	int32 FilesProcessed = 0;
	int32 FilesSkipped = 0;

	for (const TPair<FString, FString>& FilePair : FilePaths)
	{
		const FString& AbsPath = FilePair.Key;
		const FString& RelPath = FilePair.Value;

		if (!IsSupportedFileType(AbsPath))
		{
			FilesSkipped++;
			continue;
		}

		FAutonomixFileStructure Structure = ParseFile(AbsPath, RelPath);
		if (!Structure.bSuccess || Structure.Declarations.Num() == 0)
		{
			FilesSkipped++;
			continue;
		}

		FString FoldedSection = Structure.ToFoldedString();
		if (FoldedSection.IsEmpty())
		{
			FilesSkipped++;
			continue;
		}

		// Check character budget
		if (TotalChars + FoldedSection.Len() > MaxCharacters)
		{
			FilesSkipped++;
			continue;
		}

		// Wrap in <file-context> block (like Roo Code's <system-reminder>)
		FString WrappedSection = TEXT("<file-context>\n") + FoldedSection + TEXT("</file-context>\n\n");
		Combined += WrappedSection;
		TotalChars += WrappedSection.Len();
		FilesProcessed++;
	}

	if (Combined.IsEmpty())
	{
		return FString();
	}

	return FString::Printf(
		TEXT("# Code Structure Context (%d files, %d chars)\n\n"),
		FilesProcessed, TotalChars
	) + Combined;
}

// ============================================================================
// Static helper methods
// ============================================================================

bool FAutonomixCodeStructureParser::IsUEMacroLine(const FString& Line)
{
	return Line.StartsWith(TEXT("UCLASS(")) ||
		   Line.StartsWith(TEXT("UCLASS()")) ||
		   Line.StartsWith(TEXT("USTRUCT(")) ||
		   Line.StartsWith(TEXT("UENUM(")) ||
		   Line.StartsWith(TEXT("UINTERFACE(")) ||
		   Line.StartsWith(TEXT("UDELEGATE(")) ||
		   Line.StartsWith(TEXT("GENERATED_BODY()")) ||
		   Line.StartsWith(TEXT("GENERATED_UCLASS_BODY()"));
}

bool FAutonomixCodeStructureParser::IsClassDeclarationLine(const FString& Line)
{
	return Line.StartsWith(TEXT("class ")) ||
		   Line.StartsWith(TEXT("struct ")) ||
		   Line.StartsWith(TEXT("enum class ")) ||
		   Line.StartsWith(TEXT("enum "));
}

bool FAutonomixCodeStructureParser::IsUPropertyLine(const FString& Line)
{
	return Line.StartsWith(TEXT("UPROPERTY(")) || Line.StartsWith(TEXT("UPROPERTY()"));
}

bool FAutonomixCodeStructureParser::IsUFunctionLine(const FString& Line)
{
	return Line.StartsWith(TEXT("UFUNCTION(")) || Line.StartsWith(TEXT("UFUNCTION()"));
}

bool FAutonomixCodeStructureParser::IsFunctionDeclarationLine(const FString& Line)
{
	// Must contain () but not be a call (no = at end, no ; after simple identifier)
	if (!Line.Contains(TEXT("("))) return false;

	// Skip common non-declaration patterns
	if (Line.Contains(TEXT("if (")) ||
		Line.Contains(TEXT("while (")) ||
		Line.Contains(TEXT("for (")) ||
		Line.Contains(TEXT("switch (")) ||
		Line.Contains(TEXT("return ")) ||
		Line.StartsWith(TEXT("//")) ||
		Line.Contains(TEXT("#define")))
	{
		return false;
	}

	// Must have a return type pattern (virtual, static, override-bearing keywords, or type prefix)
	return Line.Contains(TEXT("virtual ")) ||
		   Line.Contains(TEXT("static ")) ||
		   Line.Contains(TEXT(" override")) ||
		   Line.Contains(TEXT(" const;")) ||
		   Line.Contains(TEXT(");")) ||
		   Line.Contains(TEXT("FString ")) ||
		   Line.Contains(TEXT("void ")) ||
		   Line.Contains(TEXT("bool ")) ||
		   Line.Contains(TEXT("int32 ")) ||
		   Line.Contains(TEXT("float "));
}

FString FAutonomixCodeStructureParser::ExtractFunctionSignature(const FString& Line) const
{
	// Strip function body if present on the same line
	FString Result = Line;

	// Remove inline implementations: "{ return x; }" style
	int32 BraceIdx;
	if (Result.FindChar(TEXT('{'), BraceIdx))
	{
		Result = Result.Left(BraceIdx).TrimEnd();
	}

	// Remove trailing semicolons and clean up
	Result.TrimStartAndEndInline();

	return Result.IsEmpty() ? FString() : Result;
}

FString FAutonomixCodeStructureParser::TruncateDeclaration(const FString& Line) const
{
	if (Line.Len() <= MaxDeclarationLineLength)
	{
		return Line;
	}
	return Line.Left(MaxDeclarationLineLength) + TEXT("...");
}
