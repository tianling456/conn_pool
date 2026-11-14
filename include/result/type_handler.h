#ifndef CONN_POOL_TYPE_HANDLER_H
#define CONN_POOL_TYPE_HANDLER_H



#include "db/db_connection.h"
#include "result/result_mapper.h"
#include <functional>
#include <stdexcept>
#include <typeindex>
#include <unordered_map>

namespace cp {
    class TypeHandlerRegistry {
    public:
        static TypeHandlerRegistry& instance();
        template <typename T>
        void registerHandler(std::function<ParamValue(const T&)> to,
                             std::function<T(const ColumnValue&)> from) {
            map_[std::type_index(typeid(T))] = Entry{
                [to](const void* p) { return to(*static_cast<const T*>(p)); },
                [from](void* out, const ColumnValue& c) { *static_cast<T*>(out) = from(c); }
            };
        }
        template <typename T>
        ParamValue toParam(const T& v) const {
            auto it = map_.find(std::type_index(typeid(T)));
            if (it == map_.end()) throw std::runtime_error("no handler toDb");
            return it->second.to(&v);
        }
        template <typename T>
        T fromColumn(const ColumnValue& c) const {
            auto it = map_.find(std::type_index(typeid(T)));
            if (it == map_.end()) throw std::runtime_error("no handler fromDb");
            T out{};
            it->second.from(&out, c);
            return out;
        }
        bool has(std::type_index t) const { return map_.count(t) > 0; }
    private:
        TypeHandlerRegistry();
        struct Entry {
            std::function<ParamValue(const void*)>         to;
            std::function<void(void*, const ColumnValue&)> from;
        };
        std::unordered_map<std::type_index, Entry> map_;
    };
} // namespace cp

#endif //CONN_POOL_TYPE_HANDLER_H