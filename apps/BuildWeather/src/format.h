#pragma once

// Duration formatting shared by the tables, the readouts and the tooltip, so
// the same number never renders two different ways in two places.

#include <QString>

#include <cmath>

namespace BW::UI
{

[[nodiscard]]
inline auto formatMs(qint64 ms) -> QString
{
    if (ms < 0) {
        return QStringLiteral("-") + formatMs(-ms);
    }
    if (ms < 1000) {
        return QString::number(ms) + QStringLiteral(" ms");
    }
    if (ms < 60000) {
        return QString::number(ms / 1000.0, 'f', ms < 10000 ? 2 : 1)
            + QStringLiteral(" s");
    }
    const qint64 minutes = ms / 60000;
    const double seconds = static_cast<double>(ms % 60000) / 1000.0;
    return QStringLiteral("%1m %2s")
        .arg(minutes)
        .arg(seconds, 0, 'f', 1);
}

[[nodiscard]]
inline auto formatUs(qint64 us) -> QString
{
    return formatMs(static_cast<qint64>(std::llround(
        static_cast<double>(us) / 1000.0)));
}

/// Signed form used in the comparison view: always carries its sign so a
/// column of deltas is scannable.
[[nodiscard]]
inline auto formatDeltaMs(qint64 ms) -> QString
{
    if (ms == 0) {
        return QStringLiteral("0");
    }
    return (ms > 0 ? QStringLiteral("+") : QStringLiteral("-"))
        + formatMs(std::abs(ms));
}

}
