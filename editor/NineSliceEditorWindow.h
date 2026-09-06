#pragma once

#ifdef DEKI_EDITOR

#include <deki-editor/EditorWindow.h>
#include <cstdint>
#include <string>
#include <vector>

namespace DekiEditor
{

/**
 * @brief Dockable editor window for visually editing 9-slice borders.
 *
 * Opens for procedural sprites (.asset with type=ProceduralSprite) and normal
 * sprites (.png with optional .png.data sidecar). Shows the sprite at a
 * comfortable zoom with 4 draggable guide lines (top/right/bottom/left).
 *
 * Edits live in-window and are committed to disk when the user clicks Save.
 * Undo/redo (Ctrl+Z / Ctrl+Y) work on the in-window edit history; Save clears
 * the history and becomes the new baseline.
 */
class NineSliceEditorWindow : public EditorWindow
{
public:
    const char* GetTitle()    override { return "9-Slice Editor"; }
    const char* GetMenuPath() override { return "2D/9-Slice Editor"; }

    bool CanOpenFile(const char* extension) override;
    bool CanOpenAssetType(const char* assetType) override;

    void OnOpen() override;
    void OnClose() override;
    void OpenFile(const char* filePath, const char* cachePath) override;
    void OnGUI() override;

private:
    enum class Source { None, ProceduralAsset, NormalSprite };

    struct Borders
    {
        int32_t top = 0, right = 0, bottom = 0, left = 0;
        bool operator==(const Borders& o) const
        {
            return top == o.top && right == o.right && bottom == o.bottom && left == o.left;
        }
        bool operator!=(const Borders& o) const { return !(*this == o); }
    };

    // Edge guides plus their intersections (corners drag two borders at once).
    enum class DragHandle
    {
        None = 0,
        Left, Right, Top, Bottom,
        TopLeft, TopRight, BottomLeft, BottomRight
    };

    void LoadFromDisk();
    void SaveToDisk();
    void PushUndoSnapshot(const Borders& previous);
    void Undo();
    void Redo();
    bool DrawCanvasAndHandles();      // returns true if handles are being dragged
    void HandleKeyboardShortcuts();
    void DrawSavePromptModalIfNeeded();

    static bool AffectsLeft(DragHandle h)   { return h == DragHandle::Left   || h == DragHandle::TopLeft    || h == DragHandle::BottomLeft; }
    static bool AffectsRight(DragHandle h)  { return h == DragHandle::Right  || h == DragHandle::TopRight   || h == DragHandle::BottomRight; }
    static bool AffectsTop(DragHandle h)    { return h == DragHandle::Top    || h == DragHandle::TopLeft    || h == DragHandle::TopRight; }
    static bool AffectsBottom(DragHandle h) { return h == DragHandle::Bottom || h == DragHandle::BottomLeft || h == DragHandle::BottomRight; }

    Source       m_Source = Source::None;
    std::string  m_ProjectPath;
    std::string  m_AssetPath;          // .asset (procedural) or .png (normal)
    std::string  m_CachePath;          // baked .dtex (optional)
    std::string  m_AssetGuid;
    std::string  m_DisplayName;        // basename for title bar

    Borders      m_Current;            // values being edited
    Borders      m_Saved;              // last value persisted to disk

    std::vector<Borders> m_UndoStack;
    std::vector<Borders> m_RedoStack;

    // Live drag state
    DragHandle   m_ActiveDrag = DragHandle::None;
    float        m_DragStartMouseX = 0.0f;
    float        m_DragStartMouseY = 0.0f;
    Borders      m_BordersAtDragStart;
    bool         m_PanningLMB = false;  // left-drag on empty canvas pans

    // Numeric-field undo bookkeeping: snapshot of the values before the
    // current field edit session began (drags span many frames)
    Borders      m_FieldsSnapshot;
    bool         m_FieldEditActive = false;

    // View
    float        m_Zoom = 4.0f;
    float        m_PanX = 0.0f;
    float        m_PanY = 0.0f;
    bool         m_FitViewPending = false;  // auto-fit zoom on next canvas draw
    uint32_t     m_TexW = 0, m_TexH = 0;    // last-loaded texture dims (for info row)

    // Modal state
    bool         m_OpenSavePromptNextFrame = false;
};

} // namespace DekiEditor

#endif // DEKI_EDITOR
