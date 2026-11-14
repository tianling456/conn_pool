#ifndef CONN_POOL_XML_MAPPER_REGISTRY_H
#define CONN_POOL_XML_MAPPER_REGISTRY_H



#include "xml/xml_types.h"
#include <string>
#include <unordered_map>
#include <vector>

namespace cp {
    class XmlMapperRegistry {
    public:
        void loadFile(const std::string& path);
        void loadFiles(const std::vector<std::string>& paths);
        const SqlStatement& get(const std::string& id) const;
        const ResultMapDef& getResultMap(const std::string& id) const;
        bool hasResultMap(const std::string& id) const { return resultMaps_.count(id) > 0; }
        size_t size() const { return stmts_.size(); }
    private:
        std::unordered_map<std::string, SqlStatement> stmts_;
        std::unordered_map<std::string, ResultMapDef> resultMaps_;
    };
} // namespace cp

#endif //CONN_POOL_XML_MAPPER_REGISTRY_H