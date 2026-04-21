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

    enum class DragHandle { None = 0, Left = 1, Right = 2, Top = 3, Bottom = 4 };

    void LoadFromDisk();
    void SaveToDisk();
    void PushUndoSnapshot(const Borders& previous);
    bool DrawCanvasAndHandles();      // returns true if handles are being dragged
    void HandleKeyboardShortcuts();
    void DrawSavePromptModalIfNeeded();

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
    float        m_DragStartMouse = 0.0f;
    int32_t      m_DragStartValue = 0;
    Borders      m_BordersAtDragStart;

    // View
    float        m_Zoom = 4.0f;
    float        m_PanX = 0.0f;
    float        m_PanY = 0.0f;

    // Modal state
    bool         m_OpenSavePromptNextFrame = false;
};

} // namespace DekiEditor

#endif // DEKI_EDITOR
