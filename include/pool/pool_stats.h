//
// Created by aobo on 2026/11/8.
//

#ifndef CONN_POOL_POOL_STATS_H
#define CONN_POOL_POOL_STATS_H

#include "pool/histogram.h"
#include <atomic>
#include <cstdint>
#include <sstream>
#include <string>

namespace cp {

struct PoolStats {
    std::atomic<uint64_t> hitsLocal{0};
    std::atomic<uint64_t> hitsShared{0};
    std::atomic<uint64_t> creations{0};
    std::atomic<uint64_t> waits{0};
    std::atomic<uint64_t> timeouts{0};
    std::atomic<uint64_t> healthCheckFailures{0};
    std::atomic<uint64_t> evictions{0};
    std::atomic<uint64_t> totalAcquires{0};
    std::atomic<uint64_t> totalReleases{0};

    Histogram waitHist_{waitBucketsSec()};
    Histogram holdHist_{holdBucketsSec()};

    std::atomic<uint64_t> waitSamples{0};
    std::atomic<uint64_t> waitNsSum{0};

    struct Snapshot {
        uint64_t hitsLocal, hitsShared, creations, waits, timeouts;
        uint64_t healthCheckFailures, evictions;
        uint64_t totalAcquires, totalReleases;

        double localHitRatio() const {
            auto s = hitsLocal + hitsShared + creations;
            return s ? static_cast<double>(hitsLocal) / s : 0.0;
        }
        double sharedHitRatio() const {
            auto s = hitsLocal + hitsShared + creations;
            return s ? static_cast<double>(hitsShared) / s : 0.0;
        }
        double creationRatio() const {
            auto s = hitsLocal + hitsShared + creations;
            return s ? static_cast<double>(creations) / s : 0.0;
        }
        double timeoutRatio() const {
            return totalAcquires ? static_cast<double>(timeouts) / totalAcquires : 0.0;
        }
    };

    Snapshot snapshot() const {
        return {
            hitsLocal.load(), hitsShared.load(), creations.load(),
            waits.load(), timeouts.load(),
            healthCheckFailures.load(), evictions.load(),
            totalAcquires.load(), totalReleases.load()
        };
    }

    void recordWait(uint64_t ns) {
        waitHist_.observeNs(ns);
        waitNsSum.fetch_add(ns, std::memory_order_relaxed);
        waitSamples.fetch_add(1, std::memory_order_relaxed);
    }
    void recordHold(uint64_t ns) { holdHist_.observeNs(ns); }

    std::string toPrometheus(const std::string& pool, int total, int active,
                             int idle, int currentTimeoutMs) const {
        auto s = snapshot();
        std::ostringstream o;
        auto C = [&](const char* n, uint64_t v, const char* h) {
            o << "# HELP connpool_" << n << " " << h << "\n";
            o << "# TYPE connpool_" << n << " counter\n";
            o << "connpool_" << n << "{pool=\"" << pool << "\"} " << v << "\n";
        };
        auto G = [&](const char* n, double v, const char* h) {
            o << "# HELP connpool_" << n << " " << h << "\n";
            o << "# TYPE connpool_" << n << " gauge\n";
            o << "connpool_" << n << "{pool=\"" << pool << "\"} " << v << "\n";
        };
        C("acquires_total", s.totalAcquires, "acquire attempts");
        C("releases_total", s.totalReleases, "release calls");
        C("hits_local_total", s.hitsLocal, "threadlocal hits");
        C("hits_shared_total", s.hitsShared, "shared segment hits");
        C("creations_total", s.creations, "new physical conns");
        C("waits_total", s.waits, "blocked on cv");
        C("timeouts_total", s.timeouts, "acquire timeouts");
        C("health_failures_total", s.healthCheckFailures, "ping failures");
        C("evictions_total", s.evictions, "evicted conns");
        G("connections_total", total, "physical connections");
        G("connections_active", active, "borrowed now");
        G("connections_idle", idle, "idle in shared segments");
        G("local_hit_ratio", s.localHitRatio(), "local hit ratio");
        G("shared_hit_ratio", s.sharedHitRatio(), "shared hit ratio");
        G("creation_ratio", s.creationRatio(), "creation ratio");
        G("timeout_ratio", s.timeoutRatio(), "timeout ratio");
        G("current_timeout_ms", currentTimeoutMs, "adaptive acquire timeout");
        waitHist_.toPrometheus(o, "connpool_wait_seconds", pool,
                               "acquire wait time (seconds)");
        holdHist_.toPrometheus(o, "connpool_hold_seconds", pool,
                               "hold time (seconds)");
        return o.str();
    }

    void reset() {
        hitsLocal = 0; hitsShared = 0; creations = 0;
        waits = 0; timeouts = 0; healthCheckFailures = 0;
        evictions = 0; totalAcquires = 0; totalReleases = 0;
        waitSamples = 0; waitNsSum = 0;
        waitHist_.reset(); holdHist_.reset();
    }
};

} // namespace cp

#endif //CONN_POOL_POOL_STATS_H
