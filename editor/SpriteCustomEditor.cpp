// Editor support for SpriteComponent: its display size in the Scene view and
// its Inspector, including 9-slice editing. Compiled into the editor only,
// not the runtime.

#ifdef DEKI_EDITOR

#include <deki-editor/EditorRegistry.h>
#include <deki-editor/EditorAssets.h>
#include <deki-editor/CustomEditor.h>
#include <deki-editor/EditorUI.h>
#include <deki-editor/AssetDatabase.h>
#include <deki-editor/SubAsset.h>
#include "NineSliceEditorWindow.h"
#include "SpriteComponent.h"
#include <deki/Engine.h>
#include <deki/LogSystem.h>
#include <cstdio>
#include <nlohmann/json.hpp>
#include <fstream>
#include <filesystem>
#include <deki-editor/EditorApplication.h>

// Editor extensions live in DekiEditor; the package's own types are in Deki2D.
using namespace Deki2D;

namespace DekiEditor
{

// Whether the asset is a ProceduralSprite.
static bool IsProceduralSpriteAsset(const std::string& assetPath)
{
    if (assetPath.empty())
    {
        return false;
    }

    if (assetPath.length() < 6 || assetPath.substr(assetPath.length() - 6) != ".asset")
    {
        return false;
    }

    // The JSON says the type.
    std::ifstream file(assetPath);
    if (!file.is_open())
    {
        return false;
    }

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

// Bold section header with a separator, as in ProceduralSpriteEditor.
// Sprite-prefixed because ProceduralSpriteEditor.cpp has its own, slightly
// different copies of these four helpers, and the names would collide when
// both files share a translation unit (unity build).
static void SpriteSectionHeader(const char* title)
{
    auto& ui = EditorUI::Get();
    ui.Spacing();
    ui.PushBoldFont();
    ui.Text(title);
    ui.PopFont();
    ui.Separator();
}

// Group header, one level above SpriteSectionHeader. Accent-coloured, so the
// subsections read as part of the group (pair with Indent/Unindent).
static void SpriteGroupHeader(const char* title)
{
    auto& ui = EditorUI::Get();
    ui.Spacing();
    ui.PushBoldFont();
    ui.TextColored(ui.GetStyleColor(EditorUI::Col::CheckMark), title);
    ui.PopFont();
    ui.Separator();
}

// Readers for ProceduralSprite JSON.
static void SpriteParseBorderWidth(const nlohmann::json& data, int32_t& top, int32_t& right, int32_t& bottom,
                                   int32_t& left)
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
        const auto& brVal = data["border_radius"];
        if (brVal.is_number())
        {
            tl = tr = br = bl = brVal.get<int32_t>();
        }
        else if (brVal.is_array())
        {
            if (brVal.size() == 1)
            {
                tl = tr = br = bl = brVal[0].get<int32_t>();
            }
            else if (brVal.size() == 2)
            {
                tl = br = brVal[0].get<int32_t>();
                tr = bl = brVal[1].get<int32_t>();
            }
            else if (brVal.size() == 3)
            {
                tl = brVal[0].get<int32_t>();
                tr = bl = brVal[1].get<int32_t>();
                br = brVal[2].get<int32_t>();
            }
            else if (brVal.size() >= 4)
            {
                tl = brVal[0].get<int32_t>();
                tr = brVal[1].get<int32_t>();
                br = brVal[2].get<int32_t>();
                bl = brVal[3].get<int32_t>();
            }
        }
    }
}

class SpriteCustomEditor : public CustomEditor
{
public:
    const char* GetComponentName() const override { return "SpriteComponent"; }

    std::string GetTextureAssetGuid(Deki::Component* comp) override
    {
        auto* spriteComp = static_cast<SpriteComponent*>(comp);
        if (!spriteComp)
        {
            return "";
        }
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

        // For a sub-asset, source holds the frame GUID used for UV and size
        // lookups.
        const std::string& frameGuid =
            spriteComp->sprite.source.empty() ? spriteComp->sprite.guid : spriteComp->sprite.source;

        uint32_t texWidth = 0, texHeight = 0;
        // LoadFrameTexture loads the parent texture for a sub-asset GUID.
        uint32_t texId = EditorAssets::Get()->LoadFrameTexture(frameGuid, &texWidth, &texHeight);

        if (texId == 0)
        {
            return false;
        }

        float displayWidth = static_cast<float>(texWidth);
        float displayHeight = static_cast<float>(texHeight);

        // A single frame of a sheet: its UVs.
        float u0, v0, u1, v1;
        if (EditorAssets::Get()->GetFrameUVs(frameGuid, &u0, &v0, &u1, &v1))
        {
            displayWidth = (u1 - u0) * static_cast<float>(texWidth);
            displayHeight = (v1 - v0) * static_cast<float>(texHeight);
        }
        else
        {
            // A spritesheet's frame settings.
            int frameWidth = 0, frameHeight = 0;
            bool hasFrameSettings = EditorAssets::Get()->LoadSpriteSettings(frameGuid, &frameWidth, &frameHeight);

            if (hasFrameSettings && frameWidth > 0 && frameHeight > 0)
            {
                displayWidth = static_cast<float>(frameWidth);
                displayHeight = static_cast<float>(frameHeight);
            }
        }

        // In Tiled and NineSlice modes the drawn quad is width x height (per
        // axis; 0 keeps the size found above), and the selection bounds must
        // match it. width/height are world meters, so multiply by ppm to get
        // pixels like the sizes above.
        if (spriteComp->renderMode == SpriteRenderMode::Tiled || spriteComp->renderMode == SpriteRenderMode::NineSlice)
        {
            const float ppm = Deki::EngineSettings::Global().pixelsPerMeter;
            if (spriteComp->width > 0.0f)
            {
                displayWidth = spriteComp->width * ppm;
            }
            if (spriteComp->height > 0.0f)
            {
                displayHeight = spriteComp->height * ppm;
            }
        }

        outWidth = displayWidth;
        outHeight = displayHeight;
        return true;
    }

