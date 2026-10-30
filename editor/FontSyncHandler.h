#pragma once

#ifdef DEKI_EDITOR

#include "FontCompiler.h"
#include <string>
#include <vector>

namespace Deki2D
{

/// The sizes a TrueType font can be baked at, in pixels.
constexpr int kMinFontSize = 6;
constexpr int kMaxFontSize = 128;

/// Registers the AssetPipeline handlers that bake font variants when TTF/OTF
/// and BDF files sync. Called when the package initializes. Goes through
/// AssetPipeline::OnStarted(), so it registers now if the pipeline is running,
/// otherwise when it starts.
void RegisterFontSyncHandlers();

/// Bakes the font `sourceGuid` (a TTF/OTF asset) at `fontSize` pixels in the
/// background, unless it is baked or baking already. Nothing is written to the
/// font's .data: the texts that use a size are the record of it. A newer
/// request from the same `requester` (a text, say) drops its older one that
/// has not started, so dragging a size bakes the size it ends on.
void EnsureFontSizeBaked(const std::string& sourceGuid, int fontSize, const void* requester = nullptr);

/// True while `fontSize` of the font `sourceGuid` waits for or runs its bake.
bool IsFontSizeBaking(const std::string& sourceGuid, int fontSize);

/// True when the last bake of `fontSize` of the font `sourceGuid` failed.
bool DidFontSizeBakeFail(const std::string& sourceGuid, int fontSize);

/// The sizes of the font `sourceGuid` baked in the cache, smallest first.
std::vector<int> GetBakedFontSizes(const std::string& sourceGuid);

/// How many bakes of the font `sourceGuid` wait or run.
int GetPendingFontBakeCount(const std::string& sourceGuid);

/// Bakes every cached size of the font `sourceGuid` again. Settings changes
/// do this by themselves; this is the manual fallback.
void RebakeFont(const std::string& sourceGuid);

/// Writes the bakes that finished to the cache and makes their texts load
/// them. Runs on the main thread; text editors and the font inspector call it
/// every frame.
void PumpFontBakes();

/// Stops the bake thread, dropping what it has not baked. Called before the
/// package unloads.
void StopFontBaker();

/// The compile options a TrueType font's .data sidecar sets (hinting,
/// oversample, decoration, characters), for any size. `fontPath` is the
/// font's absolute path. Missing settings keep their defaults.
FontCompiler::CompileOptions LoadFontCompileOptions(const std::string& fontPath);

/// The sizes a TrueType font's .data sidecar asks builds to always ship, for
/// sizes only code sets. `fontPath` is the font's absolute path.
std::vector<int> LoadFontExtraSizes(const std::string& fontPath);

}  // namespace Deki2D

#endif  // DEKI_EDITOR
