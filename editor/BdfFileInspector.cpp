#include "BdfFileInspector.h"

#ifdef DEKI_EDITOR

#include "FontCompiler.h"
#include <deki-editor/AssetPipeline.h>
#include <deki-editor/SubAsset.h>
#include <deki-editor/EditorApplication.h>
#include "imgui.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <algorithm>
#include <cstring>
#include <cmath>
#include <unordered_map>

#ifdef _WIN32
#include <windows.h>
#endif
#include <GL/gl.h>

#include "DekiLogSystem.h"
#include "Guid.h"
#include "assets/AssetManager.h"

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace Deki2D
{

static const char* s_BdfExtensions[] = { ".bdf" };

bool BdfFileInspector::IsCached(const std::string& assetGuid) const
{
    if (assetGuid.empty()) return false;
    auto* pipeline = DekiEditor::AssetPipeline::Instance();
    if (!pipeline) return false;
    std::string variantGuid = Deki::GenerateDeterministicGuid(assetGuid + ":bdf");
    std::string cachePath = pipeline->GetProjectPath() + "/cache/" + variantGuid;
    return fs::exists(cachePath);
}

const char** BdfFileInspector::GetExtensions() const
{
    return s_BdfExtensions;
}

int BdfFileInspector::GetExtensionCount() const
{
    return 1;
}

void BdfFileInspector::OnInspectorGUI(const std::string& assetPath, const std::string& assetGuid)
{
    // Reload if asset changed
    if (m_CurrentAssetPath != assetPath)
    {
        CleanupTextures();
        LoadBdf(assetPath);
        LoadSettings(assetPath);
        m_CurrentAssetPath = assetPath;
        m_SettingsModified = false;
    }

    // Header
    fs::path path(assetPath);
    ImGui::Text("BDF Font: %s", path.filename().string().c_str());
    if (!assetGuid.empty())
        ImGui::TextDisabled("GUID: %s", assetGuid.c_str());

    if (m_AvailableChars.empty())
    {
        ImGui::TextColored(ImVec4(1, 0.3f, 0.3f, 1), "No glyphs found (codepoints 0-255)");
        return;
    }

    ImGui::Text("Ascent: %d  |  Descent: %d  |  Available: %d glyphs",
                 m_Ascent, m_Descent, (int)m_AvailableChars.size());

    ImGui::Separator();

    bool isCached = IsCached(assetGuid);

    // Toolbar
    ImGui::InputTextWithHint("##search", "Search (char or codepoint)...", m_SearchFilter, sizeof(m_SearchFilter));
    ImGui::SameLine();
    ImGui::Text("%d selected", (int)m_SelectedChars.size());
    ImGui::SameLine();
    if (ImGui::Button("Clear"))
    {
        m_SelectedChars.clear();
        m_SettingsModified = true;
    }

    // Apply & Bake button
    bool canBake = m_SettingsModified || !isCached;
    if (!canBake || m_SelectedChars.empty())
        ImGui::BeginDisabled();

    if (ImGui::Button("Apply & Bake"))
    {
        SaveSettings(assetPath, assetGuid);
        BakeFont(assetPath, assetGuid);
        m_SettingsModified = false;
    }

    if (!canBake || m_SelectedChars.empty())
        ImGui::EndDisabled();

    ImGui::Separator();

    // Glyph grid
    std::vector<int> filtered;
    filtered.reserve(m_AvailableChars.size());

    std::string filterLower;
    if (m_SearchFilter[0] != '\0')
    {
        filterLower = m_SearchFilter;
        std::transform(filterLower.begin(), filterLower.end(), filterLower.begin(),
                       [](unsigned char c) { return std::tolower(c); });
    }

    for (int i = 0; i < (int)m_AvailableChars.size(); i++)
    {
        if (filterLower.empty())
        {
            filtered.push_back(i);
            continue;
        }
        std::string label = m_AvailableChars[i].label;
        std::transform(label.begin(), label.end(), label.begin(),
                       [](unsigned char c) { return std::tolower(c); });
        if (label.find(filterLower) != std::string::npos)
            filtered.push_back(i);
    }

    // Select All filtered
    if (ImGui::Button("Select All"))
    {
        for (int idx : filtered)
        {
            if (m_SelectedChars.insert(idx).second)
                m_SettingsModified = true;
        }
    }

    ImGui::Text("%d glyphs shown", (int)filtered.size());

    if (filtered.empty())
        return;

    float iconSize = 32.0f; // Fixed display size, GL_NEAREST keeps it crisp
    float rowHeight = iconSize + 4.0f;

    ImGui::BeginChild("GlyphList", ImVec2(0, 400), ImGuiChildFlags_Borders);

    ImGuiListClipper clipper;
    clipper.Begin((int)filtered.size(), rowHeight);

    while (clipper.Step())
    {
        for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; row++)
        {
            int charIdx = filtered[row];
            bool isSelected = m_SelectedChars.count(charIdx) > 0;

            ImGui::PushID(charIdx);

            ImVec2 rowStart = ImGui::GetCursorScreenPos();
            ImVec2 rowEnd(rowStart.x + ImGui::GetContentRegionAvail().x, rowStart.y + rowHeight);

            if (isSelected)
            {
                ImDrawList* drawList = ImGui::GetWindowDrawList();
                drawList->AddRectFilled(rowStart, rowEnd, IM_COL32(86, 158, 255, 80));
                drawList->AddRect(rowStart, rowEnd, IM_COL32(86, 158, 255, 200));
            }

            if (ImGui::InvisibleButton("##row", ImVec2(ImGui::GetContentRegionAvail().x, rowHeight)))
            {
                if (isSelected)
                    m_SelectedChars.erase(charIdx);
                else
                    m_SelectedChars.insert(charIdx);
                m_SettingsModified = true;
            }

            // Draw glyph image
            if (m_GlyphAtlasTexture != 0 && charIdx < (int)m_GlyphUVs.size())
            {
                auto& uv = m_GlyphUVs[charIdx];
                ImTextureID texId = (ImTextureID)(intptr_t)m_GlyphAtlasTexture;
                ImVec2 imgP0(rowStart.x + 2.0f, rowStart.y + 2.0f);
                ImVec2 imgP1(rowStart.x + 2.0f + iconSize, rowStart.y + 2.0f + iconSize);
                ImGui::GetWindowDrawList()->AddImage(texId, imgP0, imgP1,
                                                     ImVec2(uv.u0, uv.v0),
                                                     ImVec2(uv.u1, uv.v1));
            }

            // Draw label
            ImVec2 textPos(rowStart.x + iconSize + 10.0f,
                           rowStart.y + (rowHeight - ImGui::GetTextLineHeight()) * 0.5f);
            ImGui::GetWindowDrawList()->AddText(textPos, IM_COL32(255, 255, 255, 255),
                                                m_AvailableChars[charIdx].label);

            ImGui::PopID();
        }
    }

    clipper.End();
    ImGui::EndChild();
}

