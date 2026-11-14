#ifndef CONN_POOL_CONNECTION_POOL_H
#define CONN_POOL_CONNECTION_POOL_H

#include "config/yaml_config.h"
#include "db/connection_factory.h"
#include "db/db_connection.h"
#include "pool/retry.h"
#include "pool/mpmc_queue.h"
#include "pool/pool_stats.h"
#include "pool/trace.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <vector>

namespace cp {

class ConnectionPool;

struct PoolEntry {
    std::unique_ptr<IDbConnection> conn;
    std::chrono::steady_clock::time_point createdAt;
    std::chrono::steady_clock::time_point lastUsedAt;
    std::shared_ptr<std::atomic<int>> totalCounter;
    ~PoolEntry();
};
using PoolEntryPtr = std::shared_ptr<PoolEntry>;

struct TLSBag {
    std::weak_ptr<ConnectionPool> pool;
    std::vector<PoolEntryPtr> local;
    std::atomic<uint64_t> localHits{0};
    std::atomic<uint64_t> localMisses{0};
    std::atomic<size_t>   localCap{1};
    ~TLSBag();
};

std::unordered_map<const ConnectionPool*, std::unique_ptr<TLSBag>>& tlsBags();

class ConnectionPool : public std::enable_shared_from_this<ConnectionPool> {
public:
    ConnectionPool(PoolConfig cfg,
                   std::shared_ptr<IConnectionFactory> factory,
                   std::shared_ptr<ITraceSink> trace = nullptr);
    ~ConnectionPool();

    std::shared_ptr<IDbConnection> getConnection();
    void releaseMany(std::vector<std::shared_ptr<IDbConnection>>& conns);

    int totalConnections()  const { return total_->load(std::memory_order_relaxed); }
    int activeConnections() const { return active_.load(std::memory_order_relaxed); }
    int idleConnections()   const;

    const PoolStats&     stats()  const { return stats_; }
    size_t               activeSegments() const { return numActiveSegments_.load(); }
    int                  currentTimeoutMs() const { return currentTimeoutMs_.load(); }
    const PoolConfig&    config() const { return cfg_; }
    const RetryTemplate& retry()  const { return retry_; }

    void setTraceSink(std::shared_ptr<ITraceSink> s) {
        std::atomic_store(&traceSink_, std::move(s));
    }
    bool rebalanceSegments(size_t newNum);
    void close();

private:
    friend struct TLSBag;
    friend struct ConnectionReleaser;

    struct Segment {
        explicit Segment(size_t cap) : queue(cap) {}
        BoundedMpmcQueue<PoolEntryPtr> queue;
    };

    TLSBag& getThreadLocalBag();
    void    reclaimThreadLocal(std::vector<PoolEntryPtr>& bag);

    PoolEntryPtr tryAcquire();
    PoolEntryPtr popLocal(TLSBag& bag);
    PoolEntryPtr popShared();
    bool         createEntry(PoolEntryPtr& out);
    bool         isUsable(PoolEntry& e);
    void         release(PoolEntryPtr e);
    std::shared_ptr<IDbConnection> wrapEntry(PoolEntryPtr e, uint64_t waitNs);

    void warmUp();
    void maintenanceLoop();
    void doMaintenance();
    void adjustTimeoutEMA();
    void maybeAutoRebalance();

    void notifyTraceBorrow(uint64_t waitNs);

    size_t segmentIndex() const;
    size_t computeSegmentCount() const;
    size_t computeLocalCapacity(const TLSBag& bag) const;

    static constexpr size_t kMaxSegments = 32;

    PoolConfig cfg_;
    RetryTemplate retry_;
    std::shared_ptr<IConnectionFactory> factory_;

    std::vector<std::unique_ptr<Segment>> segments_;
    std::atomic<size_t>  numActiveSegments_{1};
    size_t               queueCapacity_ = 16;

    mutable std::mutex      waitMu_;
    std::condition_variable cv_;

    std::shared_ptr<std::atomic<int>> total_;
    std::atomic<int>  active_{0};
    std::atomic<bool> closed_{false};
    std::atomic<int>  batchMode_{0};

    std::atomic<int>    currentTimeoutMs_{3000};
    int                 baseTimeoutMs_ = 3000;
    std::atomic<double> emaWaitMs_{0.0};

    std::shared_ptr<ITraceSink> traceSink_;
    PoolStats                   stats_;

    std::thread maintainer_;
};

struct ConnectionReleaser {
    static void release(std::weak_ptr<ConnectionPool> wp, PoolEntryPtr e) {
        if (auto p = wp.lock()) p->release(std::move(e));
    }
};

} // namespace cp
#endif //CONN_POOL_CONNECTION_POOL_H
