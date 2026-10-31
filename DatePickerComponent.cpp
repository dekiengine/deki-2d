#include "DatePickerComponent.h"

#include <deki/Object.h>
#include "ClipComponent.h"
#include "RollerComponent.h"
#include "deki-input/InputCollider.h"
#include "deki-rendering/Fingerprint.h"
#include "WidgetShapes.h"
#include "WidgetText.h"

#include <algorithm>
#include <cstdio>
#include <string>

namespace Deki2D
{

namespace
{

const char* const kMonthNames[12] = {
    "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
};

// The chosen row is this much taller than the others.
constexpr float kChosenRowScale = 1.25f;

RollerComponent* RollerOf(Deki::Object* obj)
{
    return obj ? obj->GetComponent<RollerComponent>() : nullptr;
}

}  // namespace

bool DatePickerComponent::IsLeapYear(int32_t year)
{
    return (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
}

int32_t DatePickerComponent::DaysInMonth(int32_t year, int32_t month)
{
    static const int32_t kDays[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    month = std::clamp<int32_t>(month, 1, 12);
    return month == 2 && IsLeapYear(year) ? 29 : kDays[month - 1];
}

void DatePickerComponent::ClampDate()
{
    const int32_t lo = std::min(minYear, maxYear);
    const int32_t hi = std::max(minYear, maxYear);
    year = std::clamp(year, lo, hi);
    month = std::clamp<int32_t>(month, 1, 12);
    day = std::clamp<int32_t>(day, 1, DaysInMonth(year, month));
}

void DatePickerComponent::SetDate(int32_t newYear, int32_t newMonth, int32_t newDay)
{
    year = newYear;
    month = newMonth;
    day = newDay;
    ClampDate();
    SyncParts();
}

void DatePickerComponent::AddOnDateChangedCallback(const std::function<void(int32_t, int32_t, int32_t)>& callback)
{
    onDateChanged.push_back(callback);
}

void DatePickerComponent::Start()
{
    ClampDate();
    SyncParts();
    Listen();
}

void DatePickerComponent::Listen()
{
    if (m_Listening)
    {
        return;
    }
    RollerComponent* rollers[3] = { RollerOf(m_DayObj), RollerOf(m_MonthObj), RollerOf(m_YearObj) };
    for (RollerComponent* roller : rollers)
    {
        if (roller)
        {
            // Each roller reports here; the date picker owns their callbacks.
            roller->SetOnSelectionChanged(
                [this](int32_t index, const std::string& value)
                {
                    (void)index;
                    (void)value;
                    TakeFromRollers();
                });
            m_Listening = true;
        }
    }
}

void DatePickerComponent::TakeFromRollers()
{
    RollerComponent* dayRoller = RollerOf(m_DayObj);
    RollerComponent* monthRoller = RollerOf(m_MonthObj);
    RollerComponent* yearRoller = RollerOf(m_YearObj);
    const int32_t oldYear = year;
    const int32_t oldMonth = month;
    const int32_t oldDay = day;
    if (yearRoller)
    {
        year = std::min(minYear, maxYear) + yearRoller->GetSelectedIndex();
    }
    if (monthRoller)
    {
        month = monthRoller->GetSelectedIndex() + 1;
    }
    if (dayRoller)
    {
        day = dayRoller->GetSelectedIndex() + 1;
    }
    ClampDate();
    if (year == oldYear && month == oldMonth && day == oldDay)
    {
        return;
    }
    // A shorter month can drop the chosen day: the day roller follows.
    FillOptions();
    for (const auto& callback : onDateChanged)
    {
        callback(year, month, day);
    }
}

void DatePickerComponent::FillOptions()
{
    char text[16];
    const int32_t days = DaysInMonth(year, month);
    const int32_t yearsFrom = std::min(minYear, maxYear);
    const int32_t yearsTo = std::max(minYear, maxYear);
    const int32_t monthStyle = monthNames ? 1 : 0;

    // Building the option lists allocates, so it happens only when what they
    // hold changes: a month of another length, another year range.
    RollerComponent* dayRoller = RollerOf(m_DayObj);
    if (dayRoller && (days != m_DaysShown || static_cast<int32_t>(dayRoller->GetOptions().size()) != days))
    {
        std::vector<std::string> list;
        list.reserve(days);
        for (int32_t d = 1; d <= days; ++d)
        {
            std::snprintf(text, sizeof(text), "%02d", static_cast<int>(d));
            list.emplace_back(text);
        }
        dayRoller->SetOptions(list);
        m_DaysShown = days;
    }
    RollerComponent* monthRoller = RollerOf(m_MonthObj);
    if (monthRoller && (monthStyle != m_MonthStyle || monthRoller->GetOptions().size() != 12))
    {
        std::vector<std::string> list;
        list.reserve(12);
        for (int32_t m = 1; m <= 12; ++m)
        {
            std::snprintf(text, sizeof(text), "%02d", static_cast<int>(m));
            list.emplace_back(monthNames ? kMonthNames[m - 1] : text);
        }
        monthRoller->SetOptions(list);
        m_MonthStyle = monthStyle;
    }
    RollerComponent* yearRoller = RollerOf(m_YearObj);
    if (yearRoller && (yearsFrom != m_YearsFrom || yearsTo != m_YearsTo ||
                       static_cast<int32_t>(yearRoller->GetOptions().size()) != yearsTo - yearsFrom + 1))
    {
        std::vector<std::string> list;
        list.reserve(yearsTo - yearsFrom + 1);
        for (int32_t y = yearsFrom; y <= yearsTo; ++y)
        {
            std::snprintf(text, sizeof(text), "%d", static_cast<int>(y));
            list.emplace_back(text);
        }
        yearRoller->SetOptions(list);
        m_YearsFrom = yearsFrom;
        m_YearsTo = yearsTo;
    }

    // Each roller on the date, without its callback reaching back here.
    if (dayRoller && dayRoller->GetSelectedIndex() != day - 1)
    {
        dayRoller->selectedIndex = day - 1;
    }
    if (monthRoller && monthRoller->GetSelectedIndex() != month - 1)
    {
        monthRoller->selectedIndex = month - 1;
    }
    if (yearRoller && yearRoller->GetSelectedIndex() != year - yearsFrom)
    {
        yearRoller->selectedIndex = year - yearsFrom;
    }
}

void DatePickerComponent::OnPropertyChanged(const char* propertyName)
{
    (void)propertyName;
    ClampDate();
    m_LastLayout = 0;
    SyncParts();
}

void DatePickerComponent::RuntimeUpdate(float deltaTime)
{
    (void)deltaTime;
    SyncParts();
}

void DatePickerComponent::SyncParts()
{
    Deki::Object* owner = GetOwner();
    if (!owner)
    {
        return;
    }
    if (owner->GetChildren().size() != m_KnownChildren)
    {
        EnsureParts();
        m_KnownChildren = owner->GetChildren().size();
        m_LastLayout = 0;
    }
    DekiRendering::Fingerprint f;
    f.Add(year);
    f.Add(month);
    f.Add(day);
    f.Add(minYear);
    f.Add(maxYear);
    f.Add(static_cast<uint32_t>(order));
    f.Add(static_cast<uint32_t>(monthNames));
    f.Add(width);
    f.Add(rowHeight);
    if (f.Value() == m_LastLayout)
    {
        return;
    }
    m_LastLayout = f.Value();
    FillOptions();
    LayoutParts();
}

void DatePickerComponent::EnsureParts()
{
    Deki::Object* owner = GetOwner();
    m_Back = WidgetParts::ShapePart(owner, "Back", ShapeKind::Rectangle, WidgetColors::Panel, 0.5f);
    m_Band = WidgetParts::ShapePart(owner, "Band", ShapeKind::Rectangle, WidgetColors::Track, 0.4f);

    Deki::Object** slots[3] = { &m_DayObj, &m_MonthObj, &m_YearObj };
    const char* names[3] = { "Day", "Month", "Year" };
    for (int i = 0; i < 3; ++i)
    {
        Deki::Object* obj = WidgetParts::Find(owner, names[i]);
        *slots[i] = obj;
#ifdef DEKI_EDITOR
        if (obj)
        {
            continue;
        }
        // A new roller, made by its registered name (RollerComponent's two
        // Update overloads keep AddComponent<T> from naming it): its hit
        // area, its rows, and text in the scene's font.
        obj = WidgetParts::MakeChild(owner, names[i]);
        *slots[i] = obj;
        Deki::Internal::AddComponentByName(*obj, "Deki2D::RollerComponent");
        RollerComponent* roller = RollerOf(obj);
        if (!roller)
        {
            continue;
        }
        obj->AddComponent<DekiInput::InputCollider>();
        roller->inputCollider.Set(obj);
        roller->visibleRows = 3;
        roller->infiniteScroll = i != 2;  // days and months come round; years do not
        roller->selectedColor = WidgetColors::Text;
        roller->normalColor = WidgetColors::DimText;
        // A year must fit a third of the picker.
        roller->selectedFontSize = 18;
        roller->normalFontSize = 14;
        roller->EnsureChildObjects(obj);
        TextComponent* fonted = nullptr;
        for (Deki::Object* up = owner; up && !fonted; up = up->GetParent())
        {
            fonted = WidgetParts::FindFontedText(up, obj);
        }
        for (int32_t row = 0; row < roller->GetTextComponentCount(); ++row)
        {
            TextComponent* text = roller->GetTextComponent(row);
            if (text && fonted)
            {
                text->font = fonted->font;
            }
        }
        Deki::Object* ring = WidgetParts::ShapePart(obj, "Focus", ShapeKind::Rectangle, WidgetColors::Focus, 0.4f);
        if (ring)
        {
            ring->SetActive(false);
        }
#endif
    }
    m_DaysShown = 0;
    m_MonthStyle = -1;
    m_YearsTo = -1;
    if (HasStarted())
    {
        Listen();
    }
}

void DatePickerComponent::LayoutParts()
{
    const float chosen = rowHeight * kChosenRowScale;
    const float height = rowHeight * 2.0f + chosen;
    WidgetParts::PlaceShape(m_Back, 0.0f, 0.0f, width, height);
    WidgetParts::PlaceShape(m_Band, 0.0f, 0.0f, width - rowHeight * 0.3f, chosen);

    // Column shares: a month by name needs more room than a number.
    const float dayShare = 0.28f;
    const float monthShare = monthNames ? 0.36f : 0.28f;
    const float yearShare = 0.36f;
    Deki::Object* columns[3] = { m_DayObj, m_MonthObj, m_YearObj };
    float shares[3] = { dayShare, monthShare, yearShare };
    int32_t sequence[3] = { 0, 1, 2 };
    if (order == DateOrder::MonthDayYear)
    {
        sequence[0] = 1;
        sequence[1] = 0;
    }
    else if (order == DateOrder::YearMonthDay)
    {
        sequence[0] = 2;
        sequence[2] = 0;
    }
    const float total = dayShare + monthShare + yearShare;
    float x = -width * 0.5f;
    for (int32_t slot = 0; slot < 3; ++slot)
    {
        const int32_t which = sequence[slot];
        const float columnWidth = width * shares[which] / total;
        Deki::Object* obj = columns[which];
        RollerComponent* roller = RollerOf(obj);
        if (obj && roller)
        {
            obj->SetLocalPosition(x + columnWidth * 0.5f, 0.0f);
            roller->width = columnWidth;
            roller->itemHeight = rowHeight;
            roller->selectedItemHeight = chosen;
            if (auto* collider = obj->GetComponent<DekiInput::InputCollider>())
            {
                collider->width = columnWidth;
                collider->height = height;
            }
            if (Deki::Object* clipObj = WidgetParts::Find(obj, "Clip"))
            {
                if (auto* clip = clipObj->GetComponent<ClipComponent>())
                {
                    clip->width = columnWidth;
                    clip->height = height;
                }
            }
            Deki::Object* ring = WidgetParts::Find(obj, "Focus");
            WidgetParts::PlaceShape(ring, 0.0f, 0.0f, columnWidth, height);
            if (ShapeComponent* shape = WidgetParts::Shape(ring))
            {
                shape->lineWidth = rowHeight * 0.08f;
            }
            roller->SyncChildObjects(obj);
        }
        x += columnWidth;
    }
}

}  // namespace Deki2D
