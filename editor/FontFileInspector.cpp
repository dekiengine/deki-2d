#include "FontFileInspector.h"

#ifdef DEKI_EDITOR

#include "FontCompiler.h"
#include <deki-editor/AssetPipeline.h>
#include <deki-editor/TextureImporter.h>
#include <deki-editor/AssetData.h>  // For DekiEditor::GenerateGuid
#include <deki-editor/EditorApplication.h>
#include "imgui.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <filesystem>
#include <algorithm>
#include <sstream>
#include <iomanip>

// OpenGL for preview texture
#ifdef _WIN32
#include <windows.h>
#endif
#include <GL/gl.h>

// Engine logging and utilities
#include "DekiLogSystem.h"
#include "Guid.h"  // For Deki::GenerateDeterministicGuid (kept for preview)
#include "assets/AssetManager.h"  // For registering variant GUIDs

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace Deki2D
{

// Static extensions array
static const char* s_FontExtensions[] = { ".ttf", ".otf" };

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
    // Reload settings if asset changed
    if (m_CurrentAssetPath != assetPath)
    {
        DEKI_LOG_EDITOR("FontInspector: Asset changed, loading settings for %s (guid=%s)", assetPath.c_str(), assetGuid.c_str());
        LoadSettings(assetPath);
        m_CurrentAssetPath = assetPath;
        m_SettingsModified = false;
        DEKI_LOG_EDITOR("FontInspector: Loaded %zu sizes", m_Sizes.size());
    }

    // Font info header
    fs::path path(assetPath);
    ImGui::Text("Font: %s", path.filename().string().c_str());

    if (!assetGuid.empty())
    {
        ImGui::TextDisabled("GUID: %s", assetGuid.c_str());
    }

    ImGui::Separator();

    // Character range settings
    if (ImGui::CollapsingHeader("Character Range", ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::SetNextItemWidth(100);
        if (ImGui::InputInt("First Character", &m_FirstChar))
        {
            m_FirstChar = (std::max)(0, (std::min)(255, m_FirstChar));
            m_SettingsModified = true;
        }
        ImGui::SameLine();
        if (m_FirstChar >= 32 && m_FirstChar <= 126)
            ImGui::Text("('%c')", static_cast<char>(m_FirstChar));

        ImGui::SetNextItemWidth(100);
        if (ImGui::InputInt("Last Character", &m_LastChar))
        {
            m_LastChar = (std::max)(m_FirstChar, (std::min)(255, m_LastChar));
            m_SettingsModified = true;
        }
        ImGui::SameLine();
        if (m_LastChar >= 32 && m_LastChar <= 126)
            ImGui::Text("('%c')", static_cast<char>(m_LastChar));

        int charCount = m_LastChar - m_FirstChar + 1;
        ImGui::TextDisabled("Total: %d characters", charCount);

    }

    // Size variants
    if (ImGui::CollapsingHeader("Size Variants", ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::TextDisabled("Configure which sizes to bake for this font.");
        ImGui::Spacing();

        // List current sizes
        int indexToRemove = -1;
        for (size_t i = 0; i < m_Sizes.size(); ++i)
        {
            ImGui::PushID(static_cast<int>(i));

            // Size with variant GUID (from .data file if available)
            std::string variantGuid = GetVariantGuidFromData(assetPath, m_Sizes[i]);
            if (variantGuid.empty())
            {
                variantGuid = "(not saved)";
            }
            ImGui::Text("%d px", m_Sizes[i]);
            ImGui::SameLine();
            ImGui::TextDisabled("(%s...)", variantGuid.length() > 8 ? variantGuid.substr(0, 8).c_str() : variantGuid.c_str());

            ImGui::SameLine();
            if (ImGui::SmallButton("X"))
            {
                indexToRemove = static_cast<int>(i);
            }

            ImGui::PopID();
        }

        if (indexToRemove >= 0)
        {
            m_Sizes.erase(m_Sizes.begin() + indexToRemove);
            m_SettingsModified = true;
        }

        // Add new size
        ImGui::Separator();
        ImGui::SetNextItemWidth(80);
        ImGui::InputInt("##NewSize", &m_NewSize);
        m_NewSize = (std::max)(6, (std::min)(128, m_NewSize));

        ImGui::SameLine();
        bool sizeExists = std::find(m_Sizes.begin(), m_Sizes.end(), m_NewSize) != m_Sizes.end();
        if (sizeExists)
        {
            ImGui::BeginDisabled();
            ImGui::Button("Add Size");
            ImGui::EndDisabled();
            ImGui::SameLine();
            ImGui::TextDisabled("(already exists)");
        }
        else if (ImGui::Button("Add Size"))
        {
            m_Sizes.push_back(m_NewSize);
            std::sort(m_Sizes.begin(), m_Sizes.end());
            m_SettingsModified = true;
        }
    }

    // Check if any variant needs baking
    bool hasUncachedVariants = false;
    auto* pipeline = DekiEditor::AssetPipeline::Instance();
    if (pipeline && !m_Sizes.empty())
    {
        for (int size : m_Sizes)
        {
            std::string variantGuid = GetVariantGuidFromData(assetPath, size);
            if (variantGuid.empty())
            {
                // No GUID in .data file means not saved yet
                hasUncachedVariants = true;
                break;
            }

            // Cache file uses GUID as filename (no extension)
            std::string cachePath = pipeline->GetProjectPath() + "/cache/" + variantGuid;

            // Normalize path for Windows
            for (char& c : cachePath)
            {
                if (c == '/') c = '\\';
            }

            bool exists = fs::exists(cachePath);
            DEKI_LOG_EDITOR("FontInspector: Checking cache %s -> %s", cachePath.c_str(), exists ? "EXISTS" : "NOT FOUND");

            if (!exists)
            {
                hasUncachedVariants = true;
                break;
            }
        }
    }

    // Apply button - enabled if settings modified OR any variant not cached
    ImGui::Separator();
    bool canBake = m_SettingsModified || hasUncachedVariants;
    if (canBake && !m_Sizes.empty())
    {
        if (ImGui::Button("Apply & Bake Fonts"))
        {
            SaveSettings(assetPath, assetGuid);

            // Bake each size variant
            for (int size : m_Sizes)
            {
                BakeFontVariant(assetPath, assetGuid, size);
            }

            m_SettingsModified = false;

            // Request full asset invalidation so prefab view picks up re-baked fonts
            DekiEditor::EditorApplication::Get().RequestAssetInvalidation();
        }
        if (m_SettingsModified)
        {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "Unsaved changes");
        }
        else if (hasUncachedVariants)
        {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.9f, 0.6f, 0.2f, 1.0f), "Not cached");
        }
    }
    else
    {
        ImGui::BeginDisabled();
        ImGui::Button("Apply & Bake Fonts");
        ImGui::EndDisabled();
    }

    // Show variant status
    if (!m_Sizes.empty())
    {
        ImGui::Separator();
        if (ImGui::CollapsingHeader("Variant Status"))
        {
            for (int size : m_Sizes)
            {
                std::string variantGuid = GetVariantGuidFromData(assetPath, size);

                // Check if baked version exists in cache
                auto* pipeline = DekiEditor::AssetPipeline::Instance();
                std::string cachePath;
                if (pipeline && !variantGuid.empty())
                {
                    // Cache file uses GUID as filename (no extension)
                    cachePath = pipeline->GetProjectPath() + "/cache/" + variantGuid;
                    // Normalize path for Windows
                    for (char& c : cachePath)
                    {
                        if (c == '/') c = '\\';
                    }
                }

                bool cached = !cachePath.empty() && fs::exists(cachePath);

                if (cached)
                {
                    ImGui::TextColored(ImVec4(0.4f, 0.9f, 0.4f, 1.0f), "%d px: Cached", size);
                }
                else
                {
                    ImGui::TextColored(ImVec4(0.9f, 0.6f, 0.2f, 1.0f), "%d px: Not cached", size);
                }
            }
        }
    }

    // Preview section
    ImGui::Separator();
    if (ImGui::CollapsingHeader("Preview", ImGuiTreeNodeFlags_DefaultOpen))
    {
        // Preview size input
        ImGui::SetNextItemWidth(80);
        ImGui::InputInt("Preview Size", &m_PreviewSize);
        m_PreviewSize = (std::max)(6, (std::min)(128, m_PreviewSize));

        // Preview text input
        ImGui::SetNextItemWidth(200);
        ImGui::InputText("Preview Text", m_PreviewText, sizeof(m_PreviewText));

        // Generate preview button
        if (ImGui::Button("Generate Preview"))
        {
            GeneratePreview(assetPath, m_PreviewSize);
        }

        // Show preview image if available
        if (m_PreviewTextureId != 0 && m_PreviewTextureWidth > 0 && m_PreviewTextureHeight > 0)
        {
            ImGui::SameLine();

            // Save button - adds this size to the list and bakes it
            bool sizeExists = std::find(m_Sizes.begin(), m_Sizes.end(), m_PreviewSize) != m_Sizes.end();
            if (sizeExists)
            {
                ImGui::BeginDisabled();
                ImGui::Button("Save This Size");
                ImGui::EndDisabled();
                ImGui::SameLine();
                ImGui::TextDisabled("(already saved)");
            }
            else if (ImGui::Button("Save This Size"))
            {
                // Add size to list
                m_Sizes.push_back(m_PreviewSize);
                std::sort(m_Sizes.begin(), m_Sizes.end());
                m_SettingsModified = true;

                // Save settings and bake immediately
                SaveSettings(assetPath, assetGuid);
                BakeFontVariant(assetPath, assetGuid, m_PreviewSize);
                m_SettingsModified = false;

                // Request full asset invalidation so prefab view picks up the new font
                DekiEditor::EditorApplication::Get().RequestAssetInvalidation();
            }

            // Display preview
            ImGui::Spacing();
            ImGui::Text("Preview at %d px:", m_LastPreviewSize);

            // Draw border around preview
            ImVec2 previewSize((float)m_PreviewTextureWidth, (float)m_PreviewTextureHeight);
            ImGui::Image((ImTextureID)(uintptr_t)m_PreviewTextureId, previewSize);
        }
    }

    // Cleanup preview if asset changed
    if (m_LastPreviewAsset != assetPath)
    {
        CleanupPreview();
        m_LastPreviewAsset = assetPath;
    }
}

