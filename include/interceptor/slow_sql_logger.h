#ifndef CONN_POOL_SHOW_SQL_LOGGER_H
#define CONN_POOL_SHOW_SQL_LOGGER_H

#include "interceptor/sql_interceptor.h"
#include <cstdio>

namespace cp {
    class SlowSqlLogger : public SqlInterceptor {
    public:
        explicit SlowSqlLogger(std::chrono::milliseconds th) : th_(th) {}
        void afterExecute(const SqlStatement& st, const BoundSql& bound,
                          std::chrono::microseconds el) override {
            if (el >= th_)
                std::fprintf(stderr, "[SLOW SQL] id=%s elapsed=%lldus sql=%s\n",
                    st.id.c_str(), (long long)el.count(), bound.sql.c_str());
        }
        void onError(const SqlStatement& st, const BoundSql& bound,
                     std::chrono::microseconds el, const std::exception& e) override {
            std::fprintf(stderr, "[SQL ERROR] id=%s elapsed=%lldus err=%s sql=%s\n",
                st.id.c_str(), (long long)el.count(), e.what(), bound.sql.c_str());
        }
    private:
        std::chrono::milliseconds th_;
    };
} // namespace cp
#endif //CONN_POOL_SHOW_SQL_LOGGER_H