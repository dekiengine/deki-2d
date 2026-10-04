#pragma once

#include <deki-editor/EditorWindow.h>
#include <deki-editor/TextureImporter.h>
#include <string>
#include <cstdint>
#include <vector>

// Editor extensions live in DekiEditor; the package's own types are in Deki2D.
using namespace Deki2D;

namespace DekiEditor
{

/// How sprites are sliced.
enum class SlicingUIMode
{
    Grid = 0,  // a uniform grid
    Free = 1   // free-form, found by auto-cut
};

/// Window that slices a spritesheet texture into frames and saves them to its
/// .png.data file. Animations are made in the Animation Editor.
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
    void SaveAndReimport();   // write .png.data, re-import so the frames exist
    void OnSettingsLoaded();  // after a load: the saved baseline, the mode
    bool IsDirty() const;     // the frames differ from what is saved

    // File I/O
    void LoadTextureData();     // pixel data from the cache
    void UploadTextureToGPU();  // creates the OpenGL texture (called from OnGUI)
    bool LoadSliceSettings();   // from .png.data
    bool SaveSliceSettings();   // to .png.data

    // Frame generation
    void GenerateGrid();         // uniform grid frames
    void RunAutoCut();           // finds sprites between transparent regions
    bool IsUniformGrid() const;  // whether the atlas frames form a uniform grid

    // Paths from EditorApplication.
    std::string m_ProjectPath;
    std::string m_AssetsPath;
    std::string m_CachePath;

    // Source texture
    std::string m_TexturePath;       // full path to the texture PNG
    std::string m_TextureCachePath;  // full path to the cached DTEX
    std::string m_TextureGuid;
    uint32_t m_TextureId = 0;
    int m_TextureWidth = 0;
    int m_TextureHeight = 0;
    uint8_t* m_TextureData = nullptr;
    bool m_NeedsTextureUpload = false;

    // Slicing settings. Every frame is stored as an atlas frame.
    int m_FrameWidth = 0;   // for the grid generation UI
    int m_FrameHeight = 0;  // for the grid generation UI
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
    bool m_FitPending = true;  // fit the image to the canvas on its next draw

    std::vector<DekiEditor::AtlasFrame> m_SavedFrames;  // as last loaded or saved
    int32_t m_NextFrameId = 0;  // never reused: a reference to a deleted frame must not find a new one
    int m_SelectedFrame = -1;   // picked in the list or on the canvas
    int m_HoveredFrame = -1;    // under the mouse, in either

    // Status
    std::string m_StatusMessage;
    bool m_StatusIsError = false;
};

}  // namespace DekiEditor
