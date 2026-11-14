//
// Created by aobo on 2026/11/11.
//
#include "tx/transaction_manager.h"
#include "tx/transaction_context.h"
#include <stdexcept>

namespace cp {
TransactionManager::TransactionManager(std::shared_ptr<ConnectionPoolManager> m,
                                       const std::string& poolName)
    : mgr_(std::move(m)), poolName_(poolName) {
    if (!mgr_) throw std::invalid_argument("mgr null");
}
std::shared_ptr<TransactionStatus> TransactionManager::current() {
    return TransactionContext::get();
}

std::shared_ptr<TransactionStatus>
TransactionManager::begin(const TransactionDefinition& def) {
    auto cur = current();
    int   id = nextTxId_.fetch_add(1);
    std::string poolName = routing_ ? routing_->selectPool(def) : poolName_;

    switch (def.propagation) {
    case Propagation::MANDATORY:
        if (!cur) throw std::runtime_error("MANDATORY: no active");
        return cur;

    case Propagation::NEVER:
        if (cur) throw std::runtime_error("NEVER: active");
        {
            auto conn = mgr_->getPool(poolName)->getConnection();
            return std::make_shared<TransactionStatus>(def, std::move(conn), true, nullptr, id);
        }
    case Propagation::SUPPORTS:
        if (cur) return cur;
        {
            auto conn = mgr_->getPool(poolName)->getConnection();
            return std::make_shared<TransactionStatus>(def, std::move(conn), true, nullptr, id);
        }
    case Propagation::NOT_SUPPORTED:
        {
            auto conn = mgr_->getPool(poolName)->getConnection();
            conn->setAutoCommit(true);
            return std::make_shared<TransactionStatus>(def, std::move(conn), true, cur.get(), id);
        }
    case Propagation::REQUIRES_NEW:
        {
            auto conn = mgr_->getPool(poolName)->getConnection();
            auto s = std::make_shared<TransactionStatus>(def, std::move(conn), true, cur.get(), id);
            applyDefinition(*s);
            s->connection()->begin();
            if (watchdog_) watchdog_->registerTx(s);
            TransactionContext::set(s);
            return s;
        }
    case Propagation::NESTED:
        if (cur) {
            std::string sp = "cp_nested_sp_" + std::to_string(id);
            cur->createSavepoint(sp);
            auto s = std::make_shared<TransactionStatus>(def, cur->sharedConnection(), false, cur.get(), id);
            return s;
        }
        // fallthrough
    case Propagation::REQUIRED:
    default:
        if (cur) return cur;
        {
            auto conn = mgr_->getPool(poolName)->getConnection();
            auto s = std::make_shared<TransactionStatus>(def, std::move(conn), true, nullptr, id);
            applyDefinition(*s);
            s->connection()->begin();
            if (watchdog_) watchdog_->registerTx(s);
            TransactionContext::set(s);
            return s;
        }
    }
}
void TransactionManager::applyDefinition(TransactionStatus& s) {
    auto* c = s.connection();
    if (!c) return;
    if (s.definition().isolation != IsolationLevel::DEFAULT)
        c->setIsolationLevel(s.definition().isolation);
    if (s.definition().readOnly) c->setReadOnly(true);
}
void TransactionManager::commit(std::shared_ptr<TransactionStatus> status) {
    if (!status || status->isCompleted()) return;
    if (status->parent() && status->definition().propagation == Propagation::NESTED) {
        status->markCompleted();
        TransactionContext::clear();
        return;
    }
    if (status->isRollbackOnly()) {
        rollback(std::move(status));
        throw std::runtime_error("rollback-only");
    }
    notifyBeforeCommit(*status);
    try { status->connection()->commit(); }
    catch (...) {
        notifyAfterRollback(*status);
        notifyAfterCompletion(*status);
        status->markCompleted();
        if (watchdog_) watchdog_->unregisterTx(status.get());
        TransactionContext::clear();
        throw;
    }
    notifyAfterCommit(*status);
    notifyAfterCompletion(*status);
    status->markCompleted();
    if (watchdog_) watchdog_->unregisterTx(status.get());
    TransactionContext::clear();
}
void TransactionManager::rollback(std::shared_ptr<TransactionStatus> status) {
    if (!status || status->isCompleted()) return;
    if (status->parent() && status->definition().propagation == Propagation::NESTED) {
        status->markCompleted();
        TransactionContext::clear();
        return;
    }
    notifyBeforeRollback(*status);
    try { status->connection()->rollback(); } catch (...) {}
    notifyAfterRollback(*status);
    notifyAfterCompletion(*status);
    status->markCompleted();
    if (watchdog_) watchdog_->unregisterTx(status.get());
    TransactionContext::clear();
}
void TransactionManager::notifyBeforeCommit(TransactionStatus& s) {
    for (auto& l : listeners_) try { l->beforeCommit(s); } catch (...) {}
}
void TransactionManager::notifyAfterCommit(TransactionStatus& s) {
    for (auto& l : listeners_) try { l->afterCommit(s); } catch (...) {}
}
void TransactionManager::notifyBeforeRollback(TransactionStatus& s) {
    for (auto& l : listeners_) try { l->beforeRollback(s); } catch (...) {}
}
void TransactionManager::notifyAfterRollback(TransactionStatus& s) {
    for (auto& l : listeners_) try { l->afterRollback(s); } catch (...) {}
}
void TransactionManager::notifyAfterCompletion(TransactionStatus& s) {
    for (auto& l : listeners_) try { l->afterCompletion(s); } catch (...) {}
}
} // namespace cp