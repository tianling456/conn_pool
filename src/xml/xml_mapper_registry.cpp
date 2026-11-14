#include "xml/xml_mapper_registry.h"
#include "xml/mapper_parser.h"
#include <stdexcept>

namespace cp {
    void XmlMapperRegistry::loadFile(const std::string& path) {
        auto pf = MapperParser::parseFile(path);
        for (auto& rm : pf.resultMaps) {
            if (resultMaps_.count(rm.id)) throw std::runtime_error("dup resultMap: " + rm.id);
            resultMaps_.emplace(rm.id, std::move(rm));
        }
        for (auto& st : pf.statements) {
            if (stmts_.count(st.id)) throw std::runtime_error("dup SQL: " + st.id);
            stmts_.emplace(st.id, std::move(st));
        }
    }
    void XmlMapperRegistry::loadFiles(const std::vector<std::string>& paths) {
        for (auto& p : paths) loadFile(p);
    }
    const SqlStatement& XmlMapperRegistry::get(const std::string& id) const {
        auto it = stmts_.find(id);
        if (it == stmts_.end()) throw std::runtime_error("SQL not found: " + id);
        return it->second;
    }
    const ResultMapDef& XmlMapperRegistry::getResultMap(const std::string& id) const {
        auto it = resultMaps_.find(id);
        if (it == resultMaps_.end()) throw std::runtime_error("resultMap not found: " + id);
        return it->second;
    }
} // namespace cp