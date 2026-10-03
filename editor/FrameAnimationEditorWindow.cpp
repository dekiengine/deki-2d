// Editor-only translation unit (a firmware build compiles nothing from it).
#ifdef DEKI_EDITOR

#include "FrameAnimationEditorWindow.h"
#include <deki-editor/EditorUI.h>
#include <deki-editor/EditorTheme.h>
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
#include <algorithm>
#include <cstdio>
#include <cfloat>
#include <SDL3/SDL.h>
#include <SDL3/SDL_opengl.h>
#include <nlohmann/json.hpp>

// Editor extensions live in DekiEditor; the package's own types are in Deki2D.
using namespace Deki2D;

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

namespace
{
// A column's title: the editor's section band, without folding.
void ColumnBand(const char* label)
{
    DekiEditor::SchematicCollapsingHeader(label, ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_DefaultOpen);
}
}  // namespace

void FrameAnimationEditorWindow::OnGUI()
{
    auto& ui = EditorUI::Get();

    bool isOpen = IsOpen();

    // Build window title with dirty indicator, but use ### to keep stable ID
    std::string windowTitle = GetTitle();
    if (m_IsDirty)
        windowTitle += " *";
    windowTitle += "###AnimationEditor";  // Stable ID regardless of title changes

    const float dpi = ui.GetDpiScale();
    ui.SetNextWindowSize(980.0f * dpi, 600.0f * dpi, true);
    ui.SetNextWindowSizeConstraints(680.0f * dpi, 420.0f * dpi, FLT_MAX, FLT_MAX);

    if (ui.Begin(windowTitle.c_str(), &isOpen, 0))
    {
        // ── Toolbar ─────────────────────────────────────────────────────────
        ui.BeginToolbar();
        if (ui.ToolbarButton(m_IsDirty ? "Save*" : "Save", m_IsDirty && !m_AnimationPath.empty()))
            SaveAnimation();
        if (ui.ToolbarButton("Reload Frames", !m_SpritesheetGuid.empty()))
            LoadSpritesheetFrames();
        ui.EndToolbar();

        // ── Spritesheet row (and an error, when there is one) ───────────────
        const float pad = 12.0f * dpi;
        ui.Dummy(0.0f, 6.0f * dpi);
        DekiEditor::BeginPropertyContext();
        DrawSpritesheetPicker();
        if (m_StatusIsError && !m_StatusMessage.empty())
        {
            ui.PropertyRow("");
            ui.TextColored(DekiEditor::Palette::Red.x, DekiEditor::Palette::Red.y, DekiEditor::Palette::Red.z, 1.0f,
                           m_StatusMessage.c_str());
        }
        DekiEditor::EndPropertyContext();
        ui.Dummy(0.0f, 6.0f * dpi);

        // ── Four columns: animations | frames | timeline | properties+preview,
        // each under its own band, meeting at hairline seams ────────────────
        float availWidth, availHeight;
        ui.GetContentRegionAvail(&availWidth, &availHeight);
        const float listW = std::max(150.0f * dpi, availWidth * 0.2f);
        const float framesW = std::max(150.0f * dpi, availWidth * 0.22f);
        const float timelineW = std::max(150.0f * dpi, availWidth * 0.22f);
        const uint32_t seamCol = ImGui::ColorConvertFloat4ToU32(DekiEditor::Palette::Line2);
        auto seam = [&]()
        {
            ui.SameLine(0.0f, 0.0f);
            float x, y;
            ui.GetCursorScreenPos(&x, &y);
            ui.DrawLine(x, y, x, y + availHeight, seamCol, 1.0f);
            ui.Dummy(1.0f, availHeight);
            ui.SameLine(0.0f, 0.0f);
        };
        (void)pad;

        if (ui.BeginChild("AnimationList", listW, availHeight, false))
            DrawAnimationList();
        ui.EndChild();
        seam();
        if (ui.BeginChild("FramePalette", framesW, availHeight, false))
            DrawFramePalette();
        ui.EndChild();
        seam();
        if (ui.BeginChild("Timeline", timelineW, availHeight, false))
            DrawTimeline();
        ui.EndChild();
        seam();
        if (ui.BeginChild("PropertiesPreview", 0, availHeight, false))
        {
            DrawProperties();
            DrawPreview();
        }
        ui.EndChild();
    }
    ui.End();

    SetOpen(isOpen);
}

