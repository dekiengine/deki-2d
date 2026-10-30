#pragma once

#ifdef DEKI_EDITOR

#include <deki-editor/EditorExtension.h>
#include <string>
#include <vector>
#include "Deki2DPackage.h"
#include "FontCompiler.h"

namespace Deki2D
{

/// Inspector for font files (.ttf, .otf). Edits the rasterization settings
/// and the sizes builds always ship. Every change is saved at once; the font
/// sync handler then bakes the sizes in use again. The editor bakes each size
/// a text uses into the cache by itself (see EnsureFontSizeBaked).
///
/// .data sidecar format:
/// {
///   "guid": "main-font-guid",
///   "fontSettings": {
///     "firstChar": 32,
///     "lastChar": 126,
///     "hinting": "light",
///     "oversample": 2,
///     "decoration": "none",
///     "extraSizes": [40]
///   }
/// }
///
/// A size's variant and atlas are cache files named by GUIDs made from the
/// font GUID and the size, with no extension.
class DEKI_2D_API FontFileInspector : public DekiEditor::FileInspector
{
public:
    FontFileInspector() = default;
    ~FontFileInspector() override = default;

    // FileInspector interface
    const char** GetExtensions() const override;
    int GetExtensionCount() const override;
    void OnInspectorGUI(const std::string& assetPath, const std::string& assetGuid) override;

private:
    // Current settings, loaded from the .data file.
    std::vector<int> m_ExtraSizes;
    int m_FirstChar = 32;
    int m_LastChar = 126;
    FontCompiler::HintingMode m_Hinting = FontCompiler::HintingMode::Light;
    int m_Oversample = 2;
    FontCompiler::DecorationMode m_Decoration = FontCompiler::DecorationMode::None;
    int m_OutlineSize = 1;
    int m_ShadowDx = 1;
    int m_ShadowDy = 1;

    // Size editor state
    int m_NewSize = 16;

    // The asset the settings were loaded from, to notice a change.
    std::string m_CurrentAssetPath;

    // The sizes baked in the cache, so fs::exists is not called per size
    // every frame. Refreshed on asset change and when bakes start or land.
    std::vector<int> m_BakedSizes;
    int m_LastPendingCount = -1;

    // Preview state
    int m_PreviewSize = 24;
    char m_PreviewText[128] = "The quick brown fox";
    uint32_t m_PreviewTextureId = 0;
    int m_PreviewTextureWidth = 0;
    int m_PreviewTextureHeight = 0;
    int m_LastPreviewSize = 0;
    std::string m_LastPreviewAsset;

    /// Loads the font settings from the .data sidecar.
    void LoadSettings(const std::string& assetPath);

    /// Saves the font settings to the .data sidecar and has the pipeline
    /// sync the font, which bakes what the change made stale.
    void SaveSettings(const std::string& assetPath, const std::string& assetGuid);

    /// Makes the preview texture for the font at `fontSize`.
    void GeneratePreview(const std::string& assetPath, int fontSize);

    /// Frees the preview texture.
    void CleanupPreview();
};

/// Registers the font file inspector with the editor. Called when the
/// package initializes.
DEKI_2D_API void RegisterFontFileInspector();

}  // namespace Deki2D

#endif  // DEKI_EDITOR
