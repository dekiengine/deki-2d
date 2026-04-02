/**
 * @file SpriteCustomEditor.cpp
 * @brief Editor-only rendering for SpriteComponent in the prefab view
 *
 * This file is only compiled into the editor, not the runtime.
 * Uses CustomEditor with PrefabView::Get() singleton for Unity-like API.
 */

#ifdef DEKI_EDITOR

#include <deki-editor/EditorRegistry.h>
#include <deki-editor/EditorAssets.h>
#include <deki-editor/CustomEditor.h>
#include <deki-editor/EditorGUI.h>
#include <deki-editor/AssetDatabase.h>
#include "modules/2d/SpriteComponent.h"
#include "DekiLogSystem.h"
#include "imgui.h"
#include <nlohmann/json.hpp>
#include <fstream>

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

// Helper functions for parsing ProceduralSprite JSON
static void ParseBorderWidth(const nlohmann::json& data, int32_t& top, int32_t& right, int32_t& bottom, int32_t& left)
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

static void ParseBorderRadius(const nlohmann::json& data, int32_t& tl, int32_t& tr, int32_t& br, int32_t& bl)
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

    std::string GetTextureAssetGuid(DekiComponent* comp) override
    {
        auto* spriteComp = static_cast<SpriteComponent*>(comp);
        if (!spriteComp)
            return "";
        return spriteComp->sprite.guid;
    }

    bool GetDisplaySize(DekiComponent* comp, float& outWidth, float& outHeight) override
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

        outWidth = displayWidth;
        outHeight = displayHeight;
        return true;
    }

    bool WantsInspectorOverride(DekiComponent* comp) override
    {
        auto* spriteComp = static_cast<SpriteComponent*>(comp);
        if (!spriteComp)
            return false;

        const std::string& spriteGuid = spriteComp->sprite.guid;
        if (spriteGuid.empty())
            return false;

        // Resolve GUID to absolute asset path
        std::string assetPath = AssetDatabase::GUIDToAbsolutePath(spriteGuid);

        // Check if it's a ProceduralSprite
        return IsProceduralSpriteAsset(assetPath);
    }

    void OnInspectorGUI(DekiComponent* comp) override
    {
        auto* spriteComp = static_cast<SpriteComponent*>(comp);
        if (!spriteComp)
            return;

        const std::string& spriteGuid = spriteComp->sprite.guid;

        // Resolve GUID to absolute asset path
        std::string assetPath = AssetDatabase::GUIDToAbsolutePath(spriteGuid);

        // Draw SpriteComponent properties
        EditorGUI::Get().PropertyField("sprite");
        EditorGUI::Get().PropertyField("tint_color");
        EditorGUI::Get().PropertyField("ignore_clip");
        EditorGUI::Get().PropertyField("sortingOrder");

        if (!IsProceduralSpriteAsset(assetPath))
        {
            // Regular sprite - no additional properties
            return;
        }

        ImGui::Separator();
        ImGui::Text("Procedural Sprite");

        // Read the ProceduralSprite JSON
        std::ifstream file(assetPath);
        if (!file.is_open())
        {
            ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "Failed to open asset file");
            return;
        }

        nlohmann::json originalData;
        try
        {
            file >> originalData;
        }
        catch (const std::exception& e)
        {
            ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "Failed to parse JSON: %s", e.what());
            file.close();
            return;
        }
        file.close();

        // Work on a copy
        nlohmann::json jsonData = originalData;
        bool modified = false;

        // Dimensions
        float lw = ImGui::GetContentRegionAvail().x * 0.4f;
        int width = jsonData.value("width", 64);
        int height = jsonData.value("height", 64);
        ImGui::AlignTextToFramePadding(); ImGui::Text("Width"); ImGui::SameLine(lw); ImGui::SetNextItemWidth(-1);
        if (ImGui::DragInt("##width", &width, 1.0f, 1, 1024))
        {
            jsonData["width"] = width;
            modified = true;
        }
        ImGui::AlignTextToFramePadding(); ImGui::Text("Height"); ImGui::SameLine(lw); ImGui::SetNextItemWidth(-1);
        if (ImGui::DragInt("##height", &height, 1.0f, 1, 1024))
        {
            jsonData["height"] = height;
            modified = true;
        }

        // Background color
        float bgColor[4] = {1.0f, 1.0f, 1.0f, 1.0f};
        if (jsonData.contains("background_color") && jsonData["background_color"].is_array() && jsonData["background_color"].size() >= 4)
        {
            bgColor[0] = jsonData["background_color"][0].get<int>() / 255.0f;
            bgColor[1] = jsonData["background_color"][1].get<int>() / 255.0f;
            bgColor[2] = jsonData["background_color"][2].get<int>() / 255.0f;
            bgColor[3] = jsonData["background_color"][3].get<int>() / 255.0f;
        }
        ImGui::AlignTextToFramePadding(); ImGui::Text("Background"); ImGui::SameLine(lw); ImGui::SetNextItemWidth(-1);
        if (ImGui::ColorEdit4("##bgColor", bgColor))
        {
            jsonData["background_color"] = {
                static_cast<int>(bgColor[0] * 255),
                static_cast<int>(bgColor[1] * 255),
                static_cast<int>(bgColor[2] * 255),
                static_cast<int>(bgColor[3] * 255)
            };
            modified = true;
        }

        // Border Width
        ImGui::Text("Border Width");
        int32_t bwTop = 0, bwRight = 0, bwBottom = 0, bwLeft = 0;
        ParseBorderWidth(jsonData, bwTop, bwRight, bwBottom, bwLeft);

        bool bwChanged = false;
        ImGui::AlignTextToFramePadding(); ImGui::Text("Top"); ImGui::SameLine(lw); ImGui::SetNextItemWidth(-1);
        if (ImGui::DragInt("##bwTop", &bwTop, 1.0f, 0, 100)) bwChanged = true;
        ImGui::AlignTextToFramePadding(); ImGui::Text("Right"); ImGui::SameLine(lw); ImGui::SetNextItemWidth(-1);
        if (ImGui::DragInt("##bwRight", &bwRight, 1.0f, 0, 100)) bwChanged = true;
        ImGui::AlignTextToFramePadding(); ImGui::Text("Bottom"); ImGui::SameLine(lw); ImGui::SetNextItemWidth(-1);
        if (ImGui::DragInt("##bwBottom", &bwBottom, 1.0f, 0, 100)) bwChanged = true;
        ImGui::AlignTextToFramePadding(); ImGui::Text("Left"); ImGui::SameLine(lw); ImGui::SetNextItemWidth(-1);
        if (ImGui::DragInt("##bwLeft", &bwLeft, 1.0f, 0, 100)) bwChanged = true;

        if (bwChanged)
        {
            jsonData["border_width"] = {bwTop, bwRight, bwBottom, bwLeft};
            modified = true;
        }

        // Border Color
        ImGui::Text("Border Color");
        float borderColor[4] = {0.0f, 0.0f, 0.0f, 1.0f};
        if (jsonData.contains("border_color") && jsonData["border_color"].is_array() && jsonData["border_color"].size() >= 4)
        {
            borderColor[0] = jsonData["border_color"][0].get<int>() / 255.0f;
            borderColor[1] = jsonData["border_color"][1].get<int>() / 255.0f;
            borderColor[2] = jsonData["border_color"][2].get<int>() / 255.0f;
            borderColor[3] = jsonData["border_color"][3].get<int>() / 255.0f;
        }
        ImGui::AlignTextToFramePadding(); ImGui::Text("Color"); ImGui::SameLine(lw); ImGui::SetNextItemWidth(-1);
        if (ImGui::ColorEdit4("##borderColor", borderColor))
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

        // Border Radius
        ImGui::Text("Border Radius");
        int32_t brTL = 0, brTR = 0, brBR = 0, brBL = 0;
        ParseBorderRadius(jsonData, brTL, brTR, brBR, brBL);

        bool brChanged = false;
        ImGui::AlignTextToFramePadding(); ImGui::Text("Top Left"); ImGui::SameLine(lw); ImGui::SetNextItemWidth(-1);
        if (ImGui::DragInt("##brTL", &brTL, 1.0f, 0, 200)) brChanged = true;
        ImGui::AlignTextToFramePadding(); ImGui::Text("Top Right"); ImGui::SameLine(lw); ImGui::SetNextItemWidth(-1);
        if (ImGui::DragInt("##brTR", &brTR, 1.0f, 0, 200)) brChanged = true;
        ImGui::AlignTextToFramePadding(); ImGui::Text("Bottom Right"); ImGui::SameLine(lw); ImGui::SetNextItemWidth(-1);
        if (ImGui::DragInt("##brBR", &brBR, 1.0f, 0, 200)) brChanged = true;
        ImGui::AlignTextToFramePadding(); ImGui::Text("Bottom Left"); ImGui::SameLine(lw); ImGui::SetNextItemWidth(-1);
        if (ImGui::DragInt("##brBL", &brBL, 1.0f, 0, 200)) brChanged = true;

        if (brChanged)
        {
            jsonData["border_radius"] = {brTL, brTR, brBR, brBL};
            modified = true;
        }

        // Antialiased
        bool antialiased = jsonData.value("antialiased", false);
        ImGui::AlignTextToFramePadding(); ImGui::Text("Antialiased"); ImGui::SameLine(lw);
        if (ImGui::Checkbox("##antialiased", &antialiased))
        {
            jsonData["antialiased"] = antialiased;
            modified = true;
        }

        // Apply changes through command system if modified
        if (modified)
        {
            EditorGUI::Get().ModifyAsset(
                assetPath,
                spriteGuid,
                originalData.dump(2),
                jsonData.dump(2)
            );
        }
    }
};

REGISTER_EDITOR(SpriteCustomEditor)
REGISTER_CREATE_MENU_ITEM(SpriteComponent, "2D", "Sprite", "Sprite", "SpriteComponent")

} // namespace DekiEditor

#endif // DEKI_EDITOR
