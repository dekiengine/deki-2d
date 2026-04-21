#ifdef DEKI_EDITOR

#include "NineSliceEditorWindow.h"

#include <deki-editor/EditorApplication.h>
#include <deki-editor/EditorAssets.h>
#include <deki-editor/AssetDatabase.h>
#include "imgui.h"
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

namespace DekiEditor
{

// ============================================================================
// File-type dispatch
// ============================================================================

bool NineSliceEditorWindow::CanOpenFile(const char* extension)
{
    return extension && std::strcmp(extension, ".png") == 0;
}

bool NineSliceEditorWindow::CanOpenAssetType(const char* assetType)
{
    return assetType && std::strcmp(assetType, "ProceduralSprite") == 0;
}

// ============================================================================
// Lifecycle
// ============================================================================

void NineSliceEditorWindow::OnOpen()
{
    auto& app = EditorApplication::Get();
    m_ProjectPath = app.GetProjectPath();
}

void NineSliceEditorWindow::OnClose()
{
    // ToolHost recreates the window after OnClose, but reset here too in case
    // it doesn't (e.g. if OpenFile is called again on a still-open instance).
    m_Source = Source::None;
    m_AssetPath.clear();
    m_CachePath.clear();
    m_AssetGuid.clear();
    m_DisplayName.clear();
    m_Current = m_Saved = {};
    m_UndoStack.clear();
    m_RedoStack.clear();
    m_ActiveDrag = DragHandle::None;
    m_Zoom = 4.0f;
    m_PanX = m_PanY = 0.0f;
}

void NineSliceEditorWindow::OpenFile(const char* filePath, const char* cachePath)
{
    if (!filePath || filePath[0] == '\0')
        return;

    m_AssetPath  = filePath;
    m_CachePath  = (cachePath && cachePath[0] != '\0') ? cachePath : "";
    m_DisplayName = fs::path(m_AssetPath).filename().string();

    fs::path ext = fs::path(m_AssetPath).extension();
    if (ext == ".asset")
        m_Source = Source::ProceduralAsset;
    else if (ext == ".png")
        m_Source = Source::NormalSprite;
    else
        m_Source = Source::None;

    // Resolve GUID for texture preview
    if (!m_ProjectPath.empty())
    {
        fs::path rel = fs::relative(fs::path(m_AssetPath), m_ProjectPath);
        std::string relStr = rel.string();
        for (char& c : relStr)
            if (c == '\\') c = '/';
        m_AssetGuid = AssetDatabase::AssetPathToGUID(relStr);
    }

    LoadFromDisk();
    m_UndoStack.clear();
    m_RedoStack.clear();
    m_ActiveDrag = DragHandle::None;
}

// ============================================================================
// Disk I/O
// ============================================================================

void NineSliceEditorWindow::LoadFromDisk()
{
    m_Current = {};
    m_Saved   = {};

    if (m_Source == Source::None || m_AssetPath.empty())
        return;

    if (m_Source == Source::ProceduralAsset)
    {
        std::ifstream in(m_AssetPath);
        if (!in.is_open()) return;
        try
        {
            nlohmann::json j;
            in >> j;
            if (j.contains("nine_slice") && j["nine_slice"].is_array() && j["nine_slice"].size() >= 4)
            {
                m_Current.top    = j["nine_slice"][0].get<int32_t>();
                m_Current.right  = j["nine_slice"][1].get<int32_t>();
                m_Current.bottom = j["nine_slice"][2].get<int32_t>();
                m_Current.left   = j["nine_slice"][3].get<int32_t>();
            }
        }
        catch (...) {}
    }
    else // NormalSprite
    {
        std::string sidecar = m_AssetPath + ".data";
        if (!fs::exists(sidecar)) { m_Saved = m_Current; return; }
        std::ifstream in(sidecar);
        if (!in.is_open()) return;
        try
        {
            nlohmann::json j;
            in >> j;
            const nlohmann::json* node = nullptr;
            if (j.contains("settings") && j["settings"].contains("nine_slice"))
                node = &j["settings"]["nine_slice"];
            else if (j.contains("nine_slice"))
                node = &j["nine_slice"];
            if (node && node->is_array() && node->size() >= 4)
            {
                m_Current.top    = (*node)[0].get<int32_t>();
                m_Current.right  = (*node)[1].get<int32_t>();
                m_Current.bottom = (*node)[2].get<int32_t>();
                m_Current.left   = (*node)[3].get<int32_t>();
            }
        }
        catch (...) {}
    }
    m_Saved = m_Current;
}

void NineSliceEditorWindow::SaveToDisk()
{
    if (m_Source == Source::None || m_AssetPath.empty())
        return;

    bool clear = (m_Current.top == 0 && m_Current.right == 0 &&
                  m_Current.bottom == 0 && m_Current.left == 0);

    if (m_Source == Source::ProceduralAsset)
    {
        nlohmann::json j = nlohmann::json::object();
        {
            std::ifstream in(m_AssetPath);
            if (in.is_open())
            {
                try { in >> j; }
                catch (...) { j = nlohmann::json::object(); }
            }
        }
        if (clear)
            j.erase("nine_slice");
        else
            j["nine_slice"] = {m_Current.top, m_Current.right, m_Current.bottom, m_Current.left};

        // Scope the ofstream so its destructor flushes + closes the file
        // BEFORE we trigger the re-import that reads it back.
        {
            std::ofstream out(m_AssetPath, std::ios::trunc);
            if (out.is_open())
                out << j.dump(2);
        }

        // Force re-bake of the .dtex so the runtime sees the new metadata
        if (!m_ProjectPath.empty())
        {
            fs::path rel = fs::relative(fs::path(m_AssetPath), m_ProjectPath);
            std::string relStr = rel.string();
            for (char& c : relStr) if (c == '\\') c = '/';
            AssetDatabase::ImportAsset(relStr);
        }
    }
    else // NormalSprite
    {
        std::string sidecar = m_AssetPath + ".data";
        nlohmann::json j = nlohmann::json::object();
        if (fs::exists(sidecar))
        {
            std::ifstream in(sidecar);
            if (in.is_open())
            {
                try { in >> j; }
                catch (...) { j = nlohmann::json::object(); }
            }
        }

        bool topLevel = j.contains("nine_slice") && !j.contains("settings");
        if (clear)
        {
            if (j.contains("settings") && j["settings"].contains("nine_slice"))
                j["settings"].erase("nine_slice");
            if (j.contains("nine_slice"))
                j.erase("nine_slice");
        }
        else
        {
            nlohmann::json arr = {m_Current.top, m_Current.right, m_Current.bottom, m_Current.left};
            if (topLevel)
                j["nine_slice"] = arr;
            else
                j["settings"]["nine_slice"] = arr;
        }

        // Scope the ofstream so the destructor flushes/closes before the
        // pipeline's mtime check sees it on the next refresh.
        {
            std::ofstream out(sidecar, std::ios::trunc);
            if (out.is_open())
                out << j.dump(2);
        }
        // AssetPipeline mtime check on the .data sidecar will re-bake the .dtex.
    }

    m_Saved = m_Current;
    m_UndoStack.clear();
    m_RedoStack.clear();
    EditorApplication::Get().RequestAssetRefresh();
}

// ============================================================================
// Undo/Redo
// ============================================================================

void NineSliceEditorWindow::PushUndoSnapshot(const Borders& previous)
{
    if (previous == m_Current) return;
    m_UndoStack.push_back(previous);
    m_RedoStack.clear();
    // Cap history to keep memory bounded
    if (m_UndoStack.size() > 128)
        m_UndoStack.erase(m_UndoStack.begin());
}

void NineSliceEditorWindow::HandleKeyboardShortcuts()
{
    if (!ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows))
        return;
    ImGuiIO& io = ImGui::GetIO();
    bool ctrl = io.KeyCtrl;
    bool shift = io.KeyShift;
    if (ctrl && !shift && ImGui::IsKeyPressed(ImGuiKey_Z, false))
    {
        if (!m_UndoStack.empty())
        {
            m_RedoStack.push_back(m_Current);
            m_Current = m_UndoStack.back();
            m_UndoStack.pop_back();
        }
    }
    else if ((ctrl && shift && ImGui::IsKeyPressed(ImGuiKey_Z, false)) ||
             (ctrl && !shift && ImGui::IsKeyPressed(ImGuiKey_Y, false)))
    {
        if (!m_RedoStack.empty())
        {
            m_UndoStack.push_back(m_Current);
            m_Current = m_RedoStack.back();
            m_RedoStack.pop_back();
        }
    }
}