// ---------------------------------------------------------------------------
// Parse BDF file to populate m_AvailableChars and build glyph atlas
// ---------------------------------------------------------------------------

void BdfFileInspector::LoadBdf(const std::string& assetPath)
{
    m_AvailableChars.clear();
    m_SelectedChars.clear();
    m_GlyphUVs.clear();
    m_Ascent = 0;
    m_Descent = 0;
    CleanupTextures();

    // Resolve full path
    auto* pipeline = DekiEditor::AssetPipeline::Instance();
    if (!pipeline) return;

    std::string fullPath = (fs::path(pipeline->GetProjectPath()) / assetPath).string();
    if (!fs::exists(fullPath)) return;

    // Quick parse for glyph metadata + bitmaps
    struct ParsedGlyph
    {
        int encoding = -1;
        int bbxW = 0, bbxH = 0;
        std::vector<uint8_t> bitmap;
    };
    std::vector<ParsedGlyph> parsedGlyphs;
    ParsedGlyph cur;
    bool inChar = false, inBitmap = false;
    int bitmapRow = 0;
    std::string line;

    std::ifstream file(fullPath);
    while (std::getline(file, line))
    {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;

        if (inBitmap)
        {
            if (line == "ENDCHAR")
            {
                inBitmap = false;
                inChar = false;
                if (cur.encoding >= 0 && cur.encoding <= 255)
                    parsedGlyphs.push_back(std::move(cur));
                cur = ParsedGlyph();
                continue;
            }
            int bytesPerRow = (cur.bbxW + 7) / 8;
            for (int bi = 0; bi < bytesPerRow && (bi * 2 + 1) < (int)line.size(); bi++)
            {
                auto hex = [](char c) -> uint8_t {
                    if (c >= '0' && c <= '9') return (uint8_t)(c - '0');
                    if (c >= 'A' && c <= 'F') return (uint8_t)(c - 'A' + 10);
                    if (c >= 'a' && c <= 'f') return (uint8_t)(c - 'a' + 10);
                    return 0;
                };
                uint8_t byte = (hex(line[bi * 2]) << 4) | hex(line[bi * 2 + 1]);
                for (int bit = 7; bit >= 0; bit--)
                {
                    int px = bi * 8 + (7 - bit);
                    if (px >= cur.bbxW) break;
                    cur.bitmap[bitmapRow * cur.bbxW + px] = (byte & (1 << bit)) ? 255 : 0;
                }
            }
            bitmapRow++;
            continue;
        }

        std::istringstream iss(line);
        std::string kw;
        iss >> kw;

        if (kw == "FONT_ASCENT") iss >> m_Ascent;
        else if (kw == "FONT_DESCENT") iss >> m_Descent;
        else if (kw == "STARTCHAR") { inChar = true; cur = ParsedGlyph(); }
        else if (kw == "ENCODING" && inChar) iss >> cur.encoding;
        else if (kw == "BBX" && inChar) { int offx, offy; iss >> cur.bbxW >> cur.bbxH >> offx >> offy; }
        else if (kw == "BITMAP" && inChar) { inBitmap = true; bitmapRow = 0; cur.bitmap.resize(cur.bbxW * cur.bbxH, 0); }
        else if (kw == "ENDFONT") break;
    }

    if (parsedGlyphs.empty()) return;

    std::sort(parsedGlyphs.begin(), parsedGlyphs.end(),
              [](const auto& a, const auto& b) { return a.encoding < b.encoding; });

    // Build m_AvailableChars
    m_AvailableChars.resize(parsedGlyphs.size());
    for (int i = 0; i < (int)parsedGlyphs.size(); i++)
    {
        m_AvailableChars[i].codepoint = parsedGlyphs[i].encoding;
        char ch = (char)parsedGlyphs[i].encoding;
        if (ch >= 33 && ch <= 126)
            snprintf(m_AvailableChars[i].label, sizeof(m_AvailableChars[i].label), "%c (%d)", ch, parsedGlyphs[i].encoding);
        else
            snprintf(m_AvailableChars[i].label, sizeof(m_AvailableChars[i].label), "(%d)", parsedGlyphs[i].encoding);
    }

    // Build glyph atlas texture for display
    int maxW = 1, maxH = 1;
    for (auto& g : parsedGlyphs)
    {
        if (g.bbxW > maxW) maxW = g.bbxW;
        if (g.bbxH > maxH) maxH = g.bbxH;
    }
    m_GlyphDisplaySize = (std::max)(maxW, maxH) + 4;

    int cellSize = m_GlyphDisplaySize;
    int count = (int)parsedGlyphs.size();
    int cols = (std::max)(1, (int)std::ceil(std::sqrt((double)count)));
    int rows = (count + cols - 1) / cols;
    int atlasW = cols * cellSize;
    int atlasH = rows * cellSize;

    std::vector<uint8_t> rgba(atlasW * atlasH * 4, 0);
    m_GlyphUVs.resize(count);

    for (int i = 0; i < count; i++)
    {
        int col = i % cols;
        int row = i / cols;
        int cellX = col * cellSize;
        int cellY = row * cellSize;
        int offX = (cellSize - parsedGlyphs[i].bbxW) / 2;
        int offY = (cellSize - parsedGlyphs[i].bbxH) / 2;

        for (int y = 0; y < parsedGlyphs[i].bbxH; y++)
        {
            for (int x = 0; x < parsedGlyphs[i].bbxW; x++)
            {
                uint8_t alpha = parsedGlyphs[i].bitmap[y * parsedGlyphs[i].bbxW + x];
                int dstX = cellX + offX + x;
                int dstY = cellY + offY + y;
                if (dstX < 0 || dstX >= atlasW || dstY < 0 || dstY >= atlasH) continue;
                int dstIdx = (dstY * atlasW + dstX) * 4;
                rgba[dstIdx + 0] = 255;
                rgba[dstIdx + 1] = 255;
                rgba[dstIdx + 2] = 255;
                rgba[dstIdx + 3] = alpha;
            }
        }

        m_GlyphUVs[i].u0 = (float)cellX / (float)atlasW;
        m_GlyphUVs[i].v0 = (float)cellY / (float)atlasH;
        m_GlyphUVs[i].u1 = (float)(cellX + cellSize) / (float)atlasW;
        m_GlyphUVs[i].v1 = (float)(cellY + cellSize) / (float)atlasH;
    }

    GLuint tex;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, atlasW, atlasH, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
    glBindTexture(GL_TEXTURE_2D, 0);

    m_GlyphAtlasTexture = tex;
    m_GlyphAtlasW = atlasW;
    m_GlyphAtlasH = atlasH;
}

