#pragma once

#include <deki-editor/EditorWindow.h>
#include <string>
#include <cstdint>
#include <vector>
#include <utility>

namespace DekiEditor
{

/**
 * @brief Frame animation editor window
 *
 * Creates and edits .frameanim files by selecting frames from a spritesheet.
 * Frames are referenced by GUID (sub-assets of the parent spritesheet).
 */
class FrameAnimationEditorWindow : public EditorWindow
{
public:
    FrameAnimationEditorWindow();
    ~FrameAnimationEditorWindow() override;

    // ========================================================================
    // EditorWindow interface
    // ========================================================================

    const char* GetTitle() override { return "Animation Editor"; }
    const char* GetMenuPath() override { return "2D/Animation Editor"; }

    void OnOpen() override;
    void OnClose() override;
    void OnGUI() override;

    bool CanOpenFile(const char* extension) override;
    void OpenFile(const char* filePath, const char* cachePath) override;

private:
    // UI Drawing
    void DrawSpritesheetPicker();
    void DrawAnimationList();
    void DrawFramePalette();
    void DrawTimeline();
    void DrawProperties();
    void DrawPreview();

    // File I/O
    bool LoadAnimation(const std::string& path);
    bool SaveAnimation();
    bool SaveAnimationAs(const std::string& path);
    void CreateNewAnimation();

    // Spritesheet loading
    void LoadSpritesheetFrames();
    void UploadSpritesheetToGPU();

    // Timeline frame data
    struct TimelineFrame
    {
        std::string frameGuid;
        int duration = 100;  // milliseconds
    };

    // Animation sequence (one animation clip)
    struct AnimationSequence
    {
        std::string name;
        bool loop = true;
        std::vector<TimelineFrame> frames;
    };

    // Context paths from EditorApplication
    std::string m_ProjectPath;
    std::string m_AssetsPath;
    std::string m_CachePath;

    // Animation file
    std::string m_AnimationPath;     // Full path to .frameanim file
    bool m_IsDirty = false;          // Has unsaved changes

    // Animation data (multiple sequences per file)
    std::string m_SpritesheetGuid;
    std::vector<AnimationSequence> m_Animations;
    int m_CurrentAnimationIndex = 0;  // Currently selected animation for editing

    // Available frames from spritesheet
    struct AvailableFrame
    {
        std::string guid;
        int index;
        float u0, v0, u1, v1;  // UV coordinates
    };
    std::vector<AvailableFrame> m_AvailableFrames;

    // Spritesheet texture
    uint32_t m_SpritesheetTextureId = 0;
    int m_SpritesheetWidth = 0;
    int m_SpritesheetHeight = 0;
    bool m_NeedsTextureUpload = false;
    uint8_t* m_TextureData = nullptr;

    // Preview state
    int m_PreviewFrame = 0;
    bool m_IsPlaying = false;
    float m_PreviewTimer = 0.0f;
    uint32_t m_LastPreviewTime = 0;

    // Selection
    int m_SelectedTimelineIndex = -1;
    int m_DefaultDuration = 100;  // Default duration for new frames

    // Status
    std::string m_StatusMessage;
    bool m_StatusIsError = false;

    // Spritesheet picker state
    std::vector<std::pair<std::string, std::string>> m_SpritesheetAssets;  // guid, path pairs
    char m_SpritesheetSearchBuffer[256] = "";
    bool m_SpritesheetPickerNeedsRefresh = true;
};

} // namespace DekiEditor
