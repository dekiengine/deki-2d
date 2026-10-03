// Editor-only translation unit (a firmware build compiles nothing from it).
#ifdef DEKI_EDITOR

#include "SpritesheetEditorWindow.h"
#include <deki/assets/Texture2D.h>
#include <deki-editor/EditorUI.h>
#include <deki-editor/EditorTheme.h>
#include <deki-editor/EditorApplication.h>
#include <cctype>
#include <deki-editor/AssetDatabase.h>
#include <deki-editor/TextureImporter.h>
#include <deki-editor/TextureData.h>

#include <algorithm>
#include <cstdio>
#include <cfloat>
#include <fstream>
#include <filesystem>
#include <SDL3/SDL.h>
#include <SDL3/SDL_opengl.h>
#include <nlohmann/json.hpp>


namespace fs = std::filesystem;
using json = nlohmann::json;

namespace DekiEditor
{

SpritesheetEditorWindow::SpritesheetEditorWindow() = default;

SpritesheetEditorWindow::~SpritesheetEditorWindow()
{
    if (m_TextureData)
    {
        delete[] m_TextureData;
        m_TextureData = nullptr;
    }

    if (m_TextureId != 0)
    {
        glDeleteTextures(1, &m_TextureId);
    }
}

void SpritesheetEditorWindow::OnOpen()
{
    // Get paths from EditorApplication singleton
    auto& app = EditorApplication::Get();
    m_ProjectPath = app.GetProjectPath();
    m_AssetsPath = app.GetAssetsPath();
    m_CachePath = app.GetCachePath();
}

void SpritesheetEditorWindow::OnClose()
{
    // Clean up texture resources (we own these, not EditorAssets)
    if (m_TextureData)
    {
        delete[] m_TextureData;
        m_TextureData = nullptr;
    }
    if (m_TextureId != 0)
    {
        glDeleteTextures(1, &m_TextureId);
        m_TextureId = 0;
    }

    // Reset texture state
    m_TexturePath.clear();
    m_TextureCachePath.clear();
    m_TextureGuid.clear();
    m_TextureWidth = 0;
    m_TextureHeight = 0;
    m_NeedsTextureUpload = false;

    // Reset slicing settings
    m_FrameWidth = 0;
    m_FrameHeight = 0;
    m_AtlasFrames.clear();
    m_SavedFrames.clear();
    m_SelectedFrame = m_HoveredFrame = -1;

    // Reset UI state
    m_UIMode = SlicingUIMode::Grid;
    m_Zoom = 1.0f;
    m_PanOffsetX = 0.0f;
    m_PanOffsetY = 0.0f;
    m_IsPanning = false;
    m_LastMousePosX = 0.0f;
    m_LastMousePosY = 0.0f;

    // Reset status
    m_StatusMessage.clear();
    m_StatusIsError = false;
}

bool SpritesheetEditorWindow::CanOpenFile(const char* extension)
{
    if (!extension) return false;

    // Can open PNG textures
    return strcmp(extension, ".png") == 0 ||
           strcmp(extension, ".PNG") == 0;
}

void SpritesheetEditorWindow::OpenFile(const char* filePath, const char* cachePath)
{
    m_TexturePath = filePath ? filePath : "";
    m_TextureCachePath.clear();
    m_StatusMessage.clear();

    // Use provided cache path if available
    if (cachePath && cachePath[0] != '\0')
    {
        m_TextureCachePath = cachePath;
    }

    // Get texture GUID
    if (!m_TexturePath.empty() && !m_ProjectPath.empty())
    {
        fs::path texPath(m_TexturePath);
        fs::path relativePath = fs::relative(texPath, m_ProjectPath);
        std::string relativePathStr = relativePath.string();
        // Normalize to forward slashes
        for (char& c : relativePathStr)
        {
            if (c == '\\') c = '/';
        }
        m_TextureGuid = AssetDatabase::AssetPathToGUID(relativePathStr);
    }

    // Load texture and settings
    LoadTextureData();
    LoadSliceSettings();
    OnSettingsLoaded();
}

namespace
{
bool SameFrames(const std::vector<DekiEditor::AtlasFrame>& a, const std::vector<DekiEditor::AtlasFrame>& b)
{
    if (a.size() != b.size())
        return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (a[i].x != b[i].x || a[i].y != b[i].y || a[i].width != b[i].width || a[i].height != b[i].height)
            return false;
    return true;
}

ImU32 AccentWithAlpha(float alpha)
{
    ImVec4 c = DekiEditor::Palette::Accent;
    c.w = alpha;
    return ImGui::ColorConvertFloat4ToU32(c);
}
}  // namespace

bool SpritesheetEditorWindow::IsDirty() const
{
    return !SameFrames(m_AtlasFrames, m_SavedFrames);
}

void SpritesheetEditorWindow::OnSettingsLoaded()
{
    m_SavedFrames = m_AtlasFrames;
    m_SelectedFrame = -1;
    m_HoveredFrame = -1;
    // Frames that are not a uniform grid came from Detect (or a hand-made atlas).
    m_UIMode = (m_AtlasFrames.empty() || IsUniformGrid()) ? SlicingUIMode::Grid : SlicingUIMode::Free;
    if (m_UIMode == SlicingUIMode::Grid && !m_AtlasFrames.empty())
    {
        m_FrameWidth = m_AtlasFrames[0].width;
        m_FrameHeight = m_AtlasFrames[0].height;
    }
}

void SpritesheetEditorWindow::OnGUI()
{
    // Deferred GPU upload (can't happen in background thread)
    if (m_NeedsTextureUpload)
    {
        UploadTextureToGPU();
        m_NeedsTextureUpload = false;
        m_FitPending = true;
    }

    auto& ui = EditorUI::Get();
    bool isOpen = IsOpen();

    const float dpi = ui.GetDpiScale();
    ui.SetNextWindowSize(860.0f * dpi, 560.0f * dpi, true);
    ui.SetNextWindowSizeConstraints(560.0f * dpi, 360.0f * dpi, FLT_MAX, FLT_MAX);

    std::string title = "Sprite Slicer";
    if (IsDirty())
        title += " *";
    title += "###SpriteSlicer";

    if (ui.Begin(title.c_str(), &isOpen, 0))
    {
        const bool hasImage = m_TextureWidth > 0 && m_TextureHeight > 0;
        const bool dirty = hasImage && IsDirty();

        // ── Toolbar ─────────────────────────────────────────────────────────
        ui.BeginToolbar();
        if (ui.ToolbarButton(dirty ? "Save*" : "Save", dirty))
            SaveAndReimport();
        if (ui.ToolbarButton("Revert", dirty))
        {
            LoadSliceSettings();
            OnSettingsLoaded();
        }
        if (ui.ToolbarButton("Fit View", hasImage))
            m_FitPending = true;
        ui.EndToolbar();

        if (!hasImage)
        {
            const float pad = 12.0f * dpi;
            ui.Dummy(0.0f, pad);
            ui.Indent(pad);
            DrawSlicingControls();  // the "open the selected image" state
            ui.Unindent(pad);
        }
        else
        {
            // Settings on the left, the image on the right, meeting at a seam.
            float availW = 0.0f, availH = 0.0f;
            ui.GetContentRegionAvail(&availW, &availH);
            const float settingsW = std::min(290.0f * dpi, availW * 0.45f);
            if (ui.BeginChild("##slicer_settings", settingsW, availH, false))
                DrawSlicingControls();
            ui.EndChild();
            ui.SameLine(0.0f, 0.0f);
            float seamX = 0.0f, seamY = 0.0f;
            ui.GetCursorScreenPos(&seamX, &seamY);
            ui.DrawLine(seamX, seamY, seamX, seamY + availH, ImGui::ColorConvertFloat4ToU32(DekiEditor::Palette::Line2), 1.0f);
            ui.Dummy(1.0f, availH);
            ui.SameLine(0.0f, 0.0f);
            if (ui.BeginChild("##slicer_canvas", 0.0f, availH, false))
                DrawTexturePreview();
            ui.EndChild();
        }
    }
    ui.End();

    // Update open state
    SetOpen(isOpen);
}

void SpritesheetEditorWindow::SaveAndReimport()
{
    if (!SaveSliceSettings())
    {
        m_StatusMessage = "Could not save the slice settings.";
        m_StatusIsError = true;
        return;
    }
    m_StatusMessage.clear();
    m_StatusIsError = false;
    m_SavedFrames = m_AtlasFrames;

    // Re-import so the frames are generated
    if (!m_TextureGuid.empty())
    {
        std::string relativePathStr = fs::relative(fs::path(m_TexturePath), m_ProjectPath).string();
        for (char& c : relativePathStr)
            if (c == '\\') c = '/';
        AssetDatabase::ImportAsset(relativePathStr);
    }
}

void SpritesheetEditorWindow::DrawSlicingControls()
{
    auto& ui = EditorUI::Get();
    const float dpi = ui.GetDpiScale();

    if (m_TextureWidth == 0 || m_TextureHeight == 0)
    {
        // Nothing open: offer the image selected in the Asset Browser. (Opened
        // from the Tools menu there was no way to load one at all.)
        const std::string& selected = EditorApplication::Get().GetSelectedAssetPath();
        std::string ext = fs::path(selected).extension().string();
        for (char& c : ext)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (!selected.empty() && CanOpenFile(ext.c_str()) && !m_ProjectPath.empty())
        {
            ui.TextWrapped(("Selected: " + fs::path(selected).filename().string()).c_str());
            ui.Spacing();
            if (ui.Button("Open Selected Image"))
            {
                OpenFile((fs::path(m_ProjectPath) / selected).string().c_str(), "");
                OnSettingsLoaded();
            }
        }
        else
        {
            ui.TextDisabled("Select an image in the Asset Browser to slice it.");
        }
        return;
    }

    char buf[160];

    // ── Header: the image, as the column's title ─────────────────────────
    {
        const float pad = 14.0f * dpi;
        ui.Dummy(0.0f, 10.0f * dpi);
        ui.Indent(pad);
        ui.Text(fs::path(m_TexturePath).filename().string().c_str());
        std::snprintf(buf, sizeof(buf), "%d x %d px", m_TextureWidth, m_TextureHeight);
        ui.TextDisabled(buf);
        ui.Unindent(pad);
        ui.Dummy(0.0f, 8.0f * dpi);
    }

    // ── How it is cut: Grid or Detect, as tabs ───────────────────────────
    ui.BeginTabLinks();
    if (ui.TabLink("Grid", m_UIMode == SlicingUIMode::Grid))
        m_UIMode = SlicingUIMode::Grid;
    if (ui.TabLink("Detect", m_UIMode == SlicingUIMode::Free))
        m_UIMode = SlicingUIMode::Free;
    ui.EndTabLinks();
    ui.Dummy(0.0f, 6.0f * dpi);

    DekiEditor::BeginPropertyContext();
    if (m_UIMode == SlicingUIMode::Grid)
    {
        // One row, two fields around an x; the grid follows them live.
        ui.PropertyRow("Frame Size");
        const float gap = ImGui::GetStyle().ItemSpacing.x;
        const float fieldW = (ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize("x").x - gap * 2.0f) * 0.5f;
        bool changed = false;
        ImGui::SetNextItemWidth(fieldW);
        changed |= DekiEditor::SchematicDragInt("##fw", &m_FrameWidth, 0.25f, 0, m_TextureWidth, "%d px");
        ImGui::SameLine(0.0f, gap);
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(DekiEditor::Palette::Dim, "x");
        ImGui::SameLine(0.0f, gap);
        ImGui::SetNextItemWidth(fieldW);
        changed |= DekiEditor::SchematicDragInt("##fh", &m_FrameHeight, 0.25f, 0, m_TextureHeight, "%d px");
        m_FrameWidth = std::clamp(m_FrameWidth, 0, m_TextureWidth);
        m_FrameHeight = std::clamp(m_FrameHeight, 0, m_TextureHeight);
        if (changed)
        {
            m_SelectedFrame = -1;
            if (m_FrameWidth > 0 && m_FrameHeight > 0)
                GenerateGrid();
            else
                m_AtlasFrames.clear();
        }

        ui.PropertyRow("");
        if (m_FrameWidth > 0 && m_FrameHeight > 0)
        {
            const int cols = m_TextureWidth / m_FrameWidth;
            const int rows = m_TextureHeight / m_FrameHeight;
            std::snprintf(buf, sizeof(buf), "%d x %d grid", cols, rows);
            ui.TextDisabled(buf);
            // Pixels the grid does not cover are dropped; say so.
            const int restX = m_TextureWidth - cols * m_FrameWidth;
            const int restY = m_TextureHeight - rows * m_FrameHeight;
            if (restX > 0 || restY > 0)
            {
                ui.PropertyRow("");
                if (restX > 0 && restY > 0)
                    std::snprintf(buf, sizeof(buf), "%d px on the right and %d px at the bottom are not used", restX, restY);
                else if (restX > 0)
                    std::snprintf(buf, sizeof(buf), "%d px on the right are not used", restX);
                else
                    std::snprintf(buf, sizeof(buf), "%d px at the bottom are not used", restY);
                ImGui::PushStyleColor(ImGuiCol_Text, DekiEditor::Palette::Amber);
                ui.TextWrapped(buf);
                ImGui::PopStyleColor();
            }
        }
        else
        {
            ui.TextDisabled("Set the size of one frame.");
        }
    }
    else
    {
        ui.PropertyRow("");
        ImGui::PushStyleColor(ImGuiCol_Text, DekiEditor::Palette::Dim);
        ui.TextWrapped("Finds each sprite by the transparent space around it.");
        ImGui::PopStyleColor();
        ui.PropertyRow("");
        if (ui.Button("Detect Sprites", -FLT_MIN))
        {
            m_SelectedFrame = -1;
            RunAutoCut();
        }
    }
    DekiEditor::EndPropertyContext();
    ui.Dummy(0.0f, 6.0f * dpi);

    // ── Frames: the list, tied to the canvas ─────────────────────────────
    std::snprintf(buf, sizeof(buf), "Frames (%zu)###frames", m_AtlasFrames.size());
    if (ui.SectionHeader(buf))
    {
        if (m_AtlasFrames.empty())
        {
            DekiEditor::BeginPropertyContext();
            ui.TextDisabled("None yet.");
            DekiEditor::EndPropertyContext();
        }
        const float pad = 14.0f * dpi;
        const float rowH = ImGui::GetTextLineHeight() + 8.0f * dpi;
        int hoveredRow = -1;
        for (size_t i = 0; i < m_AtlasFrames.size(); ++i)
        {
            const auto& frame = m_AtlasFrames[i];
            ImGui::PushID(static_cast<int>(i));
            const ImVec2 rowMin = ImGui::GetCursorScreenPos();
            const bool selected = static_cast<int>(i) == m_SelectedFrame;
            const bool highlighted = selected || static_cast<int>(i) == m_HoveredFrame;
            if (ImGui::Selectable("##frame", highlighted, 0, ImVec2(0.0f, rowH)))
                m_SelectedFrame = selected ? -1 : static_cast<int>(i);
            if (ImGui::IsItemHovered())
                hoveredRow = static_cast<int>(i);
            const float rowW = ImGui::GetItemRectSize().x;
            const float textY = rowMin.y + (rowH - ImGui::GetTextLineHeight()) * 0.5f;
            ImDrawList* dl = ImGui::GetWindowDrawList();
            char idx[16], pos[32], size[32];
            std::snprintf(idx, sizeof(idx), "%zu", i);
            std::snprintf(pos, sizeof(pos), "%d, %d", frame.x, frame.y);
            std::snprintf(size, sizeof(size), "%d x %d", frame.width, frame.height);
            const ImU32 dimCol = ImGui::ColorConvertFloat4ToU32(DekiEditor::Palette::Dim);
            const ImU32 textCol = ImGui::GetColorU32(ImGuiCol_Text);
            dl->AddText(ImVec2(rowMin.x + pad, textY), selected ? AccentWithAlpha(1.0f) : dimCol, idx);
            dl->AddText(ImVec2(rowMin.x + pad + 34.0f * dpi, textY), textCol, pos);
            const float sizeW = ImGui::CalcTextSize(size).x;
            dl->AddText(ImVec2(rowMin.x + rowW - pad - sizeW, textY), dimCol, size);
            ImGui::PopID();
        }
        // The list hovers the canvas' frame, and (in DrawTexturePreview) the
        // canvas hovers the list's row.
        if (hoveredRow >= 0)
            m_HoveredFrame = hoveredRow;
    }

    if (!m_StatusMessage.empty())
    {
        ui.Spacing();
        DekiEditor::BeginPropertyContext();
        if (m_StatusIsError)
            ImGui::TextColored(DekiEditor::Palette::Red, "%s", m_StatusMessage.c_str());
        else
            ui.TextDisabled(m_StatusMessage.c_str());
        DekiEditor::EndPropertyContext();
    }
}

void SpritesheetEditorWindow::DrawTexturePreview()
{
    auto& ui = EditorUI::Get();
    if (m_TextureId == 0)
    {
        ui.TextDisabled("Loading the image...");
        return;
    }

    const float dpi = ui.GetDpiScale();
    float originX, originY, availX, availY;
    ui.GetCursorScreenPos(&originX, &originY);
    ui.GetContentRegionAvail(&availX, &availY);
    if (availX < 32.0f || availY < 32.0f)
        return;

    if (m_FitPending)
    {
        m_FitPending = false;
        const float margin = 32.0f * dpi;
        const float fit = std::min((availX - margin * 2.0f) / (float)m_TextureWidth,
                                   (availY - margin * 2.0f) / (float)m_TextureHeight);
        m_Zoom = std::clamp(fit, 0.1f, 32.0f);
        m_PanOffsetX = m_PanOffsetY = 0.0f;
    }

    ui.InvisibleButton("##slicer_canvas_input", availX, availY, /*left*/ true, /*middle*/ true);
    const bool hovered = ui.IsItemHovered();
    ui.PushClipRect(originX, originY, originX + availX, originY + availY, true);

    // Checkerboard so transparency reads
    const float cell = 8.0f;
    const uint32_t c1 = EditorUI::Rgba(60, 60, 60, 255), c2 = EditorUI::Rgba(80, 80, 80, 255);
    for (float y = originY; y < originY + availY; y += cell)
        for (float x = originX; x < originX + availX; x += cell)
        {
            const bool odd = (int((x - originX) / cell) + int((y - originY) / cell)) & 1;
            ui.DrawRectFilled(x, y, std::min(x + cell, originX + availX), std::min(y + cell, originY + availY),
                              odd ? c2 : c1);
        }

    const float displayW = m_TextureWidth * m_Zoom;
    const float displayH = m_TextureHeight * m_Zoom;
    const float imgX = originX + (availX - displayW) * 0.5f + m_PanOffsetX;
    const float imgY = originY + (availY - displayH) * 0.5f + m_PanOffsetY;
    ui.DrawImage(m_TextureId, imgX, imgY, imgX + displayW, imgY + displayH);
    ui.DrawRect(imgX, imgY, imgX + displayW, imgY + displayH, EditorUI::Rgba(90, 95, 105, 255));

    // Which frame is under the mouse
    float mx = 0.0f, my = 0.0f;
    ui.GetMousePos(&mx, &my);
    int underMouse = -1;
    if (hovered)
        for (size_t i = 0; i < m_AtlasFrames.size(); ++i)
        {
            const auto& f = m_AtlasFrames[i];
            const float x1 = imgX + f.x * m_Zoom, y1 = imgY + f.y * m_Zoom;
            if (mx >= x1 && mx < x1 + f.width * m_Zoom && my >= y1 && my < y1 + f.height * m_Zoom)
                underMouse = static_cast<int>(i);
        }

    // Frames: outlined in the accent; the hovered one tinted, the selected
    // one tinted more, each numbered when there is room for it.
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImU32 outline = AccentWithAlpha(0.75f);
    const float numberRoom = ImGui::GetTextLineHeight() * 1.2f;
    for (size_t i = 0; i < m_AtlasFrames.size(); ++i)
    {
        const auto& frame = m_AtlasFrames[i];
        const float x1 = imgX + frame.x * m_Zoom;
        const float y1 = imgY + frame.y * m_Zoom;
        const float x2 = x1 + frame.width * m_Zoom;
        const float y2 = y1 + frame.height * m_Zoom;
        const bool selected = static_cast<int>(i) == m_SelectedFrame;
        const bool isHovered = static_cast<int>(i) == underMouse || static_cast<int>(i) == m_HoveredFrame;
        if (selected || isHovered)
            dl->AddRectFilled(ImVec2(x1, y1), ImVec2(x2, y2), AccentWithAlpha(selected ? 0.22f : 0.12f));
        dl->AddRect(ImVec2(x1, y1), ImVec2(x2, y2), selected ? AccentWithAlpha(1.0f) : outline, 0.0f, 0,
                    selected ? 2.0f : 1.0f);
        if (x2 - x1 >= numberRoom && y2 - y1 >= numberRoom)
        {
            char n[16];
            std::snprintf(n, sizeof(n), "%zu", i);
            dl->AddText(ImVec2(x1 + 3.0f * dpi, y1 + 1.0f * dpi), selected ? AccentWithAlpha(1.0f) : outline, n);
        }
    }
    m_HoveredFrame = underMouse;  // the list draws next frame with this

    // Wheel zooms about the cursor; a drag pans; a click picks a frame; a
    // double-click fits.
    if (hovered && ui.GetMouseWheel() != 0.0f)
    {
        const float before = m_Zoom;
        m_Zoom = std::clamp(m_Zoom * (ui.GetMouseWheel() > 0.0f ? 1.2f : 1.0f / 1.2f), 0.1f, 32.0f);
        const float fx = (mx - imgX) / before, fy = (my - imgY) / before;
        m_PanOffsetX += mx - (originX + (availX - m_TextureWidth * m_Zoom) * 0.5f + m_PanOffsetX + fx * m_Zoom);
        m_PanOffsetY += my - (originY + (availY - m_TextureHeight * m_Zoom) * 0.5f + m_PanOffsetY + fy * m_Zoom);
    }
    if (ui.IsItemActive() && (ui.IsMouseDragging(0) || ui.IsMouseDragging(2, 0.0f)))
    {
        float dx, dy;
        ui.GetMouseDelta(&dx, &dy);
        m_PanOffsetX += dx;
        m_PanOffsetY += dy;
    }
    if (hovered && ImGui::IsMouseReleased(ImGuiMouseButton_Left) && ImGui::GetIO().MouseDragMaxDistanceSqr[0] < 16.0f)
        m_SelectedFrame = underMouse;
    if (hovered && ui.IsMouseDoubleClicked(0))
        m_FitPending = true;

    // Corner readouts: zoom and frame count; the selected frame's rect
    char info[96];
    std::snprintf(info, sizeof(info), "%.0f%%   %zu frame%s", m_Zoom * 100.0f, m_AtlasFrames.size(),
                  m_AtlasFrames.size() == 1 ? "" : "s");
    const uint32_t readout = EditorUI::Rgba(150, 150, 146, 230);
    ui.DrawTextAt(0.0f, originX + 10.0f * dpi, originY + 8.0f * dpi, readout, info);
    if (m_SelectedFrame >= 0 && m_SelectedFrame < static_cast<int>(m_AtlasFrames.size()))
    {
        const auto& f = m_AtlasFrames[m_SelectedFrame];
        std::snprintf(info, sizeof(info), "Frame %d   %d, %d   %d x %d px", m_SelectedFrame, f.x, f.y, f.width, f.height);
        float tw = 0.0f;
        ui.MeasureText(info, &tw, nullptr);
        ui.DrawTextAt(0.0f, originX + availX - tw - 10.0f * dpi, originY + 8.0f * dpi, AccentWithAlpha(1.0f), info);
    }
    ui.DrawTextAt(0.0f, originX + 10.0f * dpi, originY + availY - ui.GetTextLineHeight() - 8.0f * dpi,
                  EditorUI::Rgba(120, 120, 116, 200), "Scroll: zoom   |   Drag: pan   |   Click: pick a frame   |   Double-click: fit");
    ui.PopClipRect();
}

void SpritesheetEditorWindow::LoadTextureData()
{
    if (m_TexturePath.empty())
        return;

    // The original image, not the cache: frames are authored in the image's
    // own pixels, and a cache shrunk by Max Size has fewer of them.
    DecodedImage decoded;
    if (!DecodeImageFile(m_TexturePath, decoded))
    {
        m_StatusMessage = "Failed to load the image";
        m_StatusIsError = true;
        return;
    }

    m_TextureWidth = decoded.width;
    m_TextureHeight = decoded.height;
    const std::vector<uint8_t>& rgba = decoded.rgba;

    // Copy to member buffer
    if (m_TextureData)
        delete[] m_TextureData;

    m_TextureData = new uint8_t[rgba.size()];
    memcpy(m_TextureData, rgba.data(), rgba.size());

    m_NeedsTextureUpload = true;
}

void SpritesheetEditorWindow::UploadTextureToGPU()
{
    if (!m_TextureData || m_TextureWidth == 0 || m_TextureHeight == 0)
        return;

    // Delete old texture
    if (m_TextureId != 0)
    {
        glDeleteTextures(1, &m_TextureId);
        m_TextureId = 0;
    }

    // Create OpenGL texture
    glGenTextures(1, &m_TextureId);
    glBindTexture(GL_TEXTURE_2D, m_TextureId);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    // Unpack state is global; other GL users (e.g. ImGui glyph uploads) can leave
    // a row stride behind, which would shear/overread this tightly packed upload.
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);

