#ifndef CONN_POOL_TRANSACTION_TEMPLATE_H
#define CONN_POOL_TRANSACTION_TEMPLATE_H

#include "tx/transaction_manager.h"
#include "tx/tx_sql_executor.h"
#include "xml/xml_mapper_registry.h"
#include <chrono>
#include <stdexcept>
#include <thread>

namespace cp {
class TransactionTemplate {
public:
    TransactionTemplate(std::shared_ptr<TransactionManager> mgr,
                        std::shared_ptr<XmlMapperRegistry> reg = nullptr,
                        std::shared_ptr<ISubstitutionPolicy> policy = nullptr)
        : mgr_(std::move(mgr)), reg_(std::move(reg)), policy_(std::move(policy)) {}

    template <typename Fn>
    auto run(Fn&& fn) -> decltype(fn(std::declval<TxSqlExecutor&>())) {
        return execute(TransactionDefinition{}, std::forward<Fn>(fn));
    }

    template <typename Fn>
    auto execute(const TransactionDefinition& def, Fn&& fn)
        -> decltype(fn(std::declval<TxSqlExecutor&>())) {
        auto status = mgr_->begin(def);
        try {
            TxSqlExecutor executor(status->sharedConnection(), reg_, policy_);
            auto result = fn(executor);
            if (status->isRollbackOnly()) {
                mgr_->rollback(status);
                throw std::runtime_error("rollback-only");
            }
            mgr_->commit(status);
            return result;
        } catch (...) {
            try { mgr_->rollback(status); } catch (...) {}
            throw;
        }
    }

    // ========= 新增：从 XML 上的 tx-* 属性拿 Definition =========
    template <typename Fn>
    auto executeFromSql(const std::string& sqlId, Fn&& fn)
        -> decltype(fn(std::declval<TxSqlExecutor&>())) {
        if (!reg_) throw std::runtime_error("executeFromSql requires XmlMapperRegistry");
        const auto& st = reg_->get(sqlId);
        TransactionDefinition def;
        if (st.tx.hasDecl) {
            def = txDefFromStrings(st.tx.propagation, st.tx.isolation,
                                   st.tx.readOnly, st.tx.timeoutSec);
        }
        def.name = sqlId;
        return execute(def, std::forward<Fn>(fn));
    }

    template <typename Fn>
    auto executeWithRetry(const TransactionDefinition& def, int maxAttempts, Fn&& fn)
        -> decltype(fn(std::declval<TxSqlExecutor&>())) {
        std::int64_t delay = 50;
        for (int attempt = 1; ; ++attempt) {
            try { return execute(def, fn); }
            catch (const std::exception& e) {
                if (attempt >= maxAttempts || !isRetryable(e)) throw;
                std::this_thread::sleep_for(std::chrono::milliseconds(delay));
                delay = std::min<std::int64_t>(delay * 2, 2000);
            }
        }
    }

    void setListener(std::shared_ptr<TransactionListener> l) { mgr_->addListener(std::move(l)); }
    void setIsolation(IsolationLevel l)  { defaultDef_.isolation = l; }
    void setPropagation(Propagation p)   { defaultDef_.propagation = p; }
    void setReadOnly(bool ro)            { defaultDef_.readOnly = ro; }
    void setTimeoutSec(int s)            { defaultDef_.timeoutSec = s; }
    void setName(std::string n)          { defaultDef_.name = std::move(n); }
    std::shared_ptr<TransactionManager> manager() { return mgr_; }
    std::shared_ptr<XmlMapperRegistry>  registry() const { return reg_; }
    std::shared_ptr<ISubstitutionPolicy> policy()  const { return policy_; }

private:
    static bool isRetryable(const std::exception& e) {
        std::string m = e.what();
        for (auto& c : m) c = (char)std::tolower((unsigned char)c);
        return m.find("deadlock") != std::string::npos
            || m.find("lock wait timeout") != std::string::npos
            || m.find("try restarting") != std::string::npos
            || m.find("serialization") != std::string::npos;
    }
    std::shared_ptr<TransactionManager>  mgr_;
    std::shared_ptr<XmlMapperRegistry>   reg_;
    std::shared_ptr<ISubstitutionPolicy> policy_;
    TransactionDefinition                defaultDef_;
};
} // namespace cp

#endif //CONN_POOL_TRANSACTION_TEMPLATE_H