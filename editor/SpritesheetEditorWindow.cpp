// Editor-only translation unit (a firmware build compiles nothing from it).
#ifdef DEKI_EDITOR

#include "SpritesheetEditorWindow.h"
#include <deki/assets/Texture2D.h>
#include <deki-editor/EditorUI.h>
#include <deki-editor/EditorTheme.h>
#include <deki-editor/IconsTabler.h>
#include <deki-editor/EditorApplication.h>
#include <cctype>
#include <deki-editor/AssetDatabase.h>
#include <deki-editor/TextureImporter.h>
#include <deki-editor/TextureData.h>
#include <deki-editor/FileIO.h>
#include <deki/LogSystem.h>

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

ImU32 PaletteU32(const ImVec4& c, float alpha = 1.0f)
{
    ImVec4 v = c;
    v.w *= alpha;
    return ImGui::ColorConvertFloat4ToU32(v);
}

// A checkerboard over [a, b], so transparent pixels read as transparent.
void DrawChecker(ImDrawList* dl, ImVec2 a, ImVec2 b, float cell)
{
    dl->AddRectFilled(a, b, IM_COL32(44, 45, 48, 255));
    if (cell < 2.0f)
        return;
    // Only the cells that show: a zoomed-in image can be far larger than the canvas.
    const ImVec2 clipMin = dl->GetClipRectMin(), clipMax = dl->GetClipRectMax();
    const int i0 = (int)std::max(0.0f, std::floor((clipMin.x - a.x) / cell));
    const int j0 = (int)std::max(0.0f, std::floor((clipMin.y - a.y) / cell));
    for (int j = j0; a.y + j * cell < std::min(b.y, clipMax.y); ++j)
        for (int i = i0; a.x + i * cell < std::min(b.x, clipMax.x); ++i)
            if ((i + j) & 1)
                dl->AddRectFilled(ImVec2(a.x + i * cell, a.y + j * cell),
                                  ImVec2(std::min(a.x + (i + 1) * cell, b.x), std::min(a.y + (j + 1) * cell, b.y)),
                                  IM_COL32(56, 57, 61, 255));
}

// A rounded, translucent label for readouts drawn over the canvas. Returns
// its size; `anchor` is its top-left, or its top-right when `fromRight`.
ImVec2 DrawPill(ImDrawList* dl, ImVec2 anchor, const char* text, ImU32 textCol, bool fromRight, float dpi)
{
    const ImVec2 ts = ImGui::CalcTextSize(text);
    const ImVec2 pad(8.0f * dpi, 4.0f * dpi);
    const ImVec2 size(ts.x + pad.x * 2.0f, ts.y + pad.y * 2.0f);
    const ImVec2 min = fromRight ? ImVec2(anchor.x - size.x, anchor.y) : anchor;
    dl->AddRectFilled(min, ImVec2(min.x + size.x, min.y + size.y), IM_COL32(12, 13, 15, 215), 5.0f * dpi);
    dl->AddRect(min, ImVec2(min.x + size.x, min.y + size.y), IM_COL32(255, 255, 255, 18), 5.0f * dpi);
    dl->AddText(ImVec2(min.x + pad.x, min.y + pad.y), textCol, text);
    return size;
}