void FrameAnimationEditorWindow::DrawSpritesheetPicker()
{
    auto& ui = EditorUI::Get();

    // Resolve GUID to display name
    std::string displayName = "None";
    if (!m_SpritesheetGuid.empty())
    {
        std::string path = AssetDatabase::GUIDToAssetPath(m_SpritesheetGuid);
        if (!path.empty())
        {
            size_t lastSlash = path.find_last_of("/\\");
            displayName = (lastSlash != std::string::npos) ? path.substr(lastSlash + 1) : path;
        }
        else
        {
            displayName = "Missing: " + m_SpritesheetGuid.substr(0, 8) + "...";
        }
    }

    // A field-looking button that opens the picker.
    ui.PropertyRow("Spritesheet");
    ui.PushStyleColor(EditorUI::Col::Button, ui.GetStyleColor(EditorUI::Col::FrameBg));
    ui.PushStyleColor(EditorUI::Col::ButtonHovered, ui.GetStyleColor(EditorUI::Col::FrameBgHovered));
    ui.PushStyleColor(EditorUI::Col::ButtonActive, ui.GetStyleColor(EditorUI::Col::FrameBgActive));
    ui.PushButtonTextAlign(0.0f, 0.5f);
    float availW = 0.0f;
    ui.GetContentRegionAvail(&availW, nullptr);
    if (ui.Button((displayName + "###sheetpick").c_str(), std::min(availW, 360.0f * ui.GetDpiScale()), 0))
    {
        m_SpritesheetPickerNeedsRefresh = true;
        ui.OpenPopup("SelectSpritesheetPopup");
    }
    ui.PopStyleVar();
    ui.PopStyleColor(3);

    // Spritesheet picker popup
    if (ui.BeginPopup("SelectSpritesheetPopup"))
    {
        if (m_SpritesheetPickerNeedsRefresh)
        {
            AssetDatabase::GetSpritesheetAssets(m_SpritesheetAssets);
            m_SpritesheetPickerNeedsRefresh = false;
        }

        const float dpi = ui.GetDpiScale();
        ui.SetNextItemWidth(300.0f * dpi);
        ui.InputTextWithHint("##search", "Search spritesheets", m_SpritesheetSearchBuffer, sizeof(m_SpritesheetSearchBuffer));

        std::string searchLower = m_SpritesheetSearchBuffer;
        for (char& c : searchLower)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

        ui.BeginChild("SpritesheetList", 300.0f * dpi, 300.0f * dpi, false);

        if (ui.Selectable("None", m_SpritesheetGuid.empty()))
        {
            m_SpritesheetGuid.clear();
            m_AvailableFrames.clear();
            // Clear all animation frames when spritesheet is cleared
            for (auto& anim : m_Animations)
                anim.frames.clear();
            m_IsDirty = true;
            ui.CloseCurrentPopup();
        }

        for (const auto& [guid, path] : m_SpritesheetAssets)
        {
            if (!searchLower.empty())
            {
                std::string pathLower = path;
                for (char& c : pathLower)
                    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                if (pathLower.find(searchLower) == std::string::npos)
                    continue;
            }

            size_t lastSlash = path.find_last_of("/\\");
            std::string name = (lastSlash != std::string::npos) ? path.substr(lastSlash + 1) : path;
            const auto* subAssets = AssetDatabase::GetSubAssets(guid);
            int frameCount = subAssets ? static_cast<int>(subAssets->size()) : 0;
            std::string label = name + "   " + std::to_string(frameCount) + (frameCount == 1 ? " frame" : " frames");

            bool isSelected = (guid == m_SpritesheetGuid);
            if (ui.Selectable(label.c_str(), isSelected))
            {
                m_SpritesheetGuid = guid;
                LoadSpritesheetFrames();
                m_IsDirty = true;
                ui.CloseCurrentPopup();
            }
            if (ui.IsItemHovered())
                ui.SetTooltip(path.c_str());
        }

        if (m_SpritesheetAssets.empty())
            ui.TextDisabled("No spritesheets yet. Slice an image in the Sprite Slicer first.");

        ui.EndChild();
        ui.EndPopup();
    }
}

