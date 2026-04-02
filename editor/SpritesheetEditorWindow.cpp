#include "SpritesheetEditorWindow.h"
#include "modules/2d/Texture2D.h"
#include "imgui.h"
#include <deki-editor/EditorApplication.h>
#include <deki-editor/AssetDatabase.h>
#include <deki-editor/TextureImporter.h>
#include <deki-editor/TextureData.h>

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
    }

    // Create a separate window
    bool isOpen = IsOpen();

    // Set initial window size and constraints to prevent movement on content changes
    ImGui::SetNextWindowSize(ImVec2(800, 600), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSizeConstraints(ImVec2(500, 400), ImVec2(FLT_MAX, FLT_MAX));

    if (ImGui::Begin("Sprite Slicer###SpriteSlicer", &isOpen, ImGuiWindowFlags_None))
    {
        // Status message
        if (!m_StatusMessage.empty())
        {
            if (m_StatusIsError)
                ImGui::TextColored(ImVec4(1, 0, 0, 1), "%s", m_StatusMessage.c_str());
            else
                ImGui::TextColored(ImVec4(0, 1, 0, 1), "%s", m_StatusMessage.c_str());
        }

        DrawSlicingControls();

        ImGui::Separator();

        DrawTexturePreview();
    }
    ImGui::End();

    // Update open state
    SetOpen(isOpen);
}

void SpritesheetEditorWindow::DrawSlicingControls()
{
    ImGui::Text("Sprite Slicing Settings");
    ImGui::Separator();

    if (m_TextureWidth == 0 || m_TextureHeight == 0)
    {
        ImGui::TextColored(ImVec4(1, 0.5f, 0, 1), "No texture loaded");
        return;
    }

    ImGui::Text("Texture Size: %d x %d", m_TextureWidth, m_TextureHeight);
    ImGui::Spacing();

    // Mode selection dropdown
    const char* modes[] = { "Grid Mode", "Free Mode" };
    int currentMode = (int)m_UIMode;
    if (ImGui::Combo("Slicing Mode", &currentMode, modes, 2))
    {
        m_UIMode = (SlicingUIMode)currentMode;
    }

    ImGui::Spacing();

    // Show controls based on selected mode
    if (m_UIMode == SlicingUIMode::Grid)
    {
        // Grid mode controls
        ImGui::InputInt("Frame Width", &m_FrameWidth);
        ImGui::InputInt("Frame Height", &m_FrameHeight);

        // Clamp to valid range
        if (m_FrameWidth < 0) m_FrameWidth = 0;
        if (m_FrameHeight < 0) m_FrameHeight = 0;
        if (m_FrameWidth > m_TextureWidth) m_FrameWidth = m_TextureWidth;
        if (m_FrameHeight > m_TextureHeight) m_FrameHeight = m_TextureHeight;

        ImGui::Spacing();

        // Show grid info
        if (m_FrameWidth > 0 && m_FrameHeight > 0)
        {
            int cols = m_TextureWidth / m_FrameWidth;
            int rows = m_TextureHeight / m_FrameHeight;
            int totalFrames = rows * cols;
            ImGui::Text("Grid: %d columns x %d rows = %d frames", cols, rows, totalFrames);
        }

        ImGui::Spacing();

        if (ImGui::Button("Generate Grid"))
        {
            GenerateGrid();
        }
    }
    else // Free Mode
    {
        // Free mode controls
        ImGui::TextWrapped("Automatically detect sprite boundaries from transparent regions.");

        ImGui::Spacing();

        if (ImGui::Button("Auto-Cut Sprites"))
        {
            RunAutoCut();
        }
    }

    ImGui::Spacing();

    // Frame List
    ImGui::Text("Detected Frames: %zu", m_AtlasFrames.size());

    if (!m_AtlasFrames.empty())
    {
        if (ImGui::BeginChild("FrameList", ImVec2(0, 150), true))
        {
            for (size_t i = 0; i < m_AtlasFrames.size(); ++i)
            {
                const auto& frame = m_AtlasFrames[i];
                ImGui::Text("Frame %zu: X=%d Y=%d W=%d H=%d",
                           i, frame.x, frame.y, frame.width, frame.height);
            }
        }
        ImGui::EndChild();
    }

    ImGui::Spacing();

    // Save/Clear buttons
    if (ImGui::Button("Save Slice Settings"))
    {
        if (SaveSliceSettings())
        {
            m_StatusMessage = "Slice settings saved successfully";
            m_StatusIsError = false;

            // Trigger asset reimport so frames are generated
            if (!m_TextureGuid.empty())
            {
                fs::path texPath(m_TexturePath);
                fs::path relativePath = fs::relative(texPath, m_ProjectPath);
                std::string relativePathStr = relativePath.string();
                for (char& c : relativePathStr)
                {
                    if (c == '\\') c = '/';
                }
                AssetDatabase::ImportAsset(relativePathStr);
            }
        }
        else
        {
            m_StatusMessage = "Failed to save slice settings";
            m_StatusIsError = true;
        }
    }

    ImGui::SameLine();

    if (ImGui::Button("Clear All"))
    {
        m_FrameWidth = 0;
        m_FrameHeight = 0;
        m_AtlasFrames.clear();
    }
}

