#include "aa/data/json.h"

#include <fstream>
#include <limits>
#include <sstream>

namespace aa::data {

void JsonNode::fail(const std::string& what) const { throw JsonError(path_ + ": " + what); }

JsonNode JsonNode::child(const char* key) const {
    if (!isObject()) fail("not an object");
    const cJSON* c = cJSON_GetObjectItemCaseSensitive(node_, key);
    if (!c) fail(std::string("missing member '") + key + "'");
    return JsonNode(c, path_ + "." + key);
}

JsonNode JsonNode::optional(const char* key) const {
    const cJSON* c = isObject() ? cJSON_GetObjectItemCaseSensitive(node_, key) : nullptr;
    return JsonNode(c, path_ + "." + key);
}

bool JsonNode::has(const char* key) const {
    return isObject() && cJSON_GetObjectItemCaseSensitive(node_, key) != nullptr;
}

int JsonNode::size() const {
    if (!isArray()) fail("not an array");
    return cJSON_GetArraySize(node_);
}

JsonNode JsonNode::at(int index) const {
    if (!isArray()) fail("not an array");
    const cJSON* c = cJSON_GetArrayItem(node_, index);
    if (!c) fail("index " + std::to_string(index) + " out of range");
    return JsonNode(c, path_ + "[" + std::to_string(index) + "]");
}

std::vector<JsonNode> JsonNode::array() const {
    std::vector<JsonNode> out;
    const int n = size();
    out.reserve(static_cast<std::size_t>(n));
    for (int i = 0; i < n; ++i) out.push_back(at(i));
    return out;
}

std::vector<std::pair<std::string, JsonNode>> JsonNode::members() const {
    if (!isObject()) fail("not an object");
    std::vector<std::pair<std::string, JsonNode>> out;
    for (const cJSON* c = node_->child; c; c = c->next) {
        const std::string key = c->string ? c->string : "";
        out.emplace_back(key, JsonNode(c, path_ + "." + key));
    }
    return out;
}

int JsonNode::getInt() const {
    if (!isNumber()) fail("not a number");
    const double v = node_->valuedouble;
    // Range first: the int conversion is undefined outside [INT_MIN, INT_MAX] and would otherwise wrap a
    // hand-edited 4294967295 into -1.
    if (!(v >= static_cast<double>(std::numeric_limits<int>::min()) &&
          v <= static_cast<double>(std::numeric_limits<int>::max()))) fail("integer out of range");
    if (v != static_cast<double>(static_cast<int>(v))) fail("not an integer");
    return static_cast<int>(v);
}

float JsonNode::getFloat() const {
    if (!isNumber()) fail("not a number");
    return static_cast<float>(node_->valuedouble);
}

double JsonNode::getDouble() const {
    if (!isNumber()) fail("not a number");
    return node_->valuedouble;
}

bool JsonNode::getBool() const {
    if (!isBool()) fail("not a boolean");
    return cJSON_IsTrue(node_) != 0;
}

std::string JsonNode::getString() const {
    if (!isString()) fail("not a string");
    return node_->valuestring ? node_->valuestring : "";
}

int JsonNode::getInt(const char* key, int fallback) const {
    const JsonNode c = optional(key);
    return c.isNull() ? fallback : c.getInt();
}

bool JsonNode::getBool(const char* key, bool fallback) const {
    const JsonNode c = optional(key);
    return c.isNull() ? fallback : c.getBool();
}

std::string JsonNode::getString(const char* key, const std::string& fallback) const {
    const JsonNode c = optional(key);
    return c.isNull() ? fallback : c.getString();
}

JsonDoc::JsonDoc(const std::string& text, const std::string& name) : name_(name) {
    root_ = cJSON_Parse(text.c_str());
    if (!root_) {
        const char* err = cJSON_GetErrorPtr();
        const std::size_t offset = err ? static_cast<std::size_t>(err - text.c_str()) : 0;
        throw JsonError(name + ": parse error near byte " + std::to_string(offset));
    }
}

JsonDoc::~JsonDoc() {
    if (root_) cJSON_Delete(root_);
}

JsonDoc::JsonDoc(JsonDoc&& other) noexcept : root_(other.root_), name_(std::move(other.name_)) {
    other.root_ = nullptr;
}

JsonDoc& JsonDoc::operator=(JsonDoc&& other) noexcept {
    if (this != &other) {
        if (root_) cJSON_Delete(root_);
        root_ = other.root_;
        name_ = std::move(other.name_);
        other.root_ = nullptr;
    }
    return *this;
}

JsonDoc JsonDoc::parseFile(const std::string& path) { return JsonDoc(readTextFile(path), path); }

std::string readTextFile(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw JsonError(path + ": cannot open");
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

}  // namespace aa::data
