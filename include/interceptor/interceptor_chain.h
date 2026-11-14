#ifndef CONN_POOL_INTERCEPTOR_CHAIN_H
#define CONN_POOL_INTERCEPTOR_CHAIN_H

#include "interceptor/sql_interceptor.h"
#include <algorithm>
#include <memory>
#include <utility>
#include <vector>

namespace cp {

    class InterceptorChain {
    public:
        using Entry = std::pair<std::shared_ptr<SqlInterceptor>, int>;
        using EntryList = std::vector<Entry>;

        void add(std::shared_ptr<SqlInterceptor> i, int priority = 0) {
            if (!i) return;
            list_.emplace_back(std::move(i), priority);
            std::stable_sort(list_.begin(), list_.end(),
                [](const Entry& a, const Entry& b) { return a.second < b.second; });
        }

        bool empty() const { return list_.empty(); }

        template <typename Fn>
        auto execute(const SqlStatement& st, BoundSql& bound, Fn&& fn) -> decltype(fn()) {
            using namespace std::chrono;

            for (auto& e : list_) e.first->beforeExecute(st, bound);

            auto t0 = steady_clock::now();
            try {
                auto r = fn();
                auto el = duration_cast<microseconds>(steady_clock::now() - t0);
                for (auto it = list_.rbegin(); it != list_.rend(); ++it)
                    it->first->afterExecute(st, bound, el);
                return r;
            } catch (const std::exception& e) {
                auto el = duration_cast<microseconds>(steady_clock::now() - t0);
                for (auto& entry : list_)
                    entry.first->onError(st, bound, el, e);
                throw;
            }
        }

        const EntryList& entries() const { return list_; }

    private:
        EntryList list_;
    };

} // namespace cp

#endif // CONN_POOL_INTERCEPTOR_CHAIN_H