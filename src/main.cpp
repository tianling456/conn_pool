#include "config/yaml_config.h"
#include "interceptor/interceptor_factory.h"
#include "interceptor/metrics_collector.h"
#include "interceptor/query_cache_interceptor.h"
#include "pool/pool_manager.h"
#include "result/reflection_registry.h"
#include "result/type_handler.h"
#include "sql/sql_executor.h"
#include "sql/sql_logger.h"
#include "tx/routing_policy.h"
#include "tx/transaction_manager.h"
#include "tx/transaction_template.h"
#include "tx/transaction_watchdog.h"
#include "xml/substitution_policy.h"
#include "xml/xml_mapper_registry.h"

#include <cstdio>
#include <iostream>

using namespace cp;

struct User {
    std::int64_t id = 0;
    std::string  name;
    std::int64_t age = 0;
};

int main() {
    CP_REGISTER_FIELD(User, id);
    CP_REGISTER_FIELD(User, name);
    CP_REGISTER_FIELD(User, age);

    AppConfig cfg = AppConfig::loadFromFile("../application.yaml");
    auto poolMgr = std::make_shared<ConnectionPoolManager>(std::move(cfg));

    SqlLogger::setEnabled(poolMgr->config().sql.log.enabled);
    SqlLogger::setLogParams(poolMgr->config().sql.log.logParams);

    std::vector<InterceptorDeclInput> decls;
    for (auto& d : poolMgr->config().sql.interceptors)
        decls.push_back({d.name, d.priority, d.params});
    auto chain = buildInterceptorChain(decls);

    std::shared_ptr<QueryCacheInterceptor> cache;
    for (auto& [ptr, _] : chain->entries())
        if (auto c = std::dynamic_pointer_cast<QueryCacheInterceptor>(ptr)) cache = c;

    std::shared_ptr<ISubstitutionPolicy> policy;
    if (poolMgr->config().sql.substitutionPolicy == "identifier")
        policy = std::make_shared<SqlIdentifierSubstitution>();
    else if (poolMgr->config().sql.substitutionPolicy == "whitelist")
        policy = std::make_shared<WhitelistSubstitution>();
    else
        policy = std::make_shared<DenyAllSubstitution>();

    auto reg = std::make_shared<XmlMapperRegistry>();
    reg->loadFiles(poolMgr->config().sql.mapperLocations);

    auto wd = std::make_shared<TransactionWatchdog>(poolMgr);
    wd->start();

    auto tm = std::make_shared<TransactionManager>(poolMgr);
    tm->setWatchdog(wd);
    tm->setRoutingPolicy(std::make_shared<ReadWriteRouting>("master", "slave"));

    auto tt = std::make_shared<TransactionTemplate>(tm, reg, policy);

    SqlExecutor sql(poolMgr, reg, chain, policy);
    if (cache) sql.setCache(cache);
    sql.setTransactionTemplate(tt);   // ← 注入，用于 XML tx-* 自动事务


    // 临时放在 main.cpp 里验证
    try { auto f = makeConnectionFactory("pg");  std::cout << "pg factory ok\n"; } catch (const std::exception& e) { std::cout << e.what() << "\n"; }
    try { auto f = makeConnectionFactory("postgresql"); std::cout << "postgresql factory ok\n"; } catch (const std::exception& e) { std::cout << e.what() << "\n"; }
    try { auto f = makeConnectionFactory("oracle"); std::cout << "oracle factory ok\n"; } catch (const std::exception& e) { std::cout << "expected: " << e.what() << "\n"; }

    // 1. DDL —— XML 无 tx 声明，直接执行
    sql.exec("user.createUserTable");

    // 2. DML —— XML 声明了 tx-* 属性，自动开事务 + selectKey 回填
    {
        SqlParams p;
        p.set("name", std::string("张三"));
        p.set("age",  std::int64_t{25});
        sql.update("user.insertUser", p);   // ← 自动事务
        auto* v = p.find("id");
        std::cout << "generated id = " << (v ? valueToString(*v) : "null") << "\n";
    }

    // 3. DQL —— XML 声明了 tx-propagation="SUPPORTS" + read-only，自动走事务
    {
        SqlParams p;
        p.set("id", std::int64_t{1});
        auto users = sql.queryByResultMap<User>("user.selectUserById", p);
        for (auto& u : users)
            std::cout << "User{id=" << u.id << ", name=" << u.name
                      << ", age=" << u.age << "}\n";
    }

    if (cache) {
        std::printf("cache hit=%zu miss=%zu size=%zu\n",
                    cache->hits(), cache->misses(), cache->size());
    }

    // 4. 显式方式：从 XML 的 tx-* 拿定义 + 多语句作为一个事务单元
    {
        SqlParams p1;
        p1.set("name", std::string("李四"));
        p1.set("age",  std::int64_t{30});

        SqlParams p2;
        p2.set("name", std::string("王五"));
        p2.set("age",  std::int64_t{28});

        tt->executeFromSql("user.insertUser", [&](TxSqlExecutor& e) {
            // 同一事务里插两条，第二条失败就整体回滚
            e.update("user.insertUser", p1);
            e.update("user.insertUser", p2);
            return 0;
        });
        std::cout << "explicit multi-stmt tx ok\n";
    }

    // 5. 完全手工方式：覆盖 XML 的默认事务属性
    {
        tt->execute(TransactionDefinition{}.setIso(IsolationLevel::SERIALIZABLE).setTimeout(10),
            [&](TxSqlExecutor& e) {
                SqlParams p;
                p.set("name", std::string("赵六"));
                p.set("age",  std::int64_t{33});
                return e.update("user.insertUser", p);
            });
        std::cout << "manual def tx ok\n";
    }

    auto pool = poolMgr->getPool("master");
    std::cout << pool->stats().toPrometheus(
        "master", pool->totalConnections(), pool->activeConnections(),
        pool->idleConnections(), pool->currentTimeoutMs());



    // 走 PG
    SqlParams p;
    p.set("orderNo", std::string("ORD-001"));
    p.set("amount", 100.5);
    sql.update("orderpg.insertOrder", p);
    std::cout << "pg order id = " << valueToString(*p.find("id")) << "\n";

    // 跨库事务（同事务多数据源，各自独立 commit/rollback）
    tt->run([&](TxSqlExecutor &e) {
        SqlParams u;
        u.set("name", std::string("张三"));
        u.set("age", std::int64_t{25});
        e.update("user.insertUser", u); // 走 MySQL master
        SqlParams o;
        o.set("orderNo", std::string("ORD-002"));
        o.set("amount", 50.0);
        e.update("orderpg.insertOrder", o); // 走 PostgreSQL
        return 0;
    });


    //oracle
    SqlParams po;
    po.set("empno", std::int64_t{7788});
    po.set("ename", std::string("SCOTT"));
    po.set("sal",   3000.0);
    sql.update("employee.insertEmployee", p);


    //sqllite
    SqlParams pl;
    pl.set("title", std::string("写单元测试"));
    pl.set("done",  std::int64_t{0});
    sql.update("todo.insert", p);
    std::cout << "sqlite id = " << valueToString(*p.find("id")) << "\n";

    auto rows = sql.query("todo.selectAll");


    wd->stop();
    return 0;
}