#ifndef CONN_POOL_SQL_INTERCEPTOR_H
#define CONN_POOL_SQL_INTERCEPTOR_H

#include "xml/xml_types.h"
#include <chrono>

namespace cp {
    class SqlInterceptor {
    public:
        virtual ~SqlInterceptor() = default;
        virtual void beforeExecute(const SqlStatement&, BoundSql&) {}
        virtual void afterExecute(const SqlStatement&, const BoundSql&, std::chrono::microseconds) {}
        virtual void onError(const SqlStatement&, const BoundSql&,
                             std::chrono::microseconds, const std::exception&) {}
    };
} // namespace cp

#endif //CONN_POOL_SQL_INTERCEPTOR_H