// ---------------------------------------------------------------------------
// Load/Save .bdf.data sidecar
// ---------------------------------------------------------------------------

void BdfFileInspector::LoadSettings(const std::string& assetPath)
{
    m_SelectedChars.clear();

    auto* pipeline = DekiEditor::AssetPipeline::Instance();
    if (!pipeline) return;

    std::string fullPath = (fs::path(pipeline->GetProjectPath()) / assetPath).string();
    std::string dataPath = fullPath + ".data";

    if (!fs::exists(dataPath))
    {
        // No .data → select all by default
        for (int i = 0; i < (int)m_AvailableChars.size(); i++)
            m_SelectedChars.insert(i);
        return;
    }

    std::ifstream file(dataPath);
    if (!file.is_open()) return;

    json j;
    try { j = json::parse(file); }
    catch (...) { return; }

    if (!j.contains("bdfSettings") || !j["bdfSettings"].contains("selectedChars"))
    {
        // Settings exist but no selection → select all
        for (int i = 0; i < (int)m_AvailableChars.size(); i++)
            m_SelectedChars.insert(i);
        return;
    }

    // Build codepoint → index map
    std::unordered_map<int, int> cpToIdx;
    for (int i = 0; i < (int)m_AvailableChars.size(); i++)
        cpToIdx[m_AvailableChars[i].codepoint] = i;

    for (const auto& val : j["bdfSettings"]["selectedChars"])
    {
        if (val.is_number_integer())
        {
            int cp = val.get<int>();
            auto it = cpToIdx.find(cp);
            if (it != cpToIdx.end())
                m_SelectedChars.insert(it->second);
        }
    }
}

