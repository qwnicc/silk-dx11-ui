#include "silk/config/json.h"

#include "silk/core/utf8.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace silk::json {

namespace {
const Array kEmptyArray;
const Object kEmptyObject;
const Value kNull;

struct Parser {
    std::string_view s;
    size_t i = 0;
    std::string err;

    void Ws() {
        while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r')) ++i;
    }
    bool Fail(const char* msg) {
        if (err.empty()) err = std::string(msg) + " at offset " + std::to_string(i);
        return false;
    }
    bool Lit(std::string_view w) {
        if (s.substr(i, w.size()) != w) return false;
        i += w.size();
        return true;
    }
    bool String(std::string& out) {
        if (i >= s.size() || s[i] != '"') return Fail("expected string");
        ++i;
        while (i < s.size() && s[i] != '"') {
            char c = s[i++];
            if (c != '\\') { out += c; continue; }
            if (i >= s.size()) return Fail("bad escape");
            c = s[i++];
            switch (c) {
            case 'n': out += '\n'; break;
            case 't': out += '\t'; break;
            case 'r': out += '\r'; break;
            case 'b': out += '\b'; break;
            case 'f': out += '\f'; break;
            case 'u': {
                if (i + 4 > s.size()) return Fail("bad \\u escape");
                uint32_t cp = uint32_t(std::strtoul(std::string(s.substr(i, 4)).c_str(), nullptr, 16));
                i += 4;
                if (cp >= 0xD800 && cp < 0xDC00 && s.substr(i, 2) == "\\u" && i + 6 <= s.size()) {
                    const uint32_t lo = uint32_t(std::strtoul(std::string(s.substr(i + 2, 4)).c_str(), nullptr, 16));
                    cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                    i += 6;
                }
                utf8::Append(out, cp);
                break;
            }
            default: out += c; break;
            }
        }
        if (i >= s.size()) return Fail("unterminated string");
        ++i;
        return true;
    }
    bool Parse(Value& out, int depth = 0) {
        if (depth > 64) return Fail("too deep");
        Ws();
        if (i >= s.size()) return Fail("unexpected end");
        const char c = s[i];
        if (c == '{') {
            ++i;
            Object obj;
            Ws();
            if (i < s.size() && s[i] == '}') { ++i; out = Value(std::move(obj)); return true; }
            for (;;) {
                Ws();
                std::string key;
                if (!String(key)) return false;
                Ws();
                if (i >= s.size() || s[i] != ':') return Fail("expected ':'");
                ++i;
                Value v;
                if (!Parse(v, depth + 1)) return false;
                obj[key] = std::move(v);
                Ws();
                if (i < s.size() && s[i] == ',') { ++i; continue; }
                if (i < s.size() && s[i] == '}') { ++i; break; }
                return Fail("expected ',' or '}'");
            }
            out = Value(std::move(obj));
            return true;
        }
        if (c == '[') {
            ++i;
            Array arr;
            Ws();
            if (i < s.size() && s[i] == ']') { ++i; out = Value(std::move(arr)); return true; }
            for (;;) {
                Value v;
                if (!Parse(v, depth + 1)) return false;
                arr.push_back(std::move(v));
                Ws();
                if (i < s.size() && s[i] == ',') { ++i; continue; }
                if (i < s.size() && s[i] == ']') { ++i; break; }
                return Fail("expected ',' or ']'");
            }
            out = Value(std::move(arr));
            return true;
        }
        if (c == '"') {
            std::string str;
            if (!String(str)) return false;
            out = Value(std::move(str));
            return true;
        }
        if (Lit("true")) { out = Value(true); return true; }
        if (Lit("false")) { out = Value(false); return true; }
        if (Lit("null")) { out = Value(); return true; }
        char* end = nullptr;
        const std::string tail(s.substr(i, std::min<size_t>(64, s.size() - i)));
        const double d = std::strtod(tail.c_str(), &end);
        if (end == tail.c_str()) return Fail("unexpected character");
        i += size_t(end - tail.c_str());
        out = Value(d);
        return true;
    }
};

void Escape(std::string& out, const std::string& s) {
    out += '"';
    for (char c : s) {
        switch (c) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\t': out += "\\t"; break;
        case '\r': out += "\\r"; break;
        default:
            if (static_cast<uint8_t>(c) < 0x20) {
                char buf[8];
                std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                out += buf;
            } else {
                out += c;
            }
        }
    }
    out += '"';
}
} // namespace

