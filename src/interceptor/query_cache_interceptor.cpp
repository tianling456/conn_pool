#include "interceptor/query_cache_interceptor.h"
namespace cp {
    std::optional<ResultSet> QueryCacheInterceptor::lookup(const std::string& key) {
        std::lock_guard<std::mutex> lk(mu_);
        auto it = map_.find(key);
        if (it == map_.end()) { misses_.fetch_add(1); return std::nullopt; }
        if (std::chrono::steady_clock::now() > it->second.expiry) {
            lru_.erase(it->second.lruIt);
            map_.erase(it);
            misses_.fetch_add(1);
            return std::nullopt;
        }
        lru_.erase(it->second.lruIt);
        lru_.push_front(key);
        it->second.lruIt = lru_.begin();
        hits_.fetch_add(1);
        return it->second.result;
    }
    void QueryCacheInterceptor::store(const std::string& key, ResultSet r) {
        std::lock_guard<std::mutex> lk(mu_);
        auto it = map_.find(key);
        if (it != map_.end()) { lru_.erase(it->second.lruIt); map_.erase(it); }
        if (map_.size() >= capacity_) {
            auto last = lru_.back();
            lru_.pop_back();
            map_.erase(last);
        }
        lru_.push_front(key);
        map_[key] = Node{std::move(r),
                         std::chrono::steady_clock::now() + ttl_,
                         lru_.begin()};
    }
    void QueryCacheInterceptor::invalidateAll() {
        std::lock_guard<std::mutex> lk(mu_);
        lru_.clear();
        map_.clear();
    }
    size_t QueryCacheInterceptor::size() const {
        std::lock_guard<std::mutex> lk(mu_);
        return map_.size();
    }
} // namespace cp