    glTexImage2D(
        GL_TEXTURE_2D,
        0,
        GL_RGBA,
        m_TextureWidth,
        m_TextureHeight,
        0,
        GL_RGBA,
        GL_UNSIGNED_BYTE,
        m_TextureData
    );

    glBindTexture(GL_TEXTURE_2D, 0);
}

bool SpritesheetEditorWindow::LoadSliceSettings()
{
    if (m_TexturePath.empty())
        return false;

    // Load from .png.data file
    std::string dataPath = m_TexturePath + ".data";
    if (!fs::exists(dataPath))
    {
        // No settings file yet, use defaults
        m_FrameWidth = 0;
        m_FrameHeight = 0;
        return true;
    }

    try
    {
        std::ifstream file(dataPath);
        if (!file.is_open())
            return false;

        json j;
        file >> j;

        // Clear existing frames
        m_AtlasFrames.clear();
        m_FrameWidth = 0;
        m_FrameHeight = 0;

        // Read from settings.sprite path
        if (j.contains("settings") && j["settings"].contains("sprite"))
        {
            auto& sprite = j["settings"]["sprite"];
            std::string mode = sprite.value("mode", "grid");

            if (mode == "atlas")
            {
                // Load atlas frames
                if (sprite.contains("frames") && sprite["frames"].is_array())
                {
                    for (const auto& frameJson : sprite["frames"])
                    {
                        DekiEditor::AtlasFrame frame;
                        frame.x = frameJson.value("x", 0);
                        frame.y = frameJson.value("y", 0);
                        frame.width = frameJson.value("width", 0);
                        frame.height = frameJson.value("height", 0);
                        m_AtlasFrames.push_back(frame);
                    }
                }
            }
            else
            {
                // Load grid mode (backward compatible)
                m_FrameWidth = sprite.value("frameWidth", 0);
                m_FrameHeight = sprite.value("frameHeight", 0);

                // Convert grid to atlas frames for display
                if (m_FrameWidth > 0 && m_FrameHeight > 0)
                {
                    int cols = m_TextureWidth / m_FrameWidth;
                    int rows = m_TextureHeight / m_FrameHeight;

                    for (int row = 0; row < rows; ++row)
                    {
                        for (int col = 0; col < cols; ++col)
                        {
                            DekiEditor::AtlasFrame frame;
                            frame.x = col * m_FrameWidth;
                            frame.y = row * m_FrameHeight;
                            frame.width = m_FrameWidth;
                            frame.height = m_FrameHeight;
                            m_AtlasFrames.push_back(frame);
                        }
                    }
                }
            }
        }

        return true;
    }
    catch (...)
    {
        return false;
    }
}

