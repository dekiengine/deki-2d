/**
 * @file RollerCustomEditor.cpp
 * @brief Editor support for RollerComponent
 *
 * Provides display size info and resize support for the editor.
 */

#ifdef DEKI_EDITOR

#include <deki-editor/EditorRegistry.h>
#include <deki-editor/CustomEditor.h>
#include <deki-editor/EditorUI.h>
#include "RollerComponent.h"
#include <deki/Engine.h>
#include <string>
#include <cstdio>

namespace DekiEditor
{

class RollerCustomEditor : public CustomEditor
{
public:
    const char* GetComponentName() const override
    {
        return "RollerComponent";
    }

    // width and GetHeight() are world meters; gizmo consumers want pixels.
    bool GetDisplaySize(Deki::Component* comp, float& outWidth, float& outHeight) override
    {
        auto* roller = static_cast<RollerComponent*>(comp);
        if (!roller)
            return false;

        const float ppm = Deki::EngineSettings::Global().pixelsPerMeter;
        const float effective = ppm > 0.0f ? ppm : 1.0f;
        outWidth  = roller->width       * effective;
        outHeight = roller->GetHeight() * effective;
        return true;
    }

    bool HitTest(Deki::Component* comp, float localX, float localY, float width, float height) override
    {
        float halfW = width * 0.5f;
        float halfH = height * 0.5f;
        return (localX >= -halfW && localX <= halfW && localY >= -halfH && localY <= halfH);
    }

    // Resize gizmo not exposed: RollerComponent.width is now float
    // (world meters), but GetResizeTarget hands the gizmo int32_t* fields.
    // Until the editor API gains a float variant, the roller is sized via
    // the inspector instead.

    bool WantsInspectorOverride(Deki::Component* comp) override
    {
        return true;
    }

    void OnInspectorGUI(Deki::Component* comp) override
    {
        auto* roller = static_cast<RollerComponent*>(comp);
        if (!roller) return;

        auto& ui = EditorUI::Get();

        ui.DrawDefaultInspector();

        ui.Separator();
        if (ui.Button("Fill Options...", -1.0f))
        {
            ui.OpenPopup("FillRollerOptions");
            m_FillCount = 10;
            m_FillStart = 1;
            m_FillLeadingZeros = true;
        }

        if (ui.BeginPopup("FillRollerOptions"))
        {
            ui.Text("Fill Options Numerically");
            ui.Separator();

            ui.InputInt("Start", &m_FillStart);
            ui.InputInt("Count", &m_FillCount);
            if (m_FillCount < 1) m_FillCount = 1;
            if (m_FillCount > 9999) m_FillCount = 9999;
            ui.Checkbox("Leading zeros", &m_FillLeadingZeros);

            // Preview
            int lastVal = m_FillStart + m_FillCount - 1;
            int digits = static_cast<int>(std::to_string(lastVal).size());
            auto fmt = [&](int val) -> std::string {
                std::string s = std::to_string(val);
                if (m_FillLeadingZeros)
                    while (static_cast<int>(s.size()) < digits) s = "0" + s;
                return s;
            };
            char previewBuf[256];
            std::snprintf(previewBuf, sizeof(previewBuf), "Preview: %s, %s, ... %s",
                fmt(m_FillStart).c_str(),
                fmt(m_FillStart + 1).c_str(),
                fmt(lastVal).c_str());
            ui.TextDisabled(previewBuf);

            ui.Separator();
            if (ui.Button("Fill", 120, 0))
            {
                roller->options.clear();
                for (int i = 0; i < m_FillCount; ++i)
                    roller->options.push_back(fmt(m_FillStart + i));
                // Sync child text objects
                if (auto* owner = roller->GetOwner())
                    roller->SyncChildObjects(owner);
                ui.CloseCurrentPopup();
            }
            ui.SameLine();
            if (ui.Button("Cancel", 120, 0))
                ui.CloseCurrentPopup();

            ui.EndPopup();
        }
    }

private:
    int m_FillCount = 10;
    int m_FillStart = 1;
    bool m_FillLeadingZeros = true;
};

REGISTER_EDITOR(RollerCustomEditor)

} // namespace DekiEditor

#endif // DEKI_EDITOR
