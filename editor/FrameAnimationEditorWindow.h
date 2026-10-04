#pragma once

#include <deki-editor/EditorWindow.h>
#include <string>
#include <cstdint>
#include <vector>
#include <utility>

// Editor extensions live in DekiEditor; the package's own types are in Deki2D.
using namespace Deki2D;

namespace DekiEditor
{

/// Window that creates and edits .anim files by picking frames from a
/// spritesheet. Frames are referenced by GUID (sub-assets of the spritesheet).
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

    // Paths from EditorApplication.
    std::string m_ProjectPath;
    std::string m_AssetsPath;
    std::string m_CachePath;

    // Animation file
    std::string m_AnimationPath;  // full path to the .anim file
    bool m_IsDirty = false;       // has unsaved changes

    // Animation data (several sequences per file).
    std::string m_SpritesheetGuid;
    std::vector<AnimationSequence> m_Animations;
    int m_CurrentAnimationIndex = 0;  // the animation being edited

    // The spritesheet's frames.
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
    int m_DefaultDuration = 100;  // duration for new frames, in milliseconds

    // Status
    std::string m_StatusMessage;
    bool m_StatusIsError = false;

    // Spritesheet picker state
    std::vector<std::pair<std::string, std::string>> m_SpritesheetAssets;  // guid, path pairs
    char m_SpritesheetSearchBuffer[256] = "";
    bool m_SpritesheetPickerNeedsRefresh = true;
};

}  // namespace DekiEditor
