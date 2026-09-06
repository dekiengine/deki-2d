/**
 * @file SpriteCustomEditor.cpp
 * @brief Editor-only rendering for SpriteComponent in the scene view
 *
 * This file is only compiled into the editor, not the runtime.
 * Uses CustomEditor with SceneView::Get() singleton for Unity-like API.
 */

#ifdef DEKI_EDITOR

#include <deki-editor/EditorRegistry.h>
#include <deki-editor/EditorAssets.h>
#include <deki-editor/CustomEditor.h>
#include <deki-editor/EditorUI.h>
#include <deki-editor/AssetDatabase.h>
#include "SpriteComponent.h"
#include <deki/Engine.h>
#include <deki/LogSystem.h>
#include <cstdio>
#include <nlohmann/json.hpp>
#include <fstream>
#include <filesystem>
#include <deki-editor/EditorApplication.h>

namespace DekiEditor
{

// Helper function to check if an asset is a ProceduralSprite
static bool IsProceduralSpriteAsset(const std::string& assetPath)
{
    if (assetPath.empty())
        return false;

    // Check if it's a .asset file
    if (assetPath.length() < 6 || assetPath.substr(assetPath.length() - 6) != ".asset")
        return false;

    // Read the JSON to check the type
    std::ifstream file(assetPath);
    if (!file.is_open())
        return false;

    try
    {
        nlohmann::json jsonData;
        file >> jsonData;
        file.close();

        if (jsonData.contains("type") && jsonData["type"].is_string())
        {
            return jsonData["type"].get<std::string>() == "ProceduralSprite";
        }
    }
    catch (...)
    {
        return false;
    }

    return false;
}

// Bold section header matching ProceduralSpriteEditor. Field labels underneath
// should feel subordinate — the bold font + separator achieves that.
// Sprite-prefixed because ProceduralSpriteEditor.cpp has its own near-identical
// copies of these four helpers, and unqualified names would collide once the two
// files share a translation unit (unity build). The copies have drifted, so they
// are deliberately not merged here.
static void SpriteSectionHeader(const char* title)
{
    auto& ui = EditorUI::Get();
    ui.Spacing();
    ui.PushBoldFont();
    ui.Text(title);
    ui.PopFont();
    ui.Separator();
}

// Group header — one level above SpriteSectionHeader, accent-coloured so the
// subsections visibly belong to this group (pair with Indent/Unindent).
static void SpriteGroupHeader(const char* title)
{
    auto& ui = EditorUI::Get();
    ui.Spacing();
    ui.PushBoldFont();
    ui.TextColored(ui.GetStyleColor(EditorUI::Col::CheckMark), title);
    ui.PopFont();
    ui.Separator();
}

// Helper functions for parsing ProceduralSprite JSON
static void SpriteParseBorderWidth(const nlohmann::json& data, int32_t& top, int32_t& right, int32_t& bottom, int32_t& left)
{
    if (data.contains("border_width"))
    {
        const auto& bw = data["border_width"];
        if (bw.is_number())
        {
            top = right = bottom = left = bw.get<int32_t>();
        }
        else if (bw.is_array())
        {
            if (bw.size() == 1)
            {
                top = right = bottom = left = bw[0].get<int32_t>();
            }
            else if (bw.size() == 2)
            {
                top = bottom = bw[0].get<int32_t>();
                right = left = bw[1].get<int32_t>();
            }
            else if (bw.size() == 3)
            {
                top = bw[0].get<int32_t>();
                right = left = bw[1].get<int32_t>();
                bottom = bw[2].get<int32_t>();
            }
            else if (bw.size() >= 4)
            {
                top = bw[0].get<int32_t>();
                right = bw[1].get<int32_t>();
                bottom = bw[2].get<int32_t>();
                left = bw[3].get<int32_t>();
            }
        }
    }
}

static void SpriteParseBorderRadius(const nlohmann::json& data, int32_t& tl, int32_t& tr, int32_t& br, int32_t& bl)
{
    if (data.contains("border_radius"))
    {
        const auto& br_val = data["border_radius"];
        if (br_val.is_number())
        {
            tl = tr = br = bl = br_val.get<int32_t>();
        }
        else if (br_val.is_array())
        {
            if (br_val.size() == 1)
            {
                tl = tr = br = bl = br_val[0].get<int32_t>();
            }
            else if (br_val.size() == 2)
            {
                tl = br = br_val[0].get<int32_t>();
                tr = bl = br_val[1].get<int32_t>();
            }
            else if (br_val.size() == 3)
            {
                tl = br_val[0].get<int32_t>();
                tr = bl = br_val[1].get<int32_t>();
                br = br_val[2].get<int32_t>();
            }
            else if (br_val.size() >= 4)
            {
                tl = br_val[0].get<int32_t>();
                tr = br_val[1].get<int32_t>();
                br = br_val[2].get<int32_t>();
                bl = br_val[3].get<int32_t>();
            }
        }
    }
}

class SpriteCustomEditor : public CustomEditor
{
public:
    const char* GetComponentName() const override
    {
        return "SpriteComponent";
    }

