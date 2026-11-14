#include "pool/connection_pool.h"
#include <algorithm>
#include <cstdint>
#include <stdexcept>

namespace cp {

using Clock = std::chrono::steady_clock;
using Ms    = std::chrono::milliseconds;

namespace {
thread_local std::unordered_map<const ConnectionPool*, std::unique_ptr<TLSBag>>
    g_tlsBags;

inline uint64_t splitmix64(uint64_t x) {
    x += 0x9e3779b97f4a7c15ULL;
    x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
    x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
    return x ^ (x >> 31);
}
}

std::unordered_map<const ConnectionPool*, std::unique_ptr<TLSBag>>& tlsBags() {
    return g_tlsBags;
}

PoolEntry::~PoolEntry() {
    if (conn) { try { conn.reset(); } catch (...) {} }
    if (totalCounter) totalCounter->fetch_sub(1, std::memory_order_acq_rel);
}

TLSBag::~TLSBag() {
    if (auto p = pool.lock()) p->reclaimThreadLocal(local);
    local.clear();
}

ConnectionPool::ConnectionPool(PoolConfig cfg,
                               std::shared_ptr<IConnectionFactory> factory,
                               std::shared_ptr<ITraceSink> trace)
    : cfg_(std::move(cfg)),
      retry_(cfg_.retry),
      factory_(std::move(factory)),
      traceSink_(std::move(trace)),
      total_(std::make_shared<std::atomic<int>>(0)) {
    if (!factory_) throw std::invalid_argument("factory is null");
    if (cfg_.maxPoolSize < cfg_.minPoolSize)
        throw std::runtime_error("maxPoolSize < minPoolSize");

    baseTimeoutMs_ = static_cast<int>(cfg_.connectionTimeoutMs);
    if (baseTimeoutMs_ <= 0) baseTimeoutMs_ = 3000;
    currentTimeoutMs_.store(baseTimeoutMs_);

    queueCapacity_ = static_cast<size_t>(std::max(16, cfg_.maxPoolSize));

    segments_.reserve(kMaxSegments);
    for (size_t i = 0; i < kMaxSegments; ++i)
        segments_.push_back(std::make_unique<Segment>(queueCapacity_));

    numActiveSegments_.store(computeSegmentCount());

    warmUp();

    long interval = static_cast<long>(
        std::max<std::int64_t>(1000, cfg_.healthCheck.intervalMs));
    maintainer_ = std::thread([this, interval] {
        while (!closed_.load(std::memory_order_acquire)) {
            std::this_thread::sleep_for(Ms(interval));
            if (closed_.load(std::memory_order_acquire)) break;
            try {
                doMaintenance();
                adjustTimeoutEMA();
                maybeAutoRebalance();
            } catch (...) {}
        }
    });
}

ConnectionPool::~ConnectionPool() { close(); }

size_t ConnectionPool::computeSegmentCount() const {
    unsigned hw = std::thread::hardware_concurrency();
    if (hw == 0) hw = 4;
    size_t byHw   = std::max<size_t>(4, std::min<size_t>(32, (size_t)hw * 2));
    size_t byConn = std::max<size_t>(1, (size_t)cfg_.maxPoolSize / 2);
    size_t n      = std::min(byHw, byConn);
    return std::max<size_t>(1, std::min<size_t>(32, n));
}

size_t ConnectionPool::segmentIndex() const {
    static thread_local const uint64_t seed =
        splitmix64(static_cast<uint64_t>(
            std::hash<std::thread::id>{}(std::this_thread::get_id())));
    return seed % numActiveSegments_.load(std::memory_order_relaxed);
}

TLSBag& ConnectionPool::getThreadLocalBag() {
    auto& bags = tlsBags();
    auto it = bags.find(this);
    if (it != bags.end()) return *it->second;
    auto bag = std::make_unique<TLSBag>();
    bag->pool = weak_from_this();
    bag->localCap.store(static_cast<size_t>(std::max(1, cfg_.minPoolSize)));
    auto* raw = bag.get();
    bags[this] = std::move(bag);
    return *raw;
}

void ConnectionPool::reclaimThreadLocal(std::vector<PoolEntryPtr>& bag) {
    if (closed_.load(std::memory_order_acquire)) { bag.clear(); return; }
    auto& seg = *segments_[segmentIndex()];
    for (auto& e : bag) {
        if (!e || !e->conn) continue;
        seg.queue.enqueue(e);
        if (e) e.reset();
    }
    bag.clear();
    cv_.notify_all();
}

size_t ConnectionPool::computeLocalCapacity(const TLSBag& bag) const {
    uint64_t hits  = bag.localHits.load(std::memory_order_relaxed);
    uint64_t miss  = bag.localMisses.load(std::memory_order_relaxed);
    uint64_t total = hits + miss;
    size_t base  = static_cast<size_t>(std::max(1, cfg_.minPoolSize));
    size_t upper = static_cast<size_t>(std::max<size_t>(base, cfg_.maxPoolSize / 4));
    if (total < 100) return base;
    double hitRate = static_cast<double>(hits) / total;
    size_t cap = static_cast<size_t>(base * (1.0 + hitRate));
    return std::min(cap, upper);
}

std::shared_ptr<IDbConnection> ConnectionPool::getConnection() {
    auto begin = Clock::now();
    auto entry = retry_.execute([this] { return tryAcquire(); });
    auto waitNs = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            Clock::now() - begin).count());
    stats_.recordWait(waitNs);
    notifyTraceBorrow(waitNs);
    return wrapEntry(std::move(entry), waitNs);
}

