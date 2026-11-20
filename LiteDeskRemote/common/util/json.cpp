#include "json.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace ldr::util::json {

const Value* Value::Find(const std::string& key) const {
    if (t != Type::Obj) return nullptr;
    for (const auto& kv : obj)
        if (kv.first == key) return &kv.second;
    return nullptr;
}

Value& Value::Set(const std::string& key, Value v) {
    if (t != Type::Obj) { t = Type::Obj; obj.clear(); }
    for (auto& kv : obj)
        if (kv.first == key) { kv.second = std::move(v); return kv.second; }
    obj.emplace_back(key, std::move(v));
    return obj.back().second;
}

std::optional<std::string> Value::AsStr() const {
    if (t != Type::Str) return std::nullopt;
    return str;
}
std::optional<bool> Value::AsBool() const {
    if (t != Type::Bool) return std::nullopt;
    return b;
}
std::optional<double> Value::AsNum() const {
    if (t != Type::Num) return std::nullopt;
    return num;
}
std::optional<int64_t> Value::AsInt() const {
    if (t != Type::Num) return std::nullopt;
    return static_cast<int64_t>(num);
}

namespace {

void EscapeInto(const std::string& s, std::string& out) {
    out.push_back('"');
    for (unsigned char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b";  break;
            case '\f': out += "\\f";  break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:
                if (c < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out += buf;
                } else {
                    out.push_back(static_cast<char>(c));
                }
        }
    }
    out.push_back('"');
}

void DumpInto(const Value& v, int indent, int depth, std::string& out) {
    auto pad = [&](int d) {
        if (indent > 0) {
            out.push_back('\n');
            out.append(static_cast<size_t>(indent) * d, ' ');
        }
    };
    switch (v.t) {
        case Value::Type::Null: out += "null"; break;
        case Value::Type::Bool: out += v.b ? "true" : "false"; break;
        case Value::Type::Num: {
            double n = v.num;
            if (std::floor(n) == n && std::fabs(n) < 9.0e15) {
                char buf[32];
                std::snprintf(buf, sizeof(buf), "%lld", static_cast<long long>(n));
                out += buf;
            } else {
                char buf[40];
                std::snprintf(buf, sizeof(buf), "%.6g", n);
                out += buf;
            }
            break;
        }
        case Value::Type::Str: EscapeInto(v.str, out); break;
        case Value::Type::Arr: {
            if (v.arr.empty()) { out += "[]"; break; }
            out.push_back('[');
            for (size_t i = 0; i < v.arr.size(); ++i) {
                if (i) out.push_back(',');
                pad(depth + 1);
                DumpInto(v.arr[i], indent, depth + 1, out);
            }
            pad(depth);
            out.push_back(']');
            break;
        }
        case Value::Type::Obj: {
            if (v.obj.empty()) { out += "{}"; break; }
            out.push_back('{');
            bool first = true;
            for (const auto& kv : v.obj) {
                if (!first) out.push_back(',');
                first = false;
                pad(depth + 1);
                EscapeInto(kv.first, out);
                out.push_back(':');
                if (indent > 0) out.push_back(' ');
                DumpInto(kv.second, indent, depth + 1, out);
            }
            pad(depth);
            out.push_back('}');
            break;
        }
    }
}

