#ifndef CONN_POOL_CP_XML_XML_TYPES_H
#define CONN_POOL_CP_XML_XML_TYPES_H
#include "db/db_connection.h"
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <variant>
#include <vector>

namespace cp {

class SqlParams;
class IterableValue;

// ================= XML 参数值 =================
using XMLParamValue = std::variant<
    std::monostate, std::int64_t, std::uint64_t, double, std::string,
    std::shared_ptr<SqlParams>, std::shared_ptr<IterableValue>>;

class IterableValue {
public:
    virtual ~IterableValue() = default;
    virtual size_t size() const = 0;
    virtual std::pair<XMLParamValue, XMLParamValue> at(size_t i) const = 0;
    virtual bool isMap() const { return false; }
};

class ListIterable final : public IterableValue {
public:
    explicit ListIterable(std::vector<XMLParamValue> items) : items_(std::move(items)) {}
    size_t size() const override { return items_.size(); }
    std::pair<XMLParamValue, XMLParamValue> at(size_t i) const override {
        return { XMLParamValue(static_cast<std::int64_t>(i)), items_[i] };
    }
private:
    std::vector<XMLParamValue> items_;
};

class MapIterable final : public IterableValue {
public:
    using Entry = std::pair<XMLParamValue, XMLParamValue>;
    explicit MapIterable(std::vector<Entry> entries) : entries_(std::move(entries)) {}
    size_t size() const override { return entries_.size(); }
    std::pair<XMLParamValue, XMLParamValue> at(size_t i) const override { return entries_[i]; }
    bool isMap() const override { return true; }
private:
    std::vector<Entry> entries_;
};

template <typename T> struct is_pair_x : std::false_type {};
template <typename A, typename B> struct is_pair_x<std::pair<A,B>> : std::true_type {};
template <typename T> inline constexpr bool is_pair_x_v = is_pair_x<std::decay_t<T>>::value;

template <typename T> struct is_iter_x : std::false_type {};
template <> struct is_iter_x<std::shared_ptr<IterableValue>> : std::true_type {};

template <typename T> XMLParamValue toParamValue(T&& v);

template <typename C>
std::shared_ptr<IterableValue> makeIterableFrom(C&& container) {
    using U = std::decay_t<C>;
    using VT = typename U::value_type;
    if constexpr (is_pair_x_v<VT>) {
        std::vector<std::pair<XMLParamValue, XMLParamValue>> entries;
        for (auto&& kv : container)
            entries.emplace_back(toParamValue(kv.first), toParamValue(kv.second));
        return std::make_shared<MapIterable>(std::move(entries));
    } else {
        std::vector<XMLParamValue> vs;
        for (auto&& v : container) vs.push_back(toParamValue(v));
        return std::make_shared<ListIterable>(std::move(vs));
    }
}

template <typename It>
std::shared_ptr<IterableValue> makeIterableRange(It begin, It end) {
    std::vector<XMLParamValue> vs;
    for (auto it = begin; it != end; ++it) vs.push_back(toParamValue(*it));
    return std::make_shared<ListIterable>(std::move(vs));
}

template <typename T>
XMLParamValue toParamValue(T&& v) {
    using U = std::decay_t<T>;
    if constexpr (std::is_same_v<U, XMLParamValue>) return v;
    else if constexpr (std::is_same_v<U, std::monostate>) return XMLParamValue(std::monostate{});
    else if constexpr (std::is_same_v<U, std::string>) return XMLParamValue(std::move(v));
    else if constexpr (std::is_same_v<U, bool>) return XMLParamValue(static_cast<std::int64_t>(v ? 1 : 0));
    else if constexpr (std::is_same_v<U, std::shared_ptr<IterableValue>>) return XMLParamValue(std::move(v));
    else if constexpr (std::is_same_v<U, std::shared_ptr<SqlParams>>) return XMLParamValue(std::move(v));
    else if constexpr (std::is_convertible_v<U, const char*>) return XMLParamValue(std::string(v ? v : ""));
    else if constexpr (std::is_integral_v<U> && std::is_signed_v<U>) return XMLParamValue(static_cast<std::int64_t>(v));
    else if constexpr (std::is_integral_v<U> && std::is_unsigned_v<U>) return XMLParamValue(static_cast<std::uint64_t>(v));
    else if constexpr (std::is_floating_point_v<U>) return XMLParamValue(static_cast<double>(v));
    else if constexpr (std::is_same_v<U, SqlParams>) return XMLParamValue(std::make_shared<SqlParams>(std::move(v)));
    else if constexpr (std::is_same_v<U, std::vector<XMLParamValue>>) return XMLParamValue(std::make_shared<ListIterable>(std::move(v)));
    else return XMLParamValue(makeIterableFrom(std::forward<T>(v)));
}

class SqlParams {
public:
    SqlParams() = default;
    template <typename T>
    void set(std::string name, T&& v) { values_[std::move(name)] = toParamValue(std::forward<T>(v)); }
    template <typename C>
    void setIterable(std::string name, C&& c) {
        values_[std::move(name)] = XMLParamValue(makeIterableFrom(std::forward<C>(c)));
    }
    template <typename Map>
    void setMap(std::string name, const Map& m) {
        values_[std::move(name)] = XMLParamValue(makeIterableFrom(m));
    }
    template <typename It>
    void setIteratorRange(std::string name, It b, It e) {
        values_[std::move(name)] = XMLParamValue(makeIterableRange(b, e));
    }
    const XMLParamValue* find(const std::string& name) const {
        auto it = values_.find(name);
        return it == values_.end() ? nullptr : &it->second;
    }
    bool has(const std::string& name) const { return values_.count(name) > 0; }
    bool empty() const { return values_.empty(); }
    const XMLParamValue* resolve(const std::string& path) const;
    SqlParams child(const std::string& name, XMLParamValue v) const {
        SqlParams c = *this;
        c.values_[name] = std::move(v);
        return c;
    }
private:
    std::unordered_map<std::string, XMLParamValue> values_;
};

// ================= SQL 节点树 =================
enum class SqlNodeType {
    Container, Text, If, Where, Set, Trim, Foreach, Choose, When, Otherwise,
    Include, Bind
};

struct SqlNode {
    SqlNodeType type = SqlNodeType::Text;
    std::string text, test;
    std::string prefix, suffix, prefixOverrides, suffixOverrides;
    std::string collection, item, index;
    std::string open, close, separator;
    bool        nullable = true;
    std::string refid;
    std::string bindName, bindValue;
    std::vector<std::shared_ptr<SqlNode>> children;
};

enum class SqlType { Dql, Dml, Ddl, Dcl };

struct ResultMapDef {
    std::string id, typeName;
    std::vector<std::pair<std::string, std::string>> mappings;
};

struct SelectKey {
    std::string order = "AFTER", keyProperty, resultType;
    std::shared_ptr<SqlNode> node;
};

// ================= 新增：XML 上的事务声明 =================
struct TxDeclaration {
    bool        hasDecl = false;
    std::string propagation;   // "REQUIRED"/"REQUIRES_NEW"/...，空 = 未声明
    std::string isolation;     // "READ_COMMITTED"/...，空 = 未声明
    bool        readOnly = false;
    int         timeoutSec = -1;
};

struct SqlStatement {
    SqlType     type = SqlType::Dql;
    std::string category, id, datasource;
    std::shared_ptr<SqlNode> root;
    std::string resultMapId;
    std::shared_ptr<SelectKey> selectKey;
    TxDeclaration tx;            // ← 新增
};

struct BoundSql {
    std::string sql;
    std::vector<::cp::ParamValue> positionalParams;
};

// ================= 工具 =================
inline std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return {};
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}
inline std::string valueToString(const XMLParamValue& v) {
    if (std::holds_alternative<std::monostate>(v)) return {};
    if (auto i = std::get_if<std::int64_t>(&v))   return std::to_string(*i);
    if (auto u = std::get_if<std::uint64_t>(&v))  return std::to_string(*u);
    if (auto d = std::get_if<double>(&v)) {
        char buf[64]; std::snprintf(buf, sizeof(buf), "%g", *d); return buf;
    }
    if (auto s = std::get_if<std::string>(&v))    return *s;
    return {};
}
inline std::string toUpper(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return s;
}
inline std::vector<std::string> splitTopLevel(const std::string& s, const std::string& sep) {
    std::vector<std::string> out;
    size_t pos = 0;
    while (true) {
        size_t p = s.find(sep, pos);
        if (p == std::string::npos) { out.push_back(s.substr(pos)); return out; }
        out.push_back(s.substr(pos, p - pos));
        pos = p + sep.size();
    }
}
} // namespace cp

#endif // CONN_POOL_CP_XML_XML_TYPES_H