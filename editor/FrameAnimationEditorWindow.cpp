// Editor-only translation unit (a firmware build compiles nothing from it).
#ifdef DEKI_EDITOR

#include "FrameAnimationEditorWindow.h"
#include <deki-editor/EditorUI.h>
#include <deki-editor/EditorApplication.h>
#include <deki-editor/EditorAssets.h>
#include <deki-editor/AssetPipeline.h>
#include <deki-editor/AssetDatabase.h>
#include <deki-editor/TextureImporter.h>
#include <deki-editor/TextureData.h>
#include <deki-editor/SubAsset.h>

#include <fstream>
#include <filesystem>
#include <cctype>
#include <cstdio>
#include <cfloat>
#include <SDL3/SDL.h>
#include <SDL3/SDL_opengl.h>
#include <nlohmann/json.hpp>

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace DekiEditor
{

FrameAnimationEditorWindow::FrameAnimationEditorWindow() = default;

FrameAnimationEditorWindow::~FrameAnimationEditorWindow()
{
    if (m_TextureData)
    {
        delete[] m_TextureData;
        m_TextureData = nullptr;
    }

    // Don't delete m_SpritesheetTextureId - it's owned by EditorAssets
    m_SpritesheetTextureId = 0;
}

void FrameAnimationEditorWindow::OnOpen()
{
    auto& app = EditorApplication::Get();
    m_ProjectPath = app.GetProjectPath();
    m_AssetsPath = app.GetAssetsPath();
    m_CachePath = app.GetCachePath();
}

void FrameAnimationEditorWindow::OnClose()
{
    if (m_TextureData)
    {
        delete[] m_TextureData;
        m_TextureData = nullptr;
    }

    // Don't delete m_SpritesheetTextureId - it's owned by EditorAssets
    m_SpritesheetTextureId = 0;
    m_SpritesheetWidth = 0;
    m_SpritesheetHeight = 0;

    // Clear all animation data
    m_AnimationPath.clear();
    m_SpritesheetGuid.clear();
    m_Animations.clear();
    m_AvailableFrames.clear();
    m_CurrentAnimationIndex = 0;
    m_SelectedTimelineIndex = -1;
    m_IsDirty = false;

    // Reset preview state
    m_PreviewFrame = 0;
    m_IsPlaying = false;
    m_PreviewTimer = 0.0f;
    m_LastPreviewTime = 0;

    // Reset status
    m_StatusMessage.clear();
    m_StatusIsError = false;

    // Reset spritesheet picker
    m_SpritesheetAssets.clear();
    m_SpritesheetSearchBuffer[0] = '\0';
    m_SpritesheetPickerNeedsRefresh = true;
}

bool FrameAnimationEditorWindow::CanOpenFile(const char* extension)
{
    if (!extension) return false;
    return strcmp(extension, ".anim") == 0;
}

void FrameAnimationEditorWindow::OpenFile(const char* filePath, const char* cachePath)
{
    m_AnimationPath = filePath ? filePath : "";
    m_StatusMessage.clear();
    m_IsDirty = false;

    if (!m_AnimationPath.empty())
    {
        LoadAnimation(m_AnimationPath);
    }
    else
    {
        CreateNewAnimation();
    }
}

void FrameAnimationEditorWindow::CreateNewAnimation()
{
    m_SpritesheetGuid.clear();
    m_Animations.clear();
    m_AvailableFrames.clear();
    m_SelectedTimelineIndex = -1;
    m_PreviewFrame = 0;
    m_IsPlaying = false;
    m_CurrentAnimationIndex = 0;

    // Create a default animation sequence
    AnimationSequence defaultAnim;
    defaultAnim.name = "idle";
    defaultAnim.loop = true;
    m_Animations.push_back(defaultAnim);

    m_IsDirty = true;
}

bool FrameAnimationEditorWindow::LoadAnimation(const std::string& path)
{
    std::ifstream file(path);
    if (!file.is_open())
    {
        m_StatusMessage = "Failed to open file";
        m_StatusIsError = true;
        return false;
    }

    try
    {
        json j;
        file >> j;

        m_SpritesheetGuid = j.value("spritesheetGuid", "");
        m_Animations.clear();
        m_CurrentAnimationIndex = 0;

        // New format: animations array
        if (j.contains("animations") && j["animations"].is_array())
        {
            for (const auto& animObj : j["animations"])
            {
                AnimationSequence seq;
                seq.name = animObj.value("name", "Unnamed");
                seq.loop = animObj.value("loop", true);

                if (animObj.contains("frames") && animObj["frames"].is_array())
                {
                    for (const auto& frameObj : animObj["frames"])
                    {
                        TimelineFrame frame;
                        frame.frameGuid = frameObj.value("frameGuid", "");
                        frame.duration = frameObj.value("duration", 100);
                        seq.frames.push_back(frame);
                    }
                }

                m_Animations.push_back(seq);
            }
        }
        // Legacy format: single animation at root level
        else if (j.contains("frames") && j["frames"].is_array())
        {
            AnimationSequence seq;
            seq.name = j.value("name", "Unnamed");
            seq.loop = j.value("loop", true);

            for (const auto& frameObj : j["frames"])
            {
                TimelineFrame frame;
                frame.frameGuid = frameObj.value("frameGuid", "");
                frame.duration = frameObj.value("duration", 100);
                seq.frames.push_back(frame);
            }

            m_Animations.push_back(seq);
        }

        // Ensure at least one animation exists
        if (m_Animations.empty())
        {
            AnimationSequence defaultAnim;
            defaultAnim.name = "idle";
            defaultAnim.loop = true;
            m_Animations.push_back(defaultAnim);
        }

        // Load spritesheet frames if we have a spritesheet
        if (!m_SpritesheetGuid.empty())
        {
            LoadSpritesheetFrames();
        }

        m_StatusMessage = "Loaded " + std::to_string(m_Animations.size()) + " animation(s)";
        m_StatusIsError = false;
        m_IsDirty = false;
        return true;
    }
    catch (const std::exception& e)
    {
        m_StatusMessage = std::string("Parse error: ") + e.what();
        m_StatusIsError = true;
        return false;
    }
}

bool FrameAnimationEditorWindow::SaveAnimation()
{
    if (m_AnimationPath.empty())
    {
        m_StatusMessage = "No file path set";
        m_StatusIsError = true;
        return false;
    }
    return SaveAnimationAs(m_AnimationPath);
}

bool FrameAnimationEditorWindow::SaveAnimationAs(const std::string& path)
{
    try
    {
        json j;
        j["spritesheetGuid"] = m_SpritesheetGuid;

        // Save all animations
        json animsArr = json::array();
        for (const auto& anim : m_Animations)
        {
            json animObj;
            animObj["name"] = anim.name;
            animObj["loop"] = anim.loop;

            json framesArr = json::array();
            for (const auto& frame : anim.frames)
            {
                json frameObj;
                frameObj["frameGuid"] = frame.frameGuid;
                frameObj["duration"] = frame.duration;
                framesArr.push_back(frameObj);
            }
            animObj["frames"] = framesArr;

            animsArr.push_back(animObj);
        }
        j["animations"] = animsArr;

        std::ofstream file(path);
        if (!file.is_open())
        {
            m_StatusMessage = "Failed to open file for writing";
            m_StatusIsError = true;
            return false;
        }

        file << j.dump(2);
        m_AnimationPath = path;
        m_IsDirty = false;

        // Trigger reimport to update cache
        fs::path fsPath(path);
        fs::path relativePath = fs::relative(fsPath, m_ProjectPath);
        std::string relativePathStr = relativePath.string();
        for (char& c : relativePathStr)
        {
            if (c == '\\') c = '/';
        }
        AssetDatabase::ImportAsset(relativePathStr);

        m_StatusMessage = "Saved " + std::to_string(m_Animations.size()) + " animation(s)";
        m_StatusIsError = false;
        return true;
    }
    catch (const std::exception& e)
    {
        m_StatusMessage = std::string("Save error: ") + e.what();
        m_StatusIsError = true;
        return false;
    }
}

void FrameAnimationEditorWindow::LoadSpritesheetFrames()
{
    m_AvailableFrames.clear();

    if (m_SpritesheetGuid.empty())
        return;

    // Get sub-assets (frames) for this spritesheet
    const auto* subAssets = AssetDatabase::GetSubAssets(m_SpritesheetGuid);

    if (!subAssets || subAssets->empty())
    {
        m_StatusMessage = "No frames found - slice spritesheet first";
        m_StatusIsError = true;
        return;
    }

    // Load texture for preview
    auto* assets = EditorAssets::Get();
    m_SpritesheetTextureId = assets->LoadTexture(m_SpritesheetGuid,
        reinterpret_cast<uint32_t*>(&m_SpritesheetWidth),
        reinterpret_cast<uint32_t*>(&m_SpritesheetHeight));

    // Get frame info for each sub-asset
    for (const auto& subAsset : *subAssets)
    {
        AvailableFrame frame;
        frame.guid = subAsset.guid;
        frame.index = subAsset.subAssetIndex;

        // Get UV coordinates
        if (assets->GetFrameUVs(subAsset.guid, &frame.u0, &frame.v0, &frame.u1, &frame.v1))
        {
            m_AvailableFrames.push_back(frame);
        }
    }

    // Sort by index
    std::sort(m_AvailableFrames.begin(), m_AvailableFrames.end(),
        [](const AvailableFrame& a, const AvailableFrame& b) {
            return a.index < b.index;
        });
}

void FrameAnimationEditorWindow::OnGUI()
{
    auto& ui = EditorUI::Get();

    bool isOpen = IsOpen();

    // Build window title with dirty indicator, but use ### to keep stable ID
    std::string windowTitle = GetTitle();
    if (m_IsDirty)
        windowTitle += " *";
    windowTitle += "###AnimationEditor";  // Stable ID regardless of title changes

    // Set initial window size and constraints to prevent movement on content changes
    ui.SetNextWindowSize(900, 600, true);
    ui.SetNextWindowSizeConstraints(600, 400, FLT_MAX, FLT_MAX);

    if (ui.Begin(windowTitle.c_str(), &isOpen, 0))
    {
        // Status message
        if (!m_StatusMessage.empty())
        {
            if (m_StatusIsError)
                ui.TextColored(1, 0, 0, 1, m_StatusMessage.c_str());
            else
                ui.TextColored(0, 1, 0, 1, m_StatusMessage.c_str());
        }

        DrawSpritesheetPicker();
        ui.Separator();

        // Four-column layout: animations | frames | timeline | properties+preview
        float availWidth, availHeightRaw;
        ui.GetContentRegionAvail(&availWidth, &availHeightRaw);
        float availHeight = availHeightRaw - 40;  // Reserve space for save buttons
        float columnWidth = availWidth / 4.0f;

        // Animation list
        ui.BeginChild("AnimationList", columnWidth - 4, availHeight, true);
        DrawAnimationList();
        ui.EndChild();

        ui.SameLine();

        // Frame palette - vertical list
        ui.BeginChild("FramePalette", columnWidth - 4, availHeight, true);
        DrawFramePalette();
        ui.EndChild();

        ui.SameLine();

        // Timeline - vertical list
        ui.BeginChild("Timeline", columnWidth - 4, availHeight, true);
        DrawTimeline();
        ui.EndChild();

        ui.SameLine();

        // Properties and Preview column
        ui.BeginChild("PropertiesPreview", 0, availHeight, true);
        DrawProperties();
        ui.Separator();
        DrawPreview();
        ui.EndChild();

        ui.Separator();

        // Save buttons
        if (ui.Button("Save"))
        {
            SaveAnimation();
        }
        ui.SameLine();
        if (ui.Button("Save As..."))
        {
            // For now just save to current path
            // TODO: Implement file dialog
            SaveAnimation();
        }
    }
    ui.End();

    SetOpen(isOpen);
}

void FrameAnimationEditorWindow::DrawSpritesheetPicker()
{
    auto& ui = EditorUI::Get();

    ui.Text("Spritesheet:");
    ui.SameLine();

    // Resolve GUID to display name
    std::string displayName = "(None)";
    if (!m_SpritesheetGuid.empty())
    {
        std::string path = AssetDatabase::GUIDToAssetPath(m_SpritesheetGuid);
        if (!path.empty())
        {
            // Extract filename from path
            size_t lastSlash = path.find_last_of("/\\");
            displayName = (lastSlash != std::string::npos) ? path.substr(lastSlash + 1) : path;
        }
        else
        {
            displayName = "Missing: " + m_SpritesheetGuid.substr(0, 8) + "...";
        }
    }

    // Style the button to look like a field
    ui.PushStyleColor(EditorUI::Col::Button, ui.GetStyleColor(EditorUI::Col::FrameBg));
    ui.PushStyleColor(EditorUI::Col::ButtonHovered, ui.GetStyleColor(EditorUI::Col::FrameBgHovered));
    ui.PushStyleColor(EditorUI::Col::ButtonActive, ui.GetStyleColor(EditorUI::Col::FrameBgActive));
    ui.PushButtonTextAlign(0.0f, 0.5f);

    float availPickerW, availPickerH;
    ui.GetContentRegionAvail(&availPickerW, &availPickerH);
    float fieldWidth = availPickerW - 80.0f;  // Leave room for Refresh button
    if (ui.Button(displayName.c_str(), fieldWidth, 0))
    {
        m_SpritesheetPickerNeedsRefresh = true;
        ui.OpenPopup("SelectSpritesheetPopup");
    }

    ui.PopStyleVar();
    ui.PopStyleColor(3);

    ui.SameLine();
    if (ui.Button("Refresh"))
    {
        LoadSpritesheetFrames();
    }

    // Spritesheet picker popup
    if (ui.BeginPopup("SelectSpritesheetPopup"))
    {
        // Refresh asset list if needed
        if (m_SpritesheetPickerNeedsRefresh)
        {
            AssetDatabase::GetSpritesheetAssets(m_SpritesheetAssets);
            m_SpritesheetPickerNeedsRefresh = false;
        }

        ui.Text("Select Spritesheet");
        ui.Separator();

        // Search filter
        ui.SetNextItemWidth(300);
        ui.InputTextWithHint("##search", "Search...", m_SpritesheetSearchBuffer, sizeof(m_SpritesheetSearchBuffer));

        std::string searchLower = m_SpritesheetSearchBuffer;
        for (char& c : searchLower)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

        ui.Separator();

        // Scrollable list of spritesheets
        ui.BeginChild("SpritesheetList", 300, 300, true);

        // None option
        if (ui.Selectable("(None)", m_SpritesheetGuid.empty()))
        {
            m_SpritesheetGuid.clear();
            m_AvailableFrames.clear();
            // Clear all animation frames when spritesheet is cleared
            for (auto& anim : m_Animations)
                anim.frames.clear();
            m_IsDirty = true;
            ui.CloseCurrentPopup();
        }

        // List all spritesheets
        for (const auto& [guid, path] : m_SpritesheetAssets)
        {
            // Apply search filter
            if (!searchLower.empty())
            {
                std::string pathLower = path;
                for (char& c : pathLower)
                    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                if (pathLower.find(searchLower) == std::string::npos)
                    continue;
            }

            // Extract display name
            size_t lastSlash = path.find_last_of("/\\");
            std::string name = (lastSlash != std::string::npos) ? path.substr(lastSlash + 1) : path;

            // Show frame count
            const auto* subAssets = AssetDatabase::GetSubAssets(guid);
            int frameCount = subAssets ? static_cast<int>(subAssets->size()) : 0;

            std::string label = name + " (" + std::to_string(frameCount) + " frames)";

            bool isSelected = (guid == m_SpritesheetGuid);
            if (ui.Selectable(label.c_str(), isSelected))
            {
                m_SpritesheetGuid = guid;
                LoadSpritesheetFrames();
                m_IsDirty = true;
                ui.CloseCurrentPopup();
            }

            // Tooltip with full path
            if (ui.IsItemHovered())
            {
                ui.SetTooltip(path.c_str());
            }
        }

        ui.EndChild();

        if (m_SpritesheetAssets.empty())
        {
            ui.TextColored(1, 0.5f, 0, 1, "No spritesheets found.");
            ui.TextWrapped("Slice a texture in the Sprite Slicer first.");
        }

        ui.EndPopup();
    }
}

void FrameAnimationEditorWindow::DrawAnimationList()
{
    auto& ui = EditorUI::Get();

    ui.Text("Animations");

    // Add new animation button
    if (ui.Button("+ Add"))
    {
        AnimationSequence newAnim;
        newAnim.name = "anim_" + std::to_string(m_Animations.size());
        newAnim.loop = true;
        m_Animations.push_back(newAnim);
        m_CurrentAnimationIndex = static_cast<int>(m_Animations.size()) - 1;
        m_IsDirty = true;
    }

    ui.Separator();

    // List of animations
    for (size_t i = 0; i < m_Animations.size(); i++)
    {
        ui.PushID(static_cast<int>(i));

        bool isSelected = (static_cast<int>(i) == m_CurrentAnimationIndex);

        // Selectable animation name
        if (ui.Selectable(m_Animations[i].name.c_str(), isSelected))
        {
            m_CurrentAnimationIndex = static_cast<int>(i);
            m_SelectedTimelineIndex = -1;
            m_PreviewFrame = 0;
            m_IsPlaying = false;
        }

        // Context menu for animation operations
        if (ui.BeginPopupContextItem())
        {
            if (ui.MenuItem("Rename"))
            {
                // Open rename popup (handled below)
                ui.OpenPopup("RenameAnim");
            }
            if (ui.MenuItem("Duplicate"))
            {
                AnimationSequence copy = m_Animations[i];
                copy.name = m_Animations[i].name + "_copy";
                m_Animations.insert(m_Animations.begin() + i + 1, copy);
                m_IsDirty = true;
            }
            if (m_Animations.size() > 1 && ui.MenuItem("Delete"))
            {
                m_Animations.erase(m_Animations.begin() + i);
                if (m_CurrentAnimationIndex >= static_cast<int>(m_Animations.size()))
                    m_CurrentAnimationIndex = static_cast<int>(m_Animations.size()) - 1;
                m_IsDirty = true;
            }
            if (i > 0 && ui.MenuItem("Move Up"))
            {
                std::swap(m_Animations[i], m_Animations[i - 1]);
                if (m_CurrentAnimationIndex == static_cast<int>(i))
                    m_CurrentAnimationIndex = static_cast<int>(i) - 1;
                m_IsDirty = true;
            }
            if (i < m_Animations.size() - 1 && ui.MenuItem("Move Down"))
            {
                std::swap(m_Animations[i], m_Animations[i + 1]);
                if (m_CurrentAnimationIndex == static_cast<int>(i))
                    m_CurrentAnimationIndex = static_cast<int>(i) + 1;
                m_IsDirty = true;
            }
            ui.EndPopup();
        }

        // Show frame count
        float animAvailW, animAvailH;
        ui.GetContentRegionAvail(&animAvailW, &animAvailH);
        ui.SameLine(animAvailW - 25);
        char frameCountBuf[64];
        std::snprintf(frameCountBuf, sizeof(frameCountBuf), "%zu", m_Animations[i].frames.size());
        ui.TextDisabled(frameCountBuf);

        ui.PopID();
    }
}

void FrameAnimationEditorWindow::DrawFramePalette()
{
    auto& ui = EditorUI::Get();

    char framesHeaderBuf[64];
    std::snprintf(framesHeaderBuf, sizeof(framesHeaderBuf), "Frames (%zu)", m_AvailableFrames.size());
    ui.Text(framesHeaderBuf);
    ui.Separator();

    if (m_AvailableFrames.empty())
    {
        ui.TextColored(1, 0.5f, 0, 1, "No frames");
        ui.TextWrapped("Select a sliced spritesheet.");
        return;
    }

    if (m_SpritesheetTextureId == 0)
    {
        ui.TextColored(1, 0.5f, 0, 1, "Texture not loaded");
        return;
    }

    // Display frames as vertical list
    float maxThumbSize = 64.0f;

    for (size_t i = 0; i < m_AvailableFrames.size(); i++)
    {
        const auto& frame = m_AvailableFrames[i];

        ui.PushID(static_cast<int>(i));

        // Calculate frame dimensions from UVs
        float frameWidth = (frame.u1 - frame.u0) * m_SpritesheetWidth;
        float frameHeight = (frame.v1 - frame.v0) * m_SpritesheetHeight;
        float thumbWidth = maxThumbSize;
        float thumbHeight = maxThumbSize;
        if (frameWidth > 0 && frameHeight > 0)
        {
            float aspect = frameWidth / frameHeight;
            if (aspect > 1.0f)
                thumbHeight = maxThumbSize / aspect;
            else
                thumbWidth = maxThumbSize * aspect;
        }

        if (ui.ImageButton("##frame", m_SpritesheetTextureId, thumbWidth, thumbHeight,
                           frame.u0, frame.v0, frame.u1, frame.v1))
        {
            // Add frame to current animation's timeline
            if (m_CurrentAnimationIndex >= 0 && m_CurrentAnimationIndex < static_cast<int>(m_Animations.size()))
            {
                TimelineFrame newFrame;
                newFrame.frameGuid = frame.guid;
                newFrame.duration = m_DefaultDuration;
                m_Animations[m_CurrentAnimationIndex].frames.push_back(newFrame);
                m_IsDirty = true;
            }
        }

        ui.SameLine();
        char frameLabelBuf[64];
        std::snprintf(frameLabelBuf, sizeof(frameLabelBuf), "Frame %d", frame.index);
        ui.Text(frameLabelBuf);

        ui.PopID();
    }
}

void FrameAnimationEditorWindow::DrawTimeline()
{
    auto& ui = EditorUI::Get();

    // Get current animation's frames
    if (m_CurrentAnimationIndex < 0 || m_CurrentAnimationIndex >= static_cast<int>(m_Animations.size()))
    {
        ui.TextColored(0.5f, 0.5f, 0.5f, 1, "No animation");
        return;
    }

    auto& currentAnim = m_Animations[m_CurrentAnimationIndex];
    auto& timelineFrames = currentAnim.frames;

    char timelineHeaderBuf[64];
    std::snprintf(timelineHeaderBuf, sizeof(timelineHeaderBuf), "Timeline (%zu)", timelineFrames.size());
    ui.Text(timelineHeaderBuf);

    // Timeline controls
    if (ui.Button("Clear"))
    {
        timelineFrames.clear();
        m_SelectedTimelineIndex = -1;
        m_IsDirty = true;
    }
    ui.Separator();

    if (timelineFrames.empty())
    {
        ui.TextColored(0.5f, 0.5f, 0.5f, 1, "Click frames to add");
        return;
    }

    // Display timeline frames as vertical list
    float maxThumbSize = 64.0f;

    for (size_t i = 0; i < timelineFrames.size(); i++)
    {
        const auto& tlFrame = timelineFrames[i];

        ui.PushID(static_cast<int>(i));

        // Find the frame in available frames to get UVs
        float u0 = 0, v0 = 0, u1 = 1, v1 = 1;
        int frameIndex = -1;
        for (const auto& af : m_AvailableFrames)
        {
            if (af.guid == tlFrame.frameGuid)
            {
                u0 = af.u0; v0 = af.v0;
                u1 = af.u1; v1 = af.v1;
                frameIndex = af.index;
                break;
            }
        }

        // Calculate thumbnail size with correct aspect ratio
        float frameWidth = (u1 - u0) * m_SpritesheetWidth;
        float frameHeight = (v1 - v0) * m_SpritesheetHeight;
        float thumbWidth = maxThumbSize;
        float thumbHeight = maxThumbSize;
        if (frameWidth > 0 && frameHeight > 0)
        {
            float aspect = frameWidth / frameHeight;
            if (aspect > 1.0f)
                thumbHeight = maxThumbSize / aspect;
            else
                thumbWidth = maxThumbSize * aspect;
        }

        // Highlight selected frame
        bool isSelected = (static_cast<int>(i) == m_SelectedTimelineIndex);
        if (isSelected)
        {
            ui.PushStyleColor(EditorUI::Col::Button, EditorUI::Rgba((int)(0.3f * 255), (int)(0.5f * 255), (int)(0.8f * 255), (int)(1.0f * 255)));
        }

        if (m_SpritesheetTextureId != 0)
        {
            if (ui.ImageButton("##tlframe", m_SpritesheetTextureId, thumbWidth, thumbHeight,
                               u0, v0, u1, v1))
            {
                m_SelectedTimelineIndex = static_cast<int>(i);
            }
        }
        else
        {
            if (ui.Button("?", maxThumbSize, maxThumbSize))
            {
                m_SelectedTimelineIndex = static_cast<int>(i);
            }
        }

        if (isSelected)
        {
            ui.PopStyleColor();
        }

        // Show frame info on same line
        ui.SameLine();
        char frameInfoBuf[64];
        std::snprintf(frameInfoBuf, sizeof(frameInfoBuf), "%d: %dms", frameIndex >= 0 ? frameIndex : static_cast<int>(i), tlFrame.duration);
        ui.Text(frameInfoBuf);

        // Context menu for frame operations
        if (ui.BeginPopupContextItem("frame_ctx"))
        {
            if (ui.MenuItem("Remove"))
            {
                timelineFrames.erase(timelineFrames.begin() + i);
                if (m_SelectedTimelineIndex >= static_cast<int>(timelineFrames.size()))
                    m_SelectedTimelineIndex = static_cast<int>(timelineFrames.size()) - 1;
                m_IsDirty = true;
            }
            if (i > 0 && ui.MenuItem("Move Up"))
            {
                std::swap(timelineFrames[i], timelineFrames[i - 1]);
                m_SelectedTimelineIndex = static_cast<int>(i) - 1;
                m_IsDirty = true;
            }
            if (i < timelineFrames.size() - 1 && ui.MenuItem("Move Down"))
            {
                std::swap(timelineFrames[i], timelineFrames[i + 1]);
                m_SelectedTimelineIndex = static_cast<int>(i) + 1;
                m_IsDirty = true;
            }
            if (ui.MenuItem("Duplicate"))
            {
                timelineFrames.insert(timelineFrames.begin() + i + 1, tlFrame);
                m_IsDirty = true;
            }
            ui.EndPopup();
        }

        ui.PopID();
    }
}

void FrameAnimationEditorWindow::DrawProperties()
{
    auto& ui = EditorUI::Get();

    ui.Text("Properties");

    // Ensure we have a valid animation selected
    if (m_CurrentAnimationIndex < 0 || m_CurrentAnimationIndex >= static_cast<int>(m_Animations.size()))
    {
        ui.TextColored(0.5f, 0.5f, 0.5f, 1, "No animation selected");
        return;
    }

    auto& currentAnim = m_Animations[m_CurrentAnimationIndex];

    // Animation name
    char nameBuffer[256];
    strncpy(nameBuffer, currentAnim.name.c_str(), sizeof(nameBuffer) - 1);
    nameBuffer[sizeof(nameBuffer) - 1] = '\0';
    if (ui.InputText("Name", nameBuffer, sizeof(nameBuffer)))
    {
        currentAnim.name = nameBuffer;
        m_IsDirty = true;
    }

    // Loop checkbox
    if (ui.Checkbox("Loop", &currentAnim.loop))
    {
        m_IsDirty = true;
    }

    // Default duration for new frames
    ui.InputInt("Default Duration (ms)", &m_DefaultDuration);
    if (m_DefaultDuration < 1) m_DefaultDuration = 1;

    // Selected frame properties
    if (m_SelectedTimelineIndex >= 0 && m_SelectedTimelineIndex < static_cast<int>(currentAnim.frames.size()))
    {
        ui.Separator();
        char selFrameBuf[64];
        std::snprintf(selFrameBuf, sizeof(selFrameBuf), "Selected Frame: %d", m_SelectedTimelineIndex);
        ui.Text(selFrameBuf);

        auto& frame = currentAnim.frames[m_SelectedTimelineIndex];
        if (ui.InputInt("Duration (ms)", &frame.duration))
        {
            if (frame.duration < 1) frame.duration = 1;
            m_IsDirty = true;
        }

        // Apply to all button
        if (ui.Button("Apply Duration to All"))
        {
            for (auto& f : currentAnim.frames)
            {
                f.duration = frame.duration;
            }
            m_IsDirty = true;
        }
    }
}

void FrameAnimationEditorWindow::DrawPreview()
{
    auto& ui = EditorUI::Get();

    ui.Text("Preview");

    // Get current animation's frames
    const std::vector<TimelineFrame>* timelineFrames = nullptr;
    bool animLoop = true;

    if (m_CurrentAnimationIndex >= 0 && m_CurrentAnimationIndex < static_cast<int>(m_Animations.size()))
    {
        timelineFrames = &m_Animations[m_CurrentAnimationIndex].frames;
        animLoop = m_Animations[m_CurrentAnimationIndex].loop;
    }

    // Playback controls
    if (ui.Button(m_IsPlaying ? "Pause" : "Play"))
    {
        m_IsPlaying = !m_IsPlaying;
        m_LastPreviewTime = SDL_GetTicks();
    }
    ui.SameLine();
    if (ui.Button("Stop"))
    {
        m_IsPlaying = false;
        m_PreviewFrame = 0;
        m_PreviewTimer = 0.0f;
    }
    ui.SameLine();
    if (ui.Button("<<"))
    {
        m_PreviewFrame = std::max(0, m_PreviewFrame - 1);
    }
    ui.SameLine();
    if (ui.Button(">>"))
    {
        if (timelineFrames && !timelineFrames->empty())
            m_PreviewFrame = std::min(static_cast<int>(timelineFrames->size()) - 1, m_PreviewFrame + 1);
    }

    // Update animation if playing
    if (m_IsPlaying && timelineFrames && !timelineFrames->empty())
    {
        uint32_t currentTime = SDL_GetTicks();
        float deltaTime = static_cast<float>(currentTime - m_LastPreviewTime);
        m_LastPreviewTime = currentTime;

        m_PreviewTimer += deltaTime;

        // Clamp preview frame to valid range
        if (m_PreviewFrame >= static_cast<int>(timelineFrames->size()))
            m_PreviewFrame = 0;

        const auto& currentFrame = (*timelineFrames)[m_PreviewFrame];
        if (m_PreviewTimer >= currentFrame.duration)
        {
            m_PreviewTimer -= currentFrame.duration;
            m_PreviewFrame++;

            if (m_PreviewFrame >= static_cast<int>(timelineFrames->size()))
            {
                if (animLoop)
                    m_PreviewFrame = 0;
                else
                {
                    m_PreviewFrame = static_cast<int>(timelineFrames->size()) - 1;
                    m_IsPlaying = false;
                }
            }
        }
    }

    // Frame counter
    ui.SameLine();
    if (timelineFrames && !timelineFrames->empty())
    {
        char frameCounterBuf[64];
        std::snprintf(frameCounterBuf, sizeof(frameCounterBuf), "Frame %d / %zu", m_PreviewFrame + 1, timelineFrames->size());
        ui.Text(frameCounterBuf);
    }
    else
        ui.Text("No frames");

    // Preview area
    float previewSize = 200.0f;
    ui.BeginChild("PreviewArea", previewSize + 16, previewSize + 16, true);

    if (timelineFrames && !timelineFrames->empty() && m_SpritesheetTextureId != 0 &&
        m_PreviewFrame >= 0 && m_PreviewFrame < static_cast<int>(timelineFrames->size()))
    {
        const auto& tlFrame = (*timelineFrames)[m_PreviewFrame];

        // Find UVs
        float u0 = 0, v0 = 0, u1 = 1, v1 = 1;
        for (const auto& af : m_AvailableFrames)
        {
            if (af.guid == tlFrame.frameGuid)
            {
                u0 = af.u0; v0 = af.v0;
                u1 = af.u1; v1 = af.v1;
                break;
            }
        }

        ui.Image(m_SpritesheetTextureId, previewSize, previewSize, u0, v0, u1, v1);
    }
    else
    {
        ui.TextColored(0.5f, 0.5f, 0.5f, 1, "No preview");
    }

    ui.EndChild();

    // Calculate total duration
    int totalDuration = 0;
    if (timelineFrames)
    {
        for (const auto& f : *timelineFrames)
            totalDuration += f.duration;
    }

    char totalDurationBuf[128];
    std::snprintf(totalDurationBuf, sizeof(totalDurationBuf), "Total Duration: %d ms (%.2f fps avg)",
        totalDuration,
        (!timelineFrames || timelineFrames->empty()) ? 0.0f : 1000.0f / (totalDuration / static_cast<float>(timelineFrames->size())));
    ui.Text(totalDurationBuf);
}

} // namespace DekiEditor

using DekiEditor::FrameAnimationEditorWindow;

// Register the Animation Editor window
REGISTER_EDITOR_WINDOW(FrameAnimationEditorWindow, "Animation Editor", "2D/Animation Editor")

#endif  // DEKI_EDITOR
