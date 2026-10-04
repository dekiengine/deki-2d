#pragma once

#ifdef DEKI_EDITOR

#include <deki-editor/EditorExtension.h>
#include <string>
#include <unordered_map>
#include <vector>
#include "Deki2DPackage.h"
#include "FontCompiler.h"

namespace Deki2D
{

/// Inspector for font files (.ttf, .otf). Edits the import settings (sizes,
/// character range) and makes one variant sub-asset per size. Variants are
/// baked on demand into the cache directory.
///
/// .data sidecar format:
/// {
///   "guid": "main-font-guid",
///   "fontSettings": {
///     "sizes": [12, 16, 24],
///     "firstChar": 32,
///     "lastChar": 126
///   },
///   "variants": {
///     "12": {"guid": "random-font-guid-12", "atlasGuid": "random-atlas-guid-12"},
///     "16": {"guid": "random-font-guid-16", "atlasGuid": "random-atlas-guid-16"},
///     "24": {"guid": "random-font-guid-24", "atlasGuid": "random-atlas-guid-24"}
///   }
/// }
///
/// Cache files are named by the random GUID, with no extension.
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
    std::vector<int> m_Sizes;
    int m_FirstChar = 32;
    int m_LastChar = 126;
    FontCompiler::HintingMode m_Hinting = FontCompiler::HintingMode::Light;
    int m_Oversample = 2;
    FontCompiler::DecorationMode m_Decoration = FontCompiler::DecorationMode::None;
    int m_OutlineSize = 1;
    int m_ShadowDx = 1;
    int m_ShadowDy = 1;
    bool m_SettingsModified = false;

    // Size editor state
    int m_NewSize = 16;

    // The asset the settings were loaded from, to notice a change.
    std::string m_CurrentAssetPath;

    // Whether each size's baked cache file exists, so fs::exists is not
    // called per size every frame. Refreshed on asset change, size add or
    // remove, and after a bake.
    std::unordered_map<int, bool> m_VariantCached;
    bool m_VariantCacheDirty = true;

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

    /// Saves the font settings to the .data sidecar.
    void SaveSettings(const std::string& assetPath, const std::string& assetGuid);

    /// A deterministic GUID for a font variant, used only for the preview.
    std::string GenerateVariantGuid(const std::string& fontGuid, int fontSize);

    /// The variant GUID for `fontSize` from the .data file, or "" if none.
    std::string GetVariantGuidFromData(const std::string& assetPath, int fontSize);

    /// The atlas GUID for `fontSize` from the .data file, or "" if none.
    std::string GetAtlasGuidFromData(const std::string& assetPath, int fontSize);

    /// Bakes the font at `fontSize`.
    void BakeFontVariant(const std::string& assetPath, const std::string& fontGuid, int fontSize);

    /// Rebuilds m_VariantCached by checking each size's cache file once.
    void RefreshVariantCache(const std::string& assetPath);

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
