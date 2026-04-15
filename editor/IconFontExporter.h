#pragma once

#include <deki-editor/EditorWindow.h>
#include <string>
#include <vector>
#include <set>
#include <mutex>
#include <thread>
#include <atomic>
#include <cstdint>

struct FT_LibraryRec_;
struct FT_FaceRec_;
typedef struct FT_LibraryRec_* FT_Library;
typedef struct FT_FaceRec_* FT_Face;

namespace DekiEditor
{

struct IconAtlasEntry
{
    float u0, v0, u1, v1;
};

struct IconInfo
{
    std::string name;
    uint32_t codepoint;
};

struct IconFont
{
    std::string name;
    std::string version;
    std::string source;
    std::string npmPackage;
    std::string ttfPath;
    std::vector<IconInfo> icons;
};

class IconFontExporter : public EditorWindow
{
public:
    IconFontExporter();
    ~IconFontExporter() override;

    const char* GetTitle() override { return "Icon Font Exporter"; }
    const char* GetMenuPath() override { return "Icon Font Exporter"; }

    void OnOpen() override;
    void OnClose() override;
    void OnGUI() override;
    void OnUpdate(float deltaTime) override;
    bool CanOpenAssetType(const char* assetType) override;
    void OpenFile(const char* filePath, const char* cachePath) override;

private:
    // Font cache
    std::string m_IconFontsDir;
    std::vector<IconFont> m_Fonts;
    int m_CurrentFontIndex = -1;

    void LoadCachedFonts();
    void SwitchFont(int index);
    void RemoveCurrentFont();

    // FreeType
    FT_Library m_FtLibrary = nullptr;
    FT_Face m_FtFace = nullptr;
    bool m_FtValid = false;
    void InitFreeType();
    void CleanupFreeType();

    // Atlas
    uint32_t m_AtlasTexture = 0;
    int m_AtlasWidth = 0;
    int m_AtlasHeight = 0;
    std::vector<IconAtlasEntry> m_AtlasEntries;
    int m_IconDisplaySize = 64;
    int m_LastBuiltSize = 0;

    void BuildIconAtlas();
    void CleanupAtlas();
    bool RasterizeGlyph(uint32_t codepoint, int size,
                        std::vector<uint8_t>& outRGBA, int& outW, int& outH);

    // Search & selection
    char m_SearchFilter[256] = "";
    std::set<int> m_SelectedIcons;

    // Export
    int m_ExportSize = 32;
    std::string m_OutputPath;
    bool m_ExportAsAtlas = false;

    void ExportSelectedAsPNGs();
    void ExportSelectedAsAtlas();
    void SaveExportManifest();
    void LoadExportManifest(const std::string& manifestPath);

    // Download
    enum class DownloadState { Idle, Downloading, Parsing, Done, Error };
    std::atomic<int> m_DownloadState{(int)DownloadState::Idle};
    std::mutex m_DownloadMutex;
    std::string m_DownloadStatus;
    std::string m_DownloadError;
    IconFont m_DownloadResult;
    std::thread m_DownloadThread;
    std::atomic<bool> m_CancelDownload{false};

    std::string m_PendingManifestPath;

    bool m_ShowAddFont = false;
    int m_AddFontTab = 0; // 0 = npm, 1 = direct URL
    char m_NpmPackageInput[256] = "";
    char m_TtfUrlInput[512] = "";
    char m_CssUrlInput[512] = "";
    char m_FontNameInput[128] = "";

    void StartNpmDownload(const std::string& packageName);
    void StartDirectDownload(const std::string& ttfUrl, const std::string& cssUrl, const std::string& fontName);
    std::vector<IconInfo> ParseCssForIcons(const std::string& cssContent);
    void SaveFontMetadata(const IconFont& font);

    // Folder picker popup
    bool m_ShowFolderPicker = false;
    std::string m_FolderPickerSelected;
    char m_NewFolderName[128] = "";
    void DrawFolderPickerPopup();
    void DrawFolderTree(const std::string& dirPath);

    // UI helpers
    void DrawFontSelector();
    void DrawDownloadSection();
    void DrawToolbar();
    void DrawExportSettings();
    void DrawIconGrid();
};

} // namespace DekiEditor