void FrameAnimationEditorWindow::DrawAnimationList()
{
    auto& ui = EditorUI::Get();
    ColumnBand("Animations");
    const float pad = 8.0f * ui.GetDpiScale();
    ui.Dummy(0.0f, 4.0f * ui.GetDpiScale());
    ui.Indent(pad);

    for (size_t i = 0; i < m_Animations.size(); i++)
    {
        ui.PushID(static_cast<int>(i));

        bool isSelected = (static_cast<int>(i) == m_CurrentAnimationIndex);
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
            if (i + 1 < m_Animations.size() && ui.MenuItem("Move Down"))
            {
                std::swap(m_Animations[i], m_Animations[i + 1]);
                if (m_CurrentAnimationIndex == static_cast<int>(i))
                    m_CurrentAnimationIndex = static_cast<int>(i) + 1;
                m_IsDirty = true;
            }
            ui.EndPopup();
        }

        // Frame count, right-aligned on the row
        if (i < m_Animations.size())
        {
            char countBuf[32];
            std::snprintf(countBuf, sizeof(countBuf), "%zu", m_Animations[i].frames.size());
            float countW = 0.0f, availW = 0.0f;
            ui.MeasureText(countBuf, &countW, nullptr);
            ui.GetContentRegionAvail(&availW, nullptr);
            ui.SameLine(std::max(0.0f, availW - countW - pad));
            ui.TextDisabled(countBuf);
        }

        ui.PopID();
    }

    ui.Spacing();
    if (DekiEditor::SchematicAddButton("Add Animation"))
    {
        AnimationSequence newAnim;
        newAnim.name = "anim_" + std::to_string(m_Animations.size());
        newAnim.loop = true;
        m_Animations.push_back(newAnim);
        m_CurrentAnimationIndex = static_cast<int>(m_Animations.size()) - 1;
        m_IsDirty = true;
    }
    ui.Unindent(pad);
}

void FrameAnimationEditorWindow::DrawFramePalette()
{
    auto& ui = EditorUI::Get();
    const float dpi = ui.GetDpiScale();

    char header[64];
    std::snprintf(header, sizeof(header), "Frames (%zu)###frames", m_AvailableFrames.size());
    ColumnBand(header);
    const float pad = 8.0f * dpi;
    ui.Dummy(0.0f, 4.0f * dpi);
    ui.Indent(pad);

    if (m_AvailableFrames.empty())
    {
        ui.TextWrapped("Pick a sliced spritesheet above.");
        ui.Unindent(pad);
        return;
    }
    if (m_SpritesheetTextureId == 0)
    {
        ui.TextDisabled("Loading the spritesheet...");
        ui.Unindent(pad);
        return;
    }

    ui.TextDisabled("Click a frame to add it.");
    ui.Spacing();

    const float maxThumbSize = 56.0f * dpi;
    for (size_t i = 0; i < m_AvailableFrames.size(); i++)
    {
        const auto& frame = m_AvailableFrames[i];
        ui.PushID(static_cast<int>(i));

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
        ui.TextDisabled(frameLabelBuf);

        ui.PopID();
    }
    ui.Unindent(pad);
}

