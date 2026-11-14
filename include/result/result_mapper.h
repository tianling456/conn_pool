#ifndef CONN_POOL_RESULT_MAPPER_H
#define CONN_POOL_RESULT_MAPPER_H




#include "db/db_connection.h"
#include <optional>
#include <string>

namespace cp {
    namespace conv {
        inline std::optional<std::int64_t> toInt64(const ColumnValue& v) {
            if (!v) return std::nullopt;
            try { return std::stoll(*v); } catch (...) { return std::nullopt; }
        }
        inline std::optional<std::uint64_t> toUInt64(const ColumnValue& v) {
            if (!v) return std::nullopt;
            try { return std::stoull(*v); } catch (...) { return std::nullopt; }
        }
        inline std::optional<double> toDouble(const ColumnValue& v) {
            if (!v) return std::nullopt;
            try { return std::stod(*v); } catch (...) { return std::nullopt; }
        }
        inline std::optional<bool> toBool(const ColumnValue& v) {
            if (!v) return std::nullopt;
            return *v == "1" || *v == "true" || *v == "TRUE" || *v == "t";
        }
        inline std::optional<std::string> toString(const ColumnValue& v) {
            if (!v) return std::nullopt;
            return *v;
        }
    }

    template <typename T>
    class IResultMapper {
    public:
        virtual ~IResultMapper() = default;
        virtual T map(const ResultRow& row) = 0;
    };
} // namespace cp

#endif //CONN_POOL_RESULT_MAPPER_H