//
// Created by aobo on 2026/11/11.
//

#ifndef CONN_POOL_TRANSACTION_WATCHDOG_H
#define CONN_POOL_TRANSACTION_WATCHDOG_H

#include "pool/pool_manager.h"
#include <atomic>
#include <mutex>
#include <thread>
#include <unordered_map>

namespace cp {
    class TransactionStatus;
    class TransactionWatchdog {
    public:
        explicit TransactionWatchdog(std::shared_ptr<ConnectionPoolManager> mgr);
        ~TransactionWatchdog();
        void registerTx(std::weak_ptr<TransactionStatus> s);
        void unregisterTx(TransactionStatus* raw);
        void start();
        void stop();
    private:
        void loop();
        std::shared_ptr<ConnectionPoolManager> mgr_;
        std::mutex mu_;
        std::unordered_map<TransactionStatus*, std::weak_ptr<TransactionStatus>> active_;
        std::thread thread_;
        std::atomic<bool> stop_{false};
    };
} // namespace cp


#endif //CONN_POOL_TRANSACTION_WATCHDOG_H
