//
// Created by aobo on 2026/11/8.
//

#ifndef CONN_POOL_HISTOGRAM_H
#define CONN_POOL_HISTOGRAM_H

#include <atomic>
#include <cstdio>
#include <sstream>
#include <string>
#include <vector>

namespace cp {

class Histogram {
public:
    explicit Histogram(std::vector<double> boundsSec)
        : bounds_(std::move(boundsSec)), buckets_(bounds_.size() + 1) {}

    void observeNs(uint64_t ns) {
        double sec = static_cast<double>(ns) / 1e9;
        sumNs_.fetch_add(ns, std::memory_order_relaxed);
        count_.fetch_add(1, std::memory_order_relaxed);
        for (size_t i = 0; i < bounds_.size(); ++i) {
            if (sec <= bounds_[i]) { buckets_[i].fetch_add(1); return; }
        }
        buckets_.back().fetch_add(1);
    }

    void toPrometheus(std::ostringstream& o, const std::string& name,
                      const std::string& pool, const std::string& help) const {
        o << "# HELP " << name << " " << help << "\n";
        o << "# TYPE " << name << " histogram\n";
        for (size_t i = 0; i < bounds_.size(); ++i) {
            char buf[32]; std::snprintf(buf, sizeof(buf), "%g", bounds_[i]);
            o << name << "_bucket{pool=\"" << pool
              << "\",le=\"" << buf << "\"} "
              << buckets_[i].load() << "\n";
        }
        o << name << "_bucket{pool=\"" << pool << "\",le=\"+Inf\"} "
          << buckets_.back().load() << "\n";
        o << name << "_sum{pool=\"" << pool << "\"} "
          << (static_cast<double>(sumNs_.load()) / 1e9) << "\n";
        o << name << "_count{pool=\"" << pool << "\"} "
          << count_.load() << "\n";
    }

    void reset() {
        sumNs_ = 0; count_ = 0;
        for (auto& b : buckets_) b = 0;
    }

private:
    std::vector<double>                 bounds_;
    std::vector<std::atomic<uint64_t>>  buckets_;
    std::atomic<uint64_t>               sumNs_{0};
    std::atomic<uint64_t>               count_{0};
};

inline std::vector<double> waitBucketsSec() {
    return {0.001, 0.005, 0.01, 0.025, 0.05, 0.1, 0.25, 0.5, 1.0, 5.0};
}
inline std::vector<double> holdBucketsSec() {
    return {0.001, 0.005, 0.01, 0.05, 0.1, 0.5, 1.0, 5.0, 10.0, 30.0, 60.0};
}

} // namespace cp


#endif //CONN_POOL_HISTOGRAM_H
