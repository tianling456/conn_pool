#ifndef CONN_POOL_SQL_EXECUTOR_H
#define CONN_POOL_SQL_EXECUTOR_H

#include "interceptor/interceptor_chain.h"
#include "interceptor/query_cache_interceptor.h"
#include "pool/pool_manager.h"
#include "result/result_mapper.h"
#include "result/xml_result_mapper.h"
#include "xml/bound_sql_builder.h"
#include "xml/substitution_policy.h"
#include "xml/xml_mapper_registry.h"
#include <functional>
#include <memory>

namespace cp {

class TransactionTemplate;   // ← 前向声明（避免头文件重依赖）

class SqlExecutor {
public:
    SqlExecutor(std::shared_ptr<ConnectionPoolManager> mgr,
                std::shared_ptr<XmlMapperRegistry> registry,
                std::shared_ptr<InterceptorChain> chain = nullptr,
                std::shared_ptr<ISubstitutionPolicy> policy = nullptr);

    void setCache(std::shared_ptr<QueryCacheInterceptor> c) { cache_ = std::move(c); }

    // ← 新增：注入 TransactionTemplate，用于 XML tx-* 自动事务
    void setTransactionTemplate(std::shared_ptr<TransactionTemplate> tt) {
        txTemplate_ = std::move(tt);
    }

    ResultSet     query(const std::string& id, const SqlParams& params = {});
    std::uint64_t update(const std::string& id, const SqlParams& params = {});
    bool          exec  (const std::string& id, const SqlParams& params = {});
    std::uint64_t updateAndReturnKey(const std::string& id, SqlParams& params);

    template <typename T>
    std::vector<T> query(const std::string& id, const SqlParams& params,
                         std::shared_ptr<IResultMapper<T>> mapper) {
        auto rows = query(id, params);
        std::vector<T> out;
        out.reserve(rows.size());
        for (auto& r : rows) out.push_back(mapper->map(r));
        return out;
    }
    template <typename T>
    std::vector<T> query(const std::string& id, const SqlParams& params,
                         std::function<T(const ResultRow&)> mapper) {
        auto rows = query(id, params);
        std::vector<T> out;
        out.reserve(rows.size());
        for (auto& r : rows) out.push_back(mapper(r));
        return out;
    }
    template <typename T>
    std::vector<T> queryByResultMap(const std::string& id, const SqlParams& params) {
        const auto& st = registry_->get(id);
        if (st.resultMapId.empty()) throw std::runtime_error("no resultMap: " + id);
        const auto& def = registry_->getResultMap(st.resultMapId);
        return query<T>(id, params, std::make_shared<XmlResultMapper<T>>(def));
    }

private:
    BoundSql bindStatement(const SqlStatement& st, const SqlParams& p);
    std::string cacheKey(const BoundSql& b) const;
    bool        needsAutoTx(const SqlStatement& st) const;

    template <typename Fn>
    auto runWithInterceptors(const SqlStatement& st, BoundSql& bound, Fn&& run)
        -> decltype(run()) {
        if (chain_) return chain_->execute(st, bound, std::forward<Fn>(run));
        return run();
    }

    std::shared_ptr<ConnectionPoolManager> mgr_;
    std::shared_ptr<XmlMapperRegistry>     registry_;
    std::shared_ptr<InterceptorChain>      chain_;
    std::shared_ptr<ISubstitutionPolicy>   policy_;
    std::shared_ptr<QueryCacheInterceptor> cache_;
    std::shared_ptr<TransactionTemplate>   txTemplate_;
};
} // namespace cp

#endif //CONN_POOL_SQL_EXECUTOR_H