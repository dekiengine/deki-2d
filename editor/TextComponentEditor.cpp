// Editor support for TextComponent: its Inspector (baked font sizes, font
// preview) and its display size.

#ifdef DEKI_EDITOR

#include <deki-editor/EditorRegistry.h>
#include <deki-editor/EditorAssets.h>
#include <deki-editor/CustomEditor.h>
#include <deki-editor/SceneView.h>
#include <deki-editor/EditorUI.h>
#include <deki-editor/AssetPipeline.h>
#include <deki-editor/AssetData.h>
#include "editor/FontSyncHandler.h"
#include "TextComponent.h"
#include "BitmapFont.h"
#include <deki/Object.h>
#include <deki/Engine.h>
#include <deki/LogSystem.h>
#include <deki/Guid.h>
#include <deki/Color.h>
#include <cstdio>
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
        // When the editor is destroyed (e.g. on a scene switch), turn the
        // preview off and clear the preview font cache, so nothing stale stays.
        if (m_TextComponent && m_TextComponent->previewEnabled)
        {
            m_TextComponent->previewEnabled = false;

            // Invalidate the preview font cache if a preview was shown.
            if (!m_LastSource.empty() && m_LastPreviewSize > 0)
            {
                std::string previewSeed = "preview:" + m_LastSource + ":" + std::to_string(m_LastPreviewSize);
                std::string previewGuid = Deki::GenerateDeterministicGuid(previewSeed);
                EditorAssets::Get()->InvalidateFontAtlas(previewGuid);
            }
        }
    }

    const char* GetComponentName() const override { return "TextComponent"; }

