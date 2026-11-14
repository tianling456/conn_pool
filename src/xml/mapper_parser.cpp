#include "xml/mapper_parser.h"
#include <pugixml.hpp>
#include <stdexcept>

namespace cp {
namespace {
std::shared_ptr<SqlNode> newText(std::string s) {
    auto n = std::make_shared<SqlNode>();
    n->type = SqlNodeType::Text;
    n->text = std::move(s);
    return n;
}

void parseTxAttrs(const pugi::xml_node& child, TxDeclaration& tx) {
    if (auto a = child.attribute("tx-propagation")) {
        tx.propagation = a.as_string();
        tx.hasDecl = true;
    }
    if (auto a = child.attribute("tx-isolation")) {
        tx.isolation = a.as_string();
        tx.hasDecl = true;
    }
    if (auto a = child.attribute("tx-read-only")) {
        tx.readOnly = a.as_bool();
        tx.hasDecl = true;
    }
    if (auto a = child.attribute("tx-timeout-sec")) {
        tx.timeoutSec = a.as_int();
        tx.hasDecl = true;
    }
}
}

ParsedFile MapperParser::parseFile(const std::string& path) {
    pugi::xml_document doc;
    if (!doc.load_file(path.c_str()))
        throw std::runtime_error("parse xml failed: " + path);
    auto root = doc.child("mappers");
    if (!root) throw std::runtime_error("root must be <mappers>");

    ParsedFile pf;

    for (auto mapper : root.children("mapper")) {
        std::string ns    = mapper.attribute("namespace").as_string();
        std::string defDs = mapper.attribute("default-datasource").as_string();

        for (auto rm : mapper.children("resultMap")) {
            ResultMapDef d;
            d.id       = rm.attribute("id").as_string();
            d.typeName = rm.attribute("type").as_string();
            for (auto r : rm.children("result"))
                d.mappings.emplace_back(r.attribute("column").as_string(),
                                        r.attribute("property").as_string());
            if (!ns.empty()) d.id = ns + "." + d.id;
            pf.resultMaps.push_back(std::move(d));
        }

        std::unordered_map<std::string, std::shared_ptr<SqlNode>> frags;
        for (auto frag : mapper.children("sql")) {
            std::string fid = frag.attribute("id").as_string();
            if (fid.empty()) throw std::runtime_error("<sql> missing id");
            frags[fid] = parseRoot(&frag);
        }

        for (auto child : mapper.children()) {
            std::string tag = child.name();
            if (tag == "sql" || tag == "resultMap") continue;

            SqlType type;
            if      (tag == "select") type = SqlType::Dql;
            else if (tag == "insert" || tag == "update" || tag == "delete")
                                       type = SqlType::Dml;
            else if (tag == "ddl")    type = SqlType::Ddl;
            else if (tag == "dcl")    type = SqlType::Dcl;
            else continue;

            SqlStatement st;
            st.type     = type;
            st.category = tag;
            std::string id = child.attribute("id").as_string();
            if (id.empty()) throw std::runtime_error("missing id in <" + tag + ">");
            st.id = ns.empty() ? id : ns + "." + id;
            st.datasource = child.attribute("datasource").as_string();
            if (st.datasource.empty()) st.datasource = defDs;

            std::string rmId = child.attribute("resultMap").as_string();
            if (!rmId.empty()) st.resultMapId = ns.empty() ? rmId : ns + "." + rmId;

            parseTxAttrs(child, st.tx);   // ← 新增：解析 tx-* 属性

            for (auto sk : child.children("selectKey")) {
                auto s = std::make_shared<SelectKey>();
                s->order       = sk.attribute("order").as_string("AFTER");
                s->keyProperty = sk.attribute("keyProperty").as_string();
                s->resultType  = sk.attribute("resultType").as_string();
                s->node        = parseRoot(&sk);
                st.selectKey   = std::move(s);
            }

            st.root = parseRoot(&child);
            std::set<std::string> stack;
            st.root = expandIncludes(st.root, frags, stack);
            pf.statements.push_back(std::move(st));
        }
    }
    return pf;
}

std::shared_ptr<SqlNode> MapperParser::parseRoot(const void* p) {
    auto& x = *static_cast<const pugi::xml_node*>(p);
    auto n = std::make_shared<SqlNode>();
    n->type = SqlNodeType::Container;
    std::string pending;
    for (auto c : x.children()) {
        if (c.type() == pugi::node_pcdata || c.type() == pugi::node_cdata)
            pending += c.value();
        else if (c.type() == pugi::node_element) {
            if (!pending.empty()) { n->children.push_back(newText(std::move(pending))); pending.clear(); }
            auto sub = parseNode(&c);
            if (sub) n->children.push_back(sub);
        }
    }
    if (!pending.empty()) n->children.push_back(newText(std::move(pending)));
    return n;
}

std::shared_ptr<SqlNode> MapperParser::parseNode(const void* p) {
    auto& x = *static_cast<const pugi::xml_node*>(p);
    std::string name = x.name();
    if (name == "selectKey") return nullptr;
    auto n = std::make_shared<SqlNode>();

    if      (name == "if")      { n->type = SqlNodeType::If; n->test = x.attribute("test").as_string(); }
    else if (name == "where")   { n->type = SqlNodeType::Where; }
    else if (name == "set")     { n->type = SqlNodeType::Set; }
    else if (name == "trim") {
        n->type = SqlNodeType::Trim;
        n->prefix          = x.attribute("prefix").as_string();
        n->suffix          = x.attribute("suffix").as_string();
        n->prefixOverrides = x.attribute("prefixOverrides").as_string();
        n->suffixOverrides = x.attribute("suffixOverrides").as_string();
    } else if (name == "foreach") {
        n->type = SqlNodeType::Foreach;
        n->collection = x.attribute("collection").as_string();
        n->item       = x.attribute("item").as_string("item");
        n->index      = x.attribute("index").as_string();
        n->open       = x.attribute("open").as_string();
        n->close      = x.attribute("close").as_string();
        n->separator  = x.attribute("separator").as_string();
        n->nullable   = x.attribute("nullable").as_bool(true);
    } else if (name == "choose")    { n->type = SqlNodeType::Choose; }
    else if (name == "when")        { n->type = SqlNodeType::When; n->test = x.attribute("test").as_string(); }
    else if (name == "otherwise")   { n->type = SqlNodeType::Otherwise; }
    else if (name == "include")     { n->type = SqlNodeType::Include; n->refid = x.attribute("refid").as_string(); }
    else if (name == "bind") {
        n->type = SqlNodeType::Bind;
        n->bindName  = x.attribute("name").as_string();
        n->bindValue = x.attribute("value").as_string();
    } else {
        n->type = SqlNodeType::Text;
        n->text = x.text().get();
        return n;
    }

    std::string pending;
    for (auto c : x.children()) {
        if (c.type() == pugi::node_pcdata || c.type() == pugi::node_cdata)
            pending += c.value();
        else if (c.type() == pugi::node_element) {
            if (!pending.empty()) { n->children.push_back(newText(std::move(pending))); pending.clear(); }
            auto sub = parseNode(&c);
            if (sub) n->children.push_back(sub);
        }
    }
    if (!pending.empty()) n->children.push_back(newText(std::move(pending)));
    return n;
}

std::shared_ptr<SqlNode> MapperParser::expandIncludes(
        std::shared_ptr<SqlNode> node,
        const std::unordered_map<std::string, std::shared_ptr<SqlNode>>& frags,
        std::set<std::string>& stack) {
    if (!node) return node;
    if (node->type == SqlNodeType::Include) {
        auto it = frags.find(node->refid);
        if (it == frags.end()) throw std::runtime_error("include not found: " + node->refid);
        if (stack.count(node->refid)) throw std::runtime_error("circular include: " + node->refid);
        stack.insert(node->refid);
        auto copy = deepCopy(it->second);
        auto exp  = expandIncludes(std::move(copy), frags, stack);
        stack.erase(node->refid);
        return exp;
    }
    for (auto& c : node->children)
        c = expandIncludes(std::move(c), frags, stack);
    return node;
}

std::shared_ptr<SqlNode> MapperParser::deepCopy(const std::shared_ptr<SqlNode>& n) {
    if (!n) return nullptr;
    auto c = std::make_shared<SqlNode>(*n);
    c->children.clear();
    for (auto& ch : n->children) c->children.push_back(deepCopy(ch));
    return c;
}
} // namespace cp