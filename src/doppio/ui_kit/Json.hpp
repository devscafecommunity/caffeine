#pragma once

/// Minimal JSON reader/writer used by the UI runtime. It intentionally supports
/// only the subset needed by theme and workspace documents: objects, arrays,
/// strings, numbers, booleans and null.

#include <cctype>
#include <cerrno>
#include <cstdlib>
#include <string>
#include <utility>
#include <vector>

namespace Caffeine::UIKit::Detail {

struct JsonValue {
    enum class Type { Null, Bool, Number, String, Array, Object };

    Type type = Type::Null;
    bool boolean = false;
    double number = 0.0;
    std::string string;
    std::vector<JsonValue> array;
    std::vector<std::pair<std::string, JsonValue>> object;

    bool isNull() const { return type == Type::Null; }
    bool isBool() const { return type == Type::Bool; }
    bool isNumber() const { return type == Type::Number; }
    bool isString() const { return type == Type::String; }
    bool isArray() const { return type == Type::Array; }
    bool isObject() const { return type == Type::Object; }

    const JsonValue* find(const std::string& key) const {
        for (const auto& entry : object) {
            if (entry.first == key) return &entry.second;
        }
        return nullptr;
    }

    double numberOr(double fallback) const {
        return type == Type::Number ? number : fallback;
    }

    const std::string& stringOr(const std::string& fallback) const {
        return type == Type::String ? string : fallback;
    }
};

class JsonParser {
public:
    explicit JsonParser(const std::string& text) : m_text(text) {}

    bool parse(JsonValue& out, std::string* error) {
        skipWhitespace();
        if (!parseValue(out)) {
            if (error) *error = m_error;
            return false;
        }
        skipWhitespace();
        if (m_pos != m_text.size()) {
            if (error) *error = "trailing characters at offset " + std::to_string(m_pos);
            return false;
        }
        return true;
    }

private:
    void skipWhitespace() {
        while (m_pos < m_text.size() &&
               std::isspace(static_cast<unsigned char>(m_text[m_pos]))) {
            ++m_pos;
        }
    }

    bool fail(const char* message) {
        m_error = std::string(message) + " at offset " + std::to_string(m_pos);
        return false;
    }

    bool parseValue(JsonValue& out) {
        skipWhitespace();
        if (m_pos >= m_text.size()) return fail("unexpected end of input");
        const char c = m_text[m_pos];
        if (c == '{') return parseObject(out);
        if (c == '[') return parseArray(out);
        if (c == '"') {
            out.type = JsonValue::Type::String;
            return parseString(out.string);
        }
        if (c == 't' || c == 'f') return parseBool(out);
        if (c == 'n') return parseNull(out);
        return parseNumber(out);
    }

    bool parseObject(JsonValue& out) {
        out.type = JsonValue::Type::Object;
        ++m_pos;  // {
        skipWhitespace();
        if (m_pos < m_text.size() && m_text[m_pos] == '}') {
            ++m_pos;
            return true;
        }
        while (m_pos < m_text.size()) {
            skipWhitespace();
            if (m_pos >= m_text.size() || m_text[m_pos] != '"') return fail("expected object key");
            std::string key;
            if (!parseString(key)) return false;
            skipWhitespace();
            if (m_pos >= m_text.size() || m_text[m_pos] != ':') return fail("expected ':'");
            ++m_pos;
            JsonValue value;
            if (!parseValue(value)) return false;
            out.object.emplace_back(std::move(key), std::move(value));
            skipWhitespace();
            if (m_pos >= m_text.size()) return fail("unterminated object");
            if (m_text[m_pos] == ',') {
                ++m_pos;
                continue;
            }
            if (m_text[m_pos] == '}') {
                ++m_pos;
                return true;
            }
            return fail("expected ',' or '}'");
        }
        return fail("unterminated object");
    }