// An image region fitted (aspect kept) and centred in a box, on a checkerboard.
void DrawThumb(ImDrawList* dl, ImTextureID tex, ImVec2 boxMin, float side, float srcW, float srcH, ImVec2 uv0,
               ImVec2 uv1, float dpi)
{
    const ImVec2 boxMax(boxMin.x + side, boxMin.y + side);
    DrawChecker(dl, boxMin, boxMax, 4.0f * dpi);
    float w = side, h = side;
    if (srcW > 0 && srcH > 0)
    {
        if (srcW > srcH)
            h = side * srcH / srcW;
        else
            w = side * srcW / srcH;
    }
    const ImVec2 a(boxMin.x + (side - w) * 0.5f, boxMin.y + (side - h) * 0.5f);
    dl->AddImage(tex, a, ImVec2(a.x + w, a.y + h), uv0, uv1);
    dl->AddRect(boxMin, boxMax, IM_COL32(255, 255, 255, 22), 3.0f * dpi);
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
    ui.SetNextWindowSize(900.0f * dpi, 580.0f * dpi, true);
    ui.SetNextWindowSizeConstraints(600.0f * dpi, 380.0f * dpi, FLT_MAX, FLT_MAX);

    std::string title = "Sprite Slicer";
    if (IsDirty())
        title += " *";
    title += "###SpriteSlicer";

    if (ui.Begin(title.c_str(), &isOpen, 0))
    {
        const bool hasImage = m_TextureWidth > 0 && m_TextureHeight > 0;
        const bool dirty = hasImage && IsDirty();

        // The image selected in the Asset Browser, when it is another one
        const std::string& selected = EditorApplication::Get().GetSelectedAssetPath();
        std::string selectedExt = fs::path(selected).extension().string();
        for (char& c : selectedExt)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        const std::string selectedFull =
            (!selected.empty() && !m_ProjectPath.empty()) ? (fs::path(m_ProjectPath) / selected).string() : "";
        const bool canOpenSelected = !selectedFull.empty() && CanOpenFile(selectedExt.c_str()) &&
                                     fs::path(selectedFull) != fs::path(m_TexturePath);

        // ── Toolbar ─────────────────────────────────────────────────────────
        ui.BeginToolbar();
        if (ui.ToolbarButton(dirty ? "Save*" : "Save", dirty))
            SaveAndReimport();
        if (ui.ToolbarButton("Revert", dirty))
        {
            LoadSliceSettings();
            OnSettingsLoaded();
        }
        if (ui.ToolbarButton("Open Selected", canOpenSelected && !dirty))
        {
            OpenFile(selectedFull.c_str(), "");
            OnSettingsLoaded();
        }
        if (canOpenSelected && dirty && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            DekiEditor::Tooltip("Save or revert this image's slices first.");
        ui.EndToolbar();

        if (!hasImage)
        {
            DrawSlicingControls();  // the "open an image" state
        }
        else
        {
            // Settings on the left, the image on the right, meeting at a seam.
            float availW = 0.0f, availH = 0.0f;
            ui.GetContentRegionAvail(&availW, &availH);
            const float settingsW = std::min(300.0f * dpi, availW * 0.42f);
            if (ui.BeginChild("##slicer_settings", settingsW, availH, false))
                DrawSlicingControls();
            ui.EndChild();
            ui.SameLine(0.0f, 0.0f);
            float seamX = 0.0f, seamY = 0.0f;
            ui.GetCursorScreenPos(&seamX, &seamY);
            ui.DrawLine(seamX, seamY, seamX, seamY + availH, PaletteU32(DekiEditor::Palette::Line2), 1.0f);
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
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float pad = 14.0f * dpi;

    if (m_TextureWidth == 0 || m_TextureHeight == 0)
    {
        // Nothing open: say how to start, centred in the window.
        const std::string& selected = EditorApplication::Get().GetSelectedAssetPath();
        std::string ext = fs::path(selected).extension().string();
        for (char& c : ext)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        const bool canOpen = !selected.empty() && CanOpenFile(ext.c_str()) && !m_ProjectPath.empty();

        const ImVec2 avail = ImGui::GetContentRegionAvail();
        const ImVec2 origin = ImGui::GetCursorScreenPos();
        const std::string titleStr = canOpen ? fs::path(selected).filename().string() : "No image open";
        const char* line = canOpen ? "Cut it into frames for sprites and animations."
                                   : "Select an image in the Asset Browser, then come back here.";
        const float lineH = ImGui::GetTextLineHeight();
        const float iconSize = 28.0f * dpi;
        const float blockH = iconSize + lineH * 2.0f + 18.0f * dpi + (canOpen ? ImGui::GetFrameHeight() + 14.0f * dpi : 0.0f);
        float y = origin.y + std::max(pad, (avail.y - blockH) * 0.45f);

        ImFont* font = ImGui::GetFont();
        const char* icon = ICON_TI_GRID_DOTS;
        const ImVec2 iconTs = font->CalcTextSizeA(iconSize, FLT_MAX, 0.0f, icon);
        dl->AddText(font, iconSize, ImVec2(origin.x + (avail.x - iconTs.x) * 0.5f, y), PaletteU32(DekiEditor::Palette::Dim), icon);
        y += iconSize + 10.0f * dpi;
        const ImVec2 titleTs = ImGui::CalcTextSize(titleStr.c_str());
        dl->AddText(ImVec2(origin.x + (avail.x - titleTs.x) * 0.5f, y), ImGui::GetColorU32(ImGuiCol_Text), titleStr.c_str());
        y += lineH + 4.0f * dpi;
        const ImVec2 lineTs = ImGui::CalcTextSize(line);
        dl->AddText(ImVec2(origin.x + (avail.x - lineTs.x) * 0.5f, y), PaletteU32(DekiEditor::Palette::Dim), line);
        y += lineH + 14.0f * dpi;
        if (canOpen)
        {
            const float bw = 150.0f * dpi;
            ImGui::SetCursorScreenPos(ImVec2(origin.x + (avail.x - bw) * 0.5f, y));
            ImGui::PushStyleVar(ImGuiStyleVar_ButtonTextAlign, ImVec2(0.5f, 0.5f));
            const bool open = ui.Button("Open Image", bw);
            ImGui::PopStyleVar();
            if (open)
            {
                OpenFile((fs::path(m_ProjectPath) / selected).string().c_str(), "");
                OnSettingsLoaded();
            }
        }
        ImGui::SetCursorScreenPos(origin);
        ImGui::Dummy(avail);
        return;
    }

    char buf[192];
    const ImTextureID tex = (ImTextureID)(intptr_t)m_TextureId;

    // ── Header: a thumbnail of the image, its name and size ──────────────
    {
        ImGui::Dummy(ImVec2(0.0f, 12.0f * dpi));
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const float side = 46.0f * dpi;
        if (m_TextureId != 0)
            DrawThumb(dl, tex, ImVec2(p.x + pad, p.y), side, (float)m_TextureWidth, (float)m_TextureHeight,
                      ImVec2(0, 0), ImVec2(1, 1), dpi);
        const float tx = p.x + pad + side + 12.0f * dpi;
        const float lineH = ImGui::GetTextLineHeight();
        const float ty = p.y + (side - lineH * 2.0f - 3.0f * dpi) * 0.5f;
        dl->AddText(ImVec2(tx, ty), ImGui::GetColorU32(ImGuiCol_Text), fs::path(m_TexturePath).filename().string().c_str());
        std::snprintf(buf, sizeof(buf), "%d \xc3\x97 %d px", m_TextureWidth, m_TextureHeight);
        dl->AddText(ImVec2(tx, ty + lineH + 3.0f * dpi), PaletteU32(DekiEditor::Palette::Dim), buf);
        ImGui::Dummy(ImVec2(ImGui::GetContentRegionAvail().x, side));
        ImGui::Dummy(ImVec2(0.0f, 12.0f * dpi));
    }

    // ── How it is cut: Grid or Detect, as tabs ───────────────────────────
    ui.BeginTabLinks();
    if (ui.TabLink("Grid", m_UIMode == SlicingUIMode::Grid))
        m_UIMode = SlicingUIMode::Grid;
    if (ui.TabLink("Detect", m_UIMode == SlicingUIMode::Free))
        m_UIMode = SlicingUIMode::Free;
    ui.EndTabLinks();
    ImGui::Dummy(ImVec2(0.0f, 6.0f * dpi));

    DekiEditor::BeginPropertyContext();
    if (m_UIMode == SlicingUIMode::Grid)
    {
        // One row, two fields around a ×; the grid follows them live.
        ui.PropertyRow("Frame Size");
        const float gap = ImGui::GetStyle().ItemSpacing.x;
        const float fieldW = (ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize("\xc3\x97").x - gap * 2.0f) * 0.5f;
        bool changed = false;
        ImGui::SetNextItemWidth(fieldW);
        changed |= DekiEditor::SchematicDragInt("##fw", &m_FrameWidth, 0.25f, 0, m_TextureWidth, "%d px");
        ImGui::SameLine(0.0f, gap);
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(DekiEditor::Palette::Dim, "\xc3\x97");
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

        ui.PropertyRow("Layout");
        ImGui::AlignTextToFramePadding();
        if (m_FrameWidth > 0 && m_FrameHeight > 0)
        {
            const int cols = m_TextureWidth / m_FrameWidth;
            const int rows = m_TextureHeight / m_FrameHeight;
            std::snprintf(buf, sizeof(buf), "%d \xc3\x97 %d", cols, rows);
            ImGui::TextUnformatted(buf);
            ImGui::SameLine(0.0f, 8.0f * dpi);
            ImGui::TextColored(DekiEditor::Palette::Dim, "%d frame%s", cols * rows, cols * rows == 1 ? "" : "s");
            // Pixels the grid does not cover are dropped; say so (and the
            // canvas tints them).
            const int restX = m_TextureWidth - cols * m_FrameWidth;
            const int restY = m_TextureHeight - rows * m_FrameHeight;
            if (restX > 0 || restY > 0)
            {
                ui.PropertyRow("");
                if (restX > 0 && restY > 0)
                    std::snprintf(buf, sizeof(buf), "%d px on the right and %d px at the bottom are left out.", restX, restY);
                else if (restX > 0)
                    std::snprintf(buf, sizeof(buf), "%d px on the right are left out.", restX);
                else
                    std::snprintf(buf, sizeof(buf), "%d px at the bottom are left out.", restY);
                ImGui::PushStyleColor(ImGuiCol_Text, DekiEditor::Palette::Amber);
                ImGui::TextUnformatted(ICON_TI_ALERT_TRIANGLE);
                ImGui::SameLine(0.0f, 6.0f * dpi);
                ImGui::PushTextWrapPos(0.0f);
                ImGui::TextUnformatted(buf);
                ImGui::PopTextWrapPos();
                ImGui::PopStyleColor();
            }
        }
        else
        {
            ImGui::TextColored(DekiEditor::Palette::Dim, "Set the size of one frame.");
        }
    }
    else
    {
        ui.PropertyRow("");
        ImGui::PushStyleColor(ImGuiCol_Text, DekiEditor::Palette::Dim);
        ui.TextWrapped("Finds each sprite by the transparent space around it.");
        ImGui::PopStyleColor();
        ui.PropertyRow("");
        if (ui.Button(ICON_TI_SCAN "  Detect Sprites", -FLT_MIN))
        {
            m_SelectedFrame = -1;
            RunAutoCut();
        }
    }
    if (m_StatusIsError && !m_StatusMessage.empty())
    {
        ui.PropertyRow("");
        ImGui::PushStyleColor(ImGuiCol_Text, DekiEditor::Palette::Red);
        ui.TextWrapped(m_StatusMessage.c_str());
        ImGui::PopStyleColor();
    }
    DekiEditor::EndPropertyContext();
    ImGui::Dummy(ImVec2(0.0f, 8.0f * dpi));

    // ── Frames: thumbnails, tied to the canvas ───────────────────────────
    std::snprintf(buf, sizeof(buf), "Frames (%zu)###frames", m_AtlasFrames.size());
    if (ui.SectionHeader(buf))
    {
        if (m_AtlasFrames.empty())
        {
            DekiEditor::BeginPropertyContext();
            ImGui::TextColored(DekiEditor::Palette::Dim, "None yet.");
            DekiEditor::EndPropertyContext();
        }
        const float rowH = 40.0f * dpi;
        const float thumb = 28.0f * dpi;
        int hoveredRow = -1;
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(ImGui::GetStyle().ItemSpacing.x, 0.0f));
        for (size_t i = 0; i < m_AtlasFrames.size(); ++i)
        {
            const auto& frame = m_AtlasFrames[i];
            ImGui::PushID(static_cast<int>(i));
            const ImVec2 rowMin = ImGui::GetCursorScreenPos();
            const bool selected = static_cast<int>(i) == m_SelectedFrame;
            // Drawn by hand: the Selectable is only the hit area.
            ImGui::PushStyleColor(ImGuiCol_Header, IM_COL32(0, 0, 0, 0));
            ImGui::PushStyleColor(ImGuiCol_HeaderHovered, IM_COL32(0, 0, 0, 0));
            ImGui::PushStyleColor(ImGuiCol_HeaderActive, IM_COL32(0, 0, 0, 0));
            if (ImGui::Selectable("##frame", selected, 0, ImVec2(0.0f, rowH)))
                m_SelectedFrame = selected ? -1 : static_cast<int>(i);
            ImGui::PopStyleColor(3);
            const bool rowHovered = ImGui::IsItemHovered();
            if (rowHovered)
                hoveredRow = static_cast<int>(i);
            const float rowW = ImGui::GetItemRectSize().x;
            const ImVec2 rowMax(rowMin.x + rowW, rowMin.y + rowH);

            if (selected)
            {
                dl->AddRectFilled(rowMin, rowMax, AccentWithAlpha(0.12f));
                dl->AddRectFilled(rowMin, ImVec2(rowMin.x + 2.0f * dpi, rowMax.y), AccentWithAlpha(1.0f));
            }
            else if (rowHovered || static_cast<int>(i) == m_HoveredFrame)
            {
                dl->AddRectFilled(rowMin, rowMax, IM_COL32(255, 255, 255, 10));
            }

            if (m_TextureId != 0)
            {
                const ImVec2 uv0((float)frame.x / m_TextureWidth, (float)frame.y / m_TextureHeight);
                const ImVec2 uv1((float)(frame.x + frame.width) / m_TextureWidth,
                                 (float)(frame.y + frame.height) / m_TextureHeight);
                DrawThumb(dl, tex, ImVec2(rowMin.x + pad, rowMin.y + (rowH - thumb) * 0.5f), thumb,
                          (float)frame.width, (float)frame.height, uv0, uv1, dpi);
            }
            const float lineH = ImGui::GetTextLineHeight();
            const float tx = rowMin.x + pad + thumb + 10.0f * dpi;
            const float ty = rowMin.y + (rowH - lineH * 2.0f) * 0.5f;
            char name[32], where[48];
            std::snprintf(name, sizeof(name), "Frame %zu", i);
            std::snprintf(where, sizeof(where), "%d \xc3\x97 %d  at %d, %d", frame.width, frame.height, frame.x, frame.y);
            dl->AddText(ImVec2(tx, ty), selected ? AccentWithAlpha(1.0f) : ImGui::GetColorU32(ImGuiCol_Text), name);
            dl->AddText(ImVec2(tx, ty + lineH), PaletteU32(DekiEditor::Palette::Dim), where);
            ImGui::PopID();
        }
        ImGui::PopStyleVar();
        // The list hovers the canvas' frame, and the canvas hovers the list's row.
        if (hoveredRow >= 0)
            m_HoveredFrame = hoveredRow;
        ImGui::Dummy(ImVec2(0.0f, 8.0f * dpi));
    }
}

void SpritesheetEditorWindow::DrawTexturePreview()
{
    auto& ui = EditorUI::Get();
    const float dpi = ui.GetDpiScale();
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    if (avail.x < 32.0f || avail.y < 32.0f)
        return;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 canvasMax(origin.x + avail.x, origin.y + avail.y);
    dl->AddRectFilled(origin, canvasMax, IM_COL32(17, 18, 20, 255));
    if (m_TextureId == 0)
    {
        DrawPill(dl, ImVec2(origin.x + 12.0f * dpi, origin.y + 12.0f * dpi), "Loading the image...",
                 PaletteU32(DekiEditor::Palette::Dim), false, dpi);
        ImGui::Dummy(avail);
        return;
    }

    if (m_FitPending)
    {
        m_FitPending = false;
        const float margin = 48.0f * dpi;
        const float fit = std::min((avail.x - margin * 2.0f) / (float)m_TextureWidth,
                                   (avail.y - margin * 2.0f) / (float)m_TextureHeight);
        m_Zoom = std::clamp(fit, 0.1f, 32.0f);
        m_PanOffsetX = m_PanOffsetY = 0.0f;
    }

    // The zoom control's place, known before the canvas takes input, so a
    // click on it is not also a click on the image.
    const float ctrlH = ImGui::GetFrameHeight();
    const float btnW = ctrlH;
    const float pctW = 54.0f * dpi;
    const ImVec2 ctrlSize(btnW * 3.0f + pctW + 6.0f * dpi, ctrlH + 6.0f * dpi);
    const ImVec2 ctrlMin(canvasMax.x - ctrlSize.x - 12.0f * dpi, canvasMax.y - ctrlSize.y - 12.0f * dpi);
    const ImVec2 mouse = ImGui::GetMousePos();
    const bool overCtrl = mouse.x >= ctrlMin.x && mouse.x < ctrlMin.x + ctrlSize.x && mouse.y >= ctrlMin.y &&
                          mouse.y < ctrlMin.y + ctrlSize.y;

    ImGui::SetNextItemAllowOverlap();
    ImGui::InvisibleButton("##slicer_canvas_input", avail, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonMiddle);
    const bool hovered = ImGui::IsItemHovered() && !overCtrl;
    const bool active = ImGui::IsItemActive();
    dl->PushClipRect(origin, canvasMax, true);

    const float displayW = m_TextureWidth * m_Zoom;
    const float displayH = m_TextureHeight * m_Zoom;
    const ImVec2 imgMin(origin.x + (avail.x - displayW) * 0.5f + m_PanOffsetX,
                        origin.y + (avail.y - displayH) * 0.5f + m_PanOffsetY);
    const ImVec2 imgMax(imgMin.x + displayW, imgMin.y + displayH);

    // A soft shadow, the checkerboard under the image only, the image.
    for (int s = 3; s >= 1; --s)
    {
        const float o = s * 3.0f * dpi;
        dl->AddRectFilled(ImVec2(imgMin.x - o, imgMin.y - o + 2.0f * dpi), ImVec2(imgMax.x + o, imgMax.y + o + 2.0f * dpi),
                          IM_COL32(0, 0, 0, 22), o);
    }
    DrawChecker(dl, imgMin, imgMax, 8.0f * dpi);
    dl->AddImage((ImTextureID)(intptr_t)m_TextureId, imgMin, imgMax);

    // Grid mode: tint the pixels the grid leaves out.
    if (m_UIMode == SlicingUIMode::Grid && m_FrameWidth > 0 && m_FrameHeight > 0)
    {
        const int cols = m_TextureWidth / m_FrameWidth, rows = m_TextureHeight / m_FrameHeight;
        const float usedX = imgMin.x + cols * m_FrameWidth * m_Zoom;
        const float usedY = imgMin.y + rows * m_FrameHeight * m_Zoom;
        const ImU32 left = PaletteU32(DekiEditor::Palette::Amber, 0.22f);
        if (usedX < imgMax.x - 0.5f)
            dl->AddRectFilled(ImVec2(usedX, imgMin.y), imgMax, left);
        if (usedY < imgMax.y - 0.5f)
            dl->AddRectFilled(ImVec2(imgMin.x, usedY), ImVec2(usedX, imgMax.y), left);
    }

    // Which frame is under the mouse
    int underMouse = -1;
    if (hovered)
        for (size_t i = 0; i < m_AtlasFrames.size(); ++i)
        {
            const auto& f = m_AtlasFrames[i];
            const float x1 = imgMin.x + f.x * m_Zoom, y1 = imgMin.y + f.y * m_Zoom;
            if (mouse.x >= x1 && mouse.x < x1 + f.width * m_Zoom && mouse.y >= y1 && mouse.y < y1 + f.height * m_Zoom)
                underMouse = static_cast<int>(i);
        }

    // Frames: a quiet outline each; the hovered one lit, the selected one
    // lit and tinted; numbered on a badge when there is room.
    ImFont* small = DekiEditor::g_FontUISmall ? DekiEditor::g_FontUISmall : ImGui::GetFont();
    const float smallSize = ImGui::GetFontSize() * 0.88f;
    for (size_t i = 0; i < m_AtlasFrames.size(); ++i)
    {
        const auto& frame = m_AtlasFrames[i];
        const ImVec2 a(imgMin.x + frame.x * m_Zoom, imgMin.y + frame.y * m_Zoom);
        const ImVec2 b(a.x + frame.width * m_Zoom, a.y + frame.height * m_Zoom);
        const bool selected = static_cast<int>(i) == m_SelectedFrame;
        const bool lit = selected || static_cast<int>(i) == underMouse || static_cast<int>(i) == m_HoveredFrame;
        if (selected)
            dl->AddRectFilled(a, b, AccentWithAlpha(0.18f));
        else if (lit)
            dl->AddRectFilled(a, b, AccentWithAlpha(0.08f));
        dl->AddRect(a, b, lit ? AccentWithAlpha(1.0f) : AccentWithAlpha(0.45f), 0.0f, 0, selected ? 2.0f : 1.0f);

        char n[16];
        std::snprintf(n, sizeof(n), "%zu", i);
        const ImVec2 ts = small->CalcTextSizeA(smallSize, FLT_MAX, 0.0f, n);
        const ImVec2 badge(ts.x + 8.0f * dpi, ts.y + 2.0f * dpi);
        if (b.x - a.x >= badge.x + 6.0f * dpi && b.y - a.y >= badge.y + 6.0f * dpi)
        {
            const ImVec2 bm(a.x + 3.0f * dpi, a.y + 3.0f * dpi);
            dl->AddRectFilled(bm, ImVec2(bm.x + badge.x, bm.y + badge.y), lit ? AccentWithAlpha(1.0f) : IM_COL32(12, 13, 15, 200),
                              3.0f * dpi);
            dl->AddText(small, smallSize, ImVec2(bm.x + 4.0f * dpi, bm.y + 1.0f * dpi),
                        lit ? IM_COL32(10, 11, 13, 255) : AccentWithAlpha(0.9f), n);
        }
    }
    m_HoveredFrame = underMouse;  // the list draws next frame with this

    // Readouts in pills: the frame count, or the selected frame's rect.
    char info[128];
    if (m_SelectedFrame >= 0 && m_SelectedFrame < static_cast<int>(m_AtlasFrames.size()))
    {
        const auto& f = m_AtlasFrames[m_SelectedFrame];
        std::snprintf(info, sizeof(info), "Frame %d   %d \xc3\x97 %d at %d, %d", m_SelectedFrame, f.width, f.height, f.x, f.y);
        DrawPill(dl, ImVec2(origin.x + 12.0f * dpi, origin.y + 12.0f * dpi), info, AccentWithAlpha(1.0f), false, dpi);
    }
    else if (m_AtlasFrames.empty())
    {
        DrawPill(dl, ImVec2(origin.x + 12.0f * dpi, origin.y + 12.0f * dpi),
                 m_UIMode == SlicingUIMode::Grid ? "Set a frame size to cut the image" : "Detect the sprites to cut the image",
                 PaletteU32(DekiEditor::Palette::Dim), false, dpi);
    }
    else
    {
        std::snprintf(info, sizeof(info), "%zu frame%s   click one to inspect it", m_AtlasFrames.size(),
                      m_AtlasFrames.size() == 1 ? "" : "s");
        DrawPill(dl, ImVec2(origin.x + 12.0f * dpi, origin.y + 12.0f * dpi), info, PaletteU32(DekiEditor::Palette::Dim),
                 false, dpi);
    }

    // Wheel zooms about the cursor; a drag pans; a click picks a frame; a
    // double-click fits.
    auto zoomAbout = [&](float factor, ImVec2 at)
    {
        const float before = m_Zoom;
        m_Zoom = std::clamp(m_Zoom * factor, 0.1f, 32.0f);
        const float fx = (at.x - imgMin.x) / before, fy = (at.y - imgMin.y) / before;
        m_PanOffsetX += at.x - (origin.x + (avail.x - m_TextureWidth * m_Zoom) * 0.5f + m_PanOffsetX + fx * m_Zoom);
        m_PanOffsetY += at.y - (origin.y + (avail.y - m_TextureHeight * m_Zoom) * 0.5f + m_PanOffsetY + fy * m_Zoom);
    };
    if (hovered && ImGui::GetIO().MouseWheel != 0.0f)
        zoomAbout(ImGui::GetIO().MouseWheel > 0.0f ? 1.2f : 1.0f / 1.2f, mouse);
    if (active && (ImGui::IsMouseDragging(ImGuiMouseButton_Left) || ImGui::IsMouseDragging(ImGuiMouseButton_Middle, 0.0f)))
    {
        m_PanOffsetX += ImGui::GetIO().MouseDelta.x;
        m_PanOffsetY += ImGui::GetIO().MouseDelta.y;
    }
    if (hovered && ImGui::IsMouseReleased(ImGuiMouseButton_Left) && ImGui::GetIO().MouseDragMaxDistanceSqr[0] < 16.0f)
        m_SelectedFrame = underMouse;
    if (hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
        m_FitPending = true;

    // Zoom control: − 100% + and fit, in a pill at the bottom right.
    dl->AddRectFilled(ctrlMin, ImVec2(ctrlMin.x + ctrlSize.x, ctrlMin.y + ctrlSize.y), IM_COL32(12, 13, 15, 225), 6.0f * dpi);
    dl->AddRect(ctrlMin, ImVec2(ctrlMin.x + ctrlSize.x, ctrlMin.y + ctrlSize.y), IM_COL32(255, 255, 255, 18), 6.0f * dpi);
    ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(255, 255, 255, 22));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, IM_COL32(255, 255, 255, 36));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f * dpi);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 0.0f));
    const ImVec2 center(origin.x + avail.x * 0.5f, origin.y + avail.y * 0.5f);
    ImGui::SetCursorScreenPos(ImVec2(ctrlMin.x + 3.0f * dpi, ctrlMin.y + 3.0f * dpi));
    if (ImGui::Button(ICON_TI_MINUS "##zoomout", ImVec2(btnW, ctrlH)))
        zoomAbout(1.0f / 1.25f, center);
    if (ImGui::IsItemHovered())
        DekiEditor::Tooltip("Zoom out");
    ImGui::SameLine();
    std::snprintf(info, sizeof(info), "%.0f%%##zoompct", m_Zoom * 100.0f);
    if (ImGui::Button(info, ImVec2(pctW, ctrlH)))
        zoomAbout(1.0f / m_Zoom, center);  // 100%: one screen pixel per image pixel
    if (ImGui::IsItemHovered())
        DekiEditor::Tooltip("Actual size");
    ImGui::SameLine();
    if (ImGui::Button(ICON_TI_PLUS "##zoomin", ImVec2(btnW, ctrlH)))
        zoomAbout(1.25f, center);
    if (ImGui::IsItemHovered())
        DekiEditor::Tooltip("Zoom in");
    ImGui::SameLine();
    if (ImGui::Button(ICON_TI_MAXIMIZE "##fit", ImVec2(btnW, ctrlH)))
        m_FitPending = true;
    if (ImGui::IsItemHovered())
        DekiEditor::Tooltip("Fit the image (or double-click it)");
    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor(3);

    dl->PopClipRect();
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

    // Read existing file to preserve other settings (like GUID). One that is
    // there but does not parse (a merge conflict) is left alone: writing over
    // it dropped the GUID, and every reference to the texture with it.
    json j;
    if (fs::exists(dataPath))
    {
        std::string text, err;
        if (!DekiEditor::ReadFileToString(dataPath, text, err))
            return false;
        j = json::parse(text, nullptr, false);
        if (j.is_discarded() || !j.is_object())
        {
            DEKI_LOG_ERROR("Sprite Slicer: %s could not be read (a merge conflict?). Fix it, then save again; "
                           "it was left as it is.", dataPath.c_str());
            return false;
        }
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

    // Write back in one step (temp file + rename): a torn sidecar loses the GUID.
    std::string err;
    if (!DekiEditor::AtomicWriteFile(dataPath, j.dump(2), err))
    {
        DEKI_LOG_ERROR("Sprite Slicer: could not save %s: %s", dataPath.c_str(), err.c_str());
        return false;
    }
    return true;
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
