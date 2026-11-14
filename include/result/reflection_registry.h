#ifndef CONN_POOL_REFLECTION_REGISTRY_H
#define CONN_POOL_REFLECTION_REGISTRY_H



#include "result/result_mapper.h"
#include "result/type_handler.h"
#include <functional>
#include <string>
#include <unordered_map>

namespace cp {
    template <typename T>
    class ReflectionRegistry {
    public:
        using Setter = std::function<void(T&, const ColumnValue&)>;
        static void registerProperty(std::string name, Setter setter) {
            setters()[std::move(name)] = std::move(setter);
        }
        static const Setter* find(const std::string& name) {
            auto& m = setters();
            auto it = m.find(name);
            return it == m.end() ? nullptr : &it->second;
        }
        static bool has(const std::string& name) { return setters().count(name) > 0; }
    private:
        static std::unordered_map<std::string, Setter>& setters() {
            static std::unordered_map<std::string, Setter> m;
            return m;
        }
    };

#define CP_REGISTER_FIELD(T, field) \
    ::cp::ReflectionRegistry<T>::registerProperty(#field, \
        [](T& obj, const ::cp::ColumnValue& v) { \
            obj.field = ::cp::TypeHandlerRegistry::instance() \
                            .template fromColumn<decltype(obj.field)>(v); \
        })
} // namespace cp

#endif //CONN_POOL_REFLECTION_REGISTRY_H