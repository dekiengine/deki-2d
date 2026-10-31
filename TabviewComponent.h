#pragma once

#include <stdint.h>
#include <functional>
#include <string>
#include <vector>

#include <deki/Component.h>
#include <deki/reflection/Property.h>
#include "FocusSystem.h"

namespace Deki
{
class Object;
}

namespace Deki2D
{

/// A row of tabs over a set of pages; one page shows at a time: settings
/// sections, an inventory by category. A tap on a tab shows its page, and
/// so do Left and Right while the tab bar has keyboard focus.
///
/// Its parts are children the editor makes: Bar (with the hit area),
/// Indicator, Tab0, Tab1, ... and Focus, and an empty Page0, Page1, ... for
/// each tab to hold that page's objects.
DEKI_CATEGORY("2D")
DEKI_DESCRIPTION("A row of tabs over pages, one page showing at a time.")
class TabviewComponent : public Deki::Component, public IFocusable
{
public:
    DEKI_EXPORT
    DEKI_TOOLTIP("The tab titles, in order. Each has a page, Page0, Page1 and so on.")
    std::vector<std::string> tabs = { "One", "Two", "Three" };

    DEKI_EXPORT
    DEKI_TOOLTIP("Which tab shows, counting from 0.")
    int32_t selectedTab = 0;

    DEKI_EXPORT
    DEKI_TOOLTIP("A disabled tab view ignores taps and keys.")
    bool isEnabled = true;

    DEKI_EXPORT
    DEKI_TOOLTIP("Width of the tab view in meters.")
    DEKI_UNIT(Distance)
    float width = 16.0f;

    DEKI_EXPORT
    DEKI_TOOLTIP("Height of the tab view, the tab bar included, in meters. The pages fill the rest.")
    DEKI_UNIT(Distance)
    float height = 10.0f;

    DEKI_EXPORT
    DEKI_TOOLTIP("Height of the tab bar in meters.")
    DEKI_UNIT(Distance)
    float barHeight = 1.75f;

    DEKI_EXPORT
    DEKI_TOOLTIP("How fast the line under the tabs slides to a new tab. 0 jumps.")
    DEKI_RANGE(0, 60)
    float glideSpeed = 14.0f;

    // Runtime-only listeners
    std::vector<std::function<void(int32_t tab)>> onTabChanged;

    TabviewComponent() = default;
    ~TabviewComponent() override;

    /// Shows tab `index`. With `notify`, the tab callbacks run.
    void SelectTab(int32_t index, bool notify = true);

    void AddOnTabChangedCallback(const std::function<void(int32_t)>& callback);

    /// The page object of tab `index`, or nullptr.
    Deki::Object* GetPage(int32_t index) const;

    /// Places and sizes the parts, making any the editor is missing.
    void SyncParts();

    void Start() override;
    bool NeedsRuntimeUpdate() const override { return true; }
    void RuntimeUpdate(float deltaTime) override;
    void OnPropertyChanged(const char* propertyName) override;

    // IFocusable: the tab bar is what takes focus.
    Deki::Object* GetFocusObject() const override;
    bool CanFocus() const override { return isEnabled && !tabs.empty(); }
    void GetFocusSize(float& w, float& h) const override
    {
        w = width;
        h = barHeight;
    }
    void OnFocusChanged(bool focused) override;
    bool OnFocusKey(uint32_t key, uint32_t character) override;

private:
    Deki::Object* m_Bar = nullptr;
    Deki::Object* m_Indicator = nullptr;
    Deki::Object* m_Focus = nullptr;
    std::vector<Deki::Object*> m_TabParts;
    std::vector<Deki::Object*> m_Pages;
    size_t m_KnownChildren = static_cast<size_t>(-1);
    uint64_t m_LastLayout = 0;
    float m_Shown = -1.0f;  // the indicator's tab, gliding; below 0 until placed
    bool m_Focused = false;

    void EnsureParts();
    void LayoutParts();
};

}  // namespace Deki2D
