#include "xml/expr_evaluator.h"
#include <cstdlib>

namespace cp {

bool ExprEvaluator::eval(const std::string& expr) const {
    std::string s = trim(expr);
    if (s.empty()) return true;

    auto orParts = splitTopLevel(s, " or ");
    if (orParts.size() > 1) {
        for (auto& p : orParts) if (eval(p)) return true;
        return false;
    }
    auto andParts = splitTopLevel(s, " and ");
    if (andParts.size() > 1) {
        for (auto& p : andParts) if (!eval(p)) return false;
        return true;
    }
    return evalAtom(s);
}

bool ExprEvaluator::evalAtom(const std::string& s) const {
    if (!s.empty() && s.front() == '!' && (s.size() == 1 || s[1] != '='))
        return !evalAtom(trim(s.substr(1)));

    if (auto b = evalBoolFunc(s)) return *b;

    static const char* ops[] = {"==", "!=", ">=", "<=", ">", "<"};
    for (auto* op : ops) {
        size_t p = s.find(op);
        if (p == std::string::npos) continue;
        return compare(trim(s.substr(0, p)), op,
                       trim(s.substr(p + std::strlen(op))));
    }
    return truthy(s);
}

bool ExprEvaluator::truthy(const std::string& path) const {
    auto* v = params_.resolve(path);
    if (!v) return false;
    if (std::holds_alternative<std::monostate>(*v)) return false;
    if (auto s = std::get_if<std::string>(v))   return !s->empty();
    if (auto p = std::get_if<std::shared_ptr<SqlParams>>(v))
        return p->get() != nullptr;
    if (auto it = std::get_if<std::shared_ptr<IterableValue>>(v))
        return it->get() && (*it)->size() > 0;
    return true;
}

bool ExprEvaluator::compare(const std::string& lhs, const std::string& op,
                            const std::string& rhs) const {
    if (rhs == "null") {
        auto* v = params_.resolve(lhs);
        bool isNull = !v || std::holds_alternative<std::monostate>(*v);
        if (op == "==") return isNull;
        if (op == "!=") return !isNull;
        return false;
    }
    if (rhs == "''" || rhs == "\"\"") {
        auto* v = params_.resolve(lhs);
        if (!v) return op == "==";
        if (auto s = std::get_if<std::string>(v))
            return (op == "==") ? s->empty() : !s->empty();
        if (auto it = std::get_if<std::shared_ptr<IterableValue>>(v)) {
            bool empty = !*it || (*it)->size() == 0;
            return (op == "==") ? empty : !empty;
        }
        return op == "!=";
    }
    if (!rhs.empty() && (rhs.front() == '\'' || rhs.front() == '"')
        && rhs.back() == rhs.front() && rhs.size() >= 2) {
        std::string lit = rhs.substr(1, rhs.size() - 2);
        auto* v = params_.resolve(lhs);
        if (!v) return false;
        if (auto s = std::get_if<std::string>(v)) {
            if (op == "==") return *s == lit;
            if (op == "!=") return *s != lit;
        }
        return false;
    }
    auto lv = evalNumeric(lhs);
    auto rv = evalNumeric(rhs);
    if (lv && rv) {
        if (op == "==") return *lv == *rv;
        if (op == "!=") return *lv != *rv;
        if (op == ">")  return *lv >  *rv;
        if (op == ">=") return *lv >= *rv;
        if (op == "<")  return *lv <  *rv;
        if (op == "<=") return *lv <= *rv;
    }
    return false;
}

std::optional<bool> ExprEvaluator::evalBoolFunc(const std::string& expr) const {
    std::string t = trim(expr);

    if (t.size() > 10 && t.compare(t.size() - 10, 10, ".isEmpty()") == 0) {
        std::string path = trim(t.substr(0, t.size() - 10));
        auto* v = params_.resolve(path);
        if (!v) return true;
        if (auto it = std::get_if<std::shared_ptr<IterableValue>>(v))
            return !*it || (*it)->size() == 0;
        if (auto s = std::get_if<std::string>(v)) return s->empty();
        return false;
    }
    if (t.size() > 6 && t.compare(0, 6, "empty(") == 0 && t.back() == ')') {
        std::string path = trim(t.substr(6, t.size() - 7));
        auto* v = params_.resolve(path);
        if (!v) return true;
        if (auto it = std::get_if<std::shared_ptr<IterableValue>>(v))
            return !*it || (*it)->size() == 0;
        if (auto s = std::get_if<std::string>(v)) return s->empty();
        return false;
    }
    if (t.size() > 9 && t.compare(0, 9, "contains(") == 0 && t.back() == ')') {
        auto parts = splitTopLevel(t.substr(9, t.size() - 10), ",");
        if (parts.size() == 2)
            return iterableContains(trim(parts[0]), trim(parts[1]));
    }
    if (t.size() > 3 && t.compare(0, 3, "in(") == 0 && t.back() == ')') {
        auto parts = splitTopLevel(t.substr(3, t.size() - 4), ",");
        if (parts.size() == 2)
            return iterableContains(trim(parts[1]), trim(parts[0]));
    }
    return std::nullopt;
}

std::optional<double> ExprEvaluator::evalNumeric(const std::string& expr) const {
    std::string t = trim(expr);
    if (t.empty()) return std::nullopt;

    if (t.size() > 7 && t.compare(t.size() - 7, 7, ".size()") == 0)
        return static_cast<double>(iterableSize(trim(t.substr(0, t.size() - 7))));
    if (t.size() > 9 && t.compare(t.size() - 9, 9, ".length()") == 0)
        return static_cast<double>(iterableSize(trim(t.substr(0, t.size() - 9))));
    if (t.size() > 8 && t.compare(0, 7, "length(") == 0 && t.back() == ')')
        return static_cast<double>(iterableSize(trim(t.substr(7, t.size() - 8))));

    char* end = nullptr;
    double d = std::strtod(t.c_str(), &end);
    if (end && *end == '\0') return d;

    auto* v = params_.resolve(t);
    if (!v) return std::nullopt;
    if (auto i  = std::get_if<std::int64_t>(v))  return static_cast<double>(*i);
    if (auto u  = std::get_if<std::uint64_t>(v)) return static_cast<double>(*u);
    if (auto dv = std::get_if<double>(v))        return *dv;
    if (auto s  = std::get_if<std::string>(v)) {
        char* e2 = nullptr;
        double d2 = std::strtod(s->c_str(), &e2);
        if (e2 && *e2 == '\0') return d2;
    }
    return std::nullopt;
}

size_t ExprEvaluator::iterableSize(const std::string& path) const {
    auto* v = params_.resolve(path);
    if (!v) return 0;
    if (auto it = std::get_if<std::shared_ptr<IterableValue>>(v))
        return *it ? (*it)->size() : 0;
    if (auto s = std::get_if<std::string>(v)) return s->size();
    return 0;
}

bool ExprEvaluator::iterableContains(const std::string& path,
                                     const std::string& valExpr) const {
    auto* v = params_.resolve(path);
    if (!v) return false;
    auto it = std::get_if<std::shared_ptr<IterableValue>>(v);
    if (!it || !*it) return false;

    XMLParamValue target;
    std::string t = trim(valExpr);
    if (t.size() >= 2 && (t.front() == '\'' || t.front() == '"')
        && t.back() == t.front()) {
        target = t.substr(1, t.size() - 2);
    } else {
        char* end = nullptr;
        long long ll = std::strtoll(t.c_str(), &end, 10);
        if (end && *end == '\0') target = static_cast<std::int64_t>(ll);
        else {
            auto* pv = params_.resolve(t);
            if (!pv) return false;
            target = *pv;
        }
    }
    std::string targetStr = valueToString(target);
    auto& iter = **it;
    for (size_t i = 0; i < iter.size(); ++i)
        if (valueToString(iter.at(i).second) == targetStr) return true;
    return false;
}

} // namespace cp