class Parser {
public:
    explicit Parser(const std::string& s) : s_(s) {}
    std::optional<Value> Run() {
        SkipWs();
        Value v;
        if (!ParseValue(v, 0)) return std::nullopt;
        SkipWs();
        if (pos_ != s_.size()) return std::nullopt; // trailing garbage
        return v;
    }
private:
    bool ParseValue(Value& out, int depth) {
        if (depth > 64) return false;
        SkipWs();
        if (pos_ >= s_.size()) return false;
        char c = s_[pos_];
        if (c == '{') return ParseObj(out, depth);
        if (c == '[') return ParseArr(out, depth);
        if (c == '"') { out.t = Value::Type::Str; return ParseString(out.str); }
        if (c == 't') { if (Match("true")) { out.t = Value::Type::Bool; out.b = true; return true; } return false; }
        if (c == 'f') { if (Match("false")) { out.t = Value::Type::Bool; out.b = false; return true; } return false; }
        if (c == 'n') { if (Match("null")) { out.t = Value::Type::Null; return true; } return false; }
        return ParseNum(out);
    }
    bool ParseObj(Value& out, int depth) {
        out.t = Value::Type::Obj;
        ++pos_; // '{'
        SkipWs();
        if (pos_ < s_.size() && s_[pos_] == '}') { ++pos_; return true; }
        while (true) {
            SkipWs();
            if (pos_ >= s_.size() || s_[pos_] != '"') return false;
            std::string key;
            if (!ParseString(key)) return false;
            SkipWs();
            if (pos_ >= s_.size() || s_[pos_] != ':') return false;
            ++pos_;
            Value v;
            if (!ParseValue(v, depth + 1)) return false;
            out.obj.emplace_back(std::move(key), std::move(v));
            SkipWs();
            if (pos_ < s_.size() && s_[pos_] == ',') { ++pos_; continue; }
            if (pos_ < s_.size() && s_[pos_] == '}') { ++pos_; return true; }
            return false;
        }
    }
    bool ParseArr(Value& out, int depth) {
        out.t = Value::Type::Arr;
        ++pos_; // '['
        SkipWs();
        if (pos_ < s_.size() && s_[pos_] == ']') { ++pos_; return true; }
        while (true) {
            Value v;
            if (!ParseValue(v, depth + 1)) return false;
            out.arr.push_back(std::move(v));
            SkipWs();
            if (pos_ < s_.size() && s_[pos_] == ',') { ++pos_; continue; }
            if (pos_ < s_.size() && s_[pos_] == ']') { ++pos_; return true; }
            return false;
        }
    }
    bool ParseString(std::string& out) {
        ++pos_; // '"'
        while (pos_ < s_.size()) {
            char c = s_[pos_];
            if (c == '"') { ++pos_; return true; }
            if (c == '\\') {
                ++pos_;
                if (pos_ >= s_.size()) return false;
                char e = s_[pos_++];
                switch (e) {
                    case '"':  out.push_back('"');  break;
                    case '\\': out.push_back('\\'); break;
                    case '/':  out.push_back('/');  break;
                    case 'b':  out.push_back('\b'); break;
                    case 'f':  out.push_back('\f'); break;
                    case 'n':  out.push_back('\n'); break;
                    case 'r':  out.push_back('\r'); break;
                    case 't':  out.push_back('\t'); break;
                    case 'u': {
                        unsigned cp = 0;
                        if (!ParseHex4(cp)) return false;
                        // surrogate pair
                        if (cp >= 0xD800 && cp <= 0xDBFF && pos_ + 1 < s_.size() &&
                            s_[pos_] == '\\' && s_[pos_ + 1] == 'u') {
                            size_t save = pos_;
                            pos_ += 2;
                            unsigned lo = 0;
                            if (!ParseHex4(lo)) return false;
                            if (lo >= 0xDC00 && lo <= 0xDFFF) {
                                cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                            } else {
                                pos_ = save; // bukan pasangan; biarkan cp apa adanya
                            }
                        }
                        AppendUtf8(out, cp);
                        break;
                    }
                    default: return false;
                }
            } else {
                out.push_back(c);
                ++pos_;
            }
        }
        return false;
    }
    bool ParseHex4(unsigned& out) {
        if (pos_ + 4 > s_.size()) return false;
        unsigned v = 0;
        for (int i = 0; i < 4; ++i) {
            char c = s_[pos_++];
            v <<= 4;
            if (c >= '0' && c <= '9') v |= static_cast<unsigned>(c - '0');
            else if (c >= 'a' && c <= 'f') v |= static_cast<unsigned>(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') v |= static_cast<unsigned>(c - 'A' + 10);
            else return false;
        }
        out = v;
        return true;
    }
    static void AppendUtf8(std::string& out, unsigned cp) {
        if (cp < 0x80) {
            out.push_back(static_cast<char>(cp));
        } else if (cp < 0x800) {
            out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else if (cp < 0x10000) {
            out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else {
            out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        }
    }
    bool ParseNum(Value& out) {
        size_t start = pos_;
        if (pos_ < s_.size() && (s_[pos_] == '-' || s_[pos_] == '+')) ++pos_;
        bool anyDigit = false;
        while (pos_ < s_.size()) {
            char c = s_[pos_];
            if ((c >= '0' && c <= '9') || c == '.' || c == 'e' || c == 'E' || c == '-' || c == '+') {
                if (c >= '0' && c <= '9') anyDigit = true;
                ++pos_;
            } else break;
        }
        if (!anyDigit) return false;
        out.t = Value::Type::Num;
        out.num = std::strtod(s_.c_str() + start, nullptr);
        return true;
    }
    void SkipWs() {
        while (pos_ < s_.size() &&
               (s_[pos_] == ' ' || s_[pos_] == '\t' || s_[pos_] == '\n' || s_[pos_] == '\r'))
            ++pos_;
    }
    bool Match(const char* word) {
        size_t n = std::strlen(word);
        if (s_.compare(pos_, n, word) == 0) { pos_ += n; return true; }
        return false;
    }
    const std::string& s_;
    size_t pos_ = 0;
};

} // namespace

std::optional<Value> Parse(const std::string& text) {
    Parser p(text);
    return p.Run();
}

std::string Dump(const Value& v, int indent) {
    std::string out;
    DumpInto(v, indent, 0, out);
    out.push_back('\n');
    return out;
}

} // namespace ldr::util::json
