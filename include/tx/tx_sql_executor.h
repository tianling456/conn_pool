#ifndef CONN_POOL_TX_SQL_EXECUTOR_H
#define CONN_POOL_TX_SQL_EXECUTOR_H

#include "db/db_connection.h"
#include "sql/sql_logger.h"
#include "tx/transaction.h"
#include "xml/bound_sql_builder.h"
#include "xml/substitution_policy.h"
#include "xml/xml_mapper_registry.h"
#include <chrono>
#include <memory>
#include <stdexcept>

namespace cp {
class TxSqlExecutor {
public:
    TxSqlExecutor(std::shared_ptr<IDbConnection> conn,
                  std::shared_ptr<XmlMapperRegistry> registry,
                  std::shared_ptr<ISubstitutionPolicy> policy = nullptr)
        : conn_(std::move(conn)), registry_(std::move(registry)), policy_(std::move(policy)) {}

    ResultSet query(const std::string& id, const SqlParams& params = {}) {
        const auto& st = registry_->get(id);
        if (st.type != SqlType::Dql) throw std::runtime_error("expect DQL: " + id);
        BoundSql b = BoundSqlBuilder(params, policy_).build(st.root);
        std::string pool = st.datasource.empty() ? "<tx>" : st.datasource;

        SqlLogger::logBefore(id, pool, b.sql, b.positionalParams);
        auto t0 = std::chrono::steady_clock::now();
        try {
            auto rs = conn_->query(b.sql, b.positionalParams);
            auto el = std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now() - t0);
            SqlLogger::logAfter(id, el, rs.size(), true);
            return rs;
        } catch (const std::exception& e) {
            auto el = std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now() - t0);
            SqlLogger::logError(id, el, e);
            throw;
        }
    }

    std::uint64_t update(const std::string& id, const SqlParams& params = {}) {
        SqlParams copy = params;
        return updateAndReturnKey(id, copy);
    }

    std::uint64_t updateAndReturnKey(const std::string& id, SqlParams& params) {
        const auto& st = registry_->get(id);
        if (st.type != SqlType::Dml) throw std::runtime_error("expect DML: " + id);
        std::string pool = st.datasource.empty() ? "<tx>" : st.datasource;

        // ---- selectKey BEFORE ----
        if (st.selectKey && st.selectKey->order == "BEFORE") {
            BoundSql sk = BoundSqlBuilder(params, policy_).build(st.selectKey->node);
            SqlLogger::logBefore(id + ".selectKey[BEFORE]", pool, sk.sql, sk.positionalParams);
            auto t0 = std::chrono::steady_clock::now();
            try {
                auto rs = conn_->query(sk.sql, sk.positionalParams);
                auto el = std::chrono::duration_cast<std::chrono::microseconds>(
                    std::chrono::steady_clock::now() - t0);
                SqlLogger::logAfter(id + ".selectKey[BEFORE]", el, rs.size(), true);
                if (!rs.empty() && !rs[0].empty() && !st.selectKey->keyProperty.empty())
                    for (auto& [_, v] : rs[0]) { if (v) params.set(st.selectKey->keyProperty, *v); break; }
            } catch (const std::exception& e) {
                auto el = std::chrono::duration_cast<std::chrono::microseconds>(
                    std::chrono::steady_clock::now() - t0);
                SqlLogger::logError(id + ".selectKey[BEFORE]", el, e);
                throw;
            }
        }

        // ---- 主语句 ----
        BoundSql b = BoundSqlBuilder(params, policy_).build(st.root);
        SqlLogger::logBefore(id, pool, b.sql, b.positionalParams);
        auto t0 = std::chrono::steady_clock::now();
        std::uint64_t r = 0;
        try {
            r = conn_->update(b.sql, b.positionalParams);
            auto el = std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now() - t0);
            SqlLogger::logAfter(id, el, r, true);
        } catch (const std::exception& e) {
            auto el = std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now() - t0);
            SqlLogger::logError(id, el, e);
            throw;
        }

        // ---- selectKey AFTER ----
        if (st.selectKey && st.selectKey->order == "AFTER") {
            BoundSql sk = BoundSqlBuilder(params, policy_).build(st.selectKey->node);
            SqlLogger::logBefore(id + ".selectKey[AFTER]", pool, sk.sql, sk.positionalParams);
            auto t1 = std::chrono::steady_clock::now();
            try {
                auto rs = conn_->query(sk.sql, sk.positionalParams);
                auto el = std::chrono::duration_cast<std::chrono::microseconds>(
                    std::chrono::steady_clock::now() - t1);
                SqlLogger::logAfter(id + ".selectKey[AFTER]", el, rs.size(), true);
                if (!rs.empty() && !rs[0].empty() && !st.selectKey->keyProperty.empty())
                    for (auto& [_, v] : rs[0]) { if (v) params.set(st.selectKey->keyProperty, *v); break; }
            } catch (const std::exception& e) {
                auto el = std::chrono::duration_cast<std::chrono::microseconds>(
                    std::chrono::steady_clock::now() - t1);
                SqlLogger::logError(id + ".selectKey[AFTER]", el, e);
                throw;
            }
        }
        return r;
    }

    bool exec(const std::string& id, const SqlParams& params = {}) {
        const auto& st = registry_->get(id);
        if (st.type != SqlType::Ddl && st.type != SqlType::Dcl)
            throw std::runtime_error("expect DDL/DCL: " + id);
        BoundSql b = BoundSqlBuilder(params, policy_).build(st.root);
        std::string pool = st.datasource.empty() ? "<tx>" : st.datasource;

        SqlLogger::logBefore(id, pool, b.sql, b.positionalParams);
        auto t0 = std::chrono::steady_clock::now();
        try {
            bool ok = conn_->exec(b.sql, b.positionalParams);
            auto el = std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now() - t0);
            SqlLogger::logAfter(id, el, ok ? 1 : 0, true);
            return ok;
        } catch (const std::exception& e) {
            auto el = std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now() - t0);
            SqlLogger::logError(id, el, e);
            throw;
        }
    }

private:
    std::shared_ptr<IDbConnection>       conn_;
    std::shared_ptr<XmlMapperRegistry>   registry_;
    std::shared_ptr<ISubstitutionPolicy> policy_;
};
} // namespace cp

#endif //CONN_POOL_TX_SQL_EXECUTOR_H