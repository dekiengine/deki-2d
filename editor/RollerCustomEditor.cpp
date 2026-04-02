/**
 * @file RollerCustomEditor.cpp
 * @brief Editor support for RollerComponent
 *
 * Provides display size info and resize support for the editor.
 */

#ifdef DEKI_EDITOR

#include <deki-editor/EditorRegistry.h>
#include <deki-editor/CustomEditor.h>
#include <deki-editor/EditorGUI.h>
#include "modules/2d/RollerComponent.h"
#include "imgui.h"
#include <string>

namespace DekiEditor
{

class RollerCustomEditor : public CustomEditor
{
public:
    const char* GetComponentName() const override
    {
        return "RollerComponent";
    }

    bool GetDisplaySize(DekiComponent* comp, float& outWidth, float& outHeight) override
    {
        auto* roller = static_cast<RollerComponent*>(comp);
        if (!roller)
            return false;

        outWidth = static_cast<float>(roller->width);
        outHeight = static_cast<float>(roller->GetHeight());

        return true;
    }

    bool HitTest(DekiComponent* comp, float localX, float localY, float width, float height) override
    {
        float halfW = width * 0.5f;
        float halfH = height * 0.5f;
        return (localX >= -halfW && localX <= halfW && localY >= -halfH && localY <= halfH);
    }

    bool SupportsResize() const override
    {
        return true;
    }

    void GetResizeTarget(DekiComponent* comp, int32_t** outWidth, int32_t** outHeight) override
    {
        auto* roller = static_cast<RollerComponent*>(comp);
        if (roller)
        {
            if (outWidth) *outWidth = &roller->width;
            if (outHeight) *outHeight = nullptr;
        }
    }

    bool WantsInspectorOverride(DekiComponent* comp) override
    {
        return true;
    }

    void OnInspectorGUI(DekiComponent* comp) override
    {
        auto* roller = static_cast<RollerComponent*>(comp);
        if (!roller) return;

        EditorGUI::Get().DrawDefaultInspector();

        ImGui::Separator();
        if (ImGui::Button("Fill Options...", ImVec2(-1, 0)))
        {
            ImGui::OpenPopup("FillRollerOptions");
            m_FillCount = 10;
            m_FillStart = 1;
            m_FillLeadingZeros = true;
        }

        if (ImGui::BeginPopup("FillRollerOptions"))
        {
            ImGui::Text("Fill Options Numerically");
            ImGui::Separator();

            ImGui::InputInt("Start", &m_FillStart);
            ImGui::InputInt("Count", &m_FillCount);
            if (m_FillCount < 1) m_FillCount = 1;
            if (m_FillCount > 9999) m_FillCount = 9999;
            ImGui::Checkbox("Leading zeros", &m_FillLeadingZeros);

            // Preview
            int lastVal = m_FillStart + m_FillCount - 1;
            int digits = static_cast<int>(std::to_string(lastVal).size());
            auto fmt = [&](int val) -> std::string {
                std::string s = std::to_string(val);
                if (m_FillLeadingZeros)
                    while (static_cast<int>(s.size()) < digits) s = "0" + s;
                return s;
            };
            ImGui::TextDisabled("Preview: %s, %s, ... %s",
                fmt(m_FillStart).c_str(),
                fmt(m_FillStart + 1).c_str(),
                fmt(lastVal).c_str());

            ImGui::Separator();
            if (ImGui::Button("Fill", ImVec2(120, 0)))
            {
                roller->options.clear();
                for (int i = 0; i < m_FillCount; ++i)
                    roller->options.push_back(fmt(m_FillStart + i));
                // Sync child text objects
                if (auto* owner = roller->GetOwner())
                    roller->SyncChildObjects(owner);
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(120, 0)))
                ImGui::CloseCurrentPopup();

            ImGui::EndPopup();
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
