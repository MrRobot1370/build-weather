#pragma once

// The heat ramp the map is coloured with.
//
// Single hue family, dark to hot, near-monotonic in perceived lightness so
// rank survives a greyscale screenshot and stays readable for anyone with a
// colour vision deficiency. Explicitly not a rainbow: a rainbow encodes no
// ordering, which is the one thing this scale has to do.
//
// These stops are duplicated in BW/UICore/Style.qml (heatStops) so QML
// legends and swatches match what the scene graph samples. Change both.

#include <QColor>

#include <algorithm>
#include <array>
#include <cmath>

namespace BW::UI
{

struct Rgb
{
    float r { 0.0f };
    float g { 0.0f };
    float b { 0.0f };
};

inline constexpr std::array<Rgb, 10> kHeatStops {
    Rgb { 0.063f, 0.078f, 0.110f }, // #10141C
    Rgb { 0.141f, 0.102f, 0.180f }, // #241A2E
    Rgb { 0.275f, 0.125f, 0.235f }, // #46203C
    Rgb { 0.431f, 0.141f, 0.212f }, // #6E2436
    Rgb { 0.608f, 0.208f, 0.153f }, // #9B3527
    Rgb { 0.773f, 0.329f, 0.086f }, // #C55416
    Rgb { 0.886f, 0.482f, 0.039f }, // #E27B0A
    Rgb { 0.953f, 0.659f, 0.118f }, // #F3A81E
    Rgb { 0.984f, 0.827f, 0.361f }, // #FBD35C
    Rgb { 0.992f, 0.953f, 0.722f } // #FDF3B8
};

/// Samples the ramp. `t` is clamped to [0, 1].
[[nodiscard]]
inline auto heat(float t) -> Rgb
{
    t = std::clamp(t, 0.0f, 1.0f);
    const float scaled = t * static_cast<float>(kHeatStops.size() - 1);
    const auto index = static_cast<std::size_t>(scaled);
    if (index >= kHeatStops.size() - 1) {
        return kHeatStops.back();
    }
    const float f = scaled - static_cast<float>(index);
    const Rgb &a = kHeatStops[index];
    const Rgb &b = kHeatStops[index + 1];
    return { a.r + (b.r - a.r) * f,
             a.g + (b.g - a.g) * f,
             a.b + (b.b - a.b) * f };
}

/// Position on the ramp for a duration.
///
/// Normalised against the reference maximum the caller supplies (the 98th
/// percentile, so one pathological file cannot flatten the whole map) rather
/// than an absolute time, so the scale means the same thing on any project.
///
/// The mapping is linear on purpose. Durations are heavily skewed and a
/// compressing curve does make the fast files more distinguishable, but it
/// also brightens the middle of the range until most of the map reads as
/// hot, and area is *already* proportional to duration. Linear keeps colour
/// and area saying the same thing.
[[nodiscard]]
inline auto heatPosition(double durationMs, double maxMs) -> float
{
    if (maxMs <= 0.0 || durationMs <= 0.0) {
        return 0.0f;
    }
    return static_cast<float>(std::clamp(durationMs / maxMs, 0.0, 1.0));
}

[[nodiscard]]
inline auto toQColor(const Rgb &c, float alpha = 1.0f) -> QColor
{
    return QColor::fromRgbF(c.r, c.g, c.b, alpha);
}

/// Diverging scale for the comparison view: red for a regression, green for
/// an improvement, neutral in the middle. `t` is -1 .. +1.
[[nodiscard]]
inline auto delta(float t) -> Rgb
{
    t = std::clamp(t, -1.0f, 1.0f);
    constexpr Rgb kNeutral { 0.114f, 0.129f, 0.161f };
    constexpr Rgb kWorse { 0.886f, 0.329f, 0.353f };
    constexpr Rgb kBetter { 0.247f, 0.725f, 0.498f };
    const Rgb &target = t >= 0.0f ? kWorse : kBetter;
    const float f = std::abs(t);
    return { kNeutral.r + (target.r - kNeutral.r) * f,
             kNeutral.g + (target.g - kNeutral.g) * f,
             kNeutral.b + (target.b - kNeutral.b) * f };
}

}