void FrameAnimationEditorWindow::DrawTimeline()
{
    auto& ui = EditorUI::Get();
    const float dpi = ui.GetDpiScale();

    const bool hasAnim = m_CurrentAnimationIndex >= 0 && m_CurrentAnimationIndex < static_cast<int>(m_Animations.size());
    char header[64];
    std::snprintf(header, sizeof(header), "Timeline (%zu)###timeline",
                  hasAnim ? m_Animations[m_CurrentAnimationIndex].frames.size() : size_t(0));
    ColumnBand(header);
    if (hasAnim && !m_Animations[m_CurrentAnimationIndex].frames.empty() && DekiEditor::SchematicActionLink("CLEAR"))
    {
        m_Animations[m_CurrentAnimationIndex].frames.clear();
        m_SelectedTimelineIndex = -1;
        m_IsDirty = true;
    }
    const float pad = 8.0f * dpi;
    ui.Dummy(0.0f, 4.0f * dpi);
    ui.Indent(pad);

    if (!hasAnim)
    {
        ui.TextDisabled("No animation selected.");
        ui.Unindent(pad);
        return;
    }

    auto& timelineFrames = m_Animations[m_CurrentAnimationIndex].frames;
    if (timelineFrames.empty())
    {
        ui.TextWrapped("Empty. Click frames on the left to add them.");
        ui.Unindent(pad);
        return;
    }

    const float maxThumbSize = 56.0f * dpi;
    const uint32_t accent = ui.GetStyleColor(EditorUI::Col::CheckMark);
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

        // The selected frame takes the accent, as a selection does elsewhere.
        const bool isSelected = (static_cast<int>(i) == m_SelectedTimelineIndex);
        if (isSelected)
            ui.PushStyleColor(EditorUI::Col::Button, (accent & 0x00FFFFFFu) | (90u << 24));

        if (m_SpritesheetTextureId != 0)
        {
            if (ui.ImageButton("##tlframe", m_SpritesheetTextureId, thumbWidth, thumbHeight, u0, v0, u1, v1))
                m_SelectedTimelineIndex = static_cast<int>(i);
        }
        else if (ui.Button("?", maxThumbSize, maxThumbSize))
        {
            m_SelectedTimelineIndex = static_cast<int>(i);
        }

        if (isSelected)
            ui.PopStyleColor();

        ui.SameLine();
        char frameInfoBuf[64];
        std::snprintf(frameInfoBuf, sizeof(frameInfoBuf), "Frame %d\n%d ms",
                      frameIndex >= 0 ? frameIndex : static_cast<int>(i), tlFrame.duration);
        if (isSelected)
            ui.Text(frameInfoBuf);
        else
            ui.TextDisabled(frameInfoBuf);

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
            else if (i > 0 && ui.MenuItem("Move Up"))
            {
                std::swap(timelineFrames[i], timelineFrames[i - 1]);
                m_SelectedTimelineIndex = static_cast<int>(i) - 1;
                m_IsDirty = true;
            }
            else if (i + 1 < timelineFrames.size() && ui.MenuItem("Move Down"))
            {
                std::swap(timelineFrames[i], timelineFrames[i + 1]);
                m_SelectedTimelineIndex = static_cast<int>(i) + 1;
                m_IsDirty = true;
            }
            else if (ui.MenuItem("Duplicate"))
            {
                timelineFrames.insert(timelineFrames.begin() + i + 1, timelineFrames[i]);
                m_IsDirty = true;
            }
            ui.EndPopup();
        }

        ui.PopID();
    }
    ui.Unindent(pad);
}

