#ifdef DEKI_EDITOR

#include "NineSliceEditorWindow.h"

#include <deki-editor/EditorApplication.h>
#include <deki-editor/EditorAssets.h>
#include <deki-editor/AssetDatabase.h>
#include <deki-editor/EditorUI.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

namespace DekiEditor
{

// Replace the alpha byte of a packed Rgba() color (keeps RGB).
static uint32_t WithAlpha(uint32_t rgba, uint32_t alpha)
{
    return (rgba & 0x00FFFFFFu) | (alpha << 24u);
}

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
    m_PanningLMB = false;
    m_FieldEditActive = false;
    m_Zoom = 4.0f;
    m_PanX = m_PanY = 0.0f;
    m_FitViewPending = false;
    m_TexW = m_TexH = 0;
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
    m_PanningLMB = false;
    m_FieldEditActive = false;
    m_FitViewPending = true;   // fit the sprite once the canvas size is known
    m_TexW = m_TexH = 0;
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

void NineSliceEditorWindow::Undo()
{
    if (m_UndoStack.empty()) return;
    m_RedoStack.push_back(m_Current);
    m_Current = m_UndoStack.back();
    m_UndoStack.pop_back();
}

void NineSliceEditorWindow::Redo()
{
    if (m_RedoStack.empty()) return;
    m_UndoStack.push_back(m_Current);
    m_Current = m_RedoStack.back();
    m_RedoStack.pop_back();
}

void NineSliceEditorWindow::HandleKeyboardShortcuts()
{
    auto& ui = EditorUI::Get();
    if (!ui.IsWindowFocused(true))
        return;
    bool ctrl = ui.IsKeyCtrl();
    bool shift = ui.IsKeyShift();
    if (ctrl && !shift && ui.IsKeyPressed(EditorUI::Key::Z, false))
        Undo();
    else if ((ctrl && shift && ui.IsKeyPressed(EditorUI::Key::Z, false)) ||
             (ctrl && !shift && ui.IsKeyPressed(EditorUI::Key::Y, false)))
        Redo();
    else if (ctrl && !shift && ui.IsKeyPressed(EditorUI::Key::S, false))
    {
        if (m_Current != m_Saved)
            SaveToDisk();
    }
}

// ============================================================================
// Canvas + drag handles
// ============================================================================

bool NineSliceEditorWindow::DrawCanvasAndHandles()
{
    auto& ui = EditorUI::Get();
    if (m_AssetGuid.empty())
    {
        ui.TextDisabled("(No asset GUID resolved)");
        return false;
    }

    uint32_t texW = 0, texH = 0;
    uint32_t texId = EditorAssets::Get()->LoadFrameTexture(m_AssetGuid, &texW, &texH);
    if (texId == 0 || texW == 0 || texH == 0)
    {
        ui.TextDisabled("(Sprite preview unavailable — asset not cached)");
        return false;
    }
    m_TexW = texW;
    m_TexH = texH;

    // Reserve a canvas filling the rest of the window
    float availX, availY;
    ui.GetContentRegionAvail(&availX, &availY);
    if (availX < 64.0f) availX = 64.0f;
    if (availY < 64.0f) availY = 64.0f;

    // Deferred fit-to-view: needs the canvas size, so it runs here
    if (m_FitViewPending)
    {
        m_FitViewPending = false;
        const float margin = 48.0f;
        float fit = std::min((availX - margin * 2.0f) / (float)texW,
                             (availY - margin * 2.0f) / (float)texH);
        m_Zoom = std::clamp(fit, 0.5f, 32.0f);
        m_PanX = m_PanY = 0.0f;
    }

    float canvasOriginX, canvasOriginY;
    ui.GetCursorScreenPos(&canvasOriginX, &canvasOriginY);
    ui.InvisibleButton("##nineslice_canvas", availX, availY, /*left*/ true, /*middle*/ true);
    bool canvasHovered = ui.IsItemHovered();

    ui.PushClipRect(canvasOriginX, canvasOriginY,
                    canvasOriginX + availX, canvasOriginY + availY,
                    true);

    // Sprite top-left in canvas-space, computed from pan + zoom and centered
    float displayW = texW * m_Zoom;
    float displayH = texH * m_Zoom;
    float spriteX = canvasOriginX + (availX - displayW) * 0.5f + m_PanX;
    float spriteY = canvasOriginY + (availY - displayH) * 0.5f + m_PanY;

    // Checkered background to make alpha visible
    {
        const float cell = 8.0f;
        uint32_t c1 = EditorUI::Rgba(60, 60, 60, 255);
        uint32_t c2 = EditorUI::Rgba(80, 80, 80, 255);
        for (float y = canvasOriginY; y < canvasOriginY + availY; y += cell)
            for (float x = canvasOriginX; x < canvasOriginX + availX; x += cell)
            {
                bool checker = (int((x - canvasOriginX) / cell) + int((y - canvasOriginY) / cell)) & 1;
                ui.DrawRectFilled(x, y,
                                  std::min(x + cell, canvasOriginX + availX),
                                  std::min(y + cell, canvasOriginY + availY),
                                  checker ? c2 : c1);
            }
    }

    ui.DrawImage(texId, spriteX, spriteY, spriteX + displayW, spriteY + displayH);

    // Wheel zoom around the cursor
    if (canvasHovered && ui.GetMouseWheel() != 0.0f)
    {
        float oldZoom = m_Zoom;
        m_Zoom = std::clamp(m_Zoom * (ui.GetMouseWheel() > 0.0f ? 1.2f : 1.0f / 1.2f), 0.5f, 32.0f);
        // Keep the texel under the cursor stationary
        float mx, my;
        ui.GetMousePos(&mx, &my);
        float fx = (mx - spriteX) / oldZoom;
        float fy = (my - spriteY) / oldZoom;
        m_PanX += (mx - (canvasOriginX + (availX - texW * m_Zoom) * 0.5f + fx * m_Zoom));
        m_PanY += (my - (canvasOriginY + (availY - texH * m_Zoom) * 0.5f + fy * m_Zoom));
    }

    // Middle-mouse pan
    if (ui.IsItemActive() && ui.IsMouseDragging(/*middle*/ 2, 0.0f))
    {
        float mdx, mdy;
        ui.GetMouseDelta(&mdx, &mdy);
        m_PanX += mdx;
        m_PanY += mdy;
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

    float sx0 = spriteX, sy0 = spriteY;
    float sx1 = spriteX + displayW, sy1 = spriteY + displayH;

    float lineLeftX   = pxX(m_Current.left);
    float lineRightX  = pxX((int32_t)texW - m_Current.right);
    float lineTopY    = pxY(m_Current.top);
    float lineBottomY = pxY((int32_t)texH - m_Current.bottom);

    float mpx, mpy;
    ui.GetMousePos(&mpx, &mpy);

    // ── Hover hit-test: corners first (they drag two borders), then edges
    // bounded to the sprite span so a guide can't be grabbed from far away ──
    const float kEdgeHit   = 8.0f;
    const float kCornerHit = 10.0f;
    DragHandle hover = DragHandle::None;
    if (canvasHovered && m_ActiveDrag == DragHandle::None && !m_PanningLMB)
    {
        struct { DragHandle h; float x, y; } corners[4] = {
            { DragHandle::TopLeft,     lineLeftX,  lineTopY    },
            { DragHandle::TopRight,    lineRightX, lineTopY    },
            { DragHandle::BottomLeft,  lineLeftX,  lineBottomY },
            { DragHandle::BottomRight, lineRightX, lineBottomY },
        };
        float bestD = kCornerHit + 0.001f;
        for (const auto& c : corners)
        {
            float d = std::max(std::fabs(mpx - c.x), std::fabs(mpy - c.y));
            if (d <= kCornerHit && d < bestD) { bestD = d; hover = c.h; }
        }

        if (hover == DragHandle::None)
        {
            bool inYSpan = (mpy >= sy0 - kEdgeHit && mpy <= sy1 + kEdgeHit);
            bool inXSpan = (mpx >= sx0 - kEdgeHit && mpx <= sx1 + kEdgeHit);
            struct { DragHandle h; float dist; bool valid; } edges[4] = {
                { DragHandle::Left,   std::fabs(mpx - lineLeftX),   inYSpan },
                { DragHandle::Right,  std::fabs(mpx - lineRightX),  inYSpan },
                { DragHandle::Top,    std::fabs(mpy - lineTopY),    inXSpan },
                { DragHandle::Bottom, std::fabs(mpy - lineBottomY), inXSpan },
            };
            float best = kEdgeHit + 0.001f;
            for (const auto& e : edges)
                if (e.valid && e.dist <= kEdgeHit && e.dist < best) { best = e.dist; hover = e.h; }
        }
    }

    // ── Begin drag (on a handle) or pan (on empty canvas) ──
    if (canvasHovered && ui.IsMouseClicked(0) && m_ActiveDrag == DragHandle::None)
    {
        if (hover != DragHandle::None)
        {
            m_ActiveDrag = hover;
            m_DragStartMouseX = mpx;
            m_DragStartMouseY = mpy;
            m_BordersAtDragStart = m_Current;  // for one-undo-per-drag
        }
        else
        {
            m_PanningLMB = true;
        }
    }

    // Double-click on empty canvas re-fits the view
    if (canvasHovered && hover == DragHandle::None && ui.IsMouseDoubleClicked(0))
    {
        m_FitViewPending = true;
        m_PanningLMB = false;
    }

    // ── Live drag update ──
    if (m_ActiveDrag != DragHandle::None)
    {
        if (ui.IsMouseDown(0))
        {
            int32_t dx = (int32_t)std::round((mpx - m_DragStartMouseX) / m_Zoom);
            int32_t dy = (int32_t)std::round((mpy - m_DragStartMouseY) / m_Zoom);
            if (AffectsLeft(m_ActiveDrag))
                m_Current.left = std::clamp(m_BordersAtDragStart.left + dx, 0, (int32_t)texW - m_Current.right);
            if (AffectsRight(m_ActiveDrag))
                m_Current.right = std::clamp(m_BordersAtDragStart.right - dx, 0, (int32_t)texW - m_Current.left);
            if (AffectsTop(m_ActiveDrag))
                m_Current.top = std::clamp(m_BordersAtDragStart.top + dy, 0, (int32_t)texH - m_Current.bottom);
            if (AffectsBottom(m_ActiveDrag))
                m_Current.bottom = std::clamp(m_BordersAtDragStart.bottom - dy, 0, (int32_t)texH - m_Current.top);

            // Redraw guides at the dragged position this same frame
            lineLeftX   = pxX(m_Current.left);
            lineRightX  = pxX((int32_t)texW - m_Current.right);
            lineTopY    = pxY(m_Current.top);
            lineBottomY = pxY((int32_t)texH - m_Current.bottom);
        }
        else
        {
            // Mouse released — commit one undo step for the whole drag
            PushUndoSnapshot(m_BordersAtDragStart);
            m_ActiveDrag = DragHandle::None;
        }
    }

    // ── Left-drag pan on empty canvas ──
    if (m_PanningLMB)
    {
        if (ui.IsMouseDown(0))
        {
            float mdx, mdy;
            ui.GetMouseDelta(&mdx, &mdy);
            m_PanX += mdx;
            m_PanY += mdy;
        }
        else
        {
            m_PanningLMB = false;
        }
    }

    // ── Mouse cursor feedback ──
    DragHandle cursorHandle = (m_ActiveDrag != DragHandle::None) ? m_ActiveDrag : hover;
    if (m_PanningLMB)
        ui.SetMouseCursor(EditorUI::MouseCursor::Hand);
    else if (cursorHandle == DragHandle::Left || cursorHandle == DragHandle::Right)
        ui.SetMouseCursor(EditorUI::MouseCursor::ResizeEW);
    else if (cursorHandle == DragHandle::Top || cursorHandle == DragHandle::Bottom)
        ui.SetMouseCursor(EditorUI::MouseCursor::ResizeNS);
    else if (cursorHandle != DragHandle::None)
        ui.SetMouseCursor(EditorUI::MouseCursor::ResizeAll);

    // ── Drawing ──
    DragHandle hot = (m_ActiveDrag != DragHandle::None) ? m_ActiveDrag : hover;
    auto edgeHot = [&](DragHandle edge) {
        switch (edge)
        {
            case DragHandle::Left:   return AffectsLeft(hot);
            case DragHandle::Right:  return AffectsRight(hot);
            case DragHandle::Top:    return AffectsTop(hot);
            case DragHandle::Bottom: return AffectsBottom(hot);
            default:                 return false;
        }
    };

    // Dim the sliced-off border strips so the stretchable center reads at a glance
    uint32_t dimFill = EditorUI::Rgba(0, 0, 0, 100);
    if (m_Current.top > 0)    ui.DrawRectFilled(sx0, sy0, sx1, lineTopY, dimFill);
    if (m_Current.bottom > 0) ui.DrawRectFilled(sx0, lineBottomY, sx1, sy1, dimFill);
    if (m_Current.left > 0)   ui.DrawRectFilled(sx0, lineTopY, lineLeftX, lineBottomY, dimFill);
    if (m_Current.right > 0)  ui.DrawRectFilled(lineRightX, lineTopY, sx1, lineBottomY, dimFill);

    // Sprite outline
    ui.DrawRect(sx0, sy0, sx1, sy1, EditorUI::Rgba(90, 95, 105, 255));

    // Guide lines in the theme accent — brighter + thicker when hot
    uint32_t accent  = ui.GetStyleColor(EditorUI::Col::CheckMark);
    uint32_t colIdle = WithAlpha(accent, 150);
    auto lineStyle = [&](DragHandle edge, uint32_t& col, float& thick) {
        bool isHot = edgeHot(edge);
        col   = isHot ? accent : colIdle;
        thick = isHot ? 2.0f : 1.0f;
    };
    uint32_t lc; float lt;
    lineStyle(DragHandle::Left,   lc, lt); ui.DrawLine(lineLeftX,  sy0, lineLeftX,  sy1, lc, lt);
    lineStyle(DragHandle::Right,  lc, lt); ui.DrawLine(lineRightX, sy0, lineRightX, sy1, lc, lt);
    lineStyle(DragHandle::Top,    lc, lt); ui.DrawLine(sx0, lineTopY,    sx1, lineTopY,    lc, lt);
    lineStyle(DragHandle::Bottom, lc, lt); ui.DrawLine(sx0, lineBottomY, sx1, lineBottomY, lc, lt);

    // Midpoint grips: a small pill on each guide advertising "this line drags"
    {
        const float gLen   = ui.GetTextLineHeight() * 0.55f;  // half-length
        const float gThick = 2.5f;                             // half-thickness
        float cx = (sx0 + sx1) * 0.5f;
        float cy = (sy0 + sy1) * 0.5f;
        auto gripCol = [&](DragHandle edge) { return edgeHot(edge) ? accent : WithAlpha(accent, 200); };
        ui.DrawRectFilled(lineLeftX  - gThick, cy - gLen, lineLeftX  + gThick, cy + gLen, gripCol(DragHandle::Left),   gThick);
        ui.DrawRectFilled(lineRightX - gThick, cy - gLen, lineRightX + gThick, cy + gLen, gripCol(DragHandle::Right),  gThick);
        ui.DrawRectFilled(cx - gLen, lineTopY    - gThick, cx + gLen, lineTopY    + gThick, gripCol(DragHandle::Top),    gThick);
        ui.DrawRectFilled(cx - gLen, lineBottomY - gThick, cx + gLen, lineBottomY + gThick, gripCol(DragHandle::Bottom), gThick);
    }

    // Corner handles at the guide intersections (drag two borders at once)
    {
        const float ch = ui.GetTextLineHeight() * 0.28f;  // half-size
        uint32_t darkFill = EditorUI::Rgba(10, 11, 13, 230);
        struct { DragHandle h; float x, y; } corners[4] = {
            { DragHandle::TopLeft,     lineLeftX,  lineTopY    },
            { DragHandle::TopRight,    lineRightX, lineTopY    },
            { DragHandle::BottomLeft,  lineLeftX,  lineBottomY },
            { DragHandle::BottomRight, lineRightX, lineBottomY },
        };
        for (const auto& c : corners)
        {
            bool isHot = (hot == c.h);
            if (isHot)
                ui.DrawRectFilled(c.x - ch, c.y - ch, c.x + ch, c.y + ch, accent);
            else
            {
                ui.DrawRectFilled(c.x - ch, c.y - ch, c.x + ch, c.y + ch, darkFill);
                ui.DrawRect(c.x - ch, c.y - ch, c.x + ch, c.y + ch, WithAlpha(accent, 200));
            }
        }
    }

    // Pixel value labels just outside the sprite (hidden at 0 unless hot)
    {
        uint32_t labelDim = EditorUI::Rgba(200, 198, 190, 235);
        uint32_t backdrop = EditorUI::Rgba(10, 11, 13, 200);
        auto drawLabel = [&](DragHandle edge, int32_t value, float x, float y, bool centerX, bool centerY)
        {
            bool isHot = edgeHot(edge);
            if (value <= 0 && !isHot) return;
            char txt[32];
            std::snprintf(txt, sizeof(txt), "%d", value);
            float tw, th;
            ui.MeasureText(txt, &tw, &th);
            if (centerX) x -= tw * 0.5f;
            if (centerY) y -= th * 0.5f;
            ui.DrawRectFilled(x - 3.0f, y - 1.0f, x + tw + 3.0f, y + th + 1.0f, backdrop, 2.0f);
            ui.DrawTextAt(0.0f, x, y, isHot ? accent : labelDim, txt);
        };
        drawLabel(DragHandle::Left,   m_Current.left,   lineLeftX,   sy1 + 6.0f, true,  false);
        drawLabel(DragHandle::Right,  m_Current.right,  lineRightX,  sy1 + 6.0f, true,  false);
        drawLabel(DragHandle::Top,    m_Current.top,    sx1 + 8.0f,  lineTopY,   false, true);
        drawLabel(DragHandle::Bottom, m_Current.bottom, sx1 + 8.0f,  lineBottomY, false, true);
    }

    // Usage hint pinned to the canvas bottom-left
    ui.DrawTextAt(0.0f, canvasOriginX + 8.0f,
                  canvasOriginY + availY - ui.GetTextLineHeight() - 6.0f,
                  EditorUI::Rgba(120, 120, 116, 200),
                  "Drag guides or corners   |   Scroll: zoom   |   Drag empty space: pan   |   Double-click: fit");

    ui.PopClipRect();
    return m_ActiveDrag != DragHandle::None || m_PanningLMB;
}

// ============================================================================
// OnGUI
// ============================================================================

void NineSliceEditorWindow::DrawSavePromptModalIfNeeded()
{
    auto& ui = EditorUI::Get();
    if (m_OpenSavePromptNextFrame)
    {
        ui.OpenPopup("Save changes?##nineslice_close");
        m_OpenSavePromptNextFrame = false;
    }

    // Schematic modal shell — centered, fixed width, generous padding, matching
    // the editor's asset-browser dialogs (sizes are font-size multiples so the
    // package needs no DPI accessor).
    const float em = ui.GetTextLineHeight();
    float vx, vy, vw, vh;
    ui.GetMainViewportRect(&vx, &vy, &vw, &vh);
    ui.SetNextWindowPos(vx + vw * 0.5f, vy + vh * 0.5f, 0.5f, 0.5f);
    ui.SetNextWindowSize(em * 28.0f, 0.0f);
    ui.PushStyleVar(EditorUI::StyleVar::WindowPadding, em * 1.4f, em * 1.25f);

    if (ui.BeginPopupModal("Save changes?##nineslice_close", nullptr,
                            EditorUI::WinNoSavedSettings))
    {
        ui.Text("You have unsaved 9-slice changes.");
        ui.Spacing();
        ui.Spacing();

        float availW;
        ui.GetContentRegionAvail(&availW, nullptr);
        const float gap  = em * 0.6f;
        const float btnW = (availW - gap * 2.0f) / 3.0f;

        ui.PushButtonTextAlign(0.5f, 0.5f);
        if (ui.Button("Cancel", btnW))
        {
            ui.CloseCurrentPopup();
        }
        ui.SameLine(0.0f, gap);
        if (ui.Button("Discard", btnW))
        {
            m_Current = m_Saved;
            m_UndoStack.clear();
            m_RedoStack.clear();
            m_IsOpen = false;
            ui.CloseCurrentPopup();
        }
        ui.SameLine(0.0f, gap);
        uint32_t accent = ui.GetStyleColor(EditorUI::Col::CheckMark);
        ui.PushStyleColor(EditorUI::Col::Button,        WithAlpha(accent, 102));
        ui.PushStyleColor(EditorUI::Col::ButtonHovered, accent);
        ui.PushStyleColor(EditorUI::Col::ButtonActive,  accent);
        if (ui.Button("Save", btnW))
        {
            SaveToDisk();
            m_IsOpen = false;
            ui.CloseCurrentPopup();
        }
        ui.PopStyleColor(3);
        ui.PopStyleVar(); // ButtonTextAlign

        ui.EndPopup();
    }
    ui.PopStyleVar(); // WindowPadding
}

void NineSliceEditorWindow::OnGUI()
{
    auto& ui = EditorUI::Get();
    // Begin returns false when window is collapsed; we still want to End and run the modal.
    bool wantOpen = m_IsOpen;
    bool visible = ui.Begin(GetTitle(), &wantOpen, EditorUI::WinNoSavedSettings);

    // Veto close-via-X if there are unsaved changes — open prompt instead
    if (m_IsOpen && !wantOpen && m_Current != m_Saved)
    {
        m_OpenSavePromptNextFrame = true;
        wantOpen = true;
    }
    m_IsOpen = wantOpen;

    if (visible)
    {
        bool dirty     = (m_Current != m_Saved);
        bool hasSprite = !m_DisplayName.empty();

        // ── Toolbar (console-style full-bleed strip) ────────────────────────
        ui.BeginToolbar();
        if (ui.ToolbarButton(dirty ? "Save*" : "Save", hasSprite && dirty))
            SaveToDisk();
        if (ui.ToolbarButton("Undo", !m_UndoStack.empty()))
            Undo();
        if (ui.ToolbarButton("Redo", !m_RedoStack.empty()))
            Redo();
        if (ui.ToolbarButton("Fit View", hasSprite))
            m_FitViewPending = true;
        ui.EndToolbar();

        if (!hasSprite)
        {
            ui.Spacing();
            ui.TextDisabled("No sprite open. Use 'Edit 9-Slice...' from a Sprite inspector or the Asset Browser.");
        }
        else
        {
            // ── Info row: bold sprite name + dim dimensions/zoom ────────────
            ui.Spacing();
            ui.PushBoldFont();
            ui.Text(m_DisplayName.c_str());
            ui.PopFont();
            if (m_TexW > 0)
            {
                char info[64];
                std::snprintf(info, sizeof(info), "%u x %u px   %.0f%%",
                              m_TexW, m_TexH, m_Zoom * 100.0f);
                ui.SameLine(0.0f, 12.0f);
                ui.TextDisabled(info);
            }

            // ── Border fields (one undo step per edit session) ──────────────
            // Snapshot the pre-edit values while no field is active; drags span
            // many frames, so the snapshot must be from before the edit began.
            if (!m_FieldEditActive)
                m_FieldsSnapshot = m_Current;

            bool anyActive = false, anyCommitted = false;
            const float fieldW = ui.GetTextLineHeight() * 4.0f;
            auto borderField = [&](const char* label, int32_t* v)
            {
                ui.Text(label);
                ui.SameLine();
                ui.SetNextItemWidth(fieldW);
                char id[24];
                std::snprintf(id, sizeof(id), "##ns%s", label);
                ui.DragInt(id, v, 1.0f, 0, 8192);
                anyActive    |= ui.IsItemActive();
                anyCommitted |= ui.IsItemDeactivatedAfterEdit();
            };
            borderField("Left",   &m_Current.left);   ui.SameLine(0.0f, 14.0f);
            borderField("Top",    &m_Current.top);    ui.SameLine(0.0f, 14.0f);
            borderField("Right",  &m_Current.right);  ui.SameLine(0.0f, 14.0f);
            borderField("Bottom", &m_Current.bottom);

            if (anyCommitted)
                PushUndoSnapshot(m_FieldsSnapshot);
            m_FieldEditActive = anyActive;

            ui.Spacing();
            DrawCanvasAndHandles();
        }

        HandleKeyboardShortcuts();
    }

    DrawSavePromptModalIfNeeded();
    ui.End();
}

// Package DLLs auto-discover this on load. Macro must be inside the namespace.
REGISTER_EDITOR_WINDOW(NineSliceEditorWindow, "9-Slice Editor", "2D/9-Slice Editor")

} // namespace DekiEditor

#endif // DEKI_EDITOR