// ============================================================================
// Canvas + drag handles
// ============================================================================

bool NineSliceEditorWindow::DrawCanvasAndHandles()
{
    if (m_AssetGuid.empty())
    {
        ImGui::TextDisabled("(No asset GUID resolved)");
        return false;
    }

    uint32_t texW = 0, texH = 0;
    uint32_t texId = EditorAssets::Get()->LoadFrameTexture(m_AssetGuid, &texW, &texH);
    if (texId == 0 || texW == 0 || texH == 0)
    {
        ImGui::TextDisabled("(Sprite preview unavailable — asset not cached)");
        return false;
    }

    // Reserve a canvas filling the rest of the window
    ImVec2 avail = ImGui::GetContentRegionAvail();
    if (avail.x < 64.0f) avail.x = 64.0f;
    if (avail.y < 64.0f) avail.y = 64.0f;

    ImVec2 canvasOrigin = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##nineslice_canvas", avail,
                            ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonMiddle);
    bool canvasHovered = ImGui::IsItemHovered();

    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->PushClipRect(canvasOrigin,
                      ImVec2(canvasOrigin.x + avail.x, canvasOrigin.y + avail.y),
                      true);

    // Sprite top-left in canvas-space, computed from pan + zoom and centered
    float displayW = texW * m_Zoom;
    float displayH = texH * m_Zoom;
    float spriteX = canvasOrigin.x + (avail.x - displayW) * 0.5f + m_PanX;
    float spriteY = canvasOrigin.y + (avail.y - displayH) * 0.5f + m_PanY;

    // Checkered background to make alpha visible
    {
        const float cell = 8.0f;
        ImU32 c1 = IM_COL32(60, 60, 60, 255);
        ImU32 c2 = IM_COL32(80, 80, 80, 255);
        for (float y = canvasOrigin.y; y < canvasOrigin.y + avail.y; y += cell)
            for (float x = canvasOrigin.x; x < canvasOrigin.x + avail.x; x += cell)
            {
                bool checker = (int((x - canvasOrigin.x) / cell) + int((y - canvasOrigin.y) / cell)) & 1;
                dl->AddRectFilled(ImVec2(x, y),
                                  ImVec2(std::min(x + cell, canvasOrigin.x + avail.x),
                                         std::min(y + cell, canvasOrigin.y + avail.y)),
                                  checker ? c2 : c1);
            }
    }

    dl->AddImage((ImTextureID)(intptr_t)texId,
                 ImVec2(spriteX, spriteY),
                 ImVec2(spriteX + displayW, spriteY + displayH));

    // Wheel zoom around the cursor
    if (canvasHovered && ImGui::GetIO().MouseWheel != 0.0f)
    {
        float oldZoom = m_Zoom;
        m_Zoom = std::clamp(m_Zoom * (ImGui::GetIO().MouseWheel > 0.0f ? 1.2f : 1.0f / 1.2f), 0.5f, 32.0f);
        // Keep the texel under the cursor stationary
        ImVec2 mp = ImGui::GetMousePos();
        float fx = (mp.x - spriteX) / oldZoom;
        float fy = (mp.y - spriteY) / oldZoom;
        m_PanX += (mp.x - (canvasOrigin.x + (avail.x - texW * m_Zoom) * 0.5f + fx * m_Zoom));
        m_PanY += (mp.y - (canvasOrigin.y + (avail.y - texH * m_Zoom) * 0.5f + fy * m_Zoom));
    }

    // Middle-mouse pan
    if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Middle, 0.0f))
    {
        m_PanX += ImGui::GetIO().MouseDelta.x;
        m_PanY += ImGui::GetIO().MouseDelta.y;
    }

    // Clamp borders to the texture
    auto clampLR = [&](int32_t& a, int32_t& b) {
        if (a < 0) a = 0;
        if (b < 0) b = 0;
        if (a + b > (int32_t)texW)
        {
            int32_t over = a + b - (int32_t)texW;
            int32_t shrinkA = std::min(a, over / 2);
            a -= shrinkA; b -= (over - shrinkA);
            if (b < 0) b = 0;
        }
    };
    auto clampTB = [&](int32_t& a, int32_t& b) {
        if (a < 0) a = 0;
        if (b < 0) b = 0;
        if (a + b > (int32_t)texH)
        {
            int32_t over = a + b - (int32_t)texH;
            int32_t shrinkA = std::min(a, over / 2);
            a -= shrinkA; b -= (over - shrinkA);
            if (b < 0) b = 0;
        }
    };
    clampLR(m_Current.left, m_Current.right);
    clampTB(m_Current.top, m_Current.bottom);

    // Pixel→canvas conversions
    auto pxX = [&](int32_t px) { return spriteX + px * m_Zoom; };
    auto pxY = [&](int32_t py) { return spriteY + py * m_Zoom; };

    float lineLeftX   = pxX(m_Current.left);
    float lineRightX  = pxX((int32_t)texW - m_Current.right);
    float lineTopY    = pxY(m_Current.top);
    float lineBottomY = pxY((int32_t)texH - m_Current.bottom);

    // Hit-test handles
    const float kHandleHit = 8.0f;
    ImVec2 mp = ImGui::GetMousePos();

    if (m_ActiveDrag == DragHandle::None && canvasHovered &&
        ImGui::IsMouseClicked(ImGuiMouseButton_Left))
    {
        struct Cand { DragHandle h; float dist; int32_t val; float startCoord; };
        Cand cands[4] = {
            { DragHandle::Left,   std::fabs(mp.x - lineLeftX),   m_Current.left,   mp.x },
            { DragHandle::Right,  std::fabs(mp.x - lineRightX),  m_Current.right,  mp.x },
            { DragHandle::Top,    std::fabs(mp.y - lineTopY),    m_Current.top,    mp.y },
            { DragHandle::Bottom, std::fabs(mp.y - lineBottomY), m_Current.bottom, mp.y },
        };
        int best = -1;
        for (int i = 0; i < 4; ++i)
            if (cands[i].dist <= kHandleHit && (best == -1 || cands[i].dist < cands[best].dist))
                best = i;
        if (best != -1)
        {
            m_ActiveDrag = cands[best].h;
            m_DragStartMouse = cands[best].startCoord;
            m_DragStartValue = cands[best].val;
            m_BordersAtDragStart = m_Current;  // for one-undo-per-drag
        }
    }

    if (m_ActiveDrag != DragHandle::None)
    {
        if (ImGui::IsMouseDown(ImGuiMouseButton_Left))
        {
            int32_t v = m_DragStartValue;
            if (m_ActiveDrag == DragHandle::Left)
            {
                v += (int32_t)std::round((mp.x - m_DragStartMouse) / m_Zoom);
                v = std::clamp(v, 0, (int32_t)texW - m_Current.right);
                m_Current.left = v;
            }
            else if (m_ActiveDrag == DragHandle::Right)
            {
                v -= (int32_t)std::round((mp.x - m_DragStartMouse) / m_Zoom);
                v = std::clamp(v, 0, (int32_t)texW - m_Current.left);
                m_Current.right = v;
            }
            else if (m_ActiveDrag == DragHandle::Top)
            {
                v += (int32_t)std::round((mp.y - m_DragStartMouse) / m_Zoom);
                v = std::clamp(v, 0, (int32_t)texH - m_Current.bottom);
                m_Current.top = v;
            }
            else if (m_ActiveDrag == DragHandle::Bottom)
            {
                v -= (int32_t)std::round((mp.y - m_DragStartMouse) / m_Zoom);
                v = std::clamp(v, 0, (int32_t)texH - m_Current.top);
                m_Current.bottom = v;
            }
        }
        else
        {
            // Mouse released — commit one undo step for the whole drag
            PushUndoSnapshot(m_BordersAtDragStart);
            m_ActiveDrag = DragHandle::None;
        }
    }

    // Draw 9-slice rectangle outline + 4 lines spanning the sprite
    ImU32 colIdle   = IM_COL32(255, 200, 0, 220);
    ImU32 colActive = IM_COL32(255, 240, 80, 255);
    auto col = [&](DragHandle h) { return (m_ActiveDrag == h) ? colActive : colIdle; };

    float sx0 = spriteX, sy0 = spriteY;
    float sx1 = spriteX + displayW, sy1 = spriteY + displayH;

    dl->AddRect(ImVec2(sx0, sy0), ImVec2(sx1, sy1), IM_COL32(140, 140, 140, 255));

    dl->AddLine(ImVec2(lineLeftX,  sy0), ImVec2(lineLeftX,  sy1), col(DragHandle::Left),   1.5f);
    dl->AddLine(ImVec2(lineRightX, sy0), ImVec2(lineRightX, sy1), col(DragHandle::Right),  1.5f);
    dl->AddLine(ImVec2(sx0, lineTopY),    ImVec2(sx1, lineTopY),    col(DragHandle::Top),    1.5f);
    dl->AddLine(ImVec2(sx0, lineBottomY), ImVec2(sx1, lineBottomY), col(DragHandle::Bottom), 1.5f);

    dl->PopClipRect();
    return m_ActiveDrag != DragHandle::None;
}