void FrameAnimationEditorWindow::DrawProperties()
{
    auto& ui = EditorUI::Get();
    ColumnBand("Properties");
    ui.Dummy(0.0f, 4.0f * ui.GetDpiScale());

    if (m_CurrentAnimationIndex < 0 || m_CurrentAnimationIndex >= static_cast<int>(m_Animations.size()))
    {
        DekiEditor::BeginPropertyContext();
        ui.TextDisabled("No animation selected.");
        DekiEditor::EndPropertyContext();
        return;
    }

    auto& currentAnim = m_Animations[m_CurrentAnimationIndex];
    DekiEditor::BeginPropertyContext();

    char nameBuffer[256];
    strncpy(nameBuffer, currentAnim.name.c_str(), sizeof(nameBuffer) - 1);
    nameBuffer[sizeof(nameBuffer) - 1] = '\0';
    ui.PropertyRow("Name");
    ui.SetNextItemWidth(-FLT_MIN);
    if (ui.InputText("##name", nameBuffer, sizeof(nameBuffer)))
    {
        currentAnim.name = nameBuffer;
        m_IsDirty = true;
    }

    ui.PropertyRow("Loop");
    if (ui.Checkbox("##loop", &currentAnim.loop))
        m_IsDirty = true;

    // Duration given to frames added from now on
    ui.PropertyRow("New Frame (ms)");
    ui.SetNextItemWidth(-FLT_MIN);
    ui.DragInt("##defdur", &m_DefaultDuration, 1.0f, 1, 10000);
    if (m_DefaultDuration < 1) m_DefaultDuration = 1;

    if (m_SelectedTimelineIndex >= 0 && m_SelectedTimelineIndex < static_cast<int>(currentAnim.frames.size()))
    {
        char sub[48];
        std::snprintf(sub, sizeof(sub), "Timeline Frame %d", m_SelectedTimelineIndex);
        DekiEditor::SchematicSubHeader(sub);

        auto& frame = currentAnim.frames[m_SelectedTimelineIndex];
        ui.PropertyRow("Duration (ms)");
        ui.SetNextItemWidth(-FLT_MIN);
        if (ui.DragInt("##dur", &frame.duration, 1.0f, 1, 10000))
        {
            if (frame.duration < 1) frame.duration = 1;
            m_IsDirty = true;
        }

        ui.PropertyRow("");
        if (ui.Button("Use for All Frames", -FLT_MIN))
        {
            for (auto& f : currentAnim.frames)
                f.duration = frame.duration;
            m_IsDirty = true;
        }
    }
    DekiEditor::EndPropertyContext();
}

