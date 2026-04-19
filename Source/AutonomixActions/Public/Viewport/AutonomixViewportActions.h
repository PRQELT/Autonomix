// Copyright Autonomix. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AutonomixInterfaces.h"

/**
 * FAutonomixViewportActions
 *
 * Provides the capture_viewport tool — gives the AI "eyes" by capturing
 * the active editor viewport as a screenshot and encoding it to Base64 JPEG.
 *
 * The captured image is returned as the tool_result content, which the LLM client
 * layer can then include as an image content block for Vision-Language Models
 * (Claude Opus/Sonnet with vision, GPT-4o, Gemini Pro Vision).
 *
 * The AI can:
 *   - Visually inspect UMG widget layouts it just built
 *   - Check lighting and material appearance in-viewport
 *   - Verify actor placement and level design
 *   - Identify misaligned UI elements and fix them autonomously
 *
 * IMPLEMENTATION:
 *   Uses FViewport::ReadPixels() on the active level editor viewport to capture
 *   the current frame, then encodes to JPEG via IImageWrapper and Base64.
 *
 * CONTEXT WINDOW SAFETY:
 *   - Default 512px max dimension + JPEG Q75 keeps output to ~7-20K tokens
 *   - Previous PNG at 1024px produced ~125-500K tokens — caused context overflow
 *   - Token count is logged and included in the result message
 *   - AI models can still analyze viewport content accurately at 512px JPEG
 *
 * NOTES:
 *   - Read-only tool: Low risk (no modifications to the project)
 *   - Works best with VLM-capable models; non-vision models ignore the image
 */
class AUTONOMIXACTIONS_API FAutonomixViewportActions : public IAutonomixActionExecutor
{
public:
	FAutonomixViewportActions();
	virtual ~FAutonomixViewportActions();

	// IAutonomixActionExecutor interface
	virtual FName GetActionName() const override;
	virtual FText GetDisplayName() const override;
	virtual EAutonomixActionCategory GetCategory() const override;
	virtual EAutonomixRiskLevel GetDefaultRiskLevel() const override;
	virtual FAutonomixActionPlan PreviewAction(const TSharedRef<FJsonObject>& Params) override;
	virtual FAutonomixActionResult ExecuteAction(const TSharedRef<FJsonObject>& Params) override;
	virtual bool CanUndo() const override;
	virtual bool UndoAction() override;
	virtual TArray<FString> GetSupportedToolNames() const override;
	virtual bool ValidateParams(const TSharedRef<FJsonObject>& Params, TArray<FString>& OutErrors) const override;

private:
	/**
	 * Capture the active editor viewport and return a Base64-encoded JPEG.
	 *
	 * @param Params   JSON with optional:
	 *                   "max_dimension" (int, default 512, range 256-1024)
	 *                   "quality" (int, JPEG quality 30-95, default 75)
	 *                   "viewport_index" (int, default 0)
	 * @param Result   Action result to populate
	 * @return         Populated result with Base64 JPEG in ResultMessage
	 */
	FAutonomixActionResult ExecuteCaptureViewport(const TSharedRef<FJsonObject>& Params, FAutonomixActionResult& Result);

	/**
	 * Encode raw pixel data to a Base64 JPEG string.
	 *
	 * Uses JPEG instead of PNG — 5-10x smaller for viewport/3D content,
	 * keeping the base64 output within context window budgets.
	 *
	 * @param Pixels        Raw BGRA8 pixel data
	 * @param Width         Image width
	 * @param Height        Image height
	 * @param MaxDimension  Maximum width/height for downscaling (default 512)
	 * @param JpegQuality   JPEG compression quality 0-100 (default 75)
	 * @return              Base64-encoded JPEG string, empty on failure
	 */
	static FString EncodePixelsToBase64(
		const TArray<FColor>& Pixels,
		int32 Width,
		int32 Height,
		int32 MaxDimension,
		int32 JpegQuality = 75);
};
