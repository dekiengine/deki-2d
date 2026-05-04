#pragma once

#ifdef DEKI_EDITOR

#include <deki-editor/EditorExtension.h>
#include <string>
#include <vector>
#include <set>
#include "Deki2DModule.h"
#include "FontCompiler.h"

namespace Deki2D
{

/**
 * @brief File inspector for BDF bitmap font assets (.bdf)
 *
 * Allows selecting which characters to include and bakes the result
 * into cached .dfont + .tex files via FontSyncHandler.
 *
 * .bdf.data sidecar format:
 * {
 *   "bdfSettings": {
 *     "selectedChars": [32, 33, ..., 126]
 *   },
 *   "variants": {
 *     "bdf": {"guid": "...", "atlasGuid": "..."}
 *   }
 * }
 */
class DEKI_2D_API BdfFileInspector : public DekiEditor::FileInspector
{
public:
    BdfFileInspector() = default;
    ~BdfFileInspector() override = default;

    const char** GetExtensions() const override;
    int GetExtensionCount() const override;
    void OnInspectorGUI(const std::string& assetPath,
                        const std::string& assetGuid) override;

    bool OverridesCacheStatus() const override { return true; }
    bool IsCached(const std::string& assetGuid) const override;

private:
    // Current asset being inspected
    std::string m_CurrentAssetPath;
    bool m_SettingsModified = false;

    // Parsed BDF glyph info
    struct CharEntry
    {
        int codepoint;
        char label[16];
    };
    std::vector<CharEntry> m_AvailableChars;
    int m_Ascent = 0;
    int m_Descent = 0;

    // Selection (indices into m_AvailableChars)
    std::set<int> m_SelectedChars;
    char m_SearchFilter[256] = "";

    // Baked decoration — matches the TTF inspector; applied at bake time and
    // stored in the .dfont v4 header so runtime renders with palette lookup.
    FontCompiler::DecorationMode m_Decoration = FontCompiler::DecorationMode::None;
    int m_OutlineSize = 1;
    int m_ShadowDx = 1;
    int m_ShadowDy = 1;

    // Glyph atlas for display
    uint32_t m_GlyphAtlasTexture = 0;
    int m_GlyphAtlasW = 0;
    int m_GlyphAtlasH = 0;
    int m_GlyphDisplaySize = 0;
    struct GlyphUV { float u0, v0, u1, v1; };
    std::vector<GlyphUV> m_GlyphUVs;

    void LoadBdf(const std::string& assetPath);
    void LoadSettings(const std::string& assetPath);
    void SaveSettings(const std::string& assetPath, const std::string& assetGuid);
    void BakeFont(const std::string& assetPath, const std::string& assetGuid);
    void CleanupTextures();
    std::vector<int> GetSelectedCodepoints() const;
};

DEKI_2D_API void RegisterBdfFileInspector();

} // namespace Deki2D

#endif // DEKI_EDITOR