private:
    // The component being edited, for the cleanup in the destructor.
    TextComponent* m_TextComponent = nullptr;

    // The last state seen, to detect changes. Each component gets its own
    // editor instance, so this belongs to one component.
    std::string m_LastSource;
    int32_t m_LastRenderSize = 0;  // the size actually rendered
    bool m_LastPreviewEnabled = false;
    int32_t m_LastPreviewSize = 0;  // for invalidating the preview cache

    /// The baked sizes listed in the .data file of the font `sourceGuid`
    /// (a TTF/OTF asset); empty if none.
    std::vector<int32_t> GetBakedSizesFromData(const std::string& sourceGuid)
    {
        std::vector<int32_t> sizes;

        auto* pipeline = DekiEditor::AssetPipeline::Instance();
        const DekiEditor::AssetInfo* fontInfo = pipeline->GetAssetInfoByGuid(sourceGuid);
        if (!fontInfo)
        {
            return sizes;
        }

        namespace fs = std::filesystem;
        std::string fontPath = (fs::path(pipeline->GetProjectPath()) / fontInfo->path).string();
        std::string dataPath = fontPath + ".data";

        if (!fs::exists(dataPath))
        {
            return sizes;
        }

        std::ifstream file(dataPath);
        if (!file.is_open())
        {
            return sizes;
        }

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

            std::sort(sizes.begin(), sizes.end());
        }
        catch (...)
        {
            // Parse error
        }

        return sizes;
    }

    /// The variant GUID for `fontSize` from the .data file of the font
    /// `sourceGuid` (a TTF/OTF asset), or "" if none.
    std::string GetVariantGuidFromData(const std::string& sourceGuid, int32_t fontSize)
    {
        auto* pipeline = DekiEditor::AssetPipeline::Instance();
        const DekiEditor::AssetInfo* fontInfo = pipeline->GetAssetInfoByGuid(sourceGuid);
        if (!fontInfo)
        {
            return "";
        }

        namespace fs = std::filesystem;
        std::string fontPath = (fs::path(pipeline->GetProjectPath()) / fontInfo->path).string();
        std::string dataPath = fontPath + ".data";

        if (!fs::exists(dataPath))
        {
            return "";
        }

        std::ifstream file(dataPath);
        if (!file.is_open())
        {
            return "";
        }

        try
        {
            nlohmann::json j = nlohmann::json::parse(file);
            std::string sizeKey = std::to_string(fontSize);

            if (j.contains("variants") && j["variants"].contains(sizeKey) && j["variants"][sizeKey].contains("guid"))
            {
                return j["variants"][sizeKey]["guid"].get<std::string>();
            }
        }
        catch (...)
        {
            // Parse error: "".
        }

        return "";
    }

    std::string GetBdfVariantGuidFromData(const std::string& sourceGuid)
    {
        auto* pipeline = DekiEditor::AssetPipeline::Instance();
        const DekiEditor::AssetInfo* fontInfo = pipeline->GetAssetInfoByGuid(sourceGuid);
        if (!fontInfo)
        {
            return "";
        }

        namespace fs = std::filesystem;
        std::string fontPath = (fs::path(pipeline->GetProjectPath()) / fontInfo->path).string();
        std::string dataPath = fontPath + ".data";

        if (!fs::exists(dataPath))
        {
            return "";
        }

        std::ifstream file(dataPath);
        if (!file.is_open())
        {
            return "";
        }

        try
        {
            nlohmann::json j = nlohmann::json::parse(file);
            if (j.contains("variants") && j["variants"].contains("bdf") && j["variants"]["bdf"].contains("guid"))
            {
                return j["variants"]["bdf"]["guid"].get<std::string>();
            }
        }
        catch (...)
        {
        }

        return "";
    }

    void UpdateBakedFontGuid(TextComponent* textComp)
    {
        if (!textComp)
        {
            return;
        }

        // The source GUID: font.source if set, otherwise font.guid is the source.
        std::string resolvedSource = textComp->font.source.empty() ? textComp->font.guid : textComp->font.source;
        if (resolvedSource.empty())
        {
            return;
        }

        m_TextComponent = textComp;

        // The size actually rendered, which depends on the preview.
        int32_t actualRenderSize = textComp->previewEnabled ? textComp->previewSize : textComp->fontSize;

        // Did anything that affects rendering change?
        bool sourceChanged = (resolvedSource != m_LastSource);
        bool sizeChanged = (actualRenderSize != m_LastRenderSize);
        bool previewStateChanged = (textComp->previewEnabled != m_LastPreviewEnabled);

        if (!sourceChanged && !sizeChanged && !previewStateChanged)
        {
            return;
        }

        // The variant GUID: deterministic for BDF fonts, from the fontSize key
        // in .data for TTF.
        std::string variantGuid;
        {
            auto* pl = DekiEditor::AssetPipeline::Instance();
            const DekiEditor::AssetInfo* fi = pl ? pl->GetAssetInfoByGuid(resolvedSource) : nullptr;
            if (fi)
            {
                std::string ext = std::filesystem::path(fi->path).extension().string();
                for (char& c : ext)
                {
                    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                }
                if (ext == ".bdf")
                {
                    variantGuid = Deki::GenerateDeterministicGuid(resolvedSource + ":bdf");
                    Deki::AssetManager::Get()->RegisterGuid(variantGuid, variantGuid);
                    // Set font.source, which later lookups need.
                    if (textComp->font.source.empty())
                    {
                        textComp->font.source = resolvedSource;
                    }
                }
                else
                {
                    variantGuid = GetVariantGuidFromData(resolvedSource, textComp->fontSize);
                }
            }
        }
        if (!variantGuid.empty() && textComp->font.guid != variantGuid)
        {
            textComp->font.guid = variantGuid;
            textComp->font.ptr = nullptr;
            textComp->font.loadAttempted = false;
        }

        // Remember all of it, preview included.
        m_LastSource = resolvedSource;
        m_LastRenderSize = actualRenderSize;
        m_LastPreviewEnabled = textComp->previewEnabled;
        m_LastPreviewSize = textComp->previewSize;
    }

    /// Makes sure the font `sourceGuid` (a TTF asset) is baked at `size`: the
    /// size is saved in its .data file and compiled.
    void EnsureFontSizeBaked(const std::string& sourceGuid, int32_t size)
    {
        // FontSyncHandler has the one baking path.
        Deki2D::EnsureFontSizeBaked(sourceGuid, size);
    }

