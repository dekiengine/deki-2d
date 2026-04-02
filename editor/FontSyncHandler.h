#pragma once

#ifdef DEKI_EDITOR

#include <string>

namespace Deki2D
{

/**
 * @brief Register font sync handlers with AssetPipeline
 *
 * Called during module initialization to register handlers that auto-bake
 * font variants when TTF/OTF files are synced.
 *
 * Uses AssetPipeline::OnStarted() to handle timing - if pipeline is already
 * active, registers immediately; otherwise registers when pipeline starts.
 */
void RegisterFontSyncHandlers();

/**
 * @brief Ensure a font size is baked to disk.
 *
 * Updates the font's .data file to include the requested size, then triggers
 * FontSyncHandler via RefreshAsset to perform the actual compilation.
 * This is the single entry point for requesting font baking.
 *
 * @param sourceGuid The font asset GUID (TTF/OTF file)
 * @param fontSize The font size in pixels to bake
 */
void EnsureFontSizeBaked(const std::string& sourceGuid, int fontSize);

} // namespace Deki2D

#endif // DEKI_EDITOR
