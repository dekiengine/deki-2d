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

    if (ui.Begin("Sprite Slicer###SpriteSlicer", &isOpen, 0))
    {
        const bool hasImage = m_TextureWidth > 0 && m_TextureHeight > 0;

        // ── Toolbar (full-bleed strip, as in the other tool windows) ────────
        ui.BeginToolbar();
        if (ui.ToolbarButton("Save", hasImage))
            SaveAndReimport();
        if (ui.ToolbarButton("Clear", hasImage && !m_AtlasFrames.empty()))
        {
            m_FrameWidth = 0;
            m_FrameHeight = 0;
            m_AtlasFrames.clear();
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
            const float settingsW = std::min(300.0f * dpi, availW * 0.45f);
            if (ui.BeginChild("##slicer_settings", settingsW, availH, false))
                DrawSlicingControls();
            ui.EndChild();
            ui.SameLine(0.0f, 0.0f);
            float seamX = 0.0f, seamY = 0.0f;
            ui.GetCursorScreenPos(&seamX, &seamY);
            ui.DrawLine(seamX, seamY, seamX, seamY + availH, ImGui::ColorConvertFloat4ToU32(DekiEditor::Palette::Line2), 1.0f);
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
    m_StatusMessage = "Saved. The frames are ready to use.";
    m_StatusIsError = false;

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
                OpenFile((fs::path(m_ProjectPath) / selected).string().c_str(), "");
        }
        else
        {
            ui.TextDisabled("Select an image in the Asset Browser to slice it.");
        }
        return;
    }

    char buf[256];

    // ── Image ────────────────────────────────────────────────────────────
    if (ui.SectionHeader("Image"))
    {
        DekiEditor::BeginPropertyContext();
        ui.PropertyRow("File");
        ui.TextWrapped(fs::path(m_TexturePath).filename().string().c_str());
        ui.PropertyRow("Size");
        std::snprintf(buf, sizeof(buf), "%d x %d px", m_TextureWidth, m_TextureHeight);
        ui.TextDisabled(buf);
        DekiEditor::EndPropertyContext();
    }

    // ── Slicing ──────────────────────────────────────────────────────────
    if (ui.SectionHeader("Slicing"))
    {
        DekiEditor::BeginPropertyContext();
        static const char* const kModes[] = { "Grid", "Detect" };
        int currentMode = (int)m_UIMode;
        ui.PropertyRow("Mode");
        ui.SetNextItemWidth(-FLT_MIN);
        if (ui.Combo("##mode", &currentMode, kModes, 2))
            m_UIMode = (SlicingUIMode)currentMode;

        if (m_UIMode == SlicingUIMode::Grid)
        {
            ui.PropertyRow("Frame Width");
            ui.SetNextItemWidth(-FLT_MIN);
            ui.DragInt("##fw", &m_FrameWidth, 0.25f, 0, m_TextureWidth);
            ui.PropertyRow("Frame Height");
            ui.SetNextItemWidth(-FLT_MIN);
            ui.DragInt("##fh", &m_FrameHeight, 0.25f, 0, m_TextureHeight);
            m_FrameWidth = std::clamp(m_FrameWidth, 0, m_TextureWidth);
            m_FrameHeight = std::clamp(m_FrameHeight, 0, m_TextureHeight);

            if (m_FrameWidth > 0 && m_FrameHeight > 0)
            {
                const int cols = m_TextureWidth / m_FrameWidth;
                const int rows = m_TextureHeight / m_FrameHeight;
                std::snprintf(buf, sizeof(buf), "%d x %d grid, %d frames", cols, rows, rows * cols);
                ui.PropertyRow("");
                ui.TextDisabled(buf);
            }
            ui.PropertyRow("");
            if (ui.Button("Make Grid", -FLT_MIN))
                GenerateGrid();
        }
        else
        {
            ui.PropertyRow("");
            ui.TextWrapped("Finds each sprite by the transparent space around it.");
            ui.PropertyRow("");
            if (ui.Button("Detect Sprites", -FLT_MIN))
                RunAutoCut();
        }
        DekiEditor::EndPropertyContext();
    }

    // ── Frames ───────────────────────────────────────────────────────────
    std::snprintf(buf, sizeof(buf), "Frames (%zu)###frames", m_AtlasFrames.size());
    if (ui.SectionHeader(buf))
    {
        DekiEditor::BeginPropertyContext();
        if (m_AtlasFrames.empty())
        {
            ui.TextDisabled("None yet. Make a grid or detect the sprites.");
        }
        for (size_t i = 0; i < m_AtlasFrames.size(); ++i)
        {
            const auto& frame = m_AtlasFrames[i];
            char label[32];
            std::snprintf(label, sizeof(label), "Frame %zu", i);
            ui.PropertyRow(label);
            std::snprintf(buf, sizeof(buf), "%d, %d   %d x %d", frame.x, frame.y, frame.width, frame.height);
            ui.TextDisabled(buf);
        }
        DekiEditor::EndPropertyContext();
    }

    if (!m_StatusMessage.empty())
    {
        ui.Spacing();
        DekiEditor::BeginPropertyContext();
        if (m_StatusIsError)
            ui.TextColored(DekiEditor::Palette::Red.x, DekiEditor::Palette::Red.y, DekiEditor::Palette::Red.z, 1.0f,
                           m_StatusMessage.c_str());
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

    float originX, originY, availX, availY;
    ui.GetCursorScreenPos(&originX, &originY);
    ui.GetContentRegionAvail(&availX, &availY);
    if (availX < 32.0f || availY < 32.0f)
        return;

    if (m_FitPending)
    {
        m_FitPending = false;
        const float margin = 32.0f * ui.GetDpiScale();
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

    // Frames in the theme accent
    const uint32_t accent = ui.GetStyleColor(EditorUI::Col::CheckMark);
    for (const auto& frame : m_AtlasFrames)
    {
        const float x1 = imgX + frame.x * m_Zoom;
        const float y1 = imgY + frame.y * m_Zoom;
        ui.DrawRect(x1, y1, x1 + frame.width * m_Zoom, y1 + frame.height * m_Zoom, accent, 1.0f);
    }

    // Wheel zooms about the cursor; a drag pans; a double-click fits.
    if (hovered && ui.GetMouseWheel() != 0.0f)
    {
        float mx, my;
        ui.GetMousePos(&mx, &my);
        const float before = m_Zoom;
        m_Zoom = std::clamp(m_Zoom * (ui.GetMouseWheel() > 0.0f ? 1.2f : 1.0f / 1.2f), 0.1f, 32.0f);
        const float fx = (mx - imgX) / before, fy = (my - imgY) / before;
        m_PanOffsetX += mx - (originX + (availX - m_TextureWidth * m_Zoom) * 0.5f + m_PanOffsetX + fx * m_Zoom);
        m_PanOffsetY += my - (originY + (availY - m_TextureHeight * m_Zoom) * 0.5f + m_PanOffsetY + fy * m_Zoom);
    }
    if (ui.IsItemActive() && (ui.IsMouseDragging(0, 0.0f) || ui.IsMouseDragging(2, 0.0f)))
    {
        float dx, dy;
        ui.GetMouseDelta(&dx, &dy);
        m_PanOffsetX += dx;
        m_PanOffsetY += dy;
    }
    if (hovered && ui.IsMouseDoubleClicked(0))
        m_FitPending = true;

    char info[64];
    std::snprintf(info, sizeof(info), "%.0f%%   Scroll: zoom   |   Drag: pan   |   Double-click: fit", m_Zoom * 100.0f);
    ui.DrawTextAt(0.0f, originX + 8.0f, originY + availY - ui.GetTextLineHeight() - 6.0f,
                  EditorUI::Rgba(120, 120, 116, 200), info);
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

    m_StatusMessage = "Generated " + std::to_string(m_AtlasFrames.size()) + " grid frames";
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
        m_StatusMessage = "No sprites detected";
        m_StatusIsError = true;
    }
    else
    {
        m_StatusMessage = "Detected " + std::to_string(m_AtlasFrames.size()) + " sprites";
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