public:
    // width/height are world meters; gizmo consumers want pixels.
    bool GetDisplaySize(Deki::Component* comp, float& outWidth, float& outHeight) override
    {
        auto* textComp = static_cast<TextComponent*>(comp);
        if (!textComp)
        {
            return false;
        }

        const float ppm = Deki::EngineSettings::Global().pixelsPerMeter;
        const float effective = ppm > 0.0f ? ppm : 1.0f;
        outWidth = textComp->width * effective;
        outHeight = textComp->height * effective;
        return true;
    }

    bool HitTest(Deki::Component* comp, float localX, float localY, float width, float height) override
    {
        float halfW = width * 0.5f;
        float halfH = height * 0.5f;
        return (localX >= -halfW && localX <= halfW && localY >= -halfH && localY <= halfH);
    }

    bool WantsInspectorOverride(Deki::Component* comp) override { return true; }

    void OnInspectorGUI(Deki::Component* comp) override
    {
        auto* textComp = static_cast<TextComponent*>(comp);
        if (!textComp)
        {
            return;
        }

        UpdateBakedFontGuid(textComp);

        auto& gui = EditorUI::Get();

        // Properties one by one; fontSize gets its own dropdown below.
        gui.PropertyField("text");
        gui.PropertyField("font");

        const std::string& sourceGuid = textComp->font.source.empty() ? textComp->font.guid : textComp->font.source;

        // A BDF font? (by the source's extension)
        bool isBdfFont = false;
        if (!sourceGuid.empty())
        {
            auto* pipeline = DekiEditor::AssetPipeline::Instance();
            if (pipeline)
            {
                const DekiEditor::AssetInfo* fontInfo = pipeline->GetAssetInfoByGuid(sourceGuid);
                if (fontInfo)
                {
                    std::string ext = std::filesystem::path(fontInfo->path).extension().string();
                    for (char& c : ext)
                    {
                        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                    }
                    isBdfFont = (ext == ".bdf");
                }
            }
        }

        // Pixel Scale (useful for bitmap/BDF fonts, available for all)
        {
            gui.PropertyRow("Pixel Scale");
            // Styled drag int (no +/- steppers); DragInt clamps to [1, 8].
            gui.DragInt("##pixelScale", &textComp->pixelScale, 1.0f, 1, 8);
        }

        // fontSize dropdown listing only baked sizes (not for BDF fonts).
        if (!sourceGuid.empty() && !isBdfFont)
        {
            std::vector<int32_t> bakedSizes = GetBakedSizesFromData(sourceGuid);

            if (!bakedSizes.empty())
            {
                int currentIndex = -1;
                for (size_t i = 0; i < bakedSizes.size(); i++)
                {
                    if (bakedSizes[i] == textComp->fontSize)
                    {
                        currentIndex = static_cast<int>(i);
                        break;
                    }
                }

                std::string previewText = (currentIndex >= 0) ? std::to_string(bakedSizes[currentIndex]) + " px"
                                                              : std::to_string(textComp->fontSize) + " px (not baked)";

                gui.PropertyRow("Font Size");
                if (gui.BeginCombo("##fontSize", previewText.c_str()))
                {
                    for (size_t i = 0; i < bakedSizes.size(); i++)
                    {
                        bool isSelected = (static_cast<int>(i) == currentIndex);
                        std::string label = std::to_string(bakedSizes[i]) + " px";

                        if (gui.Selectable(label.c_str(), isSelected))
                        {
                            textComp->fontSize = bakedSizes[i];
                        }
                    }
                    gui.EndCombo();
                }

                if (currentIndex < 0)
                {
                    gui.SameLine();
                    gui.TextColored(EditorUI::Rgba(255, 178, 0, 255), "!");
                    if (gui.IsItemHovered())
                    {
                        char tipBuf[256];
                        std::snprintf(tipBuf, sizeof(tipBuf),
                                      "Current size %d is not baked. Use Font Preview to bake it.", textComp->fontSize);
                        gui.SetTooltip(tipBuf);
                    }
                }
            }
            else
            {
                // No baked sizes: show a warning and the current value.
                char sizeBuf[128];
                std::snprintf(sizeBuf, sizeof(sizeBuf), "Font Size: %d px", textComp->fontSize);
                gui.Text(sizeBuf);
                gui.SameLine();
                gui.TextColored(EditorUI::Rgba(255, 128, 0, 255), "(no baked sizes)");
                gui.TextDisabled("Use Font Preview below to bake a size.");
            }
        }
        else
        {
            // No font assigned: just show the value.
            char sizeBuf[128];
            std::snprintf(sizeBuf, sizeof(sizeBuf), "Font Size: %d px", textComp->fontSize);
            gui.Text(sizeBuf);
            gui.TextDisabled("Assign a font to see baked sizes.");
        }

        gui.PropertyField("width");
        gui.PropertyField("height");
        gui.PropertyField("color");
        gui.PropertyField("decorationColor");
        gui.PropertyField("align");
        gui.PropertyField("verticalAlign");
        gui.PropertyField("sortingOrder");

        // Font Preview: try sizes without baking them (TTF only).
        if (!sourceGuid.empty() && !isBdfFont)
        {
            gui.Separator();

            if (gui.SectionHeader("Font Preview"))
            {
                gui.TextDisabled("Preview font at different sizes without baking to disk.");
                gui.Spacing();

                bool prevPreviewEnabled = textComp->previewEnabled;
                {
                    gui.Checkbox("Enable Preview", &textComp->previewEnabled);
                }

                if (textComp->previewEnabled)
                {
                    int32_t prevPreviewSize = textComp->previewSize;
                    gui.PropertyRow("Preview Size");
                    gui.InputInt("##previewSize", &textComp->previewSize);
                    textComp->previewSize = (std::max)(6, (std::min)(128, textComp->previewSize));

                    // Log when the preview settings change.
                    if (prevPreviewEnabled != textComp->previewEnabled || prevPreviewSize != textComp->previewSize)
                    {
                        DEKI_LOG_DEBUG("TextComponentEditor: Preview changed - enabled=%d, size=%d",
                                       textComp->previewEnabled, textComp->previewSize);
                    }

                    // The preview atlas.
                    uint32_t atlasWidth = 0, atlasHeight = 0;
                    uint32_t textureId =
                        gui.GetFontAtlasTexture(sourceGuid, textComp->previewSize, &atlasWidth, &atlasHeight);

                    if (textureId != 0 && atlasWidth > 0 && atlasHeight > 0)
                    {
                        gui.Spacing();
                        char atlasBuf[128];
                        std::snprintf(atlasBuf, sizeof(atlasBuf), "Preview Atlas: %u x %u", atlasWidth, atlasHeight);
                        gui.Text(atlasBuf);

                        // Display size, at most 256px tall.
                        float maxHeight = 256.0f;
                        float scale = (atlasHeight > maxHeight) ? (maxHeight / atlasHeight) : 1.0f;

                        gui.Image(textureId, atlasWidth * scale, atlasHeight * scale);

                        gui.Spacing();

                        // Save and Use: saves the size to .data, which bakes it.
                        if (gui.Button("Save and Use"))
                        {
                            EnsureFontSizeBaked(sourceGuid, textComp->previewSize);

                            textComp->fontSize = textComp->previewSize;
                            textComp->previewEnabled = false;
                        }
                        gui.SameLine();
                        gui.TextDisabled("(Saves to font config and bakes)");
                    }
                }
                else
                {
                    gui.TextDisabled("Enable preview to test different font sizes.");
                }
            }
        }
    }
};

REGISTER_EDITOR(TextComponentEditor)
REGISTER_CREATE_MENU_ITEM(TextComponent, "2D", "Text", "Text", "TextComponent")

}  // namespace DekiEditor

#endif  // DEKI_EDITOR
