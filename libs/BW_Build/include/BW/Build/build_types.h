#pragma once

// Shared value types for the build data sources. Everything here is plain
// C++: no Qt, no I/O, so the parsers on top of it stay unit-testable.

#include "BW/Core/path_utils.h"

#include <cstdint>
#include <string>
#include <vector>

namespace BW::Build
{

using Millis = std::int64_t;
using Micros = std::int64_t;

/// One entry of a `.ninja_log`, exactly as recorded.
struct TargetRecord
{
    std::string output; ///< normalized, still relative to the build dir
    Millis startMs { 0 };
    Millis endMs { 0 };
    std::uint64_t mtime { 0 };
    std::uint64_t commandHash { 0 };

    [[nodiscard]]
    auto durationMs() const -> Millis
    {
        return endMs > startMs ? endMs - startMs : 0;
    }
};

struct ParseDiagnostic
{
    enum class Severity
    {
        Warning,
        Error
    };

    Severity severity { Severity::Warning };
    std::size_t line { 0 }; ///< 1-based; 0 when not line-oriented
    std::string message;
};

/// What kind of work a build step did. Derived from the ninja rule
/// description, so it is a hint for colouring and filtering, never a fact the
/// timing numbers depend on.
enum class StepKind
{
    Compile,
    Link,
    Generate,
    Other
};

[[nodiscard]]
auto stepKindName(StepKind kind) -> const char *;

/// A start or finish moment, used for the live view and for replay.
struct BuildEvent
{
    enum class Type
    {
        Start,
        Finish
    };

    Millis timeMs { 0 };
    int targetIndex { -1 }; ///< index into BuildSnapshot::targets()
    Type type { Type::Start };
};

}
