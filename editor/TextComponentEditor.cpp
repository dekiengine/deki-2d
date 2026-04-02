/**
 * @file TextComponentEditor.cpp
 * @brief Editor-only rendering for TextComponent
 *
 * Uses CustomEditor with PrefabView::Get() and EditorGUI::Get() singletons.
 */

#ifdef DEKI_EDITOR

#include <deki-editor/EditorRegistry.h>
#include <deki-editor/EditorAssets.h>
#include <deki-editor/CustomEditor.h>
#include <deki-editor/PrefabView.h>
#include <deki-editor/EditorGUI.h>
#include <deki-editor/AssetPipeline.h>
#include <deki-editor/AssetData.h>
#include "modules/2d/editor/FontSyncHandler.h"
#include "modules/2d/TextComponent.h"
#include "modules/2d/BitmapFont.h"
#include "DekiObject.h"
#include "DekiEngine.h"
#include "DekiLogSystem.h"
#include "Guid.h"
#include "Color.h"
#include "imgui.h"
#include <cmath>
#include <algorithm>
#include <vector>
#include <string>
#include <sstream>
#include <fstream>
#include <filesystem>
#include <nlohmann/json.hpp>

namespace DekiEditor
{

class TextComponentEditor : public CustomEditor
{
public:
    TextComponentEditor() = default;

    ~TextComponentEditor()
    {
        // When editor is destroyed (e.g., switching prefabs), disable preview
        // and clear the preview font cache to avoid stale state
        if (m_TextComponent && m_TextComponent->previewEnabled)
        {
            m_TextComponent->previewEnabled = false;

            // Invalidate the preview font cache if we were previewing
            if (!m_LastSource.empty() && m_LastPreviewSize > 0)
            {
                std::string previewSeed = "preview:" + m_LastSource + ":" + std::to_string(m_LastPreviewSize);
                std::string previewGuid = Deki::GenerateDeterministicGuid(previewSeed);
                EditorAssets::Get()->InvalidateFontAtlas(previewGuid);
            }
        }
    }

    const char* GetComponentName() const override
    {
        return "TextComponent";
    }

private:
    // Pointer to the component we're editing (for cleanup in destructor)
    TextComponent* m_TextComponent = nullptr;

    // Cached state for detecting changes within a single component.
    // Since factory pattern now creates fresh editor instances per-component,
    // these member variables start fresh for each component and don't persist
    // across different components.
    std::string m_LastSource;
    int32_t m_LastRenderSize = 0;      // Track the actual render size used
    bool m_LastPreviewEnabled = false; // Track preview state changes
    int32_t m_LastPreviewSize = 0;     // Track preview size for cache invalidation

    /**
     * @brief Read the baked font sizes from the font's .data file
     * @param sourceGuid The source font GUID (TTF/OTF file)
     * @return Vector of baked sizes, empty if none found
     */
    std::vector<int32_t> GetBakedSizesFromData(const std::string& sourceGuid)
    {
        std::vector<int32_t> sizes;

        auto* pipeline = DekiEditor::AssetPipeline::Instance();
        const DekiEditor::AssetInfo* fontInfo = pipeline->GetAssetInfoByGuid(sourceGuid);
        if (!fontInfo)
            return sizes;

        namespace fs = std::filesystem;
        std::string fontPath = (fs::path(pipeline->GetProjectPath()) / fontInfo->path).string();
        std::string dataPath = fontPath + ".data";

        if (!fs::exists(dataPath))
            return sizes;

        std::ifstream file(dataPath);
        if (!file.is_open())
            return sizes;

        try
        {
            nlohmann::json j = nlohmann::json::parse(file);

            if (j.contains("fontSettings") && j["fontSettings"].contains("sizes"))
            {
                for (const auto& sizeVal : j["fontSettings"]["sizes"])
                {
                    if (sizeVal.is_number_integer())
                    {
                        sizes.push_back(sizeVal.get<int32_t>());
                    }
                }
            }

            // Sort sizes for display
            std::sort(sizes.begin(), sizes.end());
        }
        catch (...)
        {
            // Parse error
        }

        return sizes;
    }

    /**
     * @brief Read the variant GUID from the font's .data file
     * @param sourceGuid The source font GUID (TTF/OTF file)
     * @param fontSize The font size to look up
     * @return The variant GUID if found, empty string otherwise
     */
    std::string GetVariantGuidFromData(const std::string& sourceGuid, int32_t fontSize)
    {
        auto* pipeline = DekiEditor::AssetPipeline::Instance();
        const DekiEditor::AssetInfo* fontInfo = pipeline->GetAssetInfoByGuid(sourceGuid);
        if (!fontInfo)
            return "";

        namespace fs = std::filesystem;
        std::string fontPath = (fs::path(pipeline->GetProjectPath()) / fontInfo->path).string();
        std::string dataPath = fontPath + ".data";

        if (!fs::exists(dataPath))
            return "";

        std::ifstream file(dataPath);
        if (!file.is_open())
            return "";

        try
        {
            nlohmann::json j = nlohmann::json::parse(file);
            std::string sizeKey = std::to_string(fontSize);

            if (j.contains("variants") &&
                j["variants"].contains(sizeKey) &&
                j["variants"][sizeKey].contains("guid"))
            {
                return j["variants"][sizeKey]["guid"].get<std::string>();
            }
        }
        catch (...)
        {
            // Parse error, return empty
        }

        return "";
    }

