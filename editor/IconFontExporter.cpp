#include "IconFontExporter.h"
#include <deki-editor/EditorApplication.h>
#include <deki-editor/AssetData.h>
#include <deki-editor/EditorHttpUtils.h>
#include <deki-editor/EditorPaths.h>
#include "imgui.h"
#include <SDL3/SDL_opengl.h>
#include <SDL3/SDL.h>
#include <ft2build.h>
#include FT_FREETYPE_H

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#include <nlohmann/json.hpp>
#include <filesystem>
#include <algorithm>
#include <cctype>
#include <cstring>
#include <regex>
#include <fstream>
#include <cmath>

namespace DekiEditor
{

REGISTER_EDITOR_WINDOW(IconFontExporter, "Icon Font Exporter", "Icon Font Exporter")

static const int ATLAS_MAX_WIDTH = 2048;

// ---------------------------------------------------------------------------
// Construction / Destruction
// ---------------------------------------------------------------------------

IconFontExporter::IconFontExporter()
{
}

IconFontExporter::~IconFontExporter()
{
    m_CancelDownload = true;
    if (m_DownloadThread.joinable())
        m_DownloadThread.join();
    CleanupAtlas();
    CleanupFreeType();
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

void IconFontExporter::OnOpen()
{
    std::string editorDataDir = EditorPaths::GetEditorDataDir();
    m_IconFontsDir = (std::filesystem::path(editorDataDir.empty() ? "." : editorDataDir) / "icon-fonts").string();

    InitFreeType();
    LoadCachedFonts();

    if (!m_Fonts.empty() && m_CurrentFontIndex < 0)
        SwitchFont(0);
}

void IconFontExporter::OnClose()
{
    m_CancelDownload = true;
    if (m_DownloadThread.joinable())
        m_DownloadThread.join();
    CleanupAtlas();
    CleanupFreeType();
    m_Fonts.clear();
    m_CurrentFontIndex = -1;
    m_SelectedIcons.clear();
}

void IconFontExporter::OnUpdate(float /*deltaTime*/)
{
    // Check if download completed
    int state = m_DownloadState.load();
    if (state == (int)DownloadState::Done)
    {
        IconFont font;
        {
            std::lock_guard<std::mutex> lock(m_DownloadMutex);
            font = m_DownloadResult;
        }

        if (m_DownloadThread.joinable())
            m_DownloadThread.join();

        SaveFontMetadata(font);
        m_Fonts.push_back(font);
        SwitchFont((int)m_Fonts.size() - 1);

        m_DownloadState = (int)DownloadState::Idle;
        m_ShowAddFont = false;

        // Re-load pending manifest to restore selection after auto-download
        if (!m_PendingManifestPath.empty())
        {
            std::string pending = m_PendingManifestPath;
            m_PendingManifestPath.clear();
            LoadExportManifest(pending);
        }
    }
    else if (state == (int)DownloadState::Error)
    {
        if (m_DownloadThread.joinable())
            m_DownloadThread.join();
        m_DownloadState = (int)DownloadState::Idle;
    }
}

bool IconFontExporter::CanOpenAssetType(const char* assetType)
{
    return assetType && std::string(assetType) == "iconexport";
}

void IconFontExporter::OpenFile(const char* filePath, const char* /*cachePath*/)
{
    if (!filePath)
        return;

    // Derive output folder from the .asset file path
    std::filesystem::path assetPath(filePath);
    m_OutputPath = assetPath.parent_path().string();

    // Ensure fonts are loaded
    if (m_Fonts.empty())
    {
        OnOpen();
    }

    LoadExportManifest(filePath);
}

// ---------------------------------------------------------------------------
// FreeType
// ---------------------------------------------------------------------------

void IconFontExporter::InitFreeType()
{
    if (m_FtLibrary)
        return;

    if (FT_Init_FreeType(&m_FtLibrary))
    {
        m_FtLibrary = nullptr;
        return;
    }
}

void IconFontExporter::CleanupFreeType()
{
    if (m_FtFace)
    {
        FT_Done_Face(m_FtFace);
        m_FtFace = nullptr;
    }
    if (m_FtLibrary)
    {
        FT_Done_FreeType(m_FtLibrary);
        m_FtLibrary = nullptr;
    }
    m_FtValid = false;
}

// ---------------------------------------------------------------------------
// Font Cache
// ---------------------------------------------------------------------------

void IconFontExporter::LoadCachedFonts()
{
    m_Fonts.clear();

    if (!std::filesystem::exists(m_IconFontsDir))
        return;

    for (auto& entry : std::filesystem::directory_iterator(m_IconFontsDir))
    {
        if (entry.path().extension() != ".json")
            continue;

        try
        {
            std::ifstream f(entry.path());
            nlohmann::json j;
            f >> j;

            IconFont font;
            font.name = j.value("name", "");
            font.version = j.value("version", "");
            font.source = j.value("source", "");
            font.npmPackage = j.value("npmPackage", "");

            std::string ttfName = font.name + ".ttf";
            font.ttfPath = (std::filesystem::path(m_IconFontsDir) / ttfName).string();

            if (!std::filesystem::exists(font.ttfPath))
                continue;

            for (auto& icon : j["icons"])
            {
                IconInfo info;
                info.name = icon.value("name", "");
                info.codepoint = icon.value("codepoint", 0u);
                font.icons.push_back(info);
            }

            m_Fonts.push_back(std::move(font));
        }
        catch (...)
        {
            // Skip malformed JSON
        }
    }
}

void IconFontExporter::SwitchFont(int index)
{
    if (index < 0 || index >= (int)m_Fonts.size())
        return;

    m_CurrentFontIndex = index;
    m_SelectedIcons.clear();
    m_LastBuiltSize = 0; // Force atlas rebuild

    // Load the TTF face
    if (m_FtFace)
    {
        FT_Done_Face(m_FtFace);
        m_FtFace = nullptr;
    }
    m_FtValid = false;

    if (m_FtLibrary && FT_New_Face(m_FtLibrary, m_Fonts[index].ttfPath.c_str(), 0, &m_FtFace) == 0)
    {
        m_FtValid = true;
    }
}

void IconFontExporter::RemoveCurrentFont()
{
    if (m_CurrentFontIndex < 0 || m_CurrentFontIndex >= (int)m_Fonts.size())
        return;

    auto& font = m_Fonts[m_CurrentFontIndex];

    // Delete files
    std::string jsonPath = (std::filesystem::path(m_IconFontsDir) / (font.name + ".json")).string();
    std::filesystem::remove(jsonPath);
    std::filesystem::remove(font.ttfPath);

    m_Fonts.erase(m_Fonts.begin() + m_CurrentFontIndex);
    CleanupAtlas();

    if (m_FtFace)
    {
        FT_Done_Face(m_FtFace);
        m_FtFace = nullptr;
    }
    m_FtValid = false;
    m_CurrentFontIndex = -1;
    m_SelectedIcons.clear();

    if (!m_Fonts.empty())
        SwitchFont(0);
}

void IconFontExporter::SaveFontMetadata(const IconFont& font)
{
    std::filesystem::create_directories(m_IconFontsDir);

    nlohmann::json j;
    j["name"] = font.name;
    j["version"] = font.version;
    j["source"] = font.source;
    j["npmPackage"] = font.npmPackage;

    nlohmann::json iconsArray = nlohmann::json::array();
    for (auto& icon : font.icons)
    {
        nlohmann::json ic;
        ic["name"] = icon.name;
        ic["codepoint"] = icon.codepoint;
        iconsArray.push_back(ic);
    }
    j["icons"] = iconsArray;

    std::string jsonPath = (std::filesystem::path(m_IconFontsDir) / (font.name + ".json")).string();
    std::ofstream out(jsonPath);
    out << j.dump(2);
}

// ---------------------------------------------------------------------------
// Atlas Rendering
// ---------------------------------------------------------------------------

void IconFontExporter::CleanupAtlas()
{
    if (m_AtlasTexture != 0)
    {
        GLuint id = m_AtlasTexture;
        glDeleteTextures(1, &id);
        m_AtlasTexture = 0;
    }
    m_AtlasEntries.clear();
}

bool IconFontExporter::RasterizeGlyph(uint32_t codepoint, int size,
                                      std::vector<uint8_t>& outRGBA, int& outW, int& outH)
{
    if (!m_FtValid)
        return false;

    FT_UInt glyphIndex = FT_Get_Char_Index(m_FtFace, codepoint);
    if (glyphIndex == 0)
        return false;

    // Render at 4x resolution and downsample for clean, symmetric results
    const int scale = 4;
    int hiSize = size * scale;

    FT_Set_Pixel_Sizes(m_FtFace, 0, hiSize);

    if (FT_Load_Glyph(m_FtFace, glyphIndex, FT_LOAD_RENDER | FT_LOAD_NO_HINTING))
        return false;

    FT_Bitmap& bitmap = m_FtFace->glyph->bitmap;

    // Blit into hi-res buffer
    std::vector<uint8_t> hiBuf(hiSize * hiSize, 0);
    int offsetX = (hiSize - (int)bitmap.width) / 2;
    int offsetY = (hiSize - (int)bitmap.rows) / 2;

    for (int y = 0; y < (int)bitmap.rows; y++)
    {
        for (int x = 0; x < (int)bitmap.width; x++)
        {
            int destX = offsetX + x;
            int destY = offsetY + y;
            if (destX < 0 || destX >= hiSize || destY < 0 || destY >= hiSize)
                continue;
            hiBuf[destY * hiSize + destX] = bitmap.buffer[y * bitmap.pitch + x];
        }
    }

    // Box downsample to target size
    outW = size;
    outH = size;
    outRGBA.resize(size * size * 4, 0);

    for (int dy = 0; dy < size; dy++)
    {
        for (int dx = 0; dx < size; dx++)
        {
            int sum = 0;
            for (int sy = 0; sy < scale; sy++)
                for (int sx = 0; sx < scale; sx++)
                    sum += hiBuf[(dy * scale + sy) * hiSize + (dx * scale + sx)];

            uint8_t alpha = (uint8_t)(sum / (scale * scale));
            int idx = (dy * size + dx) * 4;
            outRGBA[idx + 0] = 255;
            outRGBA[idx + 1] = 255;
            outRGBA[idx + 2] = 255;
            outRGBA[idx + 3] = alpha;
        }
    }
    return true;
}

void IconFontExporter::BuildIconAtlas()
{
    CleanupAtlas();

    if (!m_FtValid || m_CurrentFontIndex < 0)
        return;

    auto& icons = m_Fonts[m_CurrentFontIndex].icons;
    int totalIcons = (int)icons.size();
    if (totalIcons == 0)
        return;

    int cellSize = m_IconDisplaySize;
    int padding = 2;
    int cellWithPad = cellSize + padding;
    int cols = ATLAS_MAX_WIDTH / cellWithPad;
    if (cols < 1) cols = 1;
    int rows = (totalIcons + cols - 1) / cols;
    int atlasH = rows * cellWithPad;

    std::vector<uint8_t> atlasData(ATLAS_MAX_WIDTH * atlasH * 4, 0);

    m_AtlasEntries.resize(totalIcons);

    FT_Set_Pixel_Sizes(m_FtFace, 0, cellSize);

    FT_Int32 loadFlags = FT_LOAD_RENDER | FT_LOAD_NO_HINTING;

    for (int i = 0; i < totalIcons; i++)
    {
        int col = i % cols;
        int row = i / cols;
        int baseX = col * cellWithPad;
        int baseY = row * cellWithPad;

        FT_UInt glyphIndex = FT_Get_Char_Index(m_FtFace, icons[i].codepoint);
        if (glyphIndex != 0 && FT_Load_Glyph(m_FtFace, glyphIndex, loadFlags) == 0)
        {
            FT_Bitmap& bitmap = m_FtFace->glyph->bitmap;
            int offsetX = (cellSize - (int)bitmap.width) / 2;
            int offsetY = (cellSize - (int)bitmap.rows) / 2;

            for (int y = 0; y < (int)bitmap.rows; y++)
            {
                for (int x = 0; x < (int)bitmap.width; x++)
                {
                    int destX = baseX + offsetX + x;
                    int destY = baseY + offsetY + y;
                    if (destX < 0 || destX >= ATLAS_MAX_WIDTH || destY < 0 || destY >= atlasH)
                        continue;

                    uint8_t alpha = bitmap.buffer[y * bitmap.pitch + x];

                    int idx = (destY * ATLAS_MAX_WIDTH + destX) * 4;
                    atlasData[idx + 0] = 255;
                    atlasData[idx + 1] = 255;
                    atlasData[idx + 2] = 255;
                    atlasData[idx + 3] = alpha;
                }
            }
        }

        auto& entry = m_AtlasEntries[i];
        entry.u0 = (float)baseX / (float)ATLAS_MAX_WIDTH;
        entry.v0 = (float)baseY / (float)atlasH;
        entry.u1 = (float)(baseX + cellSize) / (float)ATLAS_MAX_WIDTH;
        entry.v1 = (float)(baseY + cellSize) / (float)atlasH;
    }

    GLuint textureId;
    glGenTextures(1, &textureId);
    glBindTexture(GL_TEXTURE_2D, textureId);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA,
                 ATLAS_MAX_WIDTH, atlasH,
                 0, GL_RGBA, GL_UNSIGNED_BYTE, atlasData.data());
    glBindTexture(GL_TEXTURE_2D, 0);

    m_AtlasTexture = textureId;
    m_AtlasWidth = ATLAS_MAX_WIDTH;
    m_AtlasHeight = atlasH;
    m_LastBuiltSize = m_IconDisplaySize;
}

// ---------------------------------------------------------------------------
// Download
// ---------------------------------------------------------------------------

std::vector<IconInfo> IconFontExporter::ParseCssForIcons(const std::string& cssContent)
{
    std::vector<IconInfo> icons;
    std::regex pattern(R"re(\.([\w-]+):before\s*\{\s*content:\s*"\\([0-9a-fA-F]+)")re");

    auto begin = std::sregex_iterator(cssContent.begin(), cssContent.end(), pattern);
    auto end = std::sregex_iterator();

    // Detect common prefix to strip (e.g., "ti-", "fa-")
    std::string commonPrefix;
    if (begin != end)
    {
        std::string first = (*begin)[1].str();
        auto dash = first.find('-');
        if (dash != std::string::npos && dash <= 3)
            commonPrefix = first.substr(0, dash + 1);
    }

    for (auto it = begin; it != end; ++it)
    {
        IconInfo info;
        info.name = (*it)[1].str();
        // Strip common prefix (e.g., "ti-arrow-left" -> "arrow-left")
        if (!commonPrefix.empty() && info.name.substr(0, commonPrefix.size()) == commonPrefix)
            info.name = info.name.substr(commonPrefix.size());

        info.codepoint = std::stoul((*it)[2].str(), nullptr, 16);
        icons.push_back(info);
    }

    std::sort(icons.begin(), icons.end(),
              [](const IconInfo& a, const IconInfo& b) { return a.name < b.name; });

    return icons;
}

void IconFontExporter::StartNpmDownload(const std::string& packageName)
{
    if (m_DownloadThread.joinable())
        m_DownloadThread.join();

    m_CancelDownload = false;
    m_DownloadState = (int)DownloadState::Downloading;
    {
        std::lock_guard<std::mutex> lock(m_DownloadMutex);
        m_DownloadStatus = "Fetching package info...";
        m_DownloadError.clear();
    }

    m_DownloadThread = std::thread([this, packageName]()
    {
        try
        {
            // 1. Resolve latest version from jsDelivr
            std::string pkgInfoUrl = "https://data.jsdelivr.com/v1/packages/npm/" + packageName;
            std::string pkgInfoJson = EditorHttpUtils::FetchUrl(pkgInfoUrl);

            if (m_CancelDownload) return;
            if (pkgInfoJson.empty())
            {
                std::lock_guard<std::mutex> lock(m_DownloadMutex);
                m_DownloadError = "Failed to fetch package info from jsDelivr";
                m_DownloadState = (int)DownloadState::Error;
                return;
            }

            auto pkgInfo = nlohmann::json::parse(pkgInfoJson);
            std::string version = pkgInfo.value("tags", nlohmann::json::object()).value("latest", "");
            if (version.empty())
            {
                // Fall back to first version in list
                auto& versions = pkgInfo["versions"];
                if (versions.is_array() && !versions.empty())
                    version = versions[0].value("version", "");
            }
            if (version.empty())
            {
                std::lock_guard<std::mutex> lock(m_DownloadMutex);
                m_DownloadError = "Could not resolve latest version for " + packageName;
                m_DownloadState = (int)DownloadState::Error;
                return;
            }

            {
                std::lock_guard<std::mutex> lock(m_DownloadMutex);
                m_DownloadStatus = "Found version " + version + ", scanning files...";
            }

            // 2. Fetch file listing for that specific version
            std::string listUrl = "https://data.jsdelivr.com/v1/packages/npm/" + packageName + "@" + version;
            std::string listJson = EditorHttpUtils::FetchUrl(listUrl);

            if (m_CancelDownload) return;
            if (listJson.empty())
            {
                std::lock_guard<std::mutex> lock(m_DownloadMutex);
                m_DownloadError = "Failed to fetch file listing for " + packageName + "@" + version;
                m_DownloadState = (int)DownloadState::Error;
                return;
            }

            auto listing = nlohmann::json::parse(listJson);

            std::vector<std::string> ttfFiles, cssFiles;

            // Recursive search through the file tree
            std::function<void(const nlohmann::json&, const std::string&)> findFiles;
            findFiles = [&](const nlohmann::json& files, const std::string& prefix)
            {
                for (auto& file : files)
                {
                    std::string name = file.value("name", "");
                    std::string type = file.value("type", "");
                    std::string path = prefix.empty() ? name : prefix + "/" + name;

                    if (type == "directory" && file.contains("files"))
                    {
                        findFiles(file["files"], path);
                    }
                    else if (type == "file")
                    {
                        std::string lower = name;
                        std::transform(lower.begin(), lower.end(), lower.begin(),
                                       [](unsigned char c) { return std::tolower(c); });

                        if (lower.size() > 4 && lower.substr(lower.size() - 4) == ".ttf")
                            ttfFiles.push_back(path);
                        if (lower.size() > 4 && lower.substr(lower.size() - 4) == ".css"
                            && lower.find(".min.") == std::string::npos)
                            cssFiles.push_back(path);
                    }
                }
            };

            if (listing.contains("files"))
                findFiles(listing["files"], "");

            // Prefer shortest name (e.g., "tabler-icons.ttf" over "tabler-icons-filled.ttf")
            auto pickShortest = [](std::vector<std::string>& files) -> std::string {
                if (files.empty()) return "";
                std::sort(files.begin(), files.end(),
                          [](const std::string& a, const std::string& b) {
                              return a.size() < b.size();
                          });
                return files[0];
            };

            std::string ttfFile = pickShortest(ttfFiles);
            std::string cssFile = pickShortest(cssFiles);

            if (ttfFile.empty())
            {
                std::lock_guard<std::mutex> lock(m_DownloadMutex);
                m_DownloadError = "No TTF file found in package";
                m_DownloadState = (int)DownloadState::Error;
                return;
            }
            if (cssFile.empty())
            {
                std::lock_guard<std::mutex> lock(m_DownloadMutex);
                m_DownloadError = "No CSS file found in package";
                m_DownloadState = (int)DownloadState::Error;
                return;
            }

            // 3. Derive font name from package
            std::string fontName = packageName;
            // Strip scope (e.g., "@tabler/icons-webfont" -> "icons-webfont")
            auto slashPos = fontName.find('/');
            if (slashPos != std::string::npos)
                fontName = fontName.substr(slashPos + 1);

            if (m_CancelDownload) return;

            // 4. Download TTF
            {
                std::lock_guard<std::mutex> lock(m_DownloadMutex);
                m_DownloadStatus = "Downloading TTF...";
            }

            std::string cdnBase = "https://cdn.jsdelivr.net/npm/" + packageName + "@" + version + "/";
            std::string ttfUrl = cdnBase + ttfFile;
            std::string ttfDest = (std::filesystem::path(m_IconFontsDir) / (fontName + ".ttf")).string();

            std::filesystem::create_directories(m_IconFontsDir);

            if (!EditorHttpUtils::DownloadFile(ttfUrl, ttfDest, &m_CancelDownload))
            {
                std::lock_guard<std::mutex> lock(m_DownloadMutex);
                m_DownloadError = "Failed to download TTF";
                m_DownloadState = (int)DownloadState::Error;
                return;
            }

            if (m_CancelDownload) return;

            // 5. Fetch CSS
            {
                std::lock_guard<std::mutex> lock(m_DownloadMutex);
                m_DownloadStatus = "Fetching CSS...";
            }

            std::string cssUrl = cdnBase + cssFile;
            std::string cssContent = EditorHttpUtils::FetchUrl(cssUrl);

            if (cssContent.empty())
            {
                std::lock_guard<std::mutex> lock(m_DownloadMutex);
                m_DownloadError = "Failed to fetch CSS";
                m_DownloadState = (int)DownloadState::Error;
                return;
            }

            // 6. Parse icons
            {
                std::lock_guard<std::mutex> lock(m_DownloadMutex);
                m_DownloadStatus = "Parsing icons...";
            }
            m_DownloadState = (int)DownloadState::Parsing;

            auto icons = ParseCssForIcons(cssContent);
            if (icons.empty())
            {
                std::lock_guard<std::mutex> lock(m_DownloadMutex);
                m_DownloadError = "No icons found in CSS";
                m_DownloadState = (int)DownloadState::Error;
                return;
            }

            // 7. Build result
            IconFont result;
            result.name = fontName;
            result.version = version;
            result.source = cdnBase;
            result.npmPackage = packageName;
            result.ttfPath = ttfDest;
            result.icons = std::move(icons);

            {
                std::lock_guard<std::mutex> lock(m_DownloadMutex);
                m_DownloadResult = std::move(result);
                m_DownloadStatus = "Done!";
            }
            m_DownloadState = (int)DownloadState::Done;
        }
        catch (const std::exception& e)
        {
            std::lock_guard<std::mutex> lock(m_DownloadMutex);
            m_DownloadError = std::string("Error: ") + e.what();
            m_DownloadState = (int)DownloadState::Error;
        }
    });
}

void IconFontExporter::StartDirectDownload(const std::string& ttfUrl, const std::string& cssUrl,
                                           const std::string& fontName)
{
    if (m_DownloadThread.joinable())
        m_DownloadThread.join();

    m_CancelDownload = false;
    m_DownloadState = (int)DownloadState::Downloading;
    {
        std::lock_guard<std::mutex> lock(m_DownloadMutex);
        m_DownloadStatus = "Downloading TTF...";
        m_DownloadError.clear();
    }

    m_DownloadThread = std::thread([this, ttfUrl, cssUrl, fontName]()
    {
        try
        {
            std::string ttfDest = (std::filesystem::path(m_IconFontsDir) / (fontName + ".ttf")).string();
            std::filesystem::create_directories(m_IconFontsDir);

            if (!EditorHttpUtils::DownloadFile(ttfUrl, ttfDest, &m_CancelDownload))
            {
                std::lock_guard<std::mutex> lock(m_DownloadMutex);
                m_DownloadError = "Failed to download TTF";
                m_DownloadState = (int)DownloadState::Error;
                return;
            }

            if (m_CancelDownload) return;

            {
                std::lock_guard<std::mutex> lock(m_DownloadMutex);
                m_DownloadStatus = "Fetching CSS...";
            }

            std::string cssContent = EditorHttpUtils::FetchUrl(cssUrl);
            if (cssContent.empty())
            {
                std::lock_guard<std::mutex> lock(m_DownloadMutex);
                m_DownloadError = "Failed to fetch CSS";
                m_DownloadState = (int)DownloadState::Error;
                return;
            }

            m_DownloadState = (int)DownloadState::Parsing;
            auto icons = ParseCssForIcons(cssContent);

            IconFont result;
            result.name = fontName;
            result.source = ttfUrl;
            result.ttfPath = ttfDest;
            result.icons = std::move(icons);

            {
                std::lock_guard<std::mutex> lock(m_DownloadMutex);
                m_DownloadResult = std::move(result);
            }
            m_DownloadState = (int)DownloadState::Done;
        }
        catch (const std::exception& e)
        {
            std::lock_guard<std::mutex> lock(m_DownloadMutex);
            m_DownloadError = std::string("Error: ") + e.what();
            m_DownloadState = (int)DownloadState::Error;
        }
    });
}

// ---------------------------------------------------------------------------
// Export
// ---------------------------------------------------------------------------

void IconFontExporter::ExportSelectedAsPNGs()
{
    if (!m_FtValid || m_SelectedIcons.empty() || m_CurrentFontIndex < 0)
        return;

    if (m_OutputPath.empty())
        return;

    auto& icons = m_Fonts[m_CurrentFontIndex].icons;
    std::filesystem::create_directories(m_OutputPath);

    for (int idx : m_SelectedIcons)
    {
        if (idx >= (int)icons.size())
            continue;

        std::vector<uint8_t> rgba;
        int w, h;
        if (!RasterizeGlyph(icons[idx].codepoint, m_ExportSize, rgba, w, h))
            continue;

        std::string filename = icons[idx].name + ".png";
        std::string filepath = (std::filesystem::path(m_OutputPath) / filename).string();
        stbi_write_png(filepath.c_str(), w, h, 4, rgba.data(), w * 4);
    }

    SaveExportManifest();
    EditorApplication::Get().RequestAssetRefresh();
}

void IconFontExporter::ExportSelectedAsAtlas()
{
    if (!m_FtValid || m_SelectedIcons.empty() || m_CurrentFontIndex < 0)
        return;

    if (m_OutputPath.empty())
        return;

    auto& icons = m_Fonts[m_CurrentFontIndex].icons;

    int cellSize = m_ExportSize;
    int padding = 2;
    int cellWithPad = cellSize + padding;
    int totalIcons = (int)m_SelectedIcons.size();

    // Size atlas to fit: try square-ish layout, capped at ATLAS_MAX_WIDTH
    int cols = (int)std::ceil(std::sqrt((double)totalIcons));
    if (cols < 1) cols = 1;
    if (cols * cellWithPad > ATLAS_MAX_WIDTH)
        cols = ATLAS_MAX_WIDTH / cellWithPad;
    if (cols < 1) cols = 1;
    int rows = (totalIcons + cols - 1) / cols;
    int atlasW = cols * cellWithPad;
    int atlasH = rows * cellWithPad;

    std::vector<uint8_t> atlasData(atlasW * atlasH * 4, 0);

    struct GlyphBounds { int x, y, w, h; };
    std::vector<GlyphBounds> glyphBounds;
    glyphBounds.reserve(totalIcons);

    int i = 0;
    for (int idx : m_SelectedIcons)
    {
        if (idx >= (int)icons.size())
            continue;

        int col = i % cols;
        int row = i / cols;
        int baseX = col * cellWithPad;
        int baseY = row * cellWithPad;

        std::vector<uint8_t> glyphRGBA;
        int gw, gh;
        if (RasterizeGlyph(icons[idx].codepoint, cellSize, glyphRGBA, gw, gh))
        {
            for (int y = 0; y < gh; y++)
            {
                for (int x = 0; x < gw; x++)
                {
                    int destX = baseX + x;
                    int destY = baseY + y;
                    if (destX < 0 || destX >= atlasW || destY < 0 || destY >= atlasH)
                        continue;

                    uint8_t alpha = glyphRGBA[(y * gw + x) * 4 + 3];
                    int px = (destY * atlasW + destX) * 4;
                    atlasData[px + 0] = 255;
                    atlasData[px + 1] = 255;
                    atlasData[px + 2] = 255;
                    atlasData[px + 3] = alpha;
                }
            }
        }

        // Compute tight content bounds for this glyph
        int minX = gw, minY = gh, maxX = -1, maxY = -1;
        for (int y = 0; y < gh; y++)
        {
            for (int x = 0; x < gw; x++)
            {
                if (glyphRGBA[(y * gw + x) * 4 + 3] > 0)
                {
                    if (x < minX) minX = x;
                    if (y < minY) minY = y;
                    if (x > maxX) maxX = x;
                    if (y > maxY) maxY = y;
                }
            }
        }

        // Store tight bounds (fallback to full cell if glyph is empty)
        GlyphBounds bounds;
        if (maxX >= minX && maxY >= minY)
        {
            bounds.x = baseX + minX;
            bounds.y = baseY + minY;
            bounds.w = maxX - minX + 1;
            bounds.h = maxY - minY + 1;
        }
        else
        {
            bounds.x = baseX;
            bounds.y = baseY;
            bounds.w = cellSize;
            bounds.h = cellSize;
        }
        glyphBounds.push_back(bounds);

        i++;
    }

    std::filesystem::create_directories(m_OutputPath);

    std::string fontName = m_Fonts[m_CurrentFontIndex].name;
    std::string filepath = (std::filesystem::path(m_OutputPath) / (fontName + "_atlas.png")).string();
    stbi_write_png(filepath.c_str(), atlasW, atlasH, 4, atlasData.data(), atlasW * 4);

    // Write spritesheet .data sidecar with named frames (using AssetData API to preserve GUID)
    {
        std::string dataPath = filepath + ".data";
        AssetData assetData;
        if (!LoadAssetData(dataPath, assetData))
            assetData.guid = GenerateGuid();

        nlohmann::json framesJson = nlohmann::json::array();
        int fi = 0;
        for (int idx : m_SelectedIcons)
        {
            if (idx >= (int)icons.size())
                continue;

            const auto& bounds = glyphBounds[fi];
            nlohmann::json f;
            f["name"] = icons[idx].name;
            f["x"] = bounds.x;
            f["y"] = bounds.y;
            f["width"] = bounds.w;
            f["height"] = bounds.h;
            framesJson.push_back(f);
            fi++;
        }

        assetData.settings["settings"]["sprite"]["mode"] = "atlas";
        assetData.settings["settings"]["sprite"]["frames"] = framesJson;
        SaveAssetData(dataPath, assetData);
    }

    SaveExportManifest();
    EditorApplication::Get().RequestAssetRefresh();
}

// ---------------------------------------------------------------------------
// Export Manifest
// ---------------------------------------------------------------------------

void IconFontExporter::SaveExportManifest()
{
    if (m_OutputPath.empty() || m_CurrentFontIndex < 0)
        return;

    auto& icons = m_Fonts[m_CurrentFontIndex].icons;

    nlohmann::json j;
    j["type"] = "iconexport";
    j["font"] = m_Fonts[m_CurrentFontIndex].name;
    j["npmPackage"] = m_Fonts[m_CurrentFontIndex].npmPackage;
    j["source"] = m_Fonts[m_CurrentFontIndex].source;
    j["exportSize"] = m_ExportSize;
    j["asAtlas"] = m_ExportAsAtlas;
    nlohmann::json iconNames = nlohmann::json::array();
    for (int idx : m_SelectedIcons)
    {
        if (idx < (int)icons.size())
            iconNames.push_back(icons[idx].name);
    }
    j["icons"] = iconNames;

    std::string fontName = m_Fonts[m_CurrentFontIndex].name;
    std::string manifestPath = (std::filesystem::path(m_OutputPath) / (fontName + ".asset")).string();
    std::ofstream out(manifestPath);
    out << j.dump(2);
}

void IconFontExporter::LoadExportManifest(const std::string& manifestPath)
{
    try
    {
        std::ifstream f(manifestPath);
        nlohmann::json j;
        f >> j;

        // Restore font
        std::string fontName = j.value("font", "");
        bool fontFound = false;
        for (int i = 0; i < (int)m_Fonts.size(); i++)
        {
            if (m_Fonts[i].name == fontName)
            {
                SwitchFont(i);
                fontFound = true;
                break;
            }
        }

        // Auto-download if font not cached but npm package is known
        if (!fontFound)
        {
            std::string npmPkg = j.value("npmPackage", "");
            if (!npmPkg.empty() && m_DownloadState.load() == (int)DownloadState::Idle)
            {
                m_PendingManifestPath = manifestPath;
                StartNpmDownload(npmPkg);
                return;
            }
        }

        // Restore export settings
        m_ExportSize = j.value("exportSize", 32);
        m_ExportAsAtlas = j.value("asAtlas", false);

        // Restore selection by name
        if (m_CurrentFontIndex >= 0 && j.contains("icons") && j["icons"].is_array())
        {
            auto& icons = m_Fonts[m_CurrentFontIndex].icons;
            m_SelectedIcons.clear();

            for (auto& nameVal : j["icons"])
            {
                std::string name = nameVal.get<std::string>();
                for (int i = 0; i < (int)icons.size(); i++)
                {
                    if (icons[i].name == name)
                    {
                        m_SelectedIcons.insert(i);
                        break;
                    }
                }
            }
        }
    }
    catch (...)
    {
        // Ignore malformed manifest
    }
}

// ---------------------------------------------------------------------------
// UI
// ---------------------------------------------------------------------------

void IconFontExporter::OnGUI()
{
    ImGui::Begin(GetTitle(), &m_IsOpen);

    if (!m_FtLibrary)
    {
        ImGui::TextColored(ImVec4(1, 0.3f, 0.3f, 1), "Failed to initialize FreeType");
        ImGui::End();
        return;
    }

    DrawFontSelector();
    DrawDownloadSection();

    if (m_CurrentFontIndex >= 0 && m_FtValid)
    {
        if (m_LastBuiltSize != m_IconDisplaySize || m_AtlasTexture == 0)
            BuildIconAtlas();

        DrawToolbar();
        DrawExportSettings();
        ImGui::Separator();
        DrawIconGrid();
    }
    else if (m_Fonts.empty())
    {
        ImGui::TextDisabled("No icon fonts loaded. Click '+ Add Font' to download one.");
    }

    ImGui::End();
}

void IconFontExporter::DrawFontSelector()
{
    // Font dropdown
    const char* currentName = (m_CurrentFontIndex >= 0) ?
        m_Fonts[m_CurrentFontIndex].name.c_str() : "No font loaded";

    ImGui::SetNextItemWidth(250);
    if (ImGui::BeginCombo("##font", currentName))
    {
        for (int i = 0; i < (int)m_Fonts.size(); i++)
        {
            std::string label = m_Fonts[i].name;
            if (!m_Fonts[i].version.empty())
                label += " v" + m_Fonts[i].version;

            bool selected = (i == m_CurrentFontIndex);
            if (ImGui::Selectable(label.c_str(), selected))
                SwitchFont(i);
        }
        ImGui::EndCombo();
    }

    ImGui::SameLine();
    if (ImGui::Button("+ Add Font"))
        m_ShowAddFont = !m_ShowAddFont;

    if (m_CurrentFontIndex >= 0)
    {
        ImGui::SameLine();
        if (ImGui::Button("Remove"))
            RemoveCurrentFont();
    }
}

void IconFontExporter::DrawDownloadSection()
{
    if (!m_ShowAddFont)
        return;

    ImGui::Separator();

    // Tab bar for npm vs direct URL
    if (ImGui::BeginTabBar("##addfonttabs"))
    {
        if (ImGui::BeginTabItem("npm Package"))
        {
            m_AddFontTab = 0;
            ImGui::SetNextItemWidth(300);
            ImGui::InputTextWithHint("##npm", "@tabler/icons-webfont", m_NpmPackageInput, sizeof(m_NpmPackageInput));

            bool downloading = m_DownloadState.load() != (int)DownloadState::Idle;

            ImGui::SameLine();
            if (downloading)
                ImGui::BeginDisabled();
            if (ImGui::Button("Download##npm"))
            {
                if (m_NpmPackageInput[0] != '\0')
                    StartNpmDownload(m_NpmPackageInput);
            }
            if (downloading)
                ImGui::EndDisabled();

            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Direct URL"))
        {
            m_AddFontTab = 1;
            ImGui::SetNextItemWidth(400);
            ImGui::InputTextWithHint("TTF URL", "https://...", m_TtfUrlInput, sizeof(m_TtfUrlInput));
            ImGui::SetNextItemWidth(400);
            ImGui::InputTextWithHint("CSS URL", "https://...", m_CssUrlInput, sizeof(m_CssUrlInput));
            ImGui::SetNextItemWidth(200);
            ImGui::InputTextWithHint("Font Name", "my-icons", m_FontNameInput, sizeof(m_FontNameInput));

            bool downloading = m_DownloadState.load() != (int)DownloadState::Idle;

            if (downloading)
                ImGui::BeginDisabled();
            if (ImGui::Button("Download##direct"))
            {
                if (m_TtfUrlInput[0] != '\0' && m_CssUrlInput[0] != '\0' && m_FontNameInput[0] != '\0')
                    StartDirectDownload(m_TtfUrlInput, m_CssUrlInput, m_FontNameInput);
            }
            if (downloading)
                ImGui::EndDisabled();

            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
    }

    // Download status
    int state = m_DownloadState.load();
    if (state == (int)DownloadState::Downloading || state == (int)DownloadState::Parsing)
    {
        std::string status;
        {
            std::lock_guard<std::mutex> lock(m_DownloadMutex);
            status = m_DownloadStatus;
        }
        ImGui::TextColored(ImVec4(0.5f, 0.8f, 1.0f, 1.0f), "%s", status.c_str());

        ImGui::SameLine();
        if (ImGui::Button("Cancel"))
            m_CancelDownload = true;
    }
    else if (state == (int)DownloadState::Error)
    {
        std::string error;
        {
            std::lock_guard<std::mutex> lock(m_DownloadMutex);
            error = m_DownloadError;
        }
        ImGui::TextColored(ImVec4(1, 0.3f, 0.3f, 1), "%s", error.c_str());
    }

    ImGui::Separator();
}

void IconFontExporter::DrawToolbar()
{
    ImGui::InputTextWithHint("##search", "Search icons...", m_SearchFilter, sizeof(m_SearchFilter));

    ImGui::SameLine();
    ImGui::Text("%d selected", (int)m_SelectedIcons.size());

    ImGui::SameLine();
    if (ImGui::Button("Clear"))
        m_SelectedIcons.clear();
}

void IconFontExporter::DrawExportSettings()
{
    if (!ImGui::CollapsingHeader("Export Settings"))
        return;

    ImGui::SetNextItemWidth(120);
    ImGui::SliderInt("Export Size (px)", &m_ExportSize, 8, 256);

    // Output folder with picker popup
    if (m_OutputPath.empty())
        ImGui::TextDisabled("No output folder selected");
    else
    {
        // Show path relative to assets for brevity
        auto& assetsPath = EditorApplication::Get().GetAssetsPath();
        if (!assetsPath.empty() && m_OutputPath.find(assetsPath) == 0)
        {
            std::string rel = m_OutputPath.substr(assetsPath.size());
            if (!rel.empty() && (rel[0] == '/' || rel[0] == '\\'))
                rel = rel.substr(1);
            ImGui::Text("Output: assets/%s", rel.c_str());
        }
        else
            ImGui::TextWrapped("Output: %s", m_OutputPath.c_str());
    }

    bool hasProject = EditorApplication::Get().HasProject();
    if (!hasProject)
        ImGui::BeginDisabled();

    if (ImGui::Button("Select Folder..."))
    {
        m_FolderPickerSelected = m_OutputPath.empty() ? EditorApplication::Get().GetAssetsPath() : m_OutputPath;
        m_NewFolderName[0] = '\0';
        m_ShowFolderPicker = true;
        ImGui::OpenPopup("Select Export Folder");
    }

    if (!hasProject)
        ImGui::EndDisabled();

    DrawFolderPickerPopup();

    int exportMode = m_ExportAsAtlas ? 1 : 0;
    if (ImGui::RadioButton("Individual PNGs", &exportMode, 0))
        m_ExportAsAtlas = false;
    ImGui::SameLine();
    if (ImGui::RadioButton("Atlas Spritesheet", &exportMode, 1))
        m_ExportAsAtlas = true;

    bool hasOutput = !m_OutputPath.empty();
    bool hasSelection = !m_SelectedIcons.empty();

    if (!hasOutput)
        ImGui::BeginDisabled();
    if (!hasSelection)
        ImGui::BeginDisabled();

    if (ImGui::Button("Export"))
    {
        if (m_ExportAsAtlas)
            ExportSelectedAsAtlas();
        else
            ExportSelectedAsPNGs();
    }

    if (!hasSelection)
    {
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::TextDisabled("Select icons first");
    }
    if (!hasOutput)
    {
        ImGui::EndDisabled();
        if (hasSelection)
        {
            ImGui::SameLine();
            ImGui::TextDisabled("Select output folder first");
        }
    }
}

void IconFontExporter::DrawFolderPickerPopup()
{
    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(400, 500), ImGuiCond_Appearing);

    if (!ImGui::BeginPopupModal("Select Export Folder", &m_ShowFolderPicker, ImGuiWindowFlags_NoScrollbar))
        return;

    auto& assetsPath = EditorApplication::Get().GetAssetsPath();

    ImGui::Text("Select a folder inside your project assets:");
    ImGui::Separator();

    ImGui::BeginChild("FolderTree", ImVec2(0, -ImGui::GetFrameHeightWithSpacing() * 3), ImGuiChildFlags_Borders);

    // Root node = assets folder
    bool rootSelected = (m_FolderPickerSelected == assetsPath);
    ImGuiTreeNodeFlags rootFlags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_DefaultOpen;
    if (rootSelected)
        rootFlags |= ImGuiTreeNodeFlags_Selected;

    bool rootOpen = ImGui::TreeNodeEx("assets", rootFlags);
    if (ImGui::IsItemClicked())
        m_FolderPickerSelected = assetsPath;

    if (rootOpen)
    {
        DrawFolderTree(assetsPath);
        ImGui::TreePop();
    }

    ImGui::EndChild();

    // New folder creation
    ImGui::SetNextItemWidth(200);
    ImGui::InputTextWithHint("##newfolder", "New folder name...", m_NewFolderName, sizeof(m_NewFolderName));
    ImGui::SameLine();
    bool canCreate = m_NewFolderName[0] != '\0' && !m_FolderPickerSelected.empty();
    if (!canCreate)
        ImGui::BeginDisabled();
    if (ImGui::Button("Create Folder"))
    {
        std::string newPath = (std::filesystem::path(m_FolderPickerSelected) / m_NewFolderName).string();
        std::filesystem::create_directories(newPath);
        m_FolderPickerSelected = newPath;
        m_NewFolderName[0] = '\0';
    }
    if (!canCreate)
        ImGui::EndDisabled();

    // Select / Cancel
    bool canSelect = !m_FolderPickerSelected.empty();
    if (!canSelect)
        ImGui::BeginDisabled();
    if (ImGui::Button("Select"))
    {
        m_OutputPath = m_FolderPickerSelected;

        // Auto-load .asset with type "iconexport" if one exists in the folder
        for (auto& entry : std::filesystem::directory_iterator(m_OutputPath))
        {
            if (entry.path().extension() == ".asset")
            {
                try
                {
                    std::ifstream f(entry.path());
                    nlohmann::json j;
                    f >> j;
                    if (j.value("type", "") == "iconexport")
                    {
                        LoadExportManifest(entry.path().string());
                        break;
                    }
                }
                catch (...) {}
            }
        }

        m_ShowFolderPicker = false;
        ImGui::CloseCurrentPopup();
    }
    if (!canSelect)
        ImGui::EndDisabled();

    ImGui::SameLine();
    if (ImGui::Button("Cancel"))
    {
        m_ShowFolderPicker = false;
        ImGui::CloseCurrentPopup();
    }

    ImGui::EndPopup();
}

void IconFontExporter::DrawFolderTree(const std::string& dirPath)
{
    std::vector<std::filesystem::directory_entry> subdirs;
    try
    {
        for (auto& entry : std::filesystem::directory_iterator(dirPath))
        {
            if (entry.is_directory())
                subdirs.push_back(entry);
        }
    }
    catch (...) { return; }

    std::sort(subdirs.begin(), subdirs.end(),
              [](const auto& a, const auto& b) { return a.path().filename() < b.path().filename(); });

    for (auto& subdir : subdirs)
    {
        std::string path = subdir.path().string();
        std::string name = subdir.path().filename().string();

        bool isSelected = (m_FolderPickerSelected == path);
        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow;
        if (isSelected)
            flags |= ImGuiTreeNodeFlags_Selected;

        // Check if has subdirectories
        bool hasChildren = false;
        try
        {
            for (auto& child : std::filesystem::directory_iterator(path))
            {
                if (child.is_directory()) { hasChildren = true; break; }
            }
        }
        catch (...) {}

        if (!hasChildren)
            flags |= ImGuiTreeNodeFlags_Leaf;

        bool open = ImGui::TreeNodeEx(name.c_str(), flags);
        if (ImGui::IsItemClicked())
            m_FolderPickerSelected = path;

        if (open)
        {
            if (hasChildren)
                DrawFolderTree(path);
            ImGui::TreePop();
        }
    }
}

void IconFontExporter::DrawIconGrid()
{
    if (m_CurrentFontIndex < 0 || m_AtlasTexture == 0)
        return;

    auto& icons = m_Fonts[m_CurrentFontIndex].icons;

    // Filter
    std::vector<int> filtered;
    filtered.reserve(icons.size());

    std::string filterLower;
    if (m_SearchFilter[0] != '\0')
    {
        filterLower = m_SearchFilter;
        std::transform(filterLower.begin(), filterLower.end(), filterLower.begin(),
                       [](unsigned char c) { return std::tolower(c); });
    }

    for (int i = 0; i < (int)icons.size(); i++)
    {
        if (filterLower.empty() || icons[i].name.find(filterLower) != std::string::npos)
            filtered.push_back(i);
    }

    // Select All button
    ImGui::SameLine();
    if (ImGui::Button("Select All"))
    {
        for (int idx : filtered)
            m_SelectedIcons.insert(idx);
    }

    ImGui::Text("%d icons shown", (int)filtered.size());

    if (filtered.empty())
        return;

    float iconSize = (float)m_IconDisplaySize;
    float rowHeight = iconSize + 4.0f;

    ImGui::BeginChild("IconList", ImVec2(0, 0), ImGuiChildFlags_None);

    ImGuiListClipper clipper;
    clipper.Begin((int)filtered.size(), rowHeight);

    while (clipper.Step())
    {
        for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; row++)
        {
            int iconIdx = filtered[row];
            bool isSelected = m_SelectedIcons.count(iconIdx) > 0;

            ImGui::PushID(iconIdx);

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
                    m_SelectedIcons.erase(iconIdx);
                else
                    m_SelectedIcons.insert(iconIdx);
            }

            // Draw icon image over the button
            if (iconIdx < (int)m_AtlasEntries.size())
            {
                auto& entry = m_AtlasEntries[iconIdx];
                ImTextureID texId = (ImTextureID)(intptr_t)m_AtlasTexture;
                ImVec2 imgP0(rowStart.x + 2.0f, rowStart.y + 2.0f);
                ImVec2 imgP1(rowStart.x + 2.0f + iconSize, rowStart.y + 2.0f + iconSize);
                ImGui::GetWindowDrawList()->AddImage(texId, imgP0, imgP1,
                                                     ImVec2(entry.u0, entry.v0),
                                                     ImVec2(entry.u1, entry.v1));
            }

            // Draw name next to icon
            ImVec2 textPos(rowStart.x + iconSize + 10.0f, rowStart.y + (rowHeight - ImGui::GetTextLineHeight()) * 0.5f);
            ImGui::GetWindowDrawList()->AddText(textPos, IM_COL32(255, 255, 255, 255),
                                                icons[iconIdx].name.c_str());

            ImGui::PopID();
        }
    }

    clipper.End();
    ImGui::EndChild();
}

} // namespace DekiEditor
