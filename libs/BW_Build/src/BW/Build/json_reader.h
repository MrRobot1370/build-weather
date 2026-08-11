#pragma once

// A minimal, allocation-light JSON pull parser. A -ftime-trace set is
// hundreds of megabytes and only a handful of fields per event are wanted, so
// a DOM is the wrong shape: this walks the text once and copies nothing it
// was not asked for. Private to BW_Build, not installed.

#include <charconv>
#include <cstdint>
#include <string>
#include <string_view>

namespace BW::Build::Json
{

class Reader
{
public:
    explicit Reader(std::string_view text)
        : m_text { text }
    {
    }

    [[nodiscard]]
    auto position() const -> std::size_t
    {
        return m_pos;
    }

    [[nodiscard]]
    auto size() const -> std::size_t
    {
        return m_text.size();
    }

    void skipWhitespace()
    {
        while (m_pos < m_text.size()) {
            const char c = m_text[m_pos];
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
                ++m_pos;
            }
            else {
                break;
            }
        }
    }

    [[nodiscard]]
    auto atEnd() -> bool
    {
        skipWhitespace();
        return m_pos >= m_text.size();
    }

    [[nodiscard]]
    auto peek() -> char
    {
        skipWhitespace();
        return m_pos < m_text.size() ? m_text[m_pos] : '\0';
    }

    auto consume(char expected) -> bool
    {
        if (peek() != expected) {
            return false;
        }
        ++m_pos;
        return true;
    }

    auto beginObject() -> bool
    {
        return consume('{');
    }

    auto endObject() -> bool
    {
        return consume('}');
    }

    auto beginArray() -> bool
    {
        return consume('[');
    }

    auto endArray() -> bool
    {
        return consume(']');
    }

    auto comma() -> bool
    {
        return consume(',');
    }

    /// Reads a string literal into `out` (cleared first), resolving escapes.
    auto readString(std::string &out) -> bool
    {
        out.clear();
        if (!consume('"')) {
            return false;
        }
        while (m_pos < m_text.size()) {
            const char c = m_text[m_pos++];
            if (c == '"') {
                return true;
            }
            if (c != '\\') {
                out.push_back(c);
                continue;
            }
            if (m_pos >= m_text.size()) {
                return false;
            }
            const char esc = m_text[m_pos++];
            switch (esc) {
            case '"' :
            case '\\' :
            case '/' :
                out.push_back(esc);
                break;
            case 'b' :
                out.push_back('\b');
                break;
            case 'f' :
                out.push_back('\f');
                break;
            case 'n' :
                out.push_back('\n');
                break;
            case 'r' :
                out.push_back('\r');
                break;
            case 't' :
                out.push_back('\t');
                break;
            case 'u' : {
                if (m_pos + 4 > m_text.size()) {
                    return false;
                }
                unsigned code = 0;
                const char *first = m_text.data() + m_pos;
                if (std::from_chars(first, first + 4, code, 16).ec
                    != std::errc {}) {
                    return false;
                }
                m_pos += 4;
                appendUtf8(out, code);
                break;
            }
            default :
                return false;
            }
        }
        return false;
    }

    auto readNumber(double &out) -> bool
    {
        skipWhitespace();
        const std::size_t start = m_pos;
        if (m_pos < m_text.size()
            && (m_text[m_pos] == '-' || m_text[m_pos] == '+')) {
            ++m_pos;
        }
        while (m_pos < m_text.size()) {
            const char c = m_text[m_pos];
            const bool numeric = (c >= '0' && c <= '9') || c == '.'
                || c == 'e' || c == 'E' || c == '+' || c == '-';
            if (!numeric) {
                break;
            }
            ++m_pos;
        }
        if (m_pos == start) {
            return false;
        }
        // from_chars for double is available on MSVC and libstdc++ 11+.
        const char *first = m_text.data() + start;
        const char *last = m_text.data() + m_pos;
        return std::from_chars(first, last, out).ec == std::errc {};
    }

    auto readInteger(std::int64_t &out) -> bool
    {
        double value = 0.0;
        if (!readNumber(value)) {
            return false;
        }
        out = static_cast<std::int64_t>(value);
        return true;
    }

    /// Skips exactly one value of any type.
    auto skipValue() -> bool
    {
        const char c = peek();
        switch (c) {
        case '"' : {
            std::string scratch;
            return readString(scratch);
        }
        case '{' :
        case '[' :
            return skipContainer();
        case 't' :
            return skipLiteral("true");
        case 'f' :
            return skipLiteral("false");
        case 'n' :
            return skipLiteral("null");
        case '\0' :
            return false;
        default : {
            double value = 0.0;
            return readNumber(value);
        }
        }
    }

private:
    static void appendUtf8(std::string &out, unsigned code)
    {
        if (code < 0x80) {
            out.push_back(static_cast<char>(code));
        }
        else if (code < 0x800) {
            out.push_back(static_cast<char>(0xC0 | (code >> 6)));
            out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
        }
        else {
            out.push_back(static_cast<char>(0xE0 | (code >> 12)));
            out.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
        }
    }

    auto skipLiteral(std::string_view literal) -> bool
    {
        skipWhitespace();
        if (m_text.compare(m_pos, literal.size(), literal) != 0) {
            return false;
        }
        m_pos += literal.size();
        return true;
    }

    /// Fast structural skip: counts braces and brackets while respecting
    /// string literals, which is far cheaper than parsing the contents.
    auto skipContainer() -> bool
    {
        skipWhitespace();
        if (m_pos >= m_text.size()) {
            return false;
        }
        int depth = 0;
        bool inString = false;
        while (m_pos < m_text.size()) {
            const char c = m_text[m_pos++];
            if (inString) {
                if (c == '\\') {
                    ++m_pos;
                }
                else if (c == '"') {
                    inString = false;
                }
                continue;
            }
            if (c == '"') {
                inString = true;
            }
            else if (c == '{' || c == '[') {
                ++depth;
            }
            else if (c == '}' || c == ']') {
                if (--depth == 0) {
                    return true;
                }
            }
        }
        return false;
    }

    std::string_view m_text;
    std::size_t m_pos { 0 };
};

}