    bool WantsInspectorOverride(Deki::Component* comp) override
    {
        // Always: the standard properties are drawn here, so the 9-slice
        // editing UI can be added for both procedural (.asset) and normal
        // (.png) sprites.
        return comp != nullptr;
    }

    void OnInspectorGUI(Deki::Component* comp) override
    {
        auto* spriteComp = static_cast<SpriteComponent*>(comp);
        if (!spriteComp)
        {
            return;
        }

        const std::string& spriteGuid = spriteComp->sprite.guid;

        std::string assetPath = AssetDatabase::GUIDToAbsolutePath(spriteGuid);

        EditorUI::Get().PropertyField("sprite");
        EditorUI::Get().PropertyField("tintColor");
        EditorUI::Get().PropertyField("ignore_clip");
        EditorUI::Get().PropertyField("sortingOrder");
        EditorUI::Get().PropertyField("renderMode");
        EditorUI::Get().PropertyField("width");
        EditorUI::Get().PropertyField("height");

        // One frame of a sheet: its borders are its own, kept in the
        // sheet's sidecar under the frame's index.
        int frameIndex = -1;
        if (const SubAssetInfo* sub = AssetDatabase::GetSubAsset(spriteGuid))
        {
            assetPath = AssetDatabase::GUIDToAbsolutePath(sub->parentGuid);
            frameIndex = sub->subAssetIndex;
        }

        if (!IsProceduralSpriteAsset(assetPath))
        {
            // A normal sprite: 9-slice borders are edited in the .png.data sidecar.
            DrawNormalSpriteNineSliceUI(assetPath, frameIndex);
            return;
        }

        // Accent-coloured, so the subsections below read as part of
        // "Procedural Sprite" without an indent.
        SpriteGroupHeader("Procedural Sprite");

        auto& ui = EditorUI::Get();

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
        float bgColor[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
        if (jsonData.contains("background_color") && jsonData["background_color"].is_array() &&
            jsonData["background_color"].size() >= 4)
        {
            bgColor[0] = jsonData["background_color"][0].get<int>() / 255.0f;
            bgColor[1] = jsonData["background_color"][1].get<int>() / 255.0f;
            bgColor[2] = jsonData["background_color"][2].get<int>() / 255.0f;
            bgColor[3] = jsonData["background_color"][3].get<int>() / 255.0f;
        }
        ui.PropertyRow("Color");
        if (ui.ColorEdit4("##bgColor", bgColor))
        {
            jsonData["background_color"] = { static_cast<int>(bgColor[0] * 255), static_cast<int>(bgColor[1] * 255),
                                             static_cast<int>(bgColor[2] * 255), static_cast<int>(bgColor[3] * 255) };
            modified = true;
        }

        // ── Border Width ─────────────────────────────────────────────────
        SpriteSectionHeader("Border Width");
        int32_t bwTop = 0, bwRight = 0, bwBottom = 0, bwLeft = 0;
        SpriteParseBorderWidth(jsonData, bwTop, bwRight, bwBottom, bwLeft);

        bool bwChanged = false;
        ui.PropertyRow("Top");
        if (ui.DragInt("##bwTop", &bwTop, 1.0f, 0, 100))
        {
            bwChanged = true;
        }
        ui.PropertyRow("Right");
        if (ui.DragInt("##bwRight", &bwRight, 1.0f, 0, 100))
        {
            bwChanged = true;
        }
        ui.PropertyRow("Bottom");
        if (ui.DragInt("##bwBottom", &bwBottom, 1.0f, 0, 100))
        {
            bwChanged = true;
        }
        ui.PropertyRow("Left");
        if (ui.DragInt("##bwLeft", &bwLeft, 1.0f, 0, 100))
        {
            bwChanged = true;
        }

        if (bwChanged)
        {
            jsonData["border_width"] = { bwTop, bwRight, bwBottom, bwLeft };
            modified = true;
        }

        // ── Border Color ─────────────────────────────────────────────────
        SpriteSectionHeader("Border Color");
        float borderColor[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
        if (jsonData.contains("border_color") && jsonData["border_color"].is_array() &&
            jsonData["border_color"].size() >= 4)
        {
            borderColor[0] = jsonData["border_color"][0].get<int>() / 255.0f;
            borderColor[1] = jsonData["border_color"][1].get<int>() / 255.0f;
            borderColor[2] = jsonData["border_color"][2].get<int>() / 255.0f;
            borderColor[3] = jsonData["border_color"][3].get<int>() / 255.0f;
        }
        ui.PropertyRow("Color");
        if (ui.ColorEdit4("##borderColor", borderColor))
        {
            jsonData["border_color"] = { static_cast<int>(borderColor[0] * 255), static_cast<int>(borderColor[1] * 255),
                                         static_cast<int>(borderColor[2] * 255),
                                         static_cast<int>(borderColor[3] * 255) };
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
        if (ui.DragInt("##brTL", &brTL, 1.0f, 0, 200))
        {
            brChanged = true;
        }
        ui.PropertyRow("Top Right");
        if (ui.DragInt("##brTR", &brTR, 1.0f, 0, 200))
        {
            brChanged = true;
        }
        ui.PropertyRow("Bottom Right");
        if (ui.DragInt("##brBR", &brBR, 1.0f, 0, 200))
        {
            brChanged = true;
        }
        ui.PropertyRow("Bottom Left");
        if (ui.DragInt("##brBL", &brBL, 1.0f, 0, 200))
        {
            brChanged = true;
        }

        if (brChanged)
        {
            jsonData["border_radius"] = { brTL, brTR, brBR, brBL };
            modified = true;
        }

        // ── 9-Slice ──────────────────────────────────────────────────────
        SpriteSectionHeader("9-Slice");
        {
            int32_t nsTop = 0, nsRight = 0, nsBottom = 0, nsLeft = 0;
            if (jsonData.contains("nine_slice") && jsonData["nine_slice"].is_array() &&
                jsonData["nine_slice"].size() >= 4)
            {
                nsTop = jsonData["nine_slice"][0].get<int32_t>();
                nsRight = jsonData["nine_slice"][1].get<int32_t>();
                nsBottom = jsonData["nine_slice"][2].get<int32_t>();
                nsLeft = jsonData["nine_slice"][3].get<int32_t>();
            }
            char nsBuf[128];
            std::snprintf(nsBuf, sizeof(nsBuf), "L:%d  R:%d  T:%d  B:%d", nsLeft, nsRight, nsTop, nsBottom);
            ui.Text(nsBuf);
            ui.SameLine();
            if (ui.Button("Edit 9-Slice..."))
            {
                EditorApplication::Get().RequestOpenTool(assetPath, /*cachePath*/ "", "9-Slice Editor");
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

        // Apply changes through the command system, for undo.
        if (modified)
        {
            EditorUI::Get().ModifyAsset(assetPath, spriteGuid, originalData.dump(2), jsonData.dump(2));
        }
    }

private:
    // Shows the 9-slice borders from the .png.data sidecar (if any) and an
    // "Edit 9-Slice..." button. The 9-slice window does the editing and saving.
    void DrawNormalSpriteNineSliceUI(const std::string& assetPath, int frameIndex)
    {
        if (assetPath.empty())
        {
            return;
        }

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
                    const std::string key = std::to_string(frameIndex);
                    if (frameIndex >= 0)
                    {
                        if (dataJson.contains("settings") && dataJson["settings"].contains("frame_nine_slice") &&
                            dataJson["settings"]["frame_nine_slice"].contains(key))
                        {
                            node = &dataJson["settings"]["frame_nine_slice"][key];
                        }
                    }
                    else if (dataJson.contains("settings") && dataJson["settings"].contains("nine_slice"))
                    {
                        node = &dataJson["settings"]["nine_slice"];
                    }
                    else if (dataJson.contains("nine_slice"))
                    {
                        node = &dataJson["nine_slice"];
                    }
                    if (node && node->is_array() && node->size() >= 4)
                    {
                        nsTop = (*node)[0].get<int32_t>();
                        nsRight = (*node)[1].get<int32_t>();
                        nsBottom = (*node)[2].get<int32_t>();
                        nsLeft = (*node)[3].get<int32_t>();
                    }
                }
                catch (...)
                {
                }
            }
        }

        char nsBuf[128];
        std::snprintf(nsBuf, sizeof(nsBuf), "9-slice  L:%d  R:%d  T:%d  B:%d", nsLeft, nsRight, nsTop, nsBottom);
        ui.Text(nsBuf);
        ui.SameLine();
        if (ui.Button("Edit 9-Slice..."))
        {
            NineSliceEditorWindow::SetNextFrame(frameIndex);
            EditorApplication::Get().RequestOpenTool(assetPath, /*cachePath*/ "", "9-Slice Editor");
        }
    }
};

REGISTER_EDITOR(SpriteCustomEditor)
REGISTER_CREATE_MENU_ITEM(SpriteComponent, "2D", "Sprite", "Sprite", "SpriteComponent")

}  // namespace DekiEditor

#endif  // DEKI_EDITOR
