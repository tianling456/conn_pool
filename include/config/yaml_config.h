//
// Created by aobo on 2026/10/30.
//

#ifndef CONN_POOL_YAML_CONFIG_H
#define CONN_POOL_YAML_CONFIG_H
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace cp {

    struct HealthCheckConfig {
        bool         enabled = true;
        std::int64_t intervalMs = 30000;
        std::string  validationQuery = "SELECT 1";
        std::int64_t validationTimeoutMs = 2000;
    };

    struct RetryConfig {
        int          maxAttempts = 3;
        std::int64_t backoffMs   = 300;
        double       multiplier  = 2.0;
        std::int64_t maxBackoffMs = 5000;
    };

    struct PoolConfig {
        std::string name;
        std::string driver = "mysql";
        std::string host = "127.0.0.1";
        unsigned    port = 3306;
        std::string database, username, password;
        int          minPoolSize = 1;
        int          maxPoolSize = 10;
        std::int64_t connectionTimeoutMs = 3000;
        std::int64_t idleTimeoutMs       = 600000;
        std::int64_t maxLifetimeMs       = 1800000;
        HealthCheckConfig healthCheck;
        RetryConfig       retry;
    };

    struct DataSourceConfig {
        std::string             defaultPool;
        std::vector<PoolConfig> pools;
    };

    struct InterceptorDecl {
        std::string name;
        int         priority = 0;
        std::map<std::string, std::string> params;
    };

    struct SqlLogConfigDecl {
        bool enabled   = true;
        bool logParams = true;
    };

    struct SqlConfig {
        std::string                  defaultDatasource;
        std::vector<std::string>     mapperLocations;
        std::vector<InterceptorDecl> interceptors;
        std::string                  substitutionPolicy = "deny-all";
        SqlLogConfigDecl             log;
    };

    struct AppConfig {
        DataSourceConfig datasource;
        SqlConfig        sql;
        static AppConfig loadFromFile(const std::string& path);
    };

} // namespace cp


#endif //CONN_POOL_YAML_CONFIG_H