// ============================================================================
// OnGUI
// ============================================================================

void NineSliceEditorWindow::DrawSavePromptModalIfNeeded()
{
    if (m_OpenSavePromptNextFrame)
    {
        ImGui::OpenPopup("Save changes?##nineslice_close");
        m_OpenSavePromptNextFrame = false;
    }

    if (ImGui::BeginPopupModal("Save changes?##nineslice_close", nullptr,
                                ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::TextUnformatted("You have unsaved 9-slice changes.");
        ImGui::Spacing();
        if (ImGui::Button("Save"))
        {
            SaveToDisk();
            m_IsOpen = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Discard"))
        {
            m_Current = m_Saved;
            m_UndoStack.clear();
            m_RedoStack.clear();
            m_IsOpen = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel"))
        {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

void NineSliceEditorWindow::OnGUI()
{
    // Begin returns false when window is collapsed; we still want to End and run the modal.
    bool wantOpen = m_IsOpen;
    bool visible = ImGui::Begin(GetTitle(), &wantOpen, ImGuiWindowFlags_NoSavedSettings);

    // Veto close-via-X if there are unsaved changes — open prompt instead
    if (m_IsOpen && !wantOpen && m_Current != m_Saved)
    {
        m_OpenSavePromptNextFrame = true;
        wantOpen = true;
    }
    m_IsOpen = wantOpen;

    if (visible)
    {
        // ── Toolbar ─────────────────────────────────────────────────────────
        if (m_DisplayName.empty())
            ImGui::TextDisabled("No sprite open. Use 'Edit 9-Slice…' from a SpriteComponent inspector.");
        else
            ImGui::Text("Sprite: %s", m_DisplayName.c_str());

        ImGui::SameLine(0, 20);
        ImGui::SetNextItemWidth(180);
        ImGui::SliderFloat("Zoom", &m_Zoom, 0.5f, 32.0f, "%.1fx");
        ImGui::SameLine();
        if (ImGui::SmallButton("Reset View"))
        {
            m_Zoom = 4.0f; m_PanX = m_PanY = 0.0f;
        }

        bool dirty = (m_Current != m_Saved);
        ImGui::SameLine(0, 20);
        ImGui::BeginDisabled(!dirty);
        if (ImGui::Button(dirty ? "Save*" : "Save"))
            SaveToDisk();
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(m_UndoStack.empty());
        if (ImGui::Button("Undo"))
        {
            m_RedoStack.push_back(m_Current);
            m_Current = m_UndoStack.back();
            m_UndoStack.pop_back();
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(m_RedoStack.empty());
        if (ImGui::Button("Redo"))
        {
            m_UndoStack.push_back(m_Current);
            m_Current = m_RedoStack.back();
            m_RedoStack.pop_back();
        }
        ImGui::EndDisabled();

        ImGui::Separator();

        // ── Numeric DragInts (one undo step per edit session) ──────────────
        ImGui::PushItemWidth(70);
        Borders snapshotBefore = m_Current;
        ImGui::DragInt("Top",    &m_Current.top,    1.0f, 0, 8192); ImGui::SameLine();
        ImGui::DragInt("Right",  &m_Current.right,  1.0f, 0, 8192); ImGui::SameLine();
        ImGui::DragInt("Bottom", &m_Current.bottom, 1.0f, 0, 8192); ImGui::SameLine();
        ImGui::DragInt("Left",   &m_Current.left,   1.0f, 0, 8192);
        ImGui::PopItemWidth();
        // When any DragInt is released after an edit, push the pre-edit snapshot
        if (ImGui::IsItemDeactivatedAfterEdit() && snapshotBefore != m_Current)
            PushUndoSnapshot(snapshotBefore);

        ImGui::Spacing();
        DrawCanvasAndHandles();
        HandleKeyboardShortcuts();
    }

    DrawSavePromptModalIfNeeded();
    ImGui::End();
}

// Module DLLs auto-discover this on load. Macro must be inside the namespace.
REGISTER_EDITOR_WINDOW(NineSliceEditorWindow, "9-Slice Editor", "2D/9-Slice Editor")

} // namespace DekiEditor

#endif // DEKI_EDITOR