void FontFileInspector::LoadSettings(const std::string& assetPath)
{
    m_Sizes.clear();
    m_FirstChar = 32;
    m_LastChar = 126;

    std::string dataPath = assetPath + ".data";
    if (!fs::exists(dataPath))
        return;

    std::ifstream file(dataPath);
    if (!file.is_open())
        return;

    try
    {
        json j = json::parse(file);

        if (j.contains("fontSettings") && j["fontSettings"].is_object())
        {
            auto& settings = j["fontSettings"];

            if (settings.contains("sizes") && settings["sizes"].is_array())
            {
                for (const auto& size : settings["sizes"])
                {
                    if (size.is_number_integer())
                    {
                        m_Sizes.push_back(size.get<int>());
                    }
                }
            }

            if (settings.contains("firstChar") && settings["firstChar"].is_number_integer())
                m_FirstChar = settings["firstChar"].get<int>();

            if (settings.contains("lastChar") && settings["lastChar"].is_number_integer())
                m_LastChar = settings["lastChar"].get<int>();

        }
    }
    catch (const json::exception&)
    {
        // Ignore parse errors
    }
}

void FontFileInspector::SaveSettings(const std::string& assetPath, const std::string& assetGuid)
{
    std::string dataPath = assetPath + ".data";

    // Load existing .data file or create new
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

    // Ensure GUID is set
    if (!assetGuid.empty())
        j["guid"] = assetGuid;

    // Save font settings
    j["fontSettings"] = json::object();
    j["fontSettings"]["sizes"] = m_Sizes;
    j["fontSettings"]["firstChar"] = m_FirstChar;
    j["fontSettings"]["lastChar"] = m_LastChar;
    // Generate random variant GUIDs (only if they don't exist)
    if (!j.contains("variants"))
        j["variants"] = json::object();

    for (int size : m_Sizes)
    {
        std::string sizeKey = std::to_string(size);

        // Create variant entry if missing
        if (!j["variants"].contains(sizeKey))
            j["variants"][sizeKey] = json::object();

        // Generate deterministic font GUID (based on fontGuid:size)
        std::string variantGuid = Deki::GenerateDeterministicGuid(assetGuid + ":" + sizeKey);
        j["variants"][sizeKey]["guid"] = variantGuid;

        // Generate deterministic atlas GUID (based on fontGuid:size:atlas)
        std::string atlasGuid = Deki::GenerateDeterministicGuid(assetGuid + ":" + sizeKey + ":atlas");
        j["variants"][sizeKey]["atlasGuid"] = atlasGuid;
    }

    // Write back
    std::ofstream outFile(dataPath);
    if (outFile.is_open())
    {
        outFile << j.dump(2);
    }

    // Notify AssetPipeline of the change
    auto* pipeline = DekiEditor::AssetPipeline::Instance();
    if (pipeline)
    {
        // Compute assets-relative path
        std::string assetsPath = pipeline->GetProjectPath() + "/project/assets";
        std::string relativePath = fs::relative(assetPath, assetsPath).string();
        for (char& c : relativePath)
        {
            if (c == '\\') c = '/';
        }

        // Force re-read of sidecar
        pipeline->RefreshAsset("project/assets/" + relativePath);
    }
}