void ConnectionPool::notifyTraceBorrow(uint64_t waitNs) {
    auto sink = std::atomic_load(&traceSink_);
    if (!sink) return;
    try { sink->onBorrow(cfg_.name, std::chrono::nanoseconds(waitNs)); } catch (...) {}
}

PoolEntryPtr ConnectionPool::tryAcquire() {
    auto deadline = Clock::now() + Ms(currentTimeoutMs_.load());
    TLSBag& bag = getThreadLocalBag();

    while (true) {
        if (closed_.load(std::memory_order_acquire))
            throw std::runtime_error("pool[" + cfg_.name + "] closed");

        stats_.totalAcquires.fetch_add(1, std::memory_order_relaxed);

        if (auto e = popLocal(bag)) {
            if (isUsable(*e)) {
                stats_.hitsLocal.fetch_add(1, std::memory_order_relaxed);
                active_.fetch_add(1, std::memory_order_relaxed);
                e->lastUsedAt = Clock::now();
                return e;
            }
        } else {
            bag.localMisses.fetch_add(1, std::memory_order_relaxed);
        }

        if (auto e = popShared()) {
            if (isUsable(*e)) {
                stats_.hitsShared.fetch_add(1, std::memory_order_relaxed);
                active_.fetch_add(1, std::memory_order_relaxed);
                e->lastUsedAt = Clock::now();
                return e;
            }
        }

        {
            PoolEntryPtr fresh;
            if (createEntry(fresh)) {
                stats_.creations.fetch_add(1, std::memory_order_relaxed);
                active_.fetch_add(1, std::memory_order_relaxed);
                return fresh;
            }
        }

        auto now = Clock::now();
        if (now >= deadline) {
            stats_.timeouts.fetch_add(1, std::memory_order_relaxed);
            throw std::runtime_error("pool[" + cfg_.name + "] getConnection timeout");
        }
        stats_.waits.fetch_add(1, std::memory_order_relaxed);
        std::unique_lock<std::mutex> lk(waitMu_);
        cv_.wait_until(lk, deadline);
    }
}

PoolEntryPtr ConnectionPool::popLocal(TLSBag& bag) {
    if (bag.local.empty()) return nullptr;
    auto e = std::move(bag.local.back());
    bag.local.pop_back();
    return e;
}

PoolEntryPtr ConnectionPool::popShared() {
    size_t n = numActiveSegments_.load(std::memory_order_relaxed);
    size_t my = segmentIndex();
    PoolEntryPtr e;
    if (segments_[my]->queue.dequeue(e)) return e;
    uint64_t hop = splitmix64(static_cast<uint64_t>(my) +
        static_cast<uint64_t>(Clock::now().time_since_epoch().count()));
    for (size_t k = 0; k < n; ++k) {
        size_t idx = (my + (hop * (k + 1)) % n) % n;
        if (idx == my) continue;
        if (segments_[idx]->queue.dequeue(e)) return e;
    }
    return nullptr;
}