bool SpritesheetEditorWindow::SaveSliceSettings()
{
    if (m_TexturePath.empty())
        return false;

    std::string dataPath = m_TexturePath + ".data";

    // Read existing file to preserve other settings (like GUID)
    json j;
    if (fs::exists(dataPath))
    {
        try
        {
            std::ifstream file(dataPath);
            if (file.is_open())
                file >> j;
        }
        catch (...) {}
    }

    // Intelligently choose storage format
    if (m_AtlasFrames.empty())
    {
        // No slicing
        j["settings"]["sprite"] = json::object();
    }
    else if (IsUniformGrid())
    {
        // Save as grid mode for ESP32 optimization
        j["settings"]["sprite"]["mode"] = "grid";
        j["settings"]["sprite"]["frameWidth"] = m_AtlasFrames[0].width;
        j["settings"]["sprite"]["frameHeight"] = m_AtlasFrames[0].height;
    }
    else
    {
        // Save as atlas mode
        j["settings"]["sprite"]["mode"] = "atlas";
        j["settings"]["sprite"]["frames"] = json::array();

        for (const auto& frame : m_AtlasFrames)
        {
            json frameJson;
            frameJson["x"] = frame.x;
            frameJson["y"] = frame.y;
            frameJson["width"] = frame.width;
            frameJson["height"] = frame.height;
            j["settings"]["sprite"]["frames"].push_back(frameJson);
        }
    }

    // Write back
    try
    {
        std::ofstream file(dataPath);
        if (!file.is_open())
            return false;

        file << j.dump(2);
        return true;
    }
    catch (...)
    {
        return false;
    }
}

