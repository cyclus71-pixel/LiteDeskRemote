// json.h — parser/serializer JSON minimal (RFC 8259 subset yang cukup untuk
// config, pin store, known hosts, audit). Objek mempertahankan urutan kunci.
#pragma once
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace ldr::util::json {

class Value {
public:
    enum class Type : uint8_t { Null, Bool, Num, Str, Arr, Obj };

    Type t = Type::Null;
    bool b = false;
    double num = 0.0;
    std::string str;
    std::vector<Value> arr;
    std::vector<std::pair<std::string, Value>> obj; // urut sesuai file

    Value() = default;
    static Value BoolV(bool v)   { Value x; x.t = Type::Bool; x.b = v; return x; }
    static Value NumV(double v)  { Value x; x.t = Type::Num; x.num = v; return x; }
    static Value StrV(std::string v) { Value x; x.t = Type::Str; x.str = std::move(v); return x; }
    static Value ArrayV()        { Value x; x.t = Type::Arr; return x; }
    static Value ObjectV()       { Value x; x.t = Type::Obj; return x; }

    bool IsNull() const { return t == Type::Null; }
    bool IsObj()  const { return t == Type::Obj; }
    bool IsArr()  const { return t == Type::Arr; }
    bool IsStr()  const { return t == Type::Str; }
    bool IsBool() const { return t == Type::Bool; }
    bool IsNum()  const { return t == Type::Num; }

    // Akses objek (mengembalikan nullptr bila tidak ada / bukan objek).
    const Value* Find(const std::string& key) const;
    Value& Set(const std::string& key, Value v); // replace atau push_back

    std::string Dump(int indent = 0) const;

    // Konversi aman
    std::optional<std::string> AsStr() const;
    std::optional<bool> AsBool() const;
    std::optional<double> AsNum() const;
    std::optional<int64_t> AsInt() const;
};

std::optional<Value> Parse(const std::string& text);
std::string Dump(const Value& v, int indent = 0);

} // namespace ldr::util::json
