#pragma once
// Minimal JSON value with parser and pretty printer (enough for configs).

#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace silk::json {

class Value;
using Array = std::vector<Value>;
using Object = std::map<std::string, Value>;

class Value {
public:
    Value() = default;
    Value(std::nullptr_t) {}
    Value(bool b) : v_(b) {}
    Value(int n) : v_(double(n)) {}
    Value(float n) : v_(double(n)) {}
    Value(double n) : v_(n) {}
    Value(const char* s) : v_(std::string(s)) {}
    Value(std::string s) : v_(std::move(s)) {}
    Value(Array a) : v_(std::make_shared<Array>(std::move(a))) {}
    Value(Object o) : v_(std::make_shared<Object>(std::move(o))) {}

    bool IsNull() const { return std::holds_alternative<std::monostate>(v_); }
    bool IsBool() const { return std::holds_alternative<bool>(v_); }
    bool IsNumber() const { return std::holds_alternative<double>(v_); }
    bool IsString() const { return std::holds_alternative<std::string>(v_); }
    bool IsArray() const { return std::holds_alternative<std::shared_ptr<Array>>(v_); }
    bool IsObject() const { return std::holds_alternative<std::shared_ptr<Object>>(v_); }

    bool AsBool(bool def = false) const { return IsBool() ? std::get<bool>(v_) : def; }
    double AsNumber(double def = 0.0) const { return IsNumber() ? std::get<double>(v_) : def; }
    float AsFloat(float def = 0.0f) const { return IsNumber() ? float(std::get<double>(v_)) : def; }
    int AsInt(int def = 0) const { return IsNumber() ? int(std::get<double>(v_)) : def; }
    std::string AsString(std::string def = {}) const { return IsString() ? std::get<std::string>(v_) : def; }
    const Array& AsArray() const;
    const Object& AsObject() const;

    // Object access; operator[] creates members (turning null into an object).
    Value& operator[](const std::string& key);
    const Value& Get(const std::string& key) const; // null value if missing
    bool Has(const std::string& key) const;
    void Push(Value v);                             // turns null into an array

    std::string Dump(bool pretty = true) const;
    static bool Parse(std::string_view text, Value& out, std::string* error = nullptr);

private:
    void DumpTo(std::string& out, bool pretty, int indent) const;
    std::variant<std::monostate, bool, double, std::string, std::shared_ptr<Array>, std::shared_ptr<Object>> v_;
};

bool LoadFile(const std::string& path, Value& out);
bool SaveFile(const std::string& path, const Value& v);

} // namespace silk::json
