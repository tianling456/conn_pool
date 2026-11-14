#include "xml/bound_sql_builder.h"
#include "xml/expr_evaluator.h"
#include <cstdlib>
#include <stdexcept>

namespace cp {

BoundSqlBuilder::BoundSqlBuilder(const SqlParams& params,
                                 std::shared_ptr<ISubstitutionPolicy> policy)
    : params_(params), policy_(std::move(policy)) {}

BoundSql BoundSqlBuilder::build(const std::shared_ptr<SqlNode>& root) {
    BoundSql out;
    SqlParams scope = params_;
    walk(root, out, scope);
    return out;
}

void BoundSqlBuilder::walk(const std::shared_ptr<SqlNode>& n,
                           BoundSql& out, SqlParams& scope) {
    if (!n) return;

    switch (n->type) {
    case SqlNodeType::Bind: {
        XMLParamValue v = evalValueExpr(n->bindValue, scope);
        scope.set(n->bindName, std::move(v));
        return;
    }
    case SqlNodeType::Container:
        for (auto& c : n->children) walk(c, out, scope);
        break;
    case SqlNodeType::Text:
        if (!n->text.empty()) emitText(n->text, out, scope);
        for (auto& c : n->children) walk(c, out, scope);
        break;
    case SqlNodeType::If:
        if (ExprEvaluator(scope).eval(n->test))
            for (auto& c : n->children) walk(c, out, scope);
        break;
    case SqlNodeType::Where: {
        BoundSql body;
        for (auto& c : n->children) walk(c, body, scope);
        std::string s = trim(body.sql);
        if (s.empty()) return;
        if (s.size() >= 4 && toUpper(s.substr(0, 4)) == "AND ") s = s.substr(4);
        else if (s.size() >= 3 && toUpper(s.substr(0, 3)) == "OR ") s = s.substr(3);
        out.sql += " WHERE ";
        out.sql += s;
        out.positionalParams.insert(out.positionalParams.end(),
                                    body.positionalParams.begin(),
                                    body.positionalParams.end());
        break;
    }
    case SqlNodeType::Set: {
        BoundSql body;
        for (auto& c : n->children) walk(c, body, scope);
        std::string s = trim(body.sql);
        while (!s.empty() && (s.back() == ',' || s.back() == ' ')) s.pop_back();
        if (s.empty()) return;
        out.sql += " SET ";
        out.sql += s;
        out.positionalParams.insert(out.positionalParams.end(),
                                    body.positionalParams.begin(),
                                    body.positionalParams.end());
        break;
    }
    case SqlNodeType::Trim: {
        BoundSql body;
        for (auto& c : n->children) walk(c, body, scope);
        std::string s = trim(body.sql);
        if (s.empty()) return;
        if (!n->prefixOverrides.empty()) {
            auto po = toUpper(trim(n->prefixOverrides));
            if (s.size() >= po.size() && toUpper(s.substr(0, po.size())) == po)
                s = trim(s.substr(po.size()));
        }
        if (!n->suffixOverrides.empty()) {
            auto so = toUpper(trim(n->suffixOverrides));
            if (s.size() >= so.size()
                && toUpper(s.substr(s.size() - so.size())) == so)
                s = trim(s.substr(0, s.size() - so.size()));
        }
        if (!n->prefix.empty()) out.sql += n->prefix;
        out.sql += s;
        if (!n->suffix.empty()) out.sql += n->suffix;
        out.positionalParams.insert(out.positionalParams.end(),
                                    body.positionalParams.begin(),
                                    body.positionalParams.end());
        break;
    }
    case SqlNodeType::Foreach: {
        auto* coll = scope.resolve(n->collection);
        if (!coll) {
            if (!n->nullable)
                throw std::runtime_error("<foreach> collection null: " + n->collection);
            return;
        }
        std::shared_ptr<IterableValue> iter;
        if (auto p = std::get_if<std::shared_ptr<IterableValue>>(coll))
            iter = *p;
        if (!iter) {
            if (!n->nullable)
                throw std::runtime_error("<foreach> not iterable");
            return;
        }
        if (iter->size() == 0) return;

        const std::string open  = expandTemplate(n->open, scope);
        const std::string close = expandTemplate(n->close, scope);
        const std::string sep   = expandTemplate(n->separator, scope);

        bool firstEmitted = false;
        for (size_t i = 0; i < iter->size(); ++i) {
            auto [k, v] = iter->at(i);
            SqlParams child = scope;
            if (iter->isMap()) {
                if (!n->index.empty()) child.set(n->index, std::move(k));
                child.set(n->item, std::move(v));
            } else {
                child.set(n->item, std::move(v));
                if (!n->index.empty()) child.set(n->index, std::move(k));
            }
            BoundSql itemOut;
            for (auto& c : n->children) walk(c, itemOut, child);
            if (trim(itemOut.sql).empty()) continue;
            if (firstEmitted) out.sql += sep;
            else {
                if (!open.empty()) out.sql += open;
                firstEmitted = true;
            }
            out.sql += itemOut.sql;
            out.positionalParams.insert(out.positionalParams.end(),
                                        itemOut.positionalParams.begin(),
                                        itemOut.positionalParams.end());
        }
        if (firstEmitted && !close.empty()) out.sql += close;
        break;
    }
    case SqlNodeType::Choose: {
        for (auto& c : n->children) {
            if (c->type == SqlNodeType::When && ExprEvaluator(scope).eval(c->test)) {
                for (auto& cc : c->children) walk(cc, out, scope);
                return;
            }
            if (c->type == SqlNodeType::Otherwise) {
                for (auto& cc : c->children) walk(cc, out, scope);
                return;
            }
        }
        break;
    }
    case SqlNodeType::When:
    case SqlNodeType::Otherwise:
    case SqlNodeType::Include:
        break;
    }
}

void BoundSqlBuilder::emitText(const std::string& text,
                               BoundSql& out, const SqlParams& scope) {
    size_t i = 0;
    while (i < text.size()) {
        if (text.compare(i, 2, "#{") == 0) {
            size_t end = text.find('}', i + 2);
            if (end == std::string::npos) { out.sql += text.substr(i); return; }
            std::string name = trim(text.substr(i + 2, end - i - 2));
            auto* v = scope.resolve(name);
            if (!v) throw std::runtime_error("missing param: #{" + name + "}");
            out.sql += '?';
            // 转成底层 ParamValue（拆掉 monostate）
            if (std::holds_alternative<std::monostate>(*v))
                out.positionalParams.emplace_back(std::monostate{});
            else if (auto i1 = std::get_if<std::int64_t>(v))
                out.positionalParams.emplace_back(*i1);
            else if (auto u1 = std::get_if<std::uint64_t>(v))
                out.positionalParams.emplace_back(*u1);
            else if (auto d1 = std::get_if<double>(v))
                out.positionalParams.emplace_back(*d1);
            else if (auto s1 = std::get_if<std::string>(v))
                out.positionalParams.emplace_back(*s1);
            else
                throw std::runtime_error("cannot bind complex param: " + name);
            i = end + 1;
            continue;
        }
        if (text.compare(i, 2, "${") == 0) {
            size_t end = text.find('}', i + 2);
            if (end == std::string::npos) { out.sql += text.substr(i); return; }
            std::string name = trim(text.substr(i + 2, end - i - 2));
            auto* v = scope.resolve(name);
            if (!v) throw std::runtime_error("missing param: ${" + name + "}");
            if (!policy_)
                throw std::runtime_error("${" + name + "} requires substitution policy");
            out.sql += policy_->resolve(name, *v);
            i = end + 1;
            continue;
        }
        out.sql += text[i++];
    }
}

std::string BoundSqlBuilder::expandTemplate(const std::string& tpl,
                                            const SqlParams& scope) const {
    if (tpl.empty() || tpl.find("${") == std::string::npos) return tpl;
    std::string out;
    size_t i = 0;
    while (i < tpl.size()) {
        if (tpl.compare(i, 2, "${") == 0) {
            size_t end = tpl.find('}', i + 2);
            if (end == std::string::npos) { out += tpl.substr(i); break; }
            std::string inner = trim(tpl.substr(i + 2, end - i - 2));
            try {
                XMLParamValue v = evalValueExpr(inner, scope);
                out += valueToString(v);
            } catch (...) {
                out += "${" + inner + "}";
            }
            i = end + 1;
        } else out += tpl[i++];
    }
    return out;
}

XMLParamValue BoundSqlBuilder::evalValueExpr(const std::string& expr,
                                             const SqlParams& scope) {
    auto parts = splitTopLevel(expr, "+");
    if (parts.size() == 1) return evalSingleValue(parts[0], scope);
    std::string result;
    for (auto& p : parts) result += valueToString(evalSingleValue(p, scope));
    return result;
}

XMLParamValue BoundSqlBuilder::evalSingleValue(const std::string& s,
                                               const SqlParams& scope) {
    std::string t = trim(s);
    if (t.empty()) return std::monostate{};

    if (t.size() > 7 && t.compare(t.size() - 7, 7, ".size()") == 0)
        return static_cast<std::int64_t>(
            evalSize(trim(t.substr(0, t.size() - 7)), scope));
    if (t.size() > 9 && t.compare(t.size() - 9, 9, ".length()") == 0)
        return static_cast<std::int64_t>(
            evalSize(trim(t.substr(0, t.size() - 9)), scope));
    if (t.size() > 8 && t.compare(0, 7, "length(") == 0 && t.back() == ')')
        return static_cast<std::int64_t>(
            evalSize(trim(t.substr(7, t.size() - 8)), scope));

    if (t.size() >= 2 && (t.front() == '\'' || t.front() == '"')
        && t.back() == t.front())
        return t.substr(1, t.size() - 2);

    char* end = nullptr;
    long long ll = std::strtoll(t.c_str(), &end, 10);
    if (end && *end == '\0') return static_cast<std::int64_t>(ll);
    double d = std::strtod(t.c_str(), &end);
    if (end && *end == '\0') return d;

    auto* v = scope.resolve(t);
    if (!v) throw std::runtime_error("<bind> unresolved: " + t);
    return *v;
}

size_t BoundSqlBuilder::evalSize(const std::string& path, const SqlParams& scope) {
    auto* v = scope.resolve(path);
    if (!v) return 0;
    if (auto it = std::get_if<std::shared_ptr<IterableValue>>(v))
        return *it ? (*it)->size() : 0;
    if (auto s = std::get_if<std::string>(v)) return s->size();
    return 0;
}

} // namespace cp