bool ConnectionPool::createEntry(PoolEntryPtr& out) {
    while (true) {
        int cur = total_->load(std::memory_order_relaxed);
        if (cur >= cfg_.maxPoolSize) return false;
        if (total_->compare_exchange_weak(cur, cur + 1,
                std::memory_order_acq_rel, std::memory_order_relaxed))
            break;
    }
    PoolEntryPtr e;
    try {
        e = std::make_shared<PoolEntry>();
        e->totalCounter = total_;
        e->conn = factory_->create(cfg_);
        e->createdAt = e->lastUsedAt = Clock::now();
    } catch (...) {
        if (!e) total_->fetch_sub(1, std::memory_order_acq_rel);
        throw;
    }
    out = std::move(e);
    return true;
}

bool ConnectionPool::isUsable(PoolEntry& e) {
    if (!e.conn) return false;
    if (!cfg_.healthCheck.enabled) return true;
    try { if (e.conn->ping()) return true; } catch (...) {}
    stats_.healthCheckFailures.fetch_add(1, std::memory_order_relaxed);
    return false;
}

void ConnectionPool::release(PoolEntryPtr e) {
    if (!e) return;
    active_.fetch_sub(1, std::memory_order_relaxed);
    stats_.totalReleases.fetch_add(1, std::memory_order_relaxed);

    if (closed_.load(std::memory_order_acquire) || !e->conn) return;

    TLSBag& bag = getThreadLocalBag();
    size_t cap = computeLocalCapacity(bag);
    bag.localCap.store(cap, std::memory_order_relaxed);

    if (bag.local.size() < cap) {
        e->lastUsedAt = Clock::now();
        bag.local.push_back(std::move(e));
        bag.localHits.fetch_add(1, std::memory_order_relaxed);
    } else {
        auto& seg = *segments_[segmentIndex()];
        seg.queue.enqueue(e);
        e.reset();
    }

    if (batchMode_.load(std::memory_order_relaxed) == 0)
        cv_.notify_one();
}

std::shared_ptr<IDbConnection> ConnectionPool::wrapEntry(PoolEntryPtr e, uint64_t waitNs) {
    // ① 用带 deleter 的 shared_ptr 持有 PoolEntryPtr 的控制块
    auto* holder = new PoolEntryPtr(std::move(e));
    IDbConnection* raw = (*holder)->conn.get();
    std::weak_ptr<ConnectionPool> weak = weak_from_this();
    auto borrowAt = Clock::now();

    std::shared_ptr<PoolEntryPtr> entryHolder(
        holder,
        [weak, borrowAt, waitNs](PoolEntryPtr* p) {
            auto entry = std::move(*p);
            delete p;
            auto holdNs = static_cast<uint64_t>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(
                    Clock::now() - borrowAt).count());
            if (auto pool = weak.lock()) {
                pool->stats_.recordHold(holdNs);
                if (auto sink = std::atomic_load(&pool->traceSink_)) {
                    try {
                        sink->onRelease(pool->cfg_.name,
                                        std::chrono::nanoseconds(waitNs),
                                        std::chrono::nanoseconds(holdNs));
                    } catch (...) {}
                }
                pool->release(std::move(entry));
            }
        });

    // ② aliasing 构造：返回的 shared_ptr<IDbConnection> 共享 entryHolder 的控制块
    return std::shared_ptr<IDbConnection>(entryHolder, raw);
}

void ConnectionPool::releaseMany(
        std::vector<std::shared_ptr<IDbConnection>>& conns) {
    if (conns.empty()) return;
    batchMode_.fetch_add(1, std::memory_order_acq_rel);
    for (auto& c : conns) c.reset();
    if (batchMode_.fetch_sub(1, std::memory_order_acq_rel) == 1)
        cv_.notify_all();
    conns.clear();
}

int ConnectionPool::idleConnections() const {
    int sum = 0;
    size_t n = numActiveSegments_.load();
    for (size_t i = 0; i < n; ++i)
        sum += static_cast<int>(segments_[i]->queue.approxSize());
    return sum;
}

void ConnectionPool::warmUp() {
    int target = std::min(cfg_.minPoolSize, cfg_.maxPoolSize);
    for (int i = 0; i < target; ++i) {
        try {
            PoolEntryPtr e;
            if (!createEntry(e)) break;
            auto& seg = *segments_[static_cast<size_t>(i) % numActiveSegments_.load()];
            seg.queue.enqueue(e);
        } catch (...) { break; }
    }
}

