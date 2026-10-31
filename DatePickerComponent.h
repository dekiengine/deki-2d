#pragma once

#include <stdint.h>
#include <functional>
#include <vector>

#include <deki/Component.h>
#include <deki/reflection/Property.h>

namespace Deki
{
class Object;
}

namespace Deki2D
{
class RollerComponent;

/// The order of the date picker's columns.
enum class DateOrder : uint8_t
{
    DayMonthYear = 0,
    MonthDayYear = 1,
    YearMonthDay = 2
};

/// A date picker: three rollers for day, month and year, like a phone's.
/// The day roller always holds the days the chosen month has, leap years
/// included. Each roller takes keyboard focus on its own: Left and Right
/// move between them, Up and Down roll them.
///
/// Its parts are children the editor makes: Back, Band (behind the chosen
/// row), and the rollers Day, Month and Year.
DEKI_CATEGORY("2D")
DEKI_DESCRIPTION("A date picker: rollers for day, month and year.")
class DatePickerComponent : public Deki::Component
{
public:
    DEKI_EXPORT
    DEKI_TOOLTIP("The chosen year.")
    int32_t year = 2026;

    DEKI_EXPORT
    DEKI_TOOLTIP("The chosen month, 1 to 12.")
    DEKI_RANGE(1, 12)
    int32_t month = 1;

    DEKI_EXPORT
    DEKI_TOOLTIP("The chosen day of the month, from 1.")
    DEKI_RANGE(1, 31)
    int32_t day = 1;

    DEKI_EXPORT
    DEKI_TOOLTIP("The earliest year on offer.")
    int32_t minYear = 2000;

    DEKI_EXPORT
    DEKI_TOOLTIP("The latest year on offer.")
    int32_t maxYear = 2050;

    DEKI_EXPORT
    DEKI_TOOLTIP("The order of the columns, as dates are written where the game is played.")
    DateOrder order = DateOrder::DayMonthYear;

    DEKI_EXPORT
    DEKI_TOOLTIP("Show months by name (Jan, Feb, ...) instead of by number.")
    bool monthNames = true;

    DEKI_EXPORT
    DEKI_TOOLTIP("Width of the date picker in meters.")
    DEKI_UNIT(Distance)
    float width = 11.0f;

    DEKI_EXPORT
    DEKI_TOOLTIP("Height of one row in meters. The chosen row is a little taller.")
    DEKI_UNIT(Distance)
    float rowHeight = 1.5f;

    // Runtime-only listeners
    std::vector<std::function<void(int32_t year, int32_t month, int32_t day)>> onDateChanged;

    DatePickerComponent() = default;
    ~DatePickerComponent() override = default;

    /// Sets the date, kept to a real day in the year range.
    void SetDate(int32_t newYear, int32_t newMonth, int32_t newDay);

    void AddOnDateChangedCallback(const std::function<void(int32_t, int32_t, int32_t)>& callback);

    static bool IsLeapYear(int32_t year);

    /// Days in `month` (1 to 12) of `year`.
    static int32_t DaysInMonth(int32_t year, int32_t month);

    /// Places and sizes the parts, making any the editor is missing.
    void SyncParts();

    void Start() override;
    bool NeedsRuntimeUpdate() const override { return true; }
    void RuntimeUpdate(float deltaTime) override;
    void OnPropertyChanged(const char* propertyName) override;

private:
    Deki::Object* m_Back = nullptr;
    Deki::Object* m_Band = nullptr;
    Deki::Object* m_DayObj = nullptr;
    Deki::Object* m_MonthObj = nullptr;
    Deki::Object* m_YearObj = nullptr;
    size_t m_KnownChildren = static_cast<size_t>(-1);
    uint64_t m_LastLayout = 0;
    // What the rollers hold, so their options are rebuilt only on a change.
    int32_t m_DaysShown = 0;
    int32_t m_YearsFrom = 0;
    int32_t m_YearsTo = -1;
    int32_t m_MonthStyle = -1;
    bool m_Listening = false;

    void EnsureParts();
    void LayoutParts();
    void FillOptions();
    void Listen();
    void ClampDate();
    // Reads the date from the rollers after one of them moved.
    void TakeFromRollers();
};

}  // namespace Deki2D
