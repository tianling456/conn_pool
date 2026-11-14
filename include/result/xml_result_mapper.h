//
// Created by aobo on 2026/11/8.
//

#ifndef CONN_POOL_XML_RESULT_MAPPER_H
#define CONN_POOL_XML_RESULT_MAPPER_H

#include "result/reflection_registry.h"
#include "result/result_mapper.h"
#include "xml/xml_types.h"
#include <memory>
#include <stdexcept>
#include <vector>

namespace cp {
    template <typename T>
    class XmlResultMapper : public IResultMapper<T> {
    public:
        explicit XmlResultMapper(const ResultMapDef& def) : mappings_(def.mappings) {
            for (auto& [col, prop] : mappings_)
                if (!ReflectionRegistry<T>::has(prop))
                    throw std::runtime_error("resultMap unregistered: " + prop);
        }
        T map(const ResultRow& row) override {
            T obj{};
            for (auto& [col, prop] : mappings_) {
                auto it = row.find(col);
                if (it == row.end()) continue;
                if (auto* setter = ReflectionRegistry<T>::find(prop))
                    (*setter)(obj, it->second);
            }
            return obj;
        }
    private:
        std::vector<std::pair<std::string, std::string>> mappings_;
    };
} // namespace cp

#endif //CONN_POOL_XML_RESULT_MAPPER_H