const Array& Value::AsArray() const { return IsArray() ? *std::get<std::shared_ptr<Array>>(v_) : kEmptyArray; }
const Object& Value::AsObject() const { return IsObject() ? *std::get<std::shared_ptr<Object>>(v_) : kEmptyObject; }

Value& Value::operator[](const std::string& key) {
    if (!IsObject()) v_ = std::make_shared<Object>();
    return (*std::get<std::shared_ptr<Object>>(v_))[key];
}

const Value& Value::Get(const std::string& key) const {
    if (!IsObject()) return kNull;
    const auto& o = *std::get<std::shared_ptr<Object>>(v_);
    const auto it = o.find(key);
    return it == o.end() ? kNull : it->second;
}

bool Value::Has(const std::string& key) const { return IsObject() && AsObject().contains(key); }

void Value::Push(Value v) {
    if (!IsArray()) v_ = std::make_shared<Array>();
    std::get<std::shared_ptr<Array>>(v_)->push_back(std::move(v));
}

void Value::DumpTo(std::string& out, bool pretty, int indent) const {
    auto nl = [&](int ind) {
        if (!pretty) return;
        out += '\n';
        out.append(size_t(ind) * 2, ' ');
    };
    if (IsNull()) out += "null";
    else if (IsBool()) out += AsBool() ? "true" : "false";
    else if (IsNumber()) {
        const double d = AsNumber();
        char buf[32];
        if (std::isfinite(d) && d == std::floor(d) && std::abs(d) < 1e15) std::snprintf(buf, sizeof(buf), "%.0f", d);
        else std::snprintf(buf, sizeof(buf), "%.6g", d);
        out += buf;
    } else if (IsString()) Escape(out, std::get<std::string>(v_));
    else if (IsArray()) {
        const Array& a = AsArray();
        out += '[';
        for (size_t k = 0; k < a.size(); ++k) {
            if (k) out += ',';
            nl(indent + 1);
            a[k].DumpTo(out, pretty, indent + 1);
        }
        if (!a.empty()) nl(indent);
        out += ']';
    } else {
        const Object& o = AsObject();
        out += '{';
        bool first = true;
        for (const auto& [k, v] : o) {
            if (!first) out += ',';
            first = false;
            nl(indent + 1);
            Escape(out, k);
            out += pretty ? ": " : ":";
            v.DumpTo(out, pretty, indent + 1);
        }
        if (!o.empty()) nl(indent);
        out += '}';
    }
}

std::string Value::Dump(bool pretty) const {
    std::string out;
    DumpTo(out, pretty, 0);
    return out;
}

bool Value::Parse(std::string_view text, Value& out, std::string* error) {
    Parser p{text};
    Value v;
    if (!p.Parse(v)) {
        if (error) *error = p.err;
        return false;
    }
    out = std::move(v);
    return true;
}

bool LoadFile(const std::string& path, Value& out) {
    std::ifstream f(utf8::ToWide(path), std::ios::binary);
    if (!f) return false;
    std::stringstream ss;
    ss << f.rdbuf();
    return Value::Parse(ss.str(), out);
}

bool SaveFile(const std::string& path, const Value& v) {
    std::ofstream f(utf8::ToWide(path), std::ios::binary | std::ios::trunc);
    if (!f) return false;
    f << v.Dump(true) << '\n';
    return bool(f);
}

} // namespace silk::json