std::string FontFileInspector::GenerateVariantGuid(const std::string& fontGuid, int fontSize)
{
    // Legacy: Use deterministic GUID for preview mode
    // Baked fonts use random GUIDs stored in .data file
    std::string seed = fontGuid + ":" + std::to_string(fontSize);
    return Deki::GenerateDeterministicGuid(seed);
}

std::string FontFileInspector::GetVariantGuidFromData(const std::string& assetPath, int fontSize)
{
    std::string dataPath = assetPath + ".data";
    if (!fs::exists(dataPath))
        return "";

    std::ifstream file(dataPath);
    if (!file.is_open())
        return "";

    try
    {
        json j = json::parse(file);
        std::string sizeKey = std::to_string(fontSize);

        if (j.contains("variants") && j["variants"].contains(sizeKey) &&
            j["variants"][sizeKey].contains("guid"))
        {
            return j["variants"][sizeKey]["guid"].get<std::string>();
        }
    }
    catch (const json::exception&)
    {
        // Ignore parse errors
    }

    return "";
}

std::string FontFileInspector::GetAtlasGuidFromData(const std::string& assetPath, int fontSize)
{
    std::string dataPath = assetPath + ".data";
    if (!fs::exists(dataPath))
        return "";

    std::ifstream file(dataPath);
    if (!file.is_open())
        return "";

    try
    {
        json j = json::parse(file);
        std::string sizeKey = std::to_string(fontSize);

        if (j.contains("variants") && j["variants"].contains(sizeKey) &&
            j["variants"][sizeKey].contains("atlasGuid"))
        {
            return j["variants"][sizeKey]["atlasGuid"].get<std::string>();
        }
    }
    catch (const json::exception&)
    {
        // Ignore parse errors
    }

    return "";
}

