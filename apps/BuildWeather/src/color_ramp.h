#pragma once

// The heat ramp the map is coloured with.
//
// Single hue family, near-monotonic in perceived lightness so rank survives a
// greyscale screenshot and stays readable for anyone with a colour vision
// deficiency. Explicitly not a rainbow: a rainbow encodes no ordering, which
// is the one thing this scale has to do.
//
// There are two ramps because there are two backgrounds. On the dark theme
// cold is near-black and hot is pale, so hot cells glow. Reusing that on a
// light background would be backwards: the hottest files would be the ones
// closest to the page and would disappear. The light ramp therefore runs the
// other way in lightness, pale for cheap files and deep red for expensive
// ones, so in both themes "expensive" is the value furthest from the
// background.
//
// Both sets are duplicated in BW/UICore/Style.qml (heatStops) so QML legends
// and swatches match what the scene graph samples. Change both.

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

inline constexpr std::array<Rgb, 10> kHeatStopsDark {
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

inline constexpr std::array<Rgb, 10> kHeatStopsLight {
    Rgb { 0.992f, 0.976f, 0.910f }, // #FDF9E8
    Rgb { 0.976f, 0.906f, 0.706f }, // #F9E7B4
    Rgb { 0.957f, 0.804f, 0.483f }, // #F4CD7B
    Rgb { 0.937f, 0.671f, 0.302f }, // #EFAB4D
    Rgb { 0.902f, 0.522f, 0.180f }, // #E6852E
    Rgb { 0.827f, 0.384f, 0.126f }, // #D3621F
    Rgb { 0.706f, 0.255f, 0.098f }, // #B44119
    Rgb { 0.561f, 0.176f, 0.098f }, // #8F2D19
    Rgb { 0.420f, 0.118f, 0.094f }, // #6B1E18
    Rgb { 0.290f, 0.075f, 0.067f } // #4A1311
};

/// Samples the ramp for the active theme. `t` is clamped to [0, 1].
[[nodiscard]]
inline auto heat(float t, bool dark = true) -> Rgb
{
    const auto &stops = dark ? kHeatStopsDark : kHeatStopsLight;
    t = std::clamp(t, 0.0f, 1.0f);
    const float scaled = t * static_cast<float>(stops.size() - 1);
    const auto index = static_cast<std::size_t>(scaled);
    if (index >= stops.size() - 1) {
        return stops.back();
    }
    const float f = scaled - static_cast<float>(index);
    const Rgb &a = stops[index];
    const Rgb &b = stops[index + 1];
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
/// an improvement, neutral in the middle. `t` is -1 .. +1. The neutral end
/// follows the theme so an unchanged file recedes into the background instead
/// of sitting on it as a dark blot.
[[nodiscard]]
inline auto delta(float t, bool dark = true) -> Rgb
{
    t = std::clamp(t, -1.0f, 1.0f);
    const Rgb neutral = dark ? Rgb { 0.114f, 0.129f, 0.161f }
                             : Rgb { 0.902f, 0.914f, 0.933f };
    const Rgb worse = dark ? Rgb { 0.886f, 0.329f, 0.353f }
                           : Rgb { 0.769f, 0.196f, 0.227f };
    const Rgb better = dark ? Rgb { 0.247f, 0.725f, 0.498f }
                            : Rgb { 0.122f, 0.545f, 0.341f };
    const Rgb &target = t >= 0.0f ? worse : better;
    const float f = std::abs(t);
    return { neutral.r + (target.r - neutral.r) * f,
             neutral.g + (target.g - neutral.g) * f,
             neutral.b + (target.b - neutral.b) * f };
}

/// The chrome the map draws over its cells, per theme: directory outlines,
/// leaf and directory label ink, the not-yet-built fill and the completion
/// flash. Kept here next to the ramp so the two cannot drift apart.
struct MapPalette
{
    QColor directoryOutline;
    QColor directoryLabel;
    QColor leafLabel;
    QColor pending;
    QColor unknown;
    QColor flash;
    QColor hover;
    QColor selection;
};

/// Ink for a label drawn on top of `fill`.
///
/// Picked from the cell's own brightness, not from the theme. Both ramps span
/// nearly the full lightness range, so a single ink colour is guaranteed to be
/// invisible at one end: dark ink vanishes on the deep red of an expensive
/// file in light mode, and equally on the near-black of a cheap one in dark
/// mode. Relative luminance with the usual coefficients, thresholded at the
/// midpoint.
[[nodiscard]]
inline auto labelInkFor(const QColor &fill) -> QColor
{
    const double luminance = 0.2126 * fill.redF() + 0.7152 * fill.greenF()
        + 0.0722 * fill.blueF();
    return luminance > 0.45 ? QColor { 26, 22, 18, 230 }
                            : QColor { 244, 240, 232, 230 };
}

[[nodiscard]]
inline auto mapPalette(bool dark) -> MapPalette
{
    if (dark) {
        return { QColor { 128, 146, 170 },
                 QColor { 168, 180, 196 },
                 QColor { 12, 14, 18, 210 },
                 QColor { 22, 26, 33 },
                 QColor { 30, 34, 42 },
                 QColor { 255, 250, 228 },
                 QColor { 236, 242, 250, 235 },
                 QColor { 242, 160, 61 } };
    }
    return { QColor { 96, 108, 126 },
             QColor { 58, 68, 84 },
             QColor { 28, 24, 20, 225 },
             QColor { 222, 226, 233 },
             QColor { 208, 213, 221 },
             QColor { 90, 20, 12 },
             QColor { 24, 32, 44, 235 },
             QColor { 176, 92, 8 } };
}

}