void FrameAnimationEditorWindow::DrawPreview()
{
    auto& ui = EditorUI::Get();
    const float dpi = ui.GetDpiScale();
    ui.Spacing();
    ColumnBand("Preview");

    const std::vector<TimelineFrame>* timelineFrames = nullptr;
    bool animLoop = true;
    if (m_CurrentAnimationIndex >= 0 && m_CurrentAnimationIndex < static_cast<int>(m_Animations.size()))
    {
        timelineFrames = &m_Animations[m_CurrentAnimationIndex].frames;
        animLoop = m_Animations[m_CurrentAnimationIndex].loop;
    }
    const bool hasFrames = timelineFrames && !timelineFrames->empty();

    // Playback controls, as a toolbar strip under the band
    ui.BeginToolbar();
    if (ui.ToolbarButton(m_IsPlaying ? "Pause" : "Play", hasFrames))
    {
        m_IsPlaying = !m_IsPlaying;
        m_LastPreviewTime = SDL_GetTicks();
    }
    if (ui.ToolbarButton("Stop", hasFrames))
    {
        m_IsPlaying = false;
        m_PreviewFrame = 0;
        m_PreviewTimer = 0.0f;
    }
    if (ui.ToolbarButton("Previous", hasFrames && m_PreviewFrame > 0))
        m_PreviewFrame = std::max(0, m_PreviewFrame - 1);
    if (ui.ToolbarButton("Next", hasFrames && m_PreviewFrame + 1 < static_cast<int>(hasFrames ? timelineFrames->size() : 0)))
        m_PreviewFrame = std::min(static_cast<int>(timelineFrames->size()) - 1, m_PreviewFrame + 1);
    ui.EndToolbar();

    // Advance the preview while playing
    if (m_IsPlaying && hasFrames)
    {
        uint32_t currentTime = SDL_GetTicks();
        float deltaTime = static_cast<float>(currentTime - m_LastPreviewTime);
        m_LastPreviewTime = currentTime;
        m_PreviewTimer += deltaTime;

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

    // The frame, as large as the column allows, on a checkerboard
    float availW = 0.0f, availH = 0.0f;
    ui.GetContentRegionAvail(&availW, &availH);
    const float pad = 12.0f * dpi;
    const float lineH = ui.GetTextLineHeight();
    const float side = std::max(32.0f * dpi, std::min(availW - pad * 2.0f, availH - lineH - pad * 3.0f));
    ui.Dummy(0.0f, pad);
    float x0, y0;
    ui.GetCursorScreenPos(&x0, &y0);
    x0 += (availW - side) * 0.5f;
    const float cell = 8.0f * dpi;
    for (float y = y0; y < y0 + side; y += cell)
        for (float x = x0; x < x0 + side; x += cell)
        {
            const bool odd = (int((x - x0) / cell) + int((y - y0) / cell)) & 1;
            ui.DrawRectFilled(x, y, std::min(x + cell, x0 + side), std::min(y + cell, y0 + side),
                              odd ? EditorUI::Rgba(80, 80, 80, 255) : EditorUI::Rgba(60, 60, 60, 255));
        }
    if (hasFrames && m_SpritesheetTextureId != 0 && m_PreviewFrame >= 0 &&
        m_PreviewFrame < static_cast<int>(timelineFrames->size()))
    {
        const auto& tlFrame = (*timelineFrames)[m_PreviewFrame];
        float u0 = 0, v0 = 0, u1 = 1, v1 = 1;
        for (const auto& af : m_AvailableFrames)
            if (af.guid == tlFrame.frameGuid)
            {
                u0 = af.u0; v0 = af.v0;
                u1 = af.u1; v1 = af.v1;
                break;
            }
        // Keep the frame's own aspect inside the square.
        const float fw = (u1 - u0) * m_SpritesheetWidth, fh = (v1 - v0) * m_SpritesheetHeight;
        float w = side, h = side;
        if (fw > 0 && fh > 0)
        {
            if (fw > fh)
                h = side * fh / fw;
            else
                w = side * fw / fh;
        }
        const float ix = x0 + (side - w) * 0.5f, iy = y0 + (side - h) * 0.5f;
        ui.DrawImage(m_SpritesheetTextureId, ix, iy, ix + w, iy + h, u0, v0, u1, v1);
    }
    ui.Dummy(availW, side);

    // Frame counter and timing
    int totalDuration = 0;
    if (timelineFrames)
        for (const auto& f : *timelineFrames)
            totalDuration += f.duration;
    char info[128];
    if (hasFrames)
        std::snprintf(info, sizeof(info), "Frame %d of %zu   %d ms   %.1f fps", m_PreviewFrame + 1, timelineFrames->size(),
                      totalDuration, 1000.0f / (totalDuration / static_cast<float>(timelineFrames->size())));
    else
        std::snprintf(info, sizeof(info), "No frames to play.");
    float infoW = 0.0f;
    ui.MeasureText(info, &infoW, nullptr);
    ui.Dummy(0.0f, 4.0f * dpi);
    float cx, cy;
    ui.GetCursorScreenPos(&cx, &cy);
    ui.DrawTextAt(0.0f, cx + std::max(0.0f, (availW - infoW) * 0.5f), cy, EditorUI::Rgba(140, 140, 136, 255), info);
    ui.Dummy(0.0f, lineH);
}

} // namespace DekiEditor

using DekiEditor::FrameAnimationEditorWindow;

// Register the Animation Editor window
REGISTER_EDITOR_WINDOW(FrameAnimationEditorWindow, "Animation Editor", "2D/Animation Editor")

#endif  // DEKI_EDITOR
