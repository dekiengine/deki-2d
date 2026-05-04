#pragma once

#ifdef DEKI_EDITOR

#include <deki-editor/EditorExtension.h>
#include <string>
#include <vector>
#include "Deki2DModule.h"
#include "FontCompiler.h"

namespace Deki2D
{

/**
 * @brief File inspector for font assets (.ttf, .otf)
 *
 * Allows configuring font import settings (sizes, charset) and generates
 * variant sub-assets for each size. Variants are baked on-demand and stored
 * in the cache directory.
 *
 * .data sidecar format:
 * {
 *   "guid": "main-font-guid",
 *   "fontSettings": {
 *     "sizes": [12, 16, 24],
 *     "firstChar": 32,
 *     "lastChar": 126
 *   },
 *   "variants": {
 *     "12": {"guid": "random-font-guid-12", "atlasGuid": "random-atlas-guid-12"},
 *     "16": {"guid": "random-font-guid-16", "atlasGuid": "random-atlas-guid-16"},
 *     "24": {"guid": "random-font-guid-24", "atlasGuid": "random-atlas-guid-24"}
 *   }
 * }
 *
 * Cache files use the random GUID as filename with no extension.
 */
class DEKI_2D_API FontFileInspector : public DekiEditor::FileInspector
{
public:
    FontFileInspector() = default;
    ~FontFileInspector() override = default;

    // FileInspector interface
    const char** GetExtensions() const override;
    int GetExtensionCount() const override;
    void OnInspectorGUI(const std::string& assetPath,
                        const std::string& assetGuid) override;

private:
    // Current settings (loaded from .data file)
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

    // Loaded from path (to detect asset change)
    std::string m_CurrentAssetPath;

    // Preview state
    int m_PreviewSize = 24;
    char m_PreviewText[128] = "The quick brown fox";
    uint32_t m_PreviewTextureId = 0;
    int m_PreviewTextureWidth = 0;
    int m_PreviewTextureHeight = 0;
    int m_LastPreviewSize = 0;
    std::string m_LastPreviewAsset;

    /**
     * @brief Load font settings from .data sidecar file
     */
    void LoadSettings(const std::string& assetPath);

    /**
     * @brief Save font settings to .data sidecar file
     */
    void SaveSettings(const std::string& assetPath, const std::string& assetGuid);

    /**
     * @brief Generate deterministic GUID for a font variant (used for preview only)
     */
    std::string GenerateVariantGuid(const std::string& fontGuid, int fontSize);

    /**
     * @brief Get variant GUID from .data file
     * @return GUID string, or empty if not found
     */
    std::string GetVariantGuidFromData(const std::string& assetPath, int fontSize);

    /**
     * @brief Get atlas GUID from .data file
     * @return GUID string, or empty if not found
     */
    std::string GetAtlasGuidFromData(const std::string& assetPath, int fontSize);

    /**
     * @brief Bake font at a specific size
     */
    void BakeFontVariant(const std::string& assetPath, const std::string& fontGuid, int fontSize);

    /**
     * @brief Generate preview texture for a font at a specific size
     */
    void GeneratePreview(const std::string& assetPath, int fontSize);

    /**
     * @brief Clean up preview texture
     */
    void CleanupPreview();
};

/**
 * @brief Register the font file inspector with the editor
 * Called during module initialization
 */
DEKI_2D_API void RegisterFontFileInspector();

} // namespace Deki2D

#endif // DEKI_EDITOR
