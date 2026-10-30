// Editor support for RollerComponent: its display size, and its inspector.

#ifdef DEKI_EDITOR

#include <deki-editor/EditorRegistry.h>
#include <deki-editor/CustomEditor.h>
#include <deki-editor/EditorTheme.h>
#include <deki-editor/EditorUI.h>
#include <deki-editor/Localization.h>
#include "RollerComponent.h"
#include <deki/Engine.h>
#include <algorithm>
#include <cstdio>
#include <string>

// Editor extensions live in DekiEditor; the package's own types are in Deki2D.
using namespace Deki2D;

namespace DekiEditor
{

class RollerCustomEditor : public CustomEditor
{
public:
    const char* GetComponentName() const override { return "RollerComponent"; }

    // width and GetHeight() are world meters; gizmo consumers want pixels.
    bool GetDisplaySize(Deki::Component* comp, float& outWidth, float& outHeight) override
    {
        auto* roller = static_cast<RollerComponent*>(comp);
        if (!roller)
        {
            return false;
        }

        const float ppm = Deki::EngineSettings::Global().pixelsPerMeter;
        const float effective = ppm > 0.0f ? ppm : 1.0f;
        outWidth = roller->width * effective;
        outHeight = roller->GetHeight() * effective;
        return true;
    }

    bool HitTest(Deki::Component* comp, float localX, float localY, float width, float height) override
    {
        float halfW = width * 0.5f;
        float halfH = height * 0.5f;
        return (localX >= -halfW && localX <= halfW && localY >= -halfH && localY <= halfH);
    }

    // No resize gizmo: RollerComponent.width is a float (world meters), but
    // GetResizeTarget hands the gizmo int32_t* fields. Until the editor API
    // has a float variant, the roller is sized in the Inspector.

    bool WantsInspectorOverride(Deki::Component* comp) override { return true; }

    void OnInspectorGUI(Deki::Component* comp) override
    {
        auto* roller = static_cast<RollerComponent*>(comp);
        if (!roller)
        {
            return;
        }

        auto& ui = EditorUI::Get();

        ui.DrawDefaultInspector();

        ui.Spacing();
        if (ui.ActionButton(TrLabel("Fill Options...")))
        {
            ui.OpenPopup(TrLabel("Fill Options Numerically"));
            m_FillCount = 10;
            m_FillStart = 1;
            m_FillLeadingZeros = true;
        }

        // A dialog like the editor's others: dim labels over full-width
        // fields, the kit's buttons in the footer.
        if (BeginSchematicModal(TrLabel("Fill Options Numerically"), 360.0f))
        {
            const ImVec4 dim = Palette::Dim;
            ui.TextColored(dim.x, dim.y, dim.z, dim.w, Tr("Start"));
            ui.SetNextItemWidth(-1.0f);
            ui.DragInt("##fillStart", &m_FillStart, 0.2f, -99999, 99999);
            ui.TextColored(dim.x, dim.y, dim.z, dim.w, Tr("Count"));
            ui.SetNextItemWidth(-1.0f);
            ui.DragInt("##fillCount", &m_FillCount, 0.2f, 1, 9999);
            m_FillCount = (std::max)(1, (std::min)(9999, m_FillCount));
            ui.Checkbox(TrMark("Leading zeros"), &m_FillLeadingZeros);

            // Preview
            int lastVal = m_FillStart + m_FillCount - 1;
            int digits = static_cast<int>(std::to_string(lastVal).size());
            auto fmt = [&](int val) -> std::string
            {
                std::string s = std::to_string(val);
                if (m_FillLeadingZeros)
                {
                    while (static_cast<int>(s.size()) < digits)
                    {
                        s = "0" + s;
                    }
                }
                return s;
            };
            char previewBuf[256];
            std::snprintf(previewBuf, sizeof(previewBuf), Tr("Preview: %s, %s, ... %s"), fmt(m_FillStart).c_str(),
                          fmt(m_FillStart + 1).c_str(), fmt(lastVal).c_str());
            ui.TextColored(dim.x, dim.y, dim.z, dim.w, previewBuf);

            switch (SchematicDialogButtons({ { "Cancel" }, { "Fill", DialogButton::Primary } }))
            {
                case 1:
                    roller->options.clear();
                    for (int i = 0; i < m_FillCount; ++i)
                    {
                        roller->options.push_back(fmt(m_FillStart + i));
                    }
                    // Update the child text objects.
                    if (auto* owner = roller->GetOwner())
                    {
                        roller->SyncChildObjects(owner);
                    }
                    ui.CloseCurrentPopup();
                    break;
                case 0: ui.CloseCurrentPopup(); break;
                default: break;
            }
            EndSchematicModal();
        }
    }

private:
    int m_FillCount = 10;
    int m_FillStart = 1;
    bool m_FillLeadingZeros = true;
};

REGISTER_EDITOR(RollerCustomEditor)

}  // namespace DekiEditor

#endif  // DEKI_EDITOR