void SpritesheetEditorWindow::GenerateGrid()
{
    if (m_FrameWidth <= 0 || m_FrameHeight <= 0)
    {
        m_StatusMessage = "Invalid frame dimensions";
        m_StatusIsError = true;
        return;
    }

    m_AtlasFrames.clear();

    int cols = m_TextureWidth / m_FrameWidth;
    int rows = m_TextureHeight / m_FrameHeight;

    for (int row = 0; row < rows; ++row)
    {
        for (int col = 0; col < cols; ++col)
        {
            DekiEditor::AtlasFrame frame;
            frame.x = col * m_FrameWidth;
            frame.y = row * m_FrameHeight;
            frame.width = m_FrameWidth;
            frame.height = m_FrameHeight;
            m_AtlasFrames.push_back(frame);
        }
    }

    m_StatusMessage.clear();
    m_StatusIsError = false;
}

void SpritesheetEditorWindow::RunAutoCut()
{
    if (!m_TextureData || m_TextureWidth == 0 || m_TextureHeight == 0)
    {
        m_StatusMessage = "No texture data available";
        m_StatusIsError = true;
        return;
    }

    // Run auto-detect algorithm
    m_AtlasFrames = DekiEditor::TextureImporter::AutoDetectFrames(
        m_TextureData,
        m_TextureWidth,
        m_TextureHeight,
        10,  // alpha threshold
        1,   // min width
        1    // min height
    );

    if (m_AtlasFrames.empty())
    {
        m_StatusMessage = "No sprites found: the image has no transparent space between them.";
        m_StatusIsError = true;
    }
    else
    {
        m_StatusMessage.clear();
        m_StatusIsError = false;
    }
}

bool SpritesheetEditorWindow::IsUniformGrid() const
{
    if (m_AtlasFrames.empty())
        return false;

    // Check if all frames have the same size
    int width = m_AtlasFrames[0].width;
    int height = m_AtlasFrames[0].height;

    for (const auto& frame : m_AtlasFrames)
    {
        if (frame.width != width || frame.height != height)
            return false;
    }

    // Check if frames are arranged in a grid pattern
    // Calculate expected columns based on texture width
    int cols = m_TextureWidth / width;
    if (cols == 0)
        return false;

    for (size_t i = 0; i < m_AtlasFrames.size(); ++i)
    {
        int expectedX = (i % cols) * width;
        int expectedY = (i / cols) * height;

        if (m_AtlasFrames[i].x != expectedX || m_AtlasFrames[i].y != expectedY)
            return false;
    }

    return true;
}

REGISTER_EDITOR_WINDOW(SpritesheetEditorWindow, "Sprite Slicer", "2D/Sprite Slicer")

} // namespace DekiEditor

#endif  // DEKI_EDITOR
