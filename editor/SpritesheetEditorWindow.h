#pragma once

#include <deki-editor/EditorWindow.h>
#include <deki-editor/TextureImporter.h>
#include <string>
#include <cstdint>
#include <vector>

namespace DekiEditor
{

/**
 * @brief UI mode for sprite slicing
 */
enum class SlicingUIMode
{
    Grid = 0,  // Uniform grid generation
    Free = 1   // Free-form auto-cut detection
};

/**
 * @brief Spritesheet slicing editor window
 *
 * Sets frame dimensions for spritesheet textures.
 * Saves frameWidth/frameHeight to .png.data file.
 * Does NOT handle animations - use Animation Editor for that.
 */
class SpritesheetEditorWindow : public EditorWindow
{
public:
    SpritesheetEditorWindow();
    ~SpritesheetEditorWindow() override;

    // ========================================================================
    // EditorWindow interface
    // ========================================================================

    const char* GetTitle() override { return "Sprite Slicer"; }
    const char* GetMenuPath() override { return "2D/Sprite Slicer"; }

    void OnOpen() override;
    void OnClose() override;
    void OnGUI() override;

    bool CanOpenFile(const char* extension) override;
    void OpenFile(const char* filePath, const char* cachePath) override;

private:
    // UI Drawing
    void DrawTexturePreview();
    void DrawSlicingControls();

    // File I/O
    void LoadTextureData();      // Load pixel data from cache
    void UploadTextureToGPU();   // Create OpenGL texture (called from OnGUI)
    bool LoadSliceSettings();    // Load from .png.data
    bool SaveSliceSettings();    // Save to .png.data

    // Frame generation
    void GenerateGrid();         // Generate uniform grid frames
    void RunAutoCut();           // Detect sprites from transparent regions
    bool IsUniformGrid() const;  // Check if atlas frames form a uniform grid

    // Context paths from EditorApplication
    std::string m_ProjectPath;
    std::string m_AssetsPath;
    std::string m_CachePath;

    // Source texture
    std::string m_TexturePath;      // Full path to texture PNG
    std::string m_TextureCachePath; // Full path to cached DTEX
    std::string m_TextureGuid;      // Texture GUID
    uint32_t m_TextureId = 0;
    int m_TextureWidth = 0;
    int m_TextureHeight = 0;
    uint8_t* m_TextureData = nullptr;
    bool m_NeedsTextureUpload = false;

    // Slicing settings (all frames stored as atlas internally)
    int m_FrameWidth = 0;   // For grid generation UI
    int m_FrameHeight = 0;  // For grid generation UI
    std::vector<DekiEditor::AtlasFrame> m_AtlasFrames;

    // UI mode
    SlicingUIMode m_UIMode = SlicingUIMode::Grid;

    // UI state
    float m_Zoom = 1.0f;
    float m_PanOffsetX = 0.0f;
    float m_PanOffsetY = 0.0f;
    bool m_IsPanning = false;
    float m_LastMousePosX = 0.0f;
    float m_LastMousePosY = 0.0f;

    // Status
    std::string m_StatusMessage;
    bool m_StatusIsError = false;
};

} // namespace DekiEditor
