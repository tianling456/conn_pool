#include "sql/sql_executor.h"
#include "sql/sql_logger.h"
#include "tx/transaction_manager.h"
#include "tx/transaction_template.h"
#include <chrono>
#include <cstdio>
#include <stdexcept>

namespace cp {
SqlExecutor::SqlExecutor(std::shared_ptr<ConnectionPoolManager> mgr,
                         std::shared_ptr<XmlMapperRegistry> reg,
                         std::shared_ptr<InterceptorChain> chain,
                         std::shared_ptr<ISubstitutionPolicy> policy)
    : mgr_(std::move(mgr)), registry_(std::move(reg)),
      chain_(std::move(chain)), policy_(std::move(policy)) {}

BoundSql SqlExecutor::bindStatement(const SqlStatement& st, const SqlParams& p) {
    return BoundSqlBuilder(p, policy_).build(st.root);
}
std::string SqlExecutor::cacheKey(const BoundSql& b) const {
    std::string k = b.sql;
    k.push_back('|');
    for (auto& p : b.positionalParams) {
        if (auto i = std::get_if<std::int64_t>(&p))   k += std::to_string(*i);
        else if (auto u = std::get_if<std::uint64_t>(&p)) k += std::to_string(*u);
        else if (auto d = std::get_if<double>(&p)) {
            char buf[32]; std::snprintf(buf, sizeof(buf), "%g", *d); k += buf;
        } else if (auto s = std::get_if<std::string>(&p)) k += *s;
        k.push_back('|');
    }
    return k;
}

// ← 新增：判断是否需要自动事务
bool SqlExecutor::needsAutoTx(const SqlStatement& st) const {
    if (!txTemplate_) return false;                    // 没注入就不自动
    if (!st.tx.hasDecl) return false;                  // XML 没声明就不自动
    if (TransactionManager::current()) return false;   // 已在事务中不重复开
    return true;
}

ResultSet SqlExecutor::query(const std::string& id, const SqlParams& params) {
    const auto& st = registry_->get(id);
    if (st.type != SqlType::Dql) throw std::runtime_error("expect DQL: " + id);

    // 自动事务分支
    if (needsAutoTx(st)) {
        auto def = txDefFromStrings(st.tx.propagation, st.tx.isolation,
                                    st.tx.readOnly, st.tx.timeoutSec);
        def.name = id;
        return txTemplate_->execute(def, [&](TxSqlExecutor& e) {
            return e.query(id, params);
        });
    }

    BoundSql bound = bindStatement(st, params);
    if (cache_) {
        if (auto hit = cache_->lookup(cacheKey(bound))) {
            SqlLogger::logCacheHit(id, st.datasource, hit->size());
            return *hit;
        }
    }

    SqlLogger::logBefore(id, st.datasource, bound.sql, bound.positionalParams);
    auto t0 = std::chrono::steady_clock::now();
    try {
        auto pool = mgr_->getPool(st.datasource);
        auto result = runWithInterceptors(st, bound, [&] {
            return pool->retry().execute([&] {
                auto conn = pool->getConnection();
                return conn->query(bound.sql, bound.positionalParams);
            });
        });
        auto el = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - t0);
        SqlLogger::logAfter(id, el, result.size(), true);
        if (cache_) cache_->store(cacheKey(bound), result);
        return result;
    } catch (const std::exception& e) {
        auto el = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - t0);
        SqlLogger::logError(id, el, e);
        throw;
    }
}

std::uint64_t SqlExecutor::update(const std::string& id, const SqlParams& params) {
    const auto& st = registry_->get(id);

    // 自动事务分支
    if (needsAutoTx(st)) {
        auto def = txDefFromStrings(st.tx.propagation, st.tx.isolation,
                                    st.tx.readOnly, st.tx.timeoutSec);
        def.name = id;
        return txTemplate_->execute(def, [&](TxSqlExecutor& e) {
            return e.update(id, params);
        });
    }

    SqlParams copy = params;
    return updateAndReturnKey(id, copy);
}

std::uint64_t SqlExecutor::updateAndReturnKey(const std::string& id, SqlParams& params) {
    const auto& st = registry_->get(id);
    if (st.type != SqlType::Dml) throw std::runtime_error("expect DML: " + id);
    auto pool = mgr_->getPool(st.datasource);

    if (st.selectKey && st.selectKey->order == "BEFORE") {
        BoundSql sk = BoundSqlBuilder(params, policy_).build(st.selectKey->node);
        SqlLogger::logBefore(id + ".selectKey[BEFORE]", st.datasource,
                             sk.sql, sk.positionalParams);
        auto t0 = std::chrono::steady_clock::now();
        try {
            auto rs = pool->retry().execute([&] {
                auto conn = pool->getConnection();
                return conn->query(sk.sql, sk.positionalParams);
            });
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

    BoundSql bound = bindStatement(st, params);
    SqlLogger::logBefore(id, st.datasource, bound.sql, bound.positionalParams);
    auto t0 = std::chrono::steady_clock::now();
    std::uint64_t r = 0;
    try {
        r = runWithInterceptors(st, bound, [&] {
            return pool->retry().execute([&] {
                auto conn = pool->getConnection();
                return conn->update(bound.sql, bound.positionalParams);
            });
        });
        auto el = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - t0);
        SqlLogger::logAfter(id, el, r, true);
    } catch (const std::exception& e) {
        auto el = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - t0);
        SqlLogger::logError(id, el, e);
        throw;
    }

    if (st.selectKey && st.selectKey->order == "AFTER") {
        BoundSql sk = BoundSqlBuilder(params, policy_).build(st.selectKey->node);
        SqlLogger::logBefore(id + ".selectKey[AFTER]", st.datasource,
                             sk.sql, sk.positionalParams);
        auto t1 = std::chrono::steady_clock::now();
        try {
            auto rs = pool->retry().execute([&] {
                auto conn = pool->getConnection();
                return conn->query(sk.sql, sk.positionalParams);
            });
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

bool SqlExecutor::exec(const std::string& id, const SqlParams& params) {
    const auto& st = registry_->get(id);
    if (st.type != SqlType::Ddl && st.type != SqlType::Dcl)
        throw std::runtime_error("expect DDL/DCL: " + id);

    // 自动事务分支
    if (needsAutoTx(st)) {
        auto def = txDefFromStrings(st.tx.propagation, st.tx.isolation,
                                    st.tx.readOnly, st.tx.timeoutSec);
        def.name = id;
        return txTemplate_->execute(def, [&](TxSqlExecutor& e) {
            return e.exec(id, params);
        });
    }

    BoundSql bound = bindStatement(st, params);
    SqlLogger::logBefore(id, st.datasource, bound.sql, bound.positionalParams);
    auto t0 = std::chrono::steady_clock::now();
    try {
        auto pool = mgr_->getPool(st.datasource);
        bool ok = runWithInterceptors(st, bound, [&] {
            return pool->retry().execute([&] {
                auto conn = pool->getConnection();
                return conn->exec(bound.sql, bound.positionalParams);
            });
        });
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
} // namespace cp