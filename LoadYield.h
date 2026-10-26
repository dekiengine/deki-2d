#pragma once

#include <cstdint>

#include <deki/Time.h>

namespace Deki2D
{

// Lets the device's other tasks run (its idle task, which feeds the watchdog)
// during a long run of asset loads: once the run has gone on for kLoadYieldMs,
// not after every load, which cost a scene of many small sprites a tick each.
//
// loadStart is Deki::Time::GetTime() from before the load. A load that starts
// well after the last one ended begins a new run: the frame loop, which
// yields itself, ran in between.
inline void YieldDuringLoads(uint32_t loadStart)
{
    constexpr uint32_t kLoadYieldMs = 50;
    static uint32_t s_RunStart = 0;
    static uint32_t s_LastEnd = 0;
    if (loadStart - s_LastEnd > kLoadYieldMs)
    {
        s_RunStart = loadStart;
    }
    uint32_t now = Deki::Time::GetTime();
    if (now - s_RunStart >= kLoadYieldMs)
    {
        Deki::Time::Delay(1);
        now = Deki::Time::GetTime();
        s_RunStart = now;
    }
    s_LastEnd = now;
}

}  // namespace Deki2D
