#pragma once

#ifdef DEKI_EDITOR

#include <string>

namespace Deki2D
{

/// Registers the AssetPipeline handlers that bake font variants when TTF/OTF
/// files sync. Called when the package initializes. Goes through
/// AssetPipeline::OnStarted(), so it registers now if the pipeline is running,
/// otherwise when it starts.
void RegisterFontSyncHandlers();

/// Makes sure the font `sourceGuid` (a TTF/OTF asset) is baked at `fontSize`
/// pixels. Adds the size to the font's .data file, then has RefreshAsset run
/// the font sync handler, which bakes it. The one way to request font baking.
void EnsureFontSizeBaked(const std::string& sourceGuid, int fontSize);

}  // namespace Deki2D

#endif  // DEKI_EDITOR