    std::string GetTextureAssetGuid(Deki::Component* comp) override
    {
        auto* spriteComp = static_cast<SpriteComponent*>(comp);
        if (!spriteComp)
            return "";
        return spriteComp->sprite.guid;
    }

    bool GetDisplaySize(Deki::Component* comp, float& outWidth, float& outHeight) override
    {
        auto* spriteComp = static_cast<SpriteComponent*>(comp);
        if (!spriteComp)
        {
            return false;
        }

        const std::string& spriteGuid = spriteComp->sprite.guid;

        if (spriteGuid.empty())
        {
            return false;
        }

        // For sub-assets, source holds the frame GUID needed for UV/size lookups
        const std::string& frameGuid = spriteComp->sprite.source.empty()
            ? spriteComp->sprite.guid : spriteComp->sprite.source;

        uint32_t texWidth = 0, texHeight = 0;
        // Use LoadFrameTexture to handle SubAsset GUIDs (loads parent texture)
        uint32_t texId = EditorAssets::Get()->LoadFrameTexture(frameGuid, &texWidth, &texHeight);

        if (texId == 0)
        {
            return false;
        }

        float displayWidth = static_cast<float>(texWidth);
        float displayHeight = static_cast<float>(texHeight);

        // First, try to get SubAsset frame UVs (for individual sprite frames)
        float u0, v0, u1, v1;
        if (EditorAssets::Get()->GetFrameUVs(frameGuid, &u0, &v0, &u1, &v1))
        {
            // Calculate frame dimensions from UVs
            displayWidth = (u1 - u0) * static_cast<float>(texWidth);
            displayHeight = (v1 - v0) * static_cast<float>(texHeight);
        }
        else
        {
            // Check for sprite frame settings (for spritesheets)
            int frameWidth = 0, frameHeight = 0;
            bool hasFrameSettings = EditorAssets::Get()->LoadSpriteSettings(frameGuid, &frameWidth, &frameHeight);

            if (hasFrameSettings && frameWidth > 0 && frameHeight > 0)
            {
                displayWidth = static_cast<float>(frameWidth);
                displayHeight = static_cast<float>(frameHeight);
            }
        }

        // In Tiled/NineSlice modes the on-screen quad expands to width/height
        // (per-axis; 0 keeps the native fallback above). Selection bounds must
        // match the rendered quad, not the source texture. width/height are
        // world meters; multiply by ppm to keep displayWidth in pixels like
        // the texture/frame fallback paths above.
        if (spriteComp->renderMode == SpriteRenderMode::Tiled ||
            spriteComp->renderMode == SpriteRenderMode::NineSlice)
        {
            const float ppm = Deki::EngineSettings::Global().pixelsPerMeter;
            if (spriteComp->width  > 0.0f) displayWidth  = spriteComp->width  * ppm;
            if (spriteComp->height > 0.0f) displayHeight = spriteComp->height * ppm;
        }

        outWidth = displayWidth;
        outHeight = displayHeight;
        return true;
    }

    bool WantsInspectorOverride(Deki::Component* comp) override
    {
        // Always override: we draw the standard properties manually so we can
        // inject the 9-slice editing UI for both procedural (.asset) and
        // normal (.png) sprites in addition to the auto-reflected fields.
        return comp != nullptr;
    }

