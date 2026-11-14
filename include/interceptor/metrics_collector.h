#ifndef CONN_POOL_METRICS_COLLECTOR_H
#define CONN_POOL_METRICS_COLLECTOR_H

#include "interceptor/sql_interceptor.h"
#include <atomic>
#include <cstdio>
#include <mutex>
#include <string>
#include <unordered_map>

namespace cp {
    class MetricsCollector : public SqlInterceptor {
    public:
        void afterExecute(const SqlStatement& st, const BoundSql&,
                          std::chrono::microseconds el) override { record(st.id, el, false); }
        void onError(const SqlStatement& st, const BoundSql&,
                     std::chrono::microseconds el, const std::exception&) override { record(st.id, el, true); }

        void dumpAll() const {
            std::lock_guard<std::mutex> lk(mu_);
            for (auto& [id, m] : map_) {
                auto c = m.count.load();
                std::fprintf(stderr,
                    "[METRIC] %-40s count=%llu err=%llu avg=%lluus max=%lluus\n",
                    id.c_str(), (unsigned long long)c,
                    (unsigned long long)m.errors.load(),
                    (unsigned long long)(c ? m.totalUs.load() / c : 0),
                    (unsigned long long)m.maxUs.load());
            }
        }
    private:
        struct Bucket {
            std::atomic<uint64_t> count{0}, errors{0}, totalUs{0}, maxUs{0};
        };
        void record(const std::string& id, std::chrono::microseconds el, bool err) {
            auto& m = bucket(id);
            m.count.fetch_add(1);
            if (err) m.errors.fetch_add(1);
            auto us = (uint64_t)el.count();
            m.totalUs.fetch_add(us);
            auto cur = m.maxUs.load();
            while (cur < us && !m.maxUs.compare_exchange_weak(cur, us)) {}
        }
        Bucket& bucket(const std::string& id) {
            std::lock_guard<std::mutex> lk(mu_);
            return map_[id];
        }
        mutable std::mutex mu_;
        std::unordered_map<std::string, Bucket> map_;
    };
} // namespace cp
#endif //CONN_POOL_METRICS_COLLECTOR_H