#pragma once

#include <cstdint>

namespace Superset::Units
{
// Preserve Blood's fractional hits when attacks cross game boundaries.
// These are representation conversions, without balance multipliers.
using Health = int64_t;
constexpr Health HealthPerPoint = 16;
constexpr Health FromBloodHealth(int raw) { return raw; }
constexpr Health FromDukeHealth(int points) { return Health(points) * HealthPerPoint; }
constexpr double HealthPoints(Health value) { return double(value) / HealthPerPoint; }

// Both native adapters currently simulate at 30 Hz on a 120 Hz clock.
// Frame counters and asset-specific durations must be converted explicitly.
using Duration = int64_t;
constexpr Duration ClockRate = 120;
constexpr Duration SimulationRate = 30;
constexpr Duration ClocksPerStep = ClockRate / SimulationRate;
constexpr Duration FromSimulationSteps(int steps) { return Duration(steps) * ClocksPerStep; }
constexpr double Seconds(Duration clocks) { return double(clocks) / ClockRate; }
}
