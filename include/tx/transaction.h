//
// Created by aobo on 2026/11/8.
//

#ifndef CONN_POOL_TRANSACTION_H
#define CONN_POOL_TRANSACTION_H
#include "db/db_connection.h"
#include <algorithm>
#include <cctype>
#include <chrono>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace cp {
enum class Propagation {
    REQUIRED, REQUIRES_NEW, NESTED, SUPPORTS, NOT_SUPPORTED, MANDATORY, NEVER
};

struct TransactionDefinition {
    IsolationLevel isolation   = IsolationLevel::DEFAULT;
    Propagation    propagation = Propagation::REQUIRED;
    bool           readOnly    = false;
    int            timeoutSec  = -1;
    std::string    name;
    TransactionDefinition& setIso(IsolationLevel l) { isolation = l; return *this; }
    TransactionDefinition& setProp(Propagation p)   { propagation = p; return *this; }
    TransactionDefinition& setRO(bool r)            { readOnly = r; return *this; }
    TransactionDefinition& setTimeout(int s)        { timeoutSec = s; return *this; }
    TransactionDefinition& setName(std::string n)   { name = std::move(n); return *this; }
};

// ========= 新增：从字符串创建 Definition =========
inline TransactionDefinition txDefFromStrings(
        const std::string& propagation = "",
        const std::string& isolation = "",
        bool readOnly = false,
        int timeoutSec = -1) {
    TransactionDefinition def;
    if (!propagation.empty()) {
        if      (propagation == "REQUIRED")      def.propagation = Propagation::REQUIRED;
        else if (propagation == "REQUIRES_NEW")  def.propagation = Propagation::REQUIRES_NEW;
        else if (propagation == "NESTED")        def.propagation = Propagation::NESTED;
        else if (propagation == "SUPPORTS")      def.propagation = Propagation::SUPPORTS;
        else if (propagation == "NOT_SUPPORTED") def.propagation = Propagation::NOT_SUPPORTED;
        else if (propagation == "MANDATORY")     def.propagation = Propagation::MANDATORY;
        else if (propagation == "NEVER")         def.propagation = Propagation::NEVER;
    }
    if (!isolation.empty()) {
        if      (isolation == "READ_UNCOMMITTED") def.isolation = IsolationLevel::READ_UNCOMMITTED;
        else if (isolation == "READ_COMMITTED")   def.isolation = IsolationLevel::READ_COMMITTED;
        else if (isolation == "REPEATABLE_READ")  def.isolation = IsolationLevel::REPEATABLE_READ;
        else if (isolation == "SERIALIZABLE")     def.isolation = IsolationLevel::SERIALIZABLE;
    }
    def.readOnly    = readOnly;
    def.timeoutSec  = timeoutSec;
    return def;
}

class TransactionStatus;
class TransactionListener {
public:
    virtual ~TransactionListener() = default;
    virtual void beforeCompletion(TransactionStatus&) {}
    virtual void beforeCommit    (TransactionStatus&) {}
    virtual void afterCommit     (TransactionStatus&) {}
    virtual void beforeRollback  (TransactionStatus&) {}
    virtual void afterRollback   (TransactionStatus&) {}
    virtual void afterCompletion (TransactionStatus&) {}
};

class TransactionStatus {
public:
    TransactionStatus(TransactionDefinition def, std::shared_ptr<IDbConnection> conn,
                      bool isNew, TransactionStatus* parent, int txId)
        : def_(std::move(def)), conn_(std::move(conn)),
          isNew_(isNew), parent_(parent), txId_(txId),
          startedAt_(std::chrono::steady_clock::now()) {}

    bool isNewTransaction() const { return isNew_; }
    bool isCompleted()      const { return completed_; }
    bool isRollbackOnly()   const { return rollbackOnly_; }
    bool isReadOnly()       const { return def_.readOnly; }

    IDbConnection*                 connection()       const { return conn_.get(); }
    std::shared_ptr<IDbConnection> sharedConnection() const { return conn_; }
    TransactionStatus*             parent()           const { return parent_; }
    const TransactionDefinition&   definition()       const { return def_; }
    int                            txId()             const { return txId_; }
    std::chrono::steady_clock::time_point startedAt() const { return startedAt_; }

    void setRollbackOnly() { rollbackOnly_ = true; }
    void markCompleted()   { completed_ = true; }

    void createSavepoint(const std::string& name) {
        std::string safe = "cp_user_" + name;
        validate(safe); conn_->savepoint(safe); stack_.push_back(safe);
    }
    void rollbackToSavepoint(const std::string& name) {
        std::string safe = "cp_user_" + name;
        validate(safe); conn_->rollbackToSavepoint(safe);
        auto it = std::find(stack_.begin(), stack_.end(), safe);
        if (it != stack_.end()) stack_.erase(it + 1, stack_.end());
    }
    void releaseSavepoint(const std::string& name) {
        std::string safe = "cp_user_" + name;
        validate(safe); conn_->releaseSavepoint(safe);
        auto it = std::find(stack_.begin(), stack_.end(), safe);
        if (it != stack_.end()) stack_.erase(it);
    }
private:
    static void validate(const std::string& n) {
        for (char c : n)
            if (!std::isalnum((unsigned char)c) && c != '_')
                throw std::invalid_argument("savepoint invalid");
    }
    TransactionDefinition          def_;
    std::shared_ptr<IDbConnection> conn_;
    bool                           isNew_;
    bool                           completed_    = false;
    bool                           rollbackOnly_ = false;
    TransactionStatus*             parent_       = nullptr;
    int                            txId_         = 0;
    std::chrono::steady_clock::time_point startedAt_;
    std::vector<std::string>       stack_;
};
} // namespace cp
#endif //CONN_POOL_TRANSACTION_H