void BdfFileInspector::SaveSettings(const std::string& assetPath, const std::string& assetGuid)
{
    auto* pipeline = DekiEditor::AssetPipeline::Instance();
    if (!pipeline) return;

    std::string fullPath = (fs::path(pipeline->GetProjectPath()) / assetPath).string();
    std::string dataPath = fullPath + ".data";

    // Load existing .data or create new
    json j;
    if (fs::exists(dataPath))
    {
        std::ifstream inFile(dataPath);
        if (inFile.is_open())
        {
            try { j = json::parse(inFile); }
            catch (...) { j = json::object(); }
        }
    }

    // Save selected codepoints
    std::vector<int> codepoints = GetSelectedCodepoints();
    std::sort(codepoints.begin(), codepoints.end());
    j["bdfSettings"]["selectedChars"] = codepoints;

    // Write
    std::ofstream outFile(dataPath);
    if (outFile.is_open())
    {
        outFile << j.dump(2);
        outFile.close();
    }

    // Trigger sync handler to bake
    pipeline->RefreshAsset(assetPath);

    DEKI_LOG_EDITOR("BdfInspector: Saved %d chars and triggered bake for %s",
                     (int)codepoints.size(), assetPath.c_str());
}

void BdfFileInspector::BakeFont(const std::string& assetPath, const std::string& assetGuid)
{
    auto* pipeline = DekiEditor::AssetPipeline::Instance();
    if (!pipeline || assetGuid.empty()) return;

    std::string fullPath = (fs::path(pipeline->GetProjectPath()) / assetPath).string();
    std::vector<int> codepoints = GetSelectedCodepoints();
    if (codepoints.empty()) return;

    // Deterministic GUIDs
    std::string variantGuid = Deki::GenerateDeterministicGuid(assetGuid + ":bdf");
    std::string atlasGuid = Deki::GenerateDeterministicGuid(assetGuid + ":bdf:atlas");

    std::string cacheDir = pipeline->GetProjectPath() + "/cache";
    fs::create_directories(cacheDir);

    std::string dfontPath = cacheDir + "/" + variantGuid;
    std::string atlasPath = cacheDir + "/" + atlasGuid;

    // Compile
    FontCompiler::BdfCompileOptions options;
    options.selectedChars = codepoints;
    options.padding = 2;
    options.maxAtlasSize = 2048;

    FontCompiler::CompileResult result;
    if (!FontCompiler::CompileBdfFont(fullPath, options, result))
    {
        DEKI_LOG_WARNING("BdfInspector: Failed to compile %s", fullPath.c_str());
        return;
    }

    // Write atlas
    if (!DekiEditor::TextureImporter::WriteTexFile(atlasPath, result.atlasRGBA.data(),
        result.atlasWidth, result.atlasHeight, DekiEditor::TextureFormat::ALPHA8))
    {
        DEKI_LOG_WARNING("BdfInspector: Failed to write atlas %s", atlasPath.c_str());
        return;
    }

    // Write dfont
    if (!FontCompiler::WriteDfontFile(dfontPath, result, atlasGuid))
    {
        DEKI_LOG_WARNING("BdfInspector: Failed to write dfont %s", dfontPath.c_str());
        return;
    }

    // Register GUID
    Deki::AssetManager::Get()->RegisterGuid(variantGuid, variantGuid);

    // Register sub-assets so variant appears in Asset Browser
    std::vector<DekiEditor::SubAssetInfo> subAssets;

    DekiEditor::SubAssetInfo fontSub;
    fontSub.guid = variantGuid;
    fontSub.parentGuid = assetGuid;
    fontSub.subAssetIndex = 0;
    fontSub.name = "bdf";
    fontSub.depth = 0;
    fontSub.cachePath = dfontPath;
    subAssets.push_back(fontSub);

    if (fs::exists(atlasPath))
    {
        DekiEditor::SubAssetInfo atlasSub;
        atlasSub.guid = atlasGuid;
        atlasSub.parentGuid = assetGuid;
        atlasSub.subAssetIndex = 1;
        atlasSub.name = "bdf atlas";
        atlasSub.depth = 1;
        atlasSub.cachePath = atlasPath;
        atlasSub.hasPreview = true;
        subAssets.push_back(atlasSub);
    }

    pipeline->RegisterSubAssets(assetGuid, subAssets);

    DEKI_LOG_EDITOR("BdfInspector: Baked -> font=%s, atlas=%s", variantGuid.c_str(), atlasGuid.c_str());
}

std::vector<int> BdfFileInspector::GetSelectedCodepoints() const
{
    std::vector<int> codepoints;
    codepoints.reserve(m_SelectedChars.size());
    for (int idx : m_SelectedChars)
    {
        if (idx >= 0 && idx < (int)m_AvailableChars.size())
            codepoints.push_back(m_AvailableChars[idx].codepoint);
    }
    return codepoints;
}

void BdfFileInspector::CleanupTextures()
{
    if (m_GlyphAtlasTexture != 0)
    {
        GLuint tex = m_GlyphAtlasTexture;
        glDeleteTextures(1, &tex);
        m_GlyphAtlasTexture = 0;
    }
}

// Registration
static BdfFileInspector s_BdfFileInspector;

void RegisterBdfFileInspector()
{
    DekiEditor::FileInspectorRegistry::Instance().Register(&s_BdfFileInspector);
    DEKI_LOG_EDITOR("BdfFileInspector registered for .bdf files");
}

} // namespace Deki2D

#endif // DEKI_EDITOR