void FontFileInspector::BakeFontVariant(const std::string& assetPath, const std::string& fontGuid, int fontSize)
{
    auto* pipeline = DekiEditor::AssetPipeline::Instance();
    if (!pipeline)
        return;

    // Read GUIDs from .data file (they should have been generated in SaveSettings)
    std::string variantGuid = GetVariantGuidFromData(assetPath, fontSize);
    std::string atlasGuid = GetAtlasGuidFromData(assetPath, fontSize);

    if (variantGuid.empty() || atlasGuid.empty())
    {
        DEKI_LOG_ERROR("Failed to get variant/atlas GUIDs from .data file for %s @ %d px", assetPath.c_str(), fontSize);
        return;
    }

    std::string projectPath = pipeline->GetProjectPath();
    std::string cacheDir = projectPath + "/cache";

    // Ensure cache directory exists
    fs::create_directories(cacheDir);

    // Compile font
    FontCompiler::CompileOptions options;
    options.fontSize = fontSize;
    options.firstChar = m_FirstChar;
    options.lastChar = m_LastChar;
    options.padding = 2;
    options.maxAtlasSize = 2048;


    FontCompiler::CompileResult result;
    if (!FontCompiler::CompileTrueTypeFont(assetPath, options, result))
    {
        DEKI_LOG_ERROR("Failed to compile font variant: %s @ %d px", assetPath.c_str(), fontSize);
        return;
    }

    // Write atlas as DTEX (filename is atlasGuid, no extension)
    std::string atlasPath = cacheDir + "/" + atlasGuid;
    if (!DekiEditor::TextureImporter::WriteTexFile(atlasPath, result.atlasRGBA.data(),
                                                    result.atlasWidth, result.atlasHeight,
                                                    DekiEditor::TextureFormat::RGB565A8))
    {
        DEKI_LOG_ERROR("Failed to write font atlas: %s", atlasPath.c_str());
        return;
    }

    // Write DFONT file (filename is variantGuid, no extension)
    std::string dfontPath = cacheDir + "/" + variantGuid;
    // Atlas filename stored inside dfont is just the atlasGuid (no extension)
    if (!FontCompiler::WriteDfontFile(dfontPath, result, atlasGuid))
    {
        DEKI_LOG_ERROR("Failed to write dfont file: %s", dfontPath.c_str());
        return;
    }

    // Register the variant GUID with AssetManager so it can be looked up at runtime
    // Use just the GUID as filename - AssetManager prepends cache dir
    Deki::AssetManager::Get()->RegisterGuid(variantGuid, variantGuid);

    DEKI_LOG_EDITOR("Baked font variant: %s @ %d px -> font=%s, atlas=%s",
                  assetPath.c_str(), fontSize, variantGuid.c_str(), atlasGuid.c_str());
}

