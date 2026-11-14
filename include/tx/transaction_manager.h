//
// Created by aobo on 2026/11/11.
//

#ifndef CONN_POOL_TRANSACTION_MANAGER_H
#define CONN_POOL_TRANSACTION_MANAGER_H
#include "pool/pool_manager.h"
#include "tx/routing_policy.h"
#include "tx/transaction.h"
#include "tx/transaction_watchdog.h"
#include <atomic>
#include <memory>
#include <vector>

namespace cp {
    class TransactionManager {
    public:
        explicit TransactionManager(std::shared_ptr<ConnectionPoolManager> mgr,
                                    const std::string& poolName = "");
        void setWatchdog(std::shared_ptr<TransactionWatchdog> w) { watchdog_ = std::move(w); }
        void setRoutingPolicy(std::shared_ptr<IRoutingPolicy> p) { routing_ = std::move(p); }
        void addListener(std::shared_ptr<TransactionListener> l) { listeners_.push_back(std::move(l)); }

        std::shared_ptr<TransactionStatus> begin(const TransactionDefinition& def);
        void commit(std::shared_ptr<TransactionStatus> status);
        void rollback(std::shared_ptr<TransactionStatus> status);
        static std::shared_ptr<TransactionStatus> current();

    private:
        void applyDefinition(TransactionStatus& s);
        void notifyBeforeCommit(TransactionStatus& s);
        void notifyAfterCommit (TransactionStatus& s);
        void notifyBeforeRollback(TransactionStatus& s);
        void notifyAfterRollback (TransactionStatus& s);
        void notifyAfterCompletion(TransactionStatus& s);

        std::shared_ptr<ConnectionPoolManager>            mgr_;
        std::string                                       poolName_;
        std::shared_ptr<TransactionWatchdog>              watchdog_;
        std::shared_ptr<IRoutingPolicy>                   routing_;
        std::vector<std::shared_ptr<TransactionListener>> listeners_;
        std::atomic<int>                                  nextTxId_{1};
    };
} // namespace cp
#endif //CONN_POOL_TRANSACTION_MANAGER_H