    bool parseArray(JsonValue& out) {
        out.type = JsonValue::Type::Array;
        ++m_pos;  // [
        skipWhitespace();
        if (m_pos < m_text.size() && m_text[m_pos] == ']') {
            ++m_pos;
            return true;
        }
        while (m_pos < m_text.size()) {
            JsonValue value;
            if (!parseValue(value)) return false;
            out.array.push_back(std::move(value));
            skipWhitespace();
            if (m_pos >= m_text.size()) return fail("unterminated array");
            if (m_text[m_pos] == ',') {
                ++m_pos;
                continue;
            }
            if (m_text[m_pos] == ']') {
                ++m_pos;
                return true;
            }
            return fail("expected ',' or ']'");
        }
        return fail("unterminated array");
    }

    bool parseString(std::string& out) {
        if (m_pos >= m_text.size() || m_text[m_pos] != '"') return fail("expected string");
        ++m_pos;
        out.clear();
        while (m_pos < m_text.size()) {
            const char c = m_text[m_pos++];
            if (c == '"') return true;
            if (c != '\\') {
                out.push_back(c);
                continue;
            }
            if (m_pos >= m_text.size()) return fail("unterminated escape");
            const char esc = m_text[m_pos++];
            switch (esc) {
                case '"': out.push_back('"'); break;
                case '\\': out.push_back('\\'); break;
                case '/': out.push_back('/'); break;
                case 'b': out.push_back('\b'); break;
                case 'f': out.push_back('\f'); break;
                case 'n': out.push_back('\n'); break;
                case 'r': out.push_back('\r'); break;
                case 't': out.push_back('\t'); break;
                case 'u': {
                    if (m_pos + 4 > m_text.size()) return fail("bad unicode escape");
                    unsigned code = 0;
                    for (int i = 0; i < 4; ++i) {
                        const char h = m_text[m_pos++];
                        code <<= 4;
                        if (h >= '0' && h <= '9') code |= static_cast<unsigned>(h - '0');
                        else if (h >= 'a' && h <= 'f') code |= static_cast<unsigned>(h - 'a' + 10);
                        else if (h >= 'A' && h <= 'F') code |= static_cast<unsigned>(h - 'A' + 10);
                        else return fail("bad unicode escape");
                    }
                    if (code < 0x80) {
                        out.push_back(static_cast<char>(code));
                    } else if (code < 0x800) {
                        out.push_back(static_cast<char>(0xC0 | (code >> 6)));
                        out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
                    } else {
                        out.push_back(static_cast<char>(0xE0 | (code >> 12)));
                        out.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
                        out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
                    }
                    break;
                }
                default: return fail("unknown escape");
            }
        }
        return fail("unterminated string");
    }

    bool parseBool(JsonValue& out) {
        if (m_text.compare(m_pos, 4, "true") == 0) {
            m_pos += 4;
            out.type = JsonValue::Type::Bool;
            out.boolean = true;
            return true;
        }
        if (m_text.compare(m_pos, 5, "false") == 0) {
            m_pos += 5;
            out.type = JsonValue::Type::Bool;
            out.boolean = false;
            return true;
        }
        return fail("expected boolean");
    }

    bool parseNull(JsonValue& out) {
        if (m_text.compare(m_pos, 4, "null") != 0) return fail("expected null");
        m_pos += 4;
        out.type = JsonValue::Type::Null;
        return true;
    }

    bool parseNumber(JsonValue& out) {
        const char* begin = m_text.c_str() + m_pos;
        char* end = nullptr;
        errno = 0;
        const double value = std::strtod(begin, &end);
        if (end == begin || errno == ERANGE) return fail("invalid number");
        m_pos += static_cast<std::size_t>(end - begin);
        out.type = JsonValue::Type::Number;
        out.number = value;
        return true;
    }

    const std::string& m_text;
    std::size_t m_pos = 0;
    std::string m_error;
};

inline bool parseJson(const std::string& text, JsonValue& out, std::string* error) {
    JsonParser parser(text);
    return parser.parse(out, error);
}

}  // namespace Caffeine::UIKit::Detail