    void UpdateBakedFontGuid(TextComponent* textComp)
    {
        if (!textComp || textComp->font.source.empty())
            return;

        // Store component pointer for destructor cleanup
        m_TextComponent = textComp;

        // Calculate the actual render size based on preview state
        int32_t actualRenderSize = textComp->previewEnabled
            ? textComp->previewSize
            : textComp->fontSize;

        // Check if anything rendering-relevant changed
        bool sourceChanged = (textComp->font.source != m_LastSource);
        bool sizeChanged = (actualRenderSize != m_LastRenderSize);
        bool previewStateChanged = (textComp->previewEnabled != m_LastPreviewEnabled);

        if (!sourceChanged && !sizeChanged && !previewStateChanged)
            return;

        // Read the variant GUID from the font's .data file
        // This is the actual GUID used when the font was baked
        std::string variantGuid = GetVariantGuidFromData(textComp->font.source, textComp->fontSize);
        if (!variantGuid.empty() && textComp->font.guid != variantGuid)
        {
            textComp->font.guid = variantGuid;
            textComp->font.ptr = nullptr;
            textComp->font.loadAttempted = false;
        }

        // Cache ALL state including preview
        m_LastSource = textComp->font.source;
        m_LastRenderSize = actualRenderSize;
        m_LastPreviewEnabled = textComp->previewEnabled;
        m_LastPreviewSize = textComp->previewSize;
    }

    /**
     * @brief Ensure a font size is baked to disk (saved in .data file and compiled)
     * @param sourceGuid The source font GUID (TTF file)
     * @param size The font size to bake
     */
    void EnsureFontSizeBaked(const std::string& sourceGuid, int32_t size)
    {
        // Delegate to unified baking path in FontSyncHandler
        Deki2D::EnsureFontSizeBaked(sourceGuid, size);
    }

public:

    bool GetDisplaySize(DekiComponent* comp, float& outWidth, float& outHeight) override
    {
        auto* textComp = static_cast<TextComponent*>(comp);
        if (!textComp)
            return false;

        outWidth = static_cast<float>(textComp->width);
        outHeight = static_cast<float>(textComp->height);

        return true;
    }

    bool HitTest(DekiComponent* comp, float localX, float localY, float width, float height) override
    {
        float halfW = width * 0.5f;
        float halfH = height * 0.5f;
        return (localX >= -halfW && localX <= halfW && localY >= -halfH && localY <= halfH);
    }

    bool SupportsResize() const override
    {
        return true;
    }

    void GetResizeTarget(DekiComponent* comp, int32_t** outWidth, int32_t** outHeight) override
    {
        auto* textComp = static_cast<TextComponent*>(comp);
        if (textComp)
        {
            if (outWidth) *outWidth = &textComp->width;
            if (outHeight) *outHeight = &textComp->height;
        }
    }

    bool WantsInspectorOverride(DekiComponent* comp) override
    {
        return true;
    }

