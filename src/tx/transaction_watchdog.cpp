//
// Created by aobo on 2026/11/11.
//
#include "tx/transaction_watchdog.h"
#include "pool/connection_pool.h"
#include "tx/transaction.h"
#include <chrono>
#include <vector>

namespace cp {
    TransactionWatchdog::TransactionWatchdog(std::shared_ptr<ConnectionPoolManager> m)
        : mgr_(std::move(m)) {}
    TransactionWatchdog::~TransactionWatchdog() { stop(); }
    void TransactionWatchdog::start() {
        if (thread_.joinable()) return;
        thread_ = std::thread([this] { loop(); });
    }
    void TransactionWatchdog::stop() {
        if (stop_.exchange(true)) return;
        if (thread_.joinable()) thread_.join();
    }
    void TransactionWatchdog::registerTx(std::weak_ptr<TransactionStatus> s) {
        std::lock_guard<std::mutex> lk(mu_);
        active_[s.lock().get()] = std::move(s);
    }
    void TransactionWatchdog::unregisterTx(TransactionStatus* raw) {
        std::lock_guard<std::mutex> lk(mu_);
        active_.erase(raw);
    }
    void TransactionWatchdog::loop() {
        while (!stop_.load(std::memory_order_acquire)) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
            if (stop_.load()) break;
            std::vector<std::shared_ptr<TransactionStatus>> toKill;
            {
                std::lock_guard<std::mutex> lk(mu_);
                auto now = std::chrono::steady_clock::now();
                for (auto& [raw, wp] : active_) {
                    auto s = wp.lock();
                    if (!s || s->isCompleted()) continue;
                    if (s->definition().timeoutSec <= 0) continue;
                    auto limit = s->startedAt() + std::chrono::seconds(s->definition().timeoutSec);
                    if (now >= limit) toKill.push_back(std::move(s));
                }
            }
            for (auto& s : toKill) {
                try {
                    auto c = mgr_->getPool()->getConnection();
                    c->killConnection(s->connection()->connectionId());
                } catch (...) {}
                unregisterTx(s.get());
            }
        }
    }
} // namespace cp