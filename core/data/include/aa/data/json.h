// Thin RAII wrapper over cJSON for the imported asset tree (docs/10-architecture.md §4).
//
// Numbers: cJSON parses every JSON number with strtod into `valuedouble`; the accessors here cast that
// double to float. This is the original game's own path for level reals — `strtod` into a double, then
// `(float)` in the level loader (docs/02-level-format.md §6) — and the importer emits every float with 9
// significant digits and checks that this two-step rounding reproduces the float32 it started from.
#pragma once

#include <cJSON.h>

#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace aa::data {

class JsonError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// A view of one node; the document owns the memory.
class JsonNode {
public:
    JsonNode(const cJSON* node, std::string path) : node_(node), path_(std::move(path)) {}

    bool isNull() const { return node_ == nullptr || cJSON_IsNull(node_); }
    bool isObject() const { return node_ && cJSON_IsObject(node_); }
    bool isArray() const { return node_ && cJSON_IsArray(node_); }
    bool isNumber() const { return node_ && cJSON_IsNumber(node_); }
    bool isString() const { return node_ && cJSON_IsString(node_); }
    bool isBool() const { return node_ && cJSON_IsBool(node_); }
    const std::string& path() const { return path_; }

    // Member access; `child` throws when the key is absent, `optional` returns a null node.
    JsonNode child(const char* key) const;
    JsonNode optional(const char* key) const;
    bool has(const char* key) const;
    // Array access.
    int size() const;
    JsonNode at(int index) const;
    std::vector<JsonNode> array() const;
    // Object members in file order.
    std::vector<std::pair<std::string, JsonNode>> members() const;

    int getInt() const;
    float getFloat() const;    // (float)valuedouble
    double getDouble() const;
    bool getBool() const;
    std::string getString() const;

    // Convenience: child(key).getX(), with defaults for optional members.
    int getInt(const char* key) const { return child(key).getInt(); }
    int getInt(const char* key, int fallback) const;
    float getFloat(const char* key) const { return child(key).getFloat(); }
    bool getBool(const char* key, bool fallback) const;
    std::string getString(const char* key) const { return child(key).getString(); }
    std::string getString(const char* key, const std::string& fallback) const;

private:
    [[noreturn]] void fail(const std::string& what) const;
    const cJSON* node_;
    std::string path_;
};

class JsonDoc {
public:
    JsonDoc() = default;
    explicit JsonDoc(const std::string& text, const std::string& name = "<json>");
    ~JsonDoc();
    JsonDoc(JsonDoc&& other) noexcept;
    JsonDoc& operator=(JsonDoc&& other) noexcept;
    JsonDoc(const JsonDoc&) = delete;
    JsonDoc& operator=(const JsonDoc&) = delete;

    static JsonDoc parseFile(const std::string& path);

    JsonNode root() const { return JsonNode(root_, name_); }

private:
    cJSON* root_ = nullptr;
    std::string name_;
};

std::string readTextFile(const std::string& path);

}  // namespace aa::data
