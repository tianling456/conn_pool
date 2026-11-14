//
// Created by aobo on 2026/11/11.
//

#ifndef CONN_POOL_SQL_LOGGER_H
#define CONN_POOL_SQL_LOGGER_H

#include "db/db_connection.h"
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <string>
#include <variant>
#include <vector>

namespace cp {

struct SqlLogConfig {
    bool enabled   = true;   // 是否打印
    bool logParams = true;   // 是否打印参数（生产上可关掉，避免日志量爆炸）
};

class SqlLogger {
public:
    static SqlLogConfig& config() {
        static SqlLogConfig c;
        return c;
    }
    static void setEnabled(bool e)    { config().enabled = e; }
    static void setLogParams(bool p)  { config().logParams = p; }

    // 打印前
    static void logBefore(const std::string& id,
                          const std::string& pool,
                          const std::string& sql,
                          const std::vector<ParamValue>& params) {
        if (!config().enabled) return;
        std::fprintf(stderr, "[SQL] >>> id=%s pool=%s\n", id.c_str(), pool.c_str());
        std::fprintf(stderr, "        %s\n", sql.c_str());
        if (config().logParams) {
            std::fprintf(stderr, "        params=[");
            for (size_t i = 0; i < params.size(); ++i) {
                if (i) std::fprintf(stderr, ", ");
                std::fprintf(stderr, "%s", paramToStr(params[i]).c_str());
            }
            std::fprintf(stderr, "]\n");
        }
    }

    // 打印后（含耗时）
    static void logAfter(const std::string& id,
                         std::chrono::microseconds elapsed,
                         std::uint64_t affectedOrRows,
                         bool success) {
        if (!config().enabled) return;
        if (success) {
            std::fprintf(stderr,
                "[SQL] <<< id=%s elapsed=%lldus rows/affected=%llu\n",
                id.c_str(),
                (long long)elapsed.count(),
                (unsigned long long)affectedOrRows);
        } else {
            std::fprintf(stderr,
                "[SQL] !!! id=%s elapsed=%lldus FAILED\n",
                id.c_str(), (long long)elapsed.count());
        }
    }

    static void logCacheHit(const std::string& id,
                            const std::string& pool,
                            std::size_t size) {
        if (!config().enabled) return;
        std::fprintf(stderr,
            "[SQL] <<< id=%s pool=%s (cache hit) rows=%zu\n",
            id.c_str(), pool.c_str(), size);
    }

    // 异常
    static void logError(const std::string& id,
                         std::chrono::microseconds elapsed,
                         const std::exception& e) {
        if (!config().enabled) return;
        std::fprintf(stderr,
            "[SQL] xxx id=%s elapsed=%lldus error=%s\n",
            id.c_str(), (long long)elapsed.count(), e.what());
    }

private:
    static std::string paramToStr(const ParamValue& p) {
        if (std::holds_alternative<std::monostate>(p)) return "NULL";
        if (auto i = std::get_if<std::int64_t>(&p))   return std::to_string(*i);
        if (auto u = std::get_if<std::uint64_t>(&p))  return std::to_string(*u);
        if (auto d = std::get_if<double>(&p)) {
            char buf[64];
            std::snprintf(buf, sizeof(buf), "%g", *d);
            return buf;
        }
        if (auto s = std::get_if<std::string>(&p))    return "'" + *s + "'";
        return "?";
    }
};

} // namespace cp

#endif //CONN_POOL_SQL_LOGGER_H
