#include "Core/Json.h"

#include <cctype>
#include <cstdlib>

namespace tucano::core {
namespace {

struct Parser {
  std::string_view text;
  size_t pos = 0;
  std::string err;

  bool fail(const char* what) {
    err = std::string(what) + " at offset " + std::to_string(pos);
    return false;
  }
  void skipWs() {
    while (pos < text.size()) {
      const char c = text[pos];
      if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
        ++pos;
      } else if (c == '/' && pos + 1 < text.size() && text[pos + 1] == '/') {
        while (pos < text.size() && text[pos] != '\n') {
          ++pos;
        }
      } else {
        break;
      }
    }
  }
  bool consume(char c) {
    skipWs();
    if (pos < text.size() && text[pos] == c) {
      ++pos;
      return true;
    }
    return false;
  }

  bool parseString(std::string& out) {
    if (!consume('"')) {
      return fail("expected string");
    }
    out.clear();
    while (pos < text.size()) {
      char c = text[pos++];
      if (c == '"') {
        return true;
      }
      if (c == '\\' && pos < text.size()) {
        const char e = text[pos++];
        switch (e) {
          case 'n': out.push_back('\n'); break;
          case 't': out.push_back('\t'); break;
          case 'r': out.push_back('\r'); break;
          case 'b': out.push_back('\b'); break;
          case 'f': out.push_back('\f'); break;
          case 'u': // \uXXXX → keep ASCII subset, skip others
            if (pos + 4 <= text.size()) {
              const std::string hex(text.substr(pos, 4));
              const long v = std::strtol(hex.c_str(), nullptr, 16);
              if (v < 128) {
                out.push_back(char(v));
              } else {
                out.push_back('?');
              }
              pos += 4;
            }
            break;
          default: out.push_back(e); break;
        }
      } else {
        out.push_back(c);
      }
    }
    return fail("unterminated string");
  }

  bool parseValue(JsonValue& out) {
    skipWs();
    if (pos >= text.size()) {
      return fail("unexpected end");
    }
    const char c = text[pos];
    if (c == '{') {
      ++pos;
      out.type = JsonValue::Type::Object;
      skipWs();
      if (consume('}')) {
        return true;
      }
      for (;;) {
        std::string key;
        if (!parseString(key)) {
          return false;
        }
        if (!consume(':')) {
          return fail("expected ':'");
        }
        JsonValue v;
        if (!parseValue(v)) {
          return false;
        }
        out.obj.emplace_back(std::move(key), std::move(v));
        if (consume(',')) {
          continue;
        }
        if (consume('}')) {
          return true;
        }
        return fail("expected ',' or '}'");
      }
    }
    if (c == '[') {
      ++pos;
      out.type = JsonValue::Type::Array;
      skipWs();
      if (consume(']')) {
        return true;
      }
      for (;;) {
        JsonValue v;
        if (!parseValue(v)) {
          return false;
        }
        out.arr.push_back(std::move(v));
        if (consume(',')) {
          continue;
        }
        if (consume(']')) {
          return true;
        }
        return fail("expected ',' or ']'");
      }
    }
    if (c == '"') {
      out.type = JsonValue::Type::String;
      return parseString(out.str);
    }
    if (text.compare(pos, 4, "true") == 0) {
      out.type = JsonValue::Type::Bool;
      out.boolean = true;
      pos += 4;
      return true;
    }
    if (text.compare(pos, 5, "false") == 0) {
      out.type = JsonValue::Type::Bool;
      out.boolean = false;
      pos += 5;
      return true;
    }
    if (text.compare(pos, 4, "null") == 0) {
      out.type = JsonValue::Type::Null;
      pos += 4;
      return true;
    }
    // number
    {
      const char* start = text.data() + pos;
      char* endp = nullptr;
      const double v = std::strtod(start, &endp);
      if (endp == start) {
        return fail("invalid value");
      }
      out.type = JsonValue::Type::Number;
      out.number = v;
      pos += size_t(endp - start);
      return true;
    }
  }
};

} // namespace

bool JsonValue::parse(std::string_view text, JsonValue& out, std::string* err) {
  Parser p{text};
  out = JsonValue{};
  if (!p.parseValue(out)) {
    if (err) {
      *err = p.err;
    }
    return false;
  }
  return true;
}

} // namespace tucano::core
