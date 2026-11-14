#ifndef CONN_POOL_MAPPER_PARSER_H
#define CONN_POOL_MAPPER_PARSER_H



#include "xml/xml_types.h"
#include <set>
#include <unordered_map>
#include <vector>

namespace cp {

    struct ParsedFile {
        std::vector<SqlStatement> statements;
        std::vector<ResultMapDef> resultMaps;
    };

    class MapperParser {
    public:
        static ParsedFile parseFile(const std::string& path);

    private:
        static std::shared_ptr<SqlNode> parseRoot(const void* xmlNode);
        static std::shared_ptr<SqlNode> parseNode(const void* xmlNode);
        static std::shared_ptr<SqlNode> parseSelectKey(const void* xmlNode);
        static std::shared_ptr<SqlNode> expandIncludes(
            std::shared_ptr<SqlNode> node,
            const std::unordered_map<std::string, std::shared_ptr<SqlNode>>& frags,
            std::set<std::string>& stack);
        static std::shared_ptr<SqlNode> deepCopy(const std::shared_ptr<SqlNode>& n);
    };

} // namespace cp

#endif //CONN_POOL_MAPPER_PARSER_H