void SpritesheetEditorWindow::DrawTexturePreview()
{
    if (m_TextureId == 0)
    {
        ImGui::TextColored(ImVec4(1, 0.5f, 0, 1), "No texture preview available");
        return;
    }

    ImGui::Text("Texture Preview");
    ImGui::Separator();

    // Zoom controls
    ImGui::SliderFloat("Zoom", &m_Zoom, 0.1f, 4.0f, "%.1f");
    if (ImGui::Button("Reset View"))
    {
        m_Zoom = 1.0f;
        m_PanOffsetX = 0.0f;
        m_PanOffsetY = 0.0f;
    }

    // Draw texture with grid overlay
    ImVec2 canvasPos = ImGui::GetCursorScreenPos();
    ImVec2 canvasSize = ImGui::GetContentRegionAvail();

    // Draw texture
    float displayWidth = m_TextureWidth * m_Zoom;
    float displayHeight = m_TextureHeight * m_Zoom;

    ImGui::Image(
        (ImTextureID)(intptr_t)m_TextureId,
        ImVec2(displayWidth, displayHeight),
        ImVec2(0, 0),
        ImVec2(1, 1)
    );

    // Draw frame overlay if frames are defined
    if (!m_AtlasFrames.empty())
    {
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        ImU32 frameColor = IM_COL32(255, 255, 0, 200);

        for (const auto& frame : m_AtlasFrames)
        {
            float x1 = canvasPos.x + frame.x * m_Zoom;
            float y1 = canvasPos.y + frame.y * m_Zoom;
            float x2 = x1 + frame.width * m_Zoom;
            float y2 = y1 + frame.height * m_Zoom;

            // Draw rectangle outline
            drawList->AddRect(
                ImVec2(x1, y1),
                ImVec2(x2, y2),
                frameColor,
                0.0f,  // no rounding
                0,     // flags
                2.0f   // thickness
            );
        }
    }
}

void SpritesheetEditorWindow::LoadTextureData()
{
    if (m_TextureCachePath.empty())
        return;

    // Read cached texture
    TexData texData;
    if (!TextureImporter::ReadTexFile(m_TextureCachePath, texData) || !texData.isValid())
    {
        m_StatusMessage = "Failed to load texture cache";
        m_StatusIsError = true;
        return;
    }

    m_TextureWidth = texData.header.width;
    m_TextureHeight = texData.header.height;

    // Convert to RGBA for OpenGL
    std::vector<uint8_t> rgba = TextureImporter::ConvertToRGBA(texData);

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