void ConnectionPool::doMaintenance() {
    auto now = Clock::now();
    std::vector<PoolEntryPtr> toDrop;
    size_t n = numActiveSegments_.load();

    for (size_t s = 0; s < n; ++s) {
        auto& seg = *segments_[s];
        std::vector<PoolEntryPtr> drained;
        PoolEntryPtr e;
        while (seg.queue.dequeue(e)) { drained.push_back(std::move(e)); e.reset(); }

        for (auto& item : drained) {
            if (!item || !item->conn) { toDrop.push_back(std::move(item)); continue; }
            auto idleMs = std::chrono::duration_cast<Ms>(now - item->lastUsedAt).count();
            auto lifeMs = std::chrono::duration_cast<Ms>(now - item->createdAt).count();
            if (idleMs > cfg_.idleTimeoutMs || lifeMs > cfg_.maxLifetimeMs) {
                stats_.evictions.fetch_add(1);
                toDrop.push_back(std::move(item));
                continue;
            }
            if (cfg_.healthCheck.enabled && !isUsable(*item)) {
                toDrop.push_back(std::move(item));
                continue;
            }
            if (!seg.queue.enqueue(item))
                toDrop.push_back(std::move(item));
            item.reset();
        }
    }

    while (!closed_.load(std::memory_order_acquire) &&
           total_->load() < cfg_.minPoolSize) {
        PoolEntryPtr e;
        try { if (!createEntry(e)) break; } catch (...) { break; }
        auto& seg = *segments_[segmentIndex()];
        if (!seg.queue.enqueue(e)) break;
        e.reset();
    }

    if (!toDrop.empty()) cv_.notify_all();
}

void ConnectionPool::adjustTimeoutEMA() {
    uint64_t n   = stats_.waitSamples.exchange(0, std::memory_order_acq_rel);
    uint64_t sum = stats_.waitNsSum.exchange(0, std::memory_order_acq_rel);
    if (n < 50) {
        stats_.waitSamples.fetch_add(n);
        stats_.waitNsSum.fetch_add(sum);
        return;
    }
    double meanMs = (static_cast<double>(sum) / n) / 1e6;
    double prev = emaWaitMs_.load();
    double next = (prev <= 0.0) ? meanMs : (0.2 * meanMs + 0.8 * prev);
    emaWaitMs_.store(next);
    int base = baseTimeoutMs_;
    int target = std::max(base, static_cast<int>(next * 4.0));
    int cap = base * 8;
    if (target > cap) target = cap;
    currentTimeoutMs_.store(target);
}

void ConnectionPool::maybeAutoRebalance() {
    size_t cur = numActiveSegments_.load();
    size_t desired = computeSegmentCount();
    if (cur >= 2 * desired || (desired >= 2 * cur && desired > cur))
        rebalanceSegments(std::min(desired, kMaxSegments));
}

bool ConnectionPool::rebalanceSegments(size_t newNum) {
    if (newNum == 0 || newNum > kMaxSegments) return false;
    size_t oldN = numActiveSegments_.load(std::memory_order_acquire);
    if (newNum == oldN) return true;

    numActiveSegments_.store(newNum, std::memory_order_release);
    if (newNum > oldN) return true;

    for (size_t i = newNum; i < oldN; ++i) {
        PoolEntryPtr e;
        while (segments_[i]->queue.dequeue(e)) {
            uint64_t h = reinterpret_cast<uint64_t>(e->conn.get());
            size_t   target = splitmix64(h) % newNum;
            bool placed = false;
            for (size_t k = 0; k < newNum; ++k) {
                size_t idx = (target + k) % newNum;
                if (segments_[idx]->queue.enqueue(e)) { placed = true; break; }
            }
            e.reset();
        }
    }
    cv_.notify_all();
    return true;
}

void ConnectionPool::close() {
    if (closed_.exchange(true, std::memory_order_acq_rel)) return;
    cv_.notify_all();
    if (maintainer_.joinable()) maintainer_.join();
    size_t n = kMaxSegments;
    for (size_t i = 0; i < n; ++i) {
        PoolEntryPtr e;
        while (segments_[i]->queue.dequeue(e)) e.reset();
    }
}

} // namespace cp