#ifndef CONN_POOL_QUERY_CACHE_INTERCEPTOR_H
#define CONN_POOL_QUERY_CACHE_INTERCEPTOR_H

#include "interceptor/sql_interceptor.h"
#include <atomic>
#include <chrono>
#include <list>
#include <mutex>
#include <optional>
#include <unordered_map>

namespace cp {
    class QueryCacheInterceptor : public SqlInterceptor {
    public:
        QueryCacheInterceptor(size_t capacity, std::chrono::seconds ttl)
            : capacity_(capacity ? capacity : 1), ttl_(ttl) {}

        std::optional<ResultSet> lookup(const std::string& key);
        void store(const std::string& key, ResultSet result);
        void invalidateAll();

        void afterExecute(const SqlStatement& st, const BoundSql&,
                          std::chrono::microseconds) override {
            if (st.type != SqlType::Dql) invalidateAll();
        }
        size_t size() const;
        size_t hits()   const { return hits_.load(); }
        size_t misses() const { return misses_.load(); }
    private:
        struct Node {
            ResultSet result;
            std::chrono::steady_clock::time_point expiry;
            std::list<std::string>::iterator lruIt;
        };
        size_t capacity_;
        std::chrono::seconds ttl_;
        mutable std::mutex mu_;
        std::list<std::string> lru_;
        std::unordered_map<std::string, Node> map_;
        std::atomic<size_t> hits_{0}, misses_{0};
    };
} // namespace cp
#endif //CONN_POOL_QUERY_CACHE_INTERCEPTOR_H