    void OnInspectorGUI(Deki::Component* comp) override
    {
        auto* spriteComp = static_cast<SpriteComponent*>(comp);
        if (!spriteComp)
            return;

        const std::string& spriteGuid = spriteComp->sprite.guid;

        // Resolve GUID to absolute asset path
        std::string assetPath = AssetDatabase::GUIDToAbsolutePath(spriteGuid);

        // Draw SpriteComponent properties
        EditorUI::Get().PropertyField("sprite");
        EditorUI::Get().PropertyField("tintColor");
        EditorUI::Get().PropertyField("ignore_clip");
        EditorUI::Get().PropertyField("sortingOrder");
        EditorUI::Get().PropertyField("renderMode");
        EditorUI::Get().PropertyField("width");
        EditorUI::Get().PropertyField("height");

        if (!IsProceduralSpriteAsset(assetPath))
        {
            // Regular sprite: expose 9-slice border editing via the .png.data sidecar.
            DrawNormalSpriteNineSliceUI(assetPath, spriteGuid);
            return;
        }

        // Group header — accent-coloured so the subsections below read as
        // belonging to "Procedural Sprite" without needing an indent.
        SpriteGroupHeader("Procedural Sprite");

        auto& ui = EditorUI::Get();

        // Read the ProceduralSprite JSON
        std::ifstream file(assetPath);
        if (!file.is_open())
        {
            ui.TextColored(EditorUI::Rgba(255, 102, 102, 255), "Failed to open asset file");
            return;
        }

        nlohmann::json originalData;
        try
        {
            file >> originalData;
        }
        catch (const std::exception& e)
        {
            char errBuf[512];
            std::snprintf(errBuf, sizeof(errBuf), "Failed to parse JSON: %s", e.what());
            ui.TextColored(EditorUI::Rgba(255, 102, 102, 255), errBuf);
            file.close();
            return;
        }
        file.close();

        // Work on a copy
        nlohmann::json jsonData = originalData;
        bool modified = false;

        // ── Dimensions ───────────────────────────────────────────────────
        SpriteSectionHeader("Dimensions");
        int width = jsonData.value("width", 64);
        int height = jsonData.value("height", 64);
        ui.PropertyRow("Width");
        if (ui.DragInt("##width", &width, 1.0f, 1, 1024))
        {
            jsonData["width"] = width;
            modified = true;
        }
        ui.PropertyRow("Height");
        if (ui.DragInt("##height", &height, 1.0f, 1, 1024))
        {
            jsonData["height"] = height;
            modified = true;
        }

        // ── Background ───────────────────────────────────────────────────
        SpriteSectionHeader("Background");
        float bgColor[4] = {1.0f, 1.0f, 1.0f, 1.0f};
        if (jsonData.contains("background_color") && jsonData["background_color"].is_array() && jsonData["background_color"].size() >= 4)
        {
            bgColor[0] = jsonData["background_color"][0].get<int>() / 255.0f;
            bgColor[1] = jsonData["background_color"][1].get<int>() / 255.0f;
            bgColor[2] = jsonData["background_color"][2].get<int>() / 255.0f;
            bgColor[3] = jsonData["background_color"][3].get<int>() / 255.0f;
        }
        ui.PropertyRow("Color");
        if (ui.ColorEdit4("##bgColor", bgColor))
        {
            jsonData["background_color"] = {
                static_cast<int>(bgColor[0] * 255),
                static_cast<int>(bgColor[1] * 255),
                static_cast<int>(bgColor[2] * 255),
                static_cast<int>(bgColor[3] * 255)
            };
            modified = true;
        }

        // ── Border Width ─────────────────────────────────────────────────
        SpriteSectionHeader("Border Width");
        int32_t bwTop = 0, bwRight = 0, bwBottom = 0, bwLeft = 0;
        SpriteParseBorderWidth(jsonData, bwTop, bwRight, bwBottom, bwLeft);

        bool bwChanged = false;
        ui.PropertyRow("Top");
        if (ui.DragInt("##bwTop", &bwTop, 1.0f, 0, 100)) bwChanged = true;
        ui.PropertyRow("Right");
        if (ui.DragInt("##bwRight", &bwRight, 1.0f, 0, 100)) bwChanged = true;
        ui.PropertyRow("Bottom");
        if (ui.DragInt("##bwBottom", &bwBottom, 1.0f, 0, 100)) bwChanged = true;
        ui.PropertyRow("Left");
        if (ui.DragInt("##bwLeft", &bwLeft, 1.0f, 0, 100)) bwChanged = true;

        if (bwChanged)
        {
            jsonData["border_width"] = {bwTop, bwRight, bwBottom, bwLeft};
            modified = true;
        }

        // ── Border Color ─────────────────────────────────────────────────
        SpriteSectionHeader("Border Color");
        float borderColor[4] = {0.0f, 0.0f, 0.0f, 1.0f};
        if (jsonData.contains("border_color") && jsonData["border_color"].is_array() && jsonData["border_color"].size() >= 4)
        {
            borderColor[0] = jsonData["border_color"][0].get<int>() / 255.0f;
            borderColor[1] = jsonData["border_color"][1].get<int>() / 255.0f;
            borderColor[2] = jsonData["border_color"][2].get<int>() / 255.0f;
            borderColor[3] = jsonData["border_color"][3].get<int>() / 255.0f;
        }
        ui.PropertyRow("Color");
        if (ui.ColorEdit4("##borderColor", borderColor))
        {
            jsonData["border_color"] = {
                static_cast<int>(borderColor[0] * 255),
                static_cast<int>(borderColor[1] * 255),
                static_cast<int>(borderColor[2] * 255),
                static_cast<int>(borderColor[3] * 255)
            };
            jsonData.erase("border_top_color");
            jsonData.erase("border_right_color");
            jsonData.erase("border_bottom_color");
            jsonData.erase("border_left_color");
            modified = true;
        }

        // ── Border Radius ────────────────────────────────────────────────
        SpriteSectionHeader("Border Radius");
        int32_t brTL = 0, brTR = 0, brBR = 0, brBL = 0;
        SpriteParseBorderRadius(jsonData, brTL, brTR, brBR, brBL);

        bool brChanged = false;
        ui.PropertyRow("Top Left");
        if (ui.DragInt("##brTL", &brTL, 1.0f, 0, 200)) brChanged = true;
        ui.PropertyRow("Top Right");
        if (ui.DragInt("##brTR", &brTR, 1.0f, 0, 200)) brChanged = true;
        ui.PropertyRow("Bottom Right");
        if (ui.DragInt("##brBR", &brBR, 1.0f, 0, 200)) brChanged = true;
        ui.PropertyRow("Bottom Left");
        if (ui.DragInt("##brBL", &brBL, 1.0f, 0, 200)) brChanged = true;

        if (brChanged)
        {
            jsonData["border_radius"] = {brTL, brTR, brBR, brBL};
            modified = true;
        }

        // ── 9-Slice ──────────────────────────────────────────────────────
        SpriteSectionHeader("9-Slice");
        {
            int32_t nsTop = 0, nsRight = 0, nsBottom = 0, nsLeft = 0;
            if (jsonData.contains("nine_slice") && jsonData["nine_slice"].is_array() && jsonData["nine_slice"].size() >= 4)
            {
                nsTop    = jsonData["nine_slice"][0].get<int32_t>();
                nsRight  = jsonData["nine_slice"][1].get<int32_t>();
                nsBottom = jsonData["nine_slice"][2].get<int32_t>();
                nsLeft   = jsonData["nine_slice"][3].get<int32_t>();
            }
            char nsBuf[128];
            std::snprintf(nsBuf, sizeof(nsBuf), "L:%d  R:%d  T:%d  B:%d", nsLeft, nsRight, nsTop, nsBottom);
            ui.Text(nsBuf);
            ui.SameLine();
            if (ui.Button("Edit 9-Slice..."))
            {
                EditorApplication::Get().RequestOpenTool(assetPath, /*cachePath*/ "");
            }
        }

        // ── Misc ─────────────────────────────────────────────────────────
        SpriteSectionHeader("Misc");
        bool antialiased = jsonData.value("antialiased", false);
        if (ui.Checkbox("Antialiased", &antialiased))
        {
            jsonData["antialiased"] = antialiased;
            modified = true;
        }

        // Apply changes through command system if modified
        if (modified)
        {
            EditorUI::Get().ModifyAsset(
                assetPath,
                spriteGuid,
                originalData.dump(2),
                jsonData.dump(2)
            );
        }
    }

private:
    // Reads the current 9-slice borders from the .png.data sidecar (if any) and
    // shows a summary line + "Edit 9-Slice..." button. The window handles the
    // actual edit + save round-trip.
    void DrawNormalSpriteNineSliceUI(const std::string& assetPath, const std::string& spriteGuid)
    {
        (void)spriteGuid;
        if (assetPath.empty())
            return;

        auto& ui = EditorUI::Get();
        ui.Separator();

        int32_t nsTop = 0, nsRight = 0, nsBottom = 0, nsLeft = 0;
        std::string dataPath = assetPath + ".data";
        if (std::filesystem::exists(dataPath))
        {
            std::ifstream in(dataPath);
            if (in.is_open())
            {
                try
                {
                    nlohmann::json dataJson;
                    in >> dataJson;
                    const nlohmann::json* node = nullptr;
                    if (dataJson.contains("settings") && dataJson["settings"].contains("nine_slice"))
                        node = &dataJson["settings"]["nine_slice"];
                    else if (dataJson.contains("nine_slice"))
                        node = &dataJson["nine_slice"];
                    if (node && node->is_array() && node->size() >= 4)
                    {
                        nsTop    = (*node)[0].get<int32_t>();
                        nsRight  = (*node)[1].get<int32_t>();
                        nsBottom = (*node)[2].get<int32_t>();
                        nsLeft   = (*node)[3].get<int32_t>();
                    }
                }
                catch (...) {}
            }
        }

        char nsBuf[128];
        std::snprintf(nsBuf, sizeof(nsBuf), "9-slice  L:%d  R:%d  T:%d  B:%d", nsLeft, nsRight, nsTop, nsBottom);
        ui.Text(nsBuf);
        ui.SameLine();
        if (ui.Button("Edit 9-Slice..."))
        {
            EditorApplication::Get().RequestOpenTool(assetPath, /*cachePath*/ "");
        }
    }
};

REGISTER_EDITOR(SpriteCustomEditor)
REGISTER_CREATE_MENU_ITEM(SpriteComponent, "2D", "Sprite", "Sprite", "SpriteComponent")

} // namespace DekiEditor

#endif // DEKI_EDITOR