    void OnInspectorGUI(DekiComponent* comp) override
    {
        auto* textComp = static_cast<TextComponent*>(comp);
        if (!textComp)
            return;

        UpdateBakedFontGuid(textComp);

        // Get EditorGUI singleton
        auto& gui = EditorGUI::Get();

        // Draw properties individually, skipping fontSize (we'll draw a custom dropdown)
        gui.PropertyField("text");
        gui.PropertyField("font");

        // Get source GUID for font operations
        const std::string& sourceGuid = textComp->font.source.empty()
            ? textComp->font.guid : textComp->font.source;

        // Draw custom fontSize dropdown with baked sizes only (right after font)
        if (!sourceGuid.empty())
        {
            std::vector<int32_t> bakedSizes = GetBakedSizesFromData(sourceGuid);

            if (!bakedSizes.empty())
            {
                // Find current selection index
                int currentIndex = -1;
                for (size_t i = 0; i < bakedSizes.size(); i++)
                {
                    if (bakedSizes[i] == textComp->fontSize)
                    {
                        currentIndex = static_cast<int>(i);
                        break;
                    }
                }

                // Build combo items
                std::string previewText = (currentIndex >= 0)
                    ? std::to_string(bakedSizes[currentIndex]) + " px"
                    : std::to_string(textComp->fontSize) + " px (not baked)";

                float labelWidth = ImGui::GetContentRegionAvail().x * 0.4f;
                ImGui::AlignTextToFramePadding();
                ImGui::Text("Font Size");
                ImGui::SameLine(labelWidth);
                ImGui::SetNextItemWidth(-1);
                if (ImGui::BeginCombo("##fontSize", previewText.c_str()))
                {
                    for (size_t i = 0; i < bakedSizes.size(); i++)
                    {
                        bool isSelected = (static_cast<int>(i) == currentIndex);
                        std::string label = std::to_string(bakedSizes[i]) + " px";

                        if (ImGui::Selectable(label.c_str(), isSelected))
                        {
                            textComp->fontSize = bakedSizes[i];
                        }

                        if (isSelected)
                            ImGui::SetItemDefaultFocus();
                    }
                    ImGui::EndCombo();
                }

                if (currentIndex < 0)
                {
                    ImGui::SameLine();
                    ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.0f, 1.0f), "!");
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("Current size %d is not baked. Use Font Preview to bake it.", textComp->fontSize);
                }
            }
            else
            {
                // No baked sizes - show warning and current value
                ImGui::Text("Font Size: %d px", textComp->fontSize);
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "(no baked sizes)");
                ImGui::TextDisabled("Use Font Preview below to bake a size.");
            }
        }
        else
        {
            // No font assigned - just show the value
            ImGui::Text("Font Size: %d px", textComp->fontSize);
            ImGui::TextDisabled("Assign a font to see baked sizes.");
        }

        // Continue with remaining properties
        gui.PropertyField("width");
        gui.PropertyField("height");
        gui.PropertyField("color");
        gui.PropertyField("align");
        gui.PropertyField("verticalAlign");

        // Font Preview section - allows testing different sizes without baking
        if (!sourceGuid.empty())
        {
            ImGui::Separator();

            if (ImGui::CollapsingHeader("Font Preview", ImGuiTreeNodeFlags_DefaultOpen))
            {
                ImGui::TextDisabled("Preview font at different sizes without baking to disk.");
                ImGui::Spacing();

                // Preview enable checkbox
                bool prevPreviewEnabled = textComp->previewEnabled;
                {
                    float lw = ImGui::GetContentRegionAvail().x * 0.4f;
                    ImGui::AlignTextToFramePadding();
                    ImGui::Text("Enable Preview");
                    ImGui::SameLine(lw);
                    ImGui::Checkbox("##enablePreview", &textComp->previewEnabled);
                }

                if (textComp->previewEnabled)
                {
                    // Preview size input
                    int32_t prevPreviewSize = textComp->previewSize;
                    float lw = ImGui::GetContentRegionAvail().x * 0.4f;
                    ImGui::AlignTextToFramePadding();
                    ImGui::Text("Preview Size");
                    ImGui::SameLine(lw);
                    ImGui::SetNextItemWidth(-1);
                    ImGui::InputInt("##previewSize", &textComp->previewSize);
                    textComp->previewSize = (std::max)(6, (std::min)(128, textComp->previewSize));

                    // Log when preview settings change
                    if (prevPreviewEnabled != textComp->previewEnabled || prevPreviewSize != textComp->previewSize)
                    {
                        DEKI_LOG_EDITOR("TextComponentEditor: Preview changed - enabled=%d, size=%d (component=%p)",
                                      textComp->previewEnabled, textComp->previewSize, (void*)textComp);
                    }

                    // Show preview atlas
                    uint32_t atlasWidth = 0, atlasHeight = 0;
                    uint32_t textureId = gui.GetFontAtlasTexture(sourceGuid, textComp->previewSize, &atlasWidth, &atlasHeight);

                    if (textureId != 0 && atlasWidth > 0 && atlasHeight > 0)
                    {
                        ImGui::Spacing();
                        ImGui::Text("Preview Atlas: %u x %u", atlasWidth, atlasHeight);

                        // Calculate display size (max 256px height)
                        float maxHeight = 256.0f;
                        float scale = (atlasHeight > maxHeight) ? (maxHeight / atlasHeight) : 1.0f;
                        ImVec2 displaySize(atlasWidth * scale, atlasHeight * scale);

                        ImGui::Image((ImTextureID)(intptr_t)textureId, displaySize);

                        ImGui::Spacing();

                        // Save and Use button - saves size to .data and triggers baking
                        if (ImGui::Button("Save and Use"))
                        {
                            // Ensure the preview size is baked to disk
                            EnsureFontSizeBaked(sourceGuid, textComp->previewSize);

                            // Update component
                            textComp->fontSize = textComp->previewSize;
                            textComp->previewEnabled = false;
                        }
                        ImGui::SameLine();
                        ImGui::TextDisabled("(Saves to font config and bakes)");
                    }
                }
                else
                {
                    ImGui::TextDisabled("Enable preview to test different font sizes.");
                }
            }
        }
    }
};

REGISTER_EDITOR(TextComponentEditor)
REGISTER_CREATE_MENU_ITEM(TextComponent, "2D", "Text", "Text", "TextComponent")

} // namespace DekiEditor

#endif // DEKI_EDITOR