void FontFileInspector::GeneratePreview(const std::string& assetPath, int fontSize)
{
    // Compile font to get atlas
    FontCompiler::CompileOptions options;
    options.fontSize = fontSize;
    options.firstChar = m_FirstChar;
    options.lastChar = m_LastChar;
    options.padding = 2;
    options.maxAtlasSize = 2048;


    FontCompiler::CompileResult result;
    if (!FontCompiler::CompileTrueTypeFont(assetPath, options, result))
    {
        DEKI_LOG_ERROR("Failed to compile font for preview: %s @ %d px", assetPath.c_str(), fontSize);
        return;
    }

    // Render preview text to a buffer
    const char* text = m_PreviewText;
    int textLen = static_cast<int>(strlen(text));
    if (textLen == 0)
        text = "Preview";

    // Calculate text dimensions
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

    // Add padding
    int bufferWidth = textWidth + 8;
    int bufferHeight = textHeight + 8;

    // Create RGBA buffer
    std::vector<uint8_t> buffer(bufferWidth * bufferHeight * 4, 0);

    // Fill with dark background
    for (int i = 0; i < bufferWidth * bufferHeight; ++i)
    {
        buffer[i * 4 + 0] = 40;  // R
        buffer[i * 4 + 1] = 40;  // G
        buffer[i * 4 + 2] = 40;  // B
        buffer[i * 4 + 3] = 255; // A
    }

    // Render text
    int x = 4;
    int y = 4 + result.baseline;
    for (int i = 0; text[i] != '\0'; ++i)
    {
        unsigned char c = static_cast<unsigned char>(text[i]);
        if (c >= result.firstChar && c <= result.lastChar)
        {
            const GlyphInfo& glyph = result.glyphs[c - result.firstChar];

            // Copy glyph from atlas to buffer
            int destX = x + glyph.offset_x;
            int destY = y + glyph.offset_y;

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

                        // Use alpha from atlas to blend white text
                        uint8_t alpha = result.atlasRGBA[srcIdx + 3];
                        if (alpha > 0)
                        {
                            buffer[dstIdx + 0] = 255; // R
                            buffer[dstIdx + 1] = 255; // G
                            buffer[dstIdx + 2] = 255; // B
                            buffer[dstIdx + 3] = alpha;
                        }
                    }
                }
            }

            x += glyph.advance;
        }
    }

    // Create or update OpenGL texture
    if (m_PreviewTextureId == 0)
    {
        glGenTextures(1, &m_PreviewTextureId);
    }

    glBindTexture(GL_TEXTURE_2D, m_PreviewTextureId);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, bufferWidth, bufferHeight, 0, GL_RGBA, GL_UNSIGNED_BYTE, buffer.data());

    m_PreviewTextureWidth = bufferWidth;
    m_PreviewTextureHeight = bufferHeight;
    m_LastPreviewSize = fontSize;

    DEKI_LOG_EDITOR("Generated font preview: %s @ %d px (%dx%d)", assetPath.c_str(), fontSize, bufferWidth, bufferHeight);
}

void FontFileInspector::CleanupPreview()
{
    if (m_PreviewTextureId != 0)
    {
        glDeleteTextures(1, &m_PreviewTextureId);
        m_PreviewTextureId = 0;
    }
    m_PreviewTextureWidth = 0;
    m_PreviewTextureHeight = 0;
    m_LastPreviewSize = 0;
}

// Static instance for registration
static FontFileInspector s_FontFileInspector;

void RegisterFontFileInspector()
{
    DekiEditor::FileInspectorRegistry::Instance().Register(&s_FontFileInspector);
    DEKI_LOG_EDITOR("FontFileInspector registered for .ttf and .otf files");
}

} // namespace Deki2D

#endif // DEKI_EDITOR
