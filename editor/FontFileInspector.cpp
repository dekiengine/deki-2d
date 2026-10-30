#include "FontFileInspector.h"

#ifdef DEKI_EDITOR

#include "FontCompiler.h"
#include "FontSyncHandler.h"
#include <deki-editor/AssetPipeline.h>
#include <deki-editor/EditorUI.h>
#include <deki-editor/IconsTabler.h>
#include <deki-editor/Localization.h>
#include <nlohmann/json.hpp>
#include <cstdio>
#include <fstream>
#include <filesystem>
#include <algorithm>

#include <deki-editor/EditorTexture.h>

#include <deki/LogSystem.h>

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace Deki2D
{

static const char* s_FontExtensions[] = { ".ttf", ".otf" };

// Hinting mode to and from its JSON string.
static const char* HintingToString(FontCompiler::HintingMode m)
{
    switch (m)
    {
        case FontCompiler::HintingMode::None: return "none";
        case FontCompiler::HintingMode::Normal: return "normal";
        case FontCompiler::HintingMode::Mono: return "mono";
        case FontCompiler::HintingMode::Light:
        default: return "light";
    }
}

static FontCompiler::HintingMode HintingFromString(const std::string& s)
{
    if (s == "none")
    {
        return FontCompiler::HintingMode::None;
    }
    if (s == "normal")
    {
        return FontCompiler::HintingMode::Normal;
    }
    if (s == "mono")
    {
        return FontCompiler::HintingMode::Mono;
    }
    return FontCompiler::HintingMode::Light;
}

// Decoration mode to and from its JSON string.
static const char* DecorationToString(FontCompiler::DecorationMode m)
{
    switch (m)
    {
        case FontCompiler::DecorationMode::Outline: return "outline";
        case FontCompiler::DecorationMode::Shadow: return "shadow";
        case FontCompiler::DecorationMode::None:
        default: return "none";
    }
}

static FontCompiler::DecorationMode DecorationFromString(const std::string& s)
{
    if (s == "outline")
    {
        return FontCompiler::DecorationMode::Outline;
    }
    if (s == "shadow")
    {
        return FontCompiler::DecorationMode::Shadow;
    }
    return FontCompiler::DecorationMode::None;
}

const char** FontFileInspector::GetExtensions() const
{
    return s_FontExtensions;
}

int FontFileInspector::GetExtensionCount() const
{
    return 2;
}

void FontFileInspector::OnInspectorGUI(const std::string& assetPath, const std::string& assetGuid)
{
    auto& ui = DekiEditor::EditorUI::Get();

    // Bakes may land while no text is in view.
    PumpFontBakes();

    if (m_CurrentAssetPath != assetPath)
    {
        DEKI_LOG_DEBUG("FontInspector: Asset changed, loading settings for %s (guid=%s)", assetPath.c_str(),
                       assetGuid.c_str());
        LoadSettings(assetPath);
        m_CurrentAssetPath = assetPath;
        m_LastPendingCount = -1;
    }

    // Every change is saved at once; the sync then bakes again what it made
    // stale.
    bool changed = false;

    // Sections of property rows, as every other inspector: a label column
    // and a value column, hints lined up with the labels, and the actions as
    // centred outlined buttons.
    if (ui.SectionHeader(DekiEditor::Tr("Character Range")))
    {
        ui.PropertyRow("First Character");
        if (ui.DragInt("##firstChar", &m_FirstChar, 0.2f, 0, 255))
        {
            m_FirstChar = (std::max)(0, (std::min)(255, m_FirstChar));
            changed = true;
        }
        ui.PropertyRow("Last Character");
        if (ui.DragInt("##lastChar", &m_LastChar, 0.2f, 0, 255))
        {
            m_LastChar = (std::max)(m_FirstChar, (std::min)(255, m_LastChar));
            changed = true;
        }

        // How many, and the first and last as characters when printable.
        char buf[64];
        const int charCount = m_LastChar - m_FirstChar + 1;
        if (m_FirstChar >= 32 && m_FirstChar <= 126 && m_LastChar >= 32 && m_LastChar <= 126)
        {
            std::snprintf(buf, sizeof(buf), "%d  '%c' - '%c'", charCount, static_cast<char>(m_FirstChar),
                          static_cast<char>(m_LastChar));
        }
        else
        {
            std::snprintf(buf, sizeof(buf), "%d", charCount);
        }
        ui.PropertyRow("Characters");
        ui.ValueText(buf);
    }

    if (ui.SectionHeader(DekiEditor::Tr("Rasterization")))
    {
        static const char* const kHintingLabels[] = { DekiEditor::TrMark("None (unhinted AA)"),
                                                      DekiEditor::TrMark("Light (smoother AA)"),
                                                      DekiEditor::TrMark("Normal (FreeType default)"),
                                                      DekiEditor::TrMark("Mono (1-bit, no AA)") };
        static const FontCompiler::HintingMode kHintingValues[] = {
            FontCompiler::HintingMode::None,
            FontCompiler::HintingMode::Light,
            FontCompiler::HintingMode::Normal,
            FontCompiler::HintingMode::Mono,
        };
        int currentIndex = 1;  // Light
        for (int i = 0; i < 4; ++i)
        {
            if (kHintingValues[i] == m_Hinting)
            {
                currentIndex = i;
                break;
            }
        }
        ui.PropertyRow("Hinting Mode");
        if (ui.Combo("##hinting", &currentIndex, kHintingLabels, 4))
        {
            m_Hinting = kHintingValues[currentIndex];
            changed = true;
        }

        // Oversampling is off with Mono hinting: a 1-bit source gains nothing.
        static const char* const kOversampleLabels[] = { DekiEditor::TrMark("1x (off)"), "2x", "3x", "4x" };
        int oversampleIndex = (std::max)(0, (std::min)(3, m_Oversample - 1));
        const bool oversampleDisabled = (m_Hinting == FontCompiler::HintingMode::Mono);
        if (oversampleDisabled)
        {
            m_Oversample = 1;
            oversampleIndex = 0;
        }
        ui.BeginDisabled(oversampleDisabled);
        ui.PropertyRow("Oversample");
        if (ui.Combo("##oversample", &oversampleIndex, kOversampleLabels, 4))
        {
            m_Oversample = oversampleIndex + 1;
            changed = true;
        }
        ui.EndDisabled();

        // Decoration: a baked outline or drop shadow, as on the Nintendo DS.
        static const char* const kDecorationLabels[] = { DekiEditor::TrMark("None"), DekiEditor::TrMark("Outline"),
                                                         DekiEditor::TrMark("Shadow") };
        static const FontCompiler::DecorationMode kDecorationValues[] = {
            FontCompiler::DecorationMode::None,
            FontCompiler::DecorationMode::Outline,
            FontCompiler::DecorationMode::Shadow,
        };
        int decorationIndex = 0;
        for (int i = 0; i < 3; ++i)
        {
            if (kDecorationValues[i] == m_Decoration)
            {
                decorationIndex = i;
                break;
            }
        }
        ui.PropertyRow("Decoration");
        if (ui.Combo("##decoration", &decorationIndex, kDecorationLabels, 3))
        {
            m_Decoration = kDecorationValues[decorationIndex];
            changed = true;
        }
        if (m_Decoration == FontCompiler::DecorationMode::Outline)
        {
            ui.PropertyRow("Outline Size (px)");
            if (ui.DragInt("##outlineSize", &m_OutlineSize, 0.05f, 1, 3))
            {
                m_OutlineSize = (std::max)(1, (std::min)(3, m_OutlineSize));
                changed = true;
            }
        }
        else if (m_Decoration == FontCompiler::DecorationMode::Shadow)
        {
            ui.PropertyRow("Shadow dX");
            if (ui.DragInt("##shadowDx", &m_ShadowDx, 0.05f, -3, 3))
            {
                m_ShadowDx = (std::max)(-3, (std::min)(3, m_ShadowDx));
                changed = true;
            }
            ui.PropertyRow("Shadow dY");
            if (ui.DragInt("##shadowDy", &m_ShadowDy, 0.05f, -3, 3))
            {
                m_ShadowDy = (std::max)(-3, (std::min)(3, m_ShadowDy));
                changed = true;
            }
        }

        ui.Hint(DekiEditor::Tr("Changes bake the sizes in use again."));
    }

    // The baked list is read again only when bakes start or land.
    const int pendingCount = GetPendingFontBakeCount(assetGuid);
    if (pendingCount != m_LastPendingCount)
    {
        m_BakedSizes = GetBakedFontSizes(assetGuid);
        m_LastPendingCount = pendingCount;
    }

    if (ui.SectionHeader(DekiEditor::Tr("Sizes")))
    {
        std::string baked;
        for (int size : m_BakedSizes)
        {
            baked += (baked.empty() ? "" : ", ") + std::to_string(size);
        }
        ui.PropertyRow("Baked");
        if (baked.empty())
        {
            ui.ValueText(DekiEditor::Tr("None yet"));
        }
        else
        {
            baked += " px";
            ui.ValueText(baked.c_str());
        }
        if (pendingCount > 0)
        {
            char buf[64];
            std::snprintf(buf, sizeof(buf), DekiEditor::Tr("Baking %d sizes..."), pendingCount);
            ui.PropertyRow("Status");
            ui.ValueText(buf);
        }
        ui.Hint(DekiEditor::Tr("The editor bakes each size a text uses. A build ships the sizes its texts use, and "
                               "the sizes below."));

        // Sizes only code sets (a Roller's selected row, a script), which no
        // text in a scene names.
        int indexToRemove = -1;
        for (size_t i = 0; i < m_ExtraSizes.size(); ++i)
        {
            ui.PushID(static_cast<int>(i));
            char label[32];
            std::snprintf(label, sizeof(label), "%d px", m_ExtraSizes[i]);
            ui.PropertyRow("Also Ship");
            ui.ValueText(label);
            ui.SameLine();
            if (ui.SmallButton(ICON_TI_X "##remove"))
            {
                indexToRemove = static_cast<int>(i);
            }
            if (ui.IsItemHovered())
            {
                ui.SetTooltip(DekiEditor::Tr("Remove this size"));
            }
            ui.PopID();
        }
        if (indexToRemove >= 0)
        {
            m_ExtraSizes.erase(m_ExtraSizes.begin() + indexToRemove);
            changed = true;
        }

        ui.PropertyRow("New Size");
        ui.DragInt("##newSize", &m_NewSize, 0.2f, kMinFontSize, kMaxFontSize);
        m_NewSize = (std::max)(kMinFontSize, (std::min)(kMaxFontSize, m_NewSize));
        const bool sizeExists = std::find(m_ExtraSizes.begin(), m_ExtraSizes.end(), m_NewSize) != m_ExtraSizes.end();
        ui.BeginDisabled(sizeExists);
        if (ui.ActionButton(DekiEditor::TrLabel("Ship This Size")) && !sizeExists)
        {
            m_ExtraSizes.push_back(m_NewSize);
            std::sort(m_ExtraSizes.begin(), m_ExtraSizes.end());
            changed = true;
        }
        ui.EndDisabled();
        ui.Hint(DekiEditor::Tr("For sizes only code sets, such as the selected row of a Roller."));

        // Settings changes bake by themselves; this is for anything else.
        ui.BeginDisabled(m_BakedSizes.empty() || pendingCount > 0);
        if (ui.ActionButton(DekiEditor::TrLabel("Rebake")))
        {
            RebakeFont(assetGuid);
            m_LastPendingCount = -1;
        }
        ui.EndDisabled();
        if (ui.IsItemHovered())
        {
            ui.SetTooltip(DekiEditor::Tr("Bake every baked size again"));
        }
    }

    if (changed)
    {
        SaveSettings(assetPath, assetGuid);
        m_LastPendingCount = -1;
    }

    if (ui.SectionHeader(DekiEditor::Tr("Preview")))
    {
        ui.PropertyRow("Preview Size");
        ui.DragInt("##previewSize", &m_PreviewSize, 0.2f, kMinFontSize, kMaxFontSize);
        m_PreviewSize = (std::max)(kMinFontSize, (std::min)(kMaxFontSize, m_PreviewSize));

        ui.PropertyRow("Preview Text");
        ui.InputText("##previewText", m_PreviewText, sizeof(m_PreviewText));

        if (ui.ActionButton(DekiEditor::TrLabel("Generate Preview")))
        {
            GeneratePreview(assetPath, m_PreviewSize);
        }

        if (m_PreviewTextureId != 0 && m_PreviewTextureWidth > 0 && m_PreviewTextureHeight > 0)
        {
            char buf[32];
            std::snprintf(buf, sizeof(buf), "%d px", m_LastPreviewSize);
            ui.PropertyRow("Previewed");
            ui.ValueText(buf);
            ui.Image(m_PreviewTextureId, (float)m_PreviewTextureWidth, (float)m_PreviewTextureHeight);
        }
    }

    // Drop the preview when another asset is shown.
    if (m_LastPreviewAsset != assetPath)
    {
        CleanupPreview();
        m_LastPreviewAsset = assetPath;
    }
}

void FontFileInspector::LoadSettings(const std::string& assetPath)
{
    m_ExtraSizes.clear();
    m_FirstChar = 32;
    m_LastChar = 126;
    m_Hinting = FontCompiler::HintingMode::Light;
    m_Oversample = 2;
    m_Decoration = FontCompiler::DecorationMode::None;
    m_OutlineSize = 1;
    m_ShadowDx = 1;
    m_ShadowDy = 1;

    std::string dataPath = assetPath + ".data";
    if (!fs::exists(dataPath))
    {
        return;
    }

    std::ifstream file(dataPath);
    if (!file.is_open())
    {
        return;
    }

    try
    {
        json j = json::parse(file);

        if (j.contains("fontSettings") && j["fontSettings"].is_object())
        {
            auto& settings = j["fontSettings"];

            if (settings.contains("extraSizes") && settings["extraSizes"].is_array())
            {
                for (const auto& size : settings["extraSizes"])
                {
                    if (size.is_number_integer())
                    {
                        m_ExtraSizes.push_back(size.get<int>());
                    }
                }
            }

            if (settings.contains("firstChar") && settings["firstChar"].is_number_integer())
            {
                m_FirstChar = settings["firstChar"].get<int>();
            }

            if (settings.contains("lastChar") && settings["lastChar"].is_number_integer())
            {
                m_LastChar = settings["lastChar"].get<int>();
            }

            if (settings.contains("hinting") && settings["hinting"].is_string())
            {
                m_Hinting = HintingFromString(settings["hinting"].get<std::string>());
            }

            if (settings.contains("oversample") && settings["oversample"].is_number_integer())
            {
                int v = settings["oversample"].get<int>();
                if (v < 1)
                {
                    v = 1;
                }
                if (v > 4)
                {
                    v = 4;
                }
                m_Oversample = v;
            }

            if (settings.contains("decoration") && settings["decoration"].is_string())
            {
                m_Decoration = DecorationFromString(settings["decoration"].get<std::string>());
            }
            if (settings.contains("outlineSize") && settings["outlineSize"].is_number_integer())
            {
                int v = settings["outlineSize"].get<int>();
                if (v < 1)
                {
                    v = 1;
                }
                if (v > 3)
                {
                    v = 3;
                }
                m_OutlineSize = v;
            }
            if (settings.contains("shadowDx") && settings["shadowDx"].is_number_integer())
            {
                int v = settings["shadowDx"].get<int>();
                if (v < -3)
                {
                    v = -3;
                }
                if (v > 3)
                {
                    v = 3;
                }
                m_ShadowDx = v;
            }
            if (settings.contains("shadowDy") && settings["shadowDy"].is_number_integer())
            {
                int v = settings["shadowDy"].get<int>();
                if (v < -3)
                {
                    v = -3;
                }
                if (v > 3)
                {
                    v = 3;
                }
                m_ShadowDy = v;
            }
        }
    }
    catch (const json::exception&)
    {
        // A file that does not parse is ignored.
    }
}

void FontFileInspector::SaveSettings(const std::string& assetPath, const std::string& assetGuid)
{
    std::string dataPath = assetPath + ".data";

    // Start from the existing .data file, if any.
    json j;
    if (fs::exists(dataPath))
    {
        std::ifstream file(dataPath);
        if (file.is_open())
        {
            try
            {
                j = json::parse(file);
            }
            catch (const json::exception&)
            {
                j = json::object();
            }
        }
    }

    if (!assetGuid.empty())
    {
        j["guid"] = assetGuid;
    }

    // Sizes are not listed: the texts that use them are the record. The
    // "sizes" and "variants" keys older versions wrote are dropped.
    j["fontSettings"] = json::object();
    j["fontSettings"]["firstChar"] = m_FirstChar;
    j["fontSettings"]["lastChar"] = m_LastChar;
    j["fontSettings"]["hinting"] = HintingToString(m_Hinting);
    j["fontSettings"]["oversample"] = m_Oversample;
    j["fontSettings"]["decoration"] = DecorationToString(m_Decoration);
    j["fontSettings"]["outlineSize"] = m_OutlineSize;
    j["fontSettings"]["shadowDx"] = m_ShadowDx;
    j["fontSettings"]["shadowDy"] = m_ShadowDy;
    if (!m_ExtraSizes.empty())
    {
        j["fontSettings"]["extraSizes"] = m_ExtraSizes;
    }
    j.erase("variants");

    // Closed before the refresh below reads it.
    std::ofstream outFile(dataPath);
    if (outFile.is_open())
    {
        outFile << j.dump(2);
        outFile.close();
    }

    // The pipeline syncs the font again, which bakes what the change made
    // stale.
    auto* pipeline = DekiEditor::AssetPipeline::Instance();
    if (pipeline)
    {
        pipeline->RefreshAsset(pipeline->MakeProjectRelative(assetPath));
    }
}

void FontFileInspector::GeneratePreview(const std::string& assetPath, int fontSize)
{
    FontCompiler::CompileOptions options;
    options.fontSize = fontSize;
    options.firstChar = m_FirstChar;
    options.lastChar = m_LastChar;
    options.padding = 2;
    options.maxAtlasSize = 2048;
    options.hinting = m_Hinting;
    options.oversample = m_Oversample;
    options.decoration = m_Decoration;
    options.outlineSize = m_OutlineSize;
    options.shadowDx = m_ShadowDx;
    options.shadowDy = m_ShadowDy;

    FontCompiler::CompileResult result;
    if (!FontCompiler::CompileTrueTypeFont(assetPath, options, result))
    {
        DEKI_LOG_ERROR("Failed to compile font for preview: %s @ %d px", assetPath.c_str(), fontSize);
        return;
    }

    const char* text = m_PreviewText;
    int textLen = static_cast<int>(strlen(text));
    if (textLen == 0)
    {
        text = "Preview";
    }

    int textWidth = 0;
    int textHeight = result.lineHeight;
    for (int i = 0; text[i] != '\0'; ++i)
    {
        unsigned char c = static_cast<unsigned char>(text[i]);
        if (c >= result.firstChar && c <= result.lastChar)
        {
            const GlyphInfo& glyph = result.glyphs[c - result.firstChar];
            textWidth += glyph.advance;
        }
    }

    // 4 px of padding on each side.
    int bufferWidth = textWidth + 8;
    int bufferHeight = textHeight + 8;

    std::vector<uint8_t> buffer(bufferWidth * bufferHeight * 4, 0);

    // Dark background.
    for (int i = 0; i < bufferWidth * bufferHeight; ++i)
    {
        buffer[i * 4 + 0] = 40;   // R
        buffer[i * 4 + 1] = 40;   // G
        buffer[i * 4 + 2] = 40;   // B
        buffer[i * 4 + 3] = 255;  // A
    }

    int x = 4;
    int y = 4 + result.baseline;
    for (int i = 0; text[i] != '\0'; ++i)
    {
        unsigned char c = static_cast<unsigned char>(text[i]);
        if (c >= result.firstChar && c <= result.lastChar)
        {
            const GlyphInfo& glyph = result.glyphs[c - result.firstChar];

            int destX = x + glyph.offsetX;
            int destY = y + glyph.offsetY;

            for (int gy = 0; gy < glyph.height; ++gy)
            {
                for (int gx = 0; gx < glyph.width; ++gx)
                {
                    int srcX = glyph.x + gx;
                    int srcY = glyph.y + gy;
                    int dstX = destX + gx;
                    int dstY = destY + gy;

                    if (dstX >= 0 && dstX < bufferWidth && dstY >= 0 && dstY < bufferHeight)
                    {
                        int srcIdx = (srcY * result.atlasWidth + srcX) * 4;
                        int dstIdx = (dstY * bufferWidth + dstX) * 4;

                        // White text, with the atlas alpha.
                        uint8_t alpha = result.atlasRGBA[srcIdx + 3];
                        if (alpha > 0)
                        {
                            buffer[dstIdx + 0] = 255;  // R
                            buffer[dstIdx + 1] = 255;  // G
                            buffer[dstIdx + 2] = 255;  // B
                            buffer[dstIdx + 3] = alpha;
                        }
                    }
                }
            }

            x += glyph.advance;
        }
    }

    DekiEditor::EditorTexture::Destroy(m_PreviewTextureId);
    m_PreviewTextureId = DekiEditor::EditorTexture::CreateRgba(bufferWidth, bufferHeight, buffer.data());

    m_PreviewTextureWidth = bufferWidth;
    m_PreviewTextureHeight = bufferHeight;
    m_LastPreviewSize = fontSize;

    DEKI_LOG_DEBUG("Generated font preview: %s @ %d px (%dx%d)", assetPath.c_str(), fontSize, bufferWidth,
                   bufferHeight);
}

void FontFileInspector::CleanupPreview()
{
    if (m_PreviewTextureId != 0)
    {
        DekiEditor::EditorTexture::Destroy(m_PreviewTextureId);
        m_PreviewTextureId = 0;
    }
    m_PreviewTextureWidth = 0;
    m_PreviewTextureHeight = 0;
    m_LastPreviewSize = 0;
}

static FontFileInspector s_FontFileInspector;

void RegisterFontFileInspector()
{
    DekiEditor::FileInspectorRegistry::Instance().Register(&s_FontFileInspector);
}

}  // namespace Deki2D

#endif  // DEKI_EDITOR
