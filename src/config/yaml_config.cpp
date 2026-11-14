#include "config/yaml_config.h"
#include <yaml-cpp/yaml.h>
#include <stdexcept>

namespace cp {
namespace {
template <typename T>
T getOr(const YAML::Node& n, const char* k, T d) { return n[k] ? n[k].as<T>() : d; }

HealthCheckConfig parseHC(const YAML::Node& n) {
    HealthCheckConfig h;
    if (!n) return h;
    h.enabled             = getOr<bool>(n, "enabled", h.enabled);
    h.intervalMs          = getOr<std::int64_t>(n, "interval", h.intervalMs);
    h.validationQuery     = getOr<std::string>(n, "validation-query", h.validationQuery);
    h.validationTimeoutMs = getOr<std::int64_t>(n, "validation-timeout", h.validationTimeoutMs);
    return h;
}
RetryConfig parseRT(const YAML::Node& n) {
    RetryConfig r;
    if (!n) return r;
    r.maxAttempts  = getOr<int>(n, "max-attempts", r.maxAttempts);
    r.backoffMs    = getOr<std::int64_t>(n, "backoff", r.backoffMs);
    r.multiplier   = getOr<double>(n, "multiplier", r.multiplier);
    r.maxBackoffMs = getOr<std::int64_t>(n, "max-backoff", r.maxBackoffMs);
    return r;
}
PoolConfig parsePool(const YAML::Node& n) {
    PoolConfig p;
    p.name     = n["name"].as<std::string>();
    p.driver   = getOr<std::string>(n, "driver", p.driver);
    p.host     = getOr<std::string>(n, "host", p.host);
    p.port     = getOr<unsigned>(n, "port", p.port);
    p.database = getOr<std::string>(n, "database", p.database);
    p.username = getOr<std::string>(n, "username", p.username);
    p.password = getOr<std::string>(n, "password", p.password);
    p.minPoolSize         = getOr<int>(n, "min-pool-size", p.minPoolSize);
    p.maxPoolSize         = getOr<int>(n, "max-pool-size", p.maxPoolSize);
    p.connectionTimeoutMs = getOr<std::int64_t>(n, "connection-timeout", p.connectionTimeoutMs);
    p.idleTimeoutMs       = getOr<std::int64_t>(n, "idle-timeout", p.idleTimeoutMs);
    p.maxLifetimeMs       = getOr<std::int64_t>(n, "max-lifetime", p.maxLifetimeMs);
    p.healthCheck = parseHC(n["health-check"]);
    p.retry       = parseRT(n["retry"]);
    if (p.maxPoolSize < p.minPoolSize) throw std::runtime_error("maxPoolSize < minPoolSize");
    return p;
}
}

AppConfig AppConfig::loadFromFile(const std::string& path) {
    AppConfig cfg;
    YAML::Node root = YAML::LoadFile(path);
    auto app = root["app"];
    if (!app) throw std::runtime_error("yaml missing 'app'");

    auto ds = app["datasource"];
    cfg.datasource.defaultPool = getOr<std::string>(ds, "default-pool", "");
    if (auto pools = ds["pools"]; pools && pools.IsSequence())
        for (auto&& n : pools) cfg.datasource.pools.push_back(parsePool(n));

    if (auto s = app["sql"]; s) {
        cfg.sql.defaultDatasource  = getOr<std::string>(s, "default-datasource", "");
        cfg.sql.substitutionPolicy = getOr<std::string>(s, "substitution-policy", "deny-all");
        if (auto loc = s["mapper-locations"]; loc && loc.IsSequence())
            for (auto&& n : loc) cfg.sql.mapperLocations.push_back(n.as<std::string>());
        if (auto arr = s["interceptors"]; arr && arr.IsSequence())
            for (auto&& n : arr) {
                InterceptorDecl d;
                d.name     = n["name"].as<std::string>();
                d.priority = getOr<int>(n, "priority", 0);
                if (auto ps = n["params"]; ps && ps.IsMap())
                    for (auto&& kv : ps) d.params[kv.first.as<std::string>()] = kv.second.as<std::string>();
                cfg.sql.interceptors.push_back(std::move(d));
            }
        if (auto lg = s["log"]; lg) {
            cfg.sql.log.enabled   = getOr<bool>(lg, "enabled", true);
            cfg.sql.log.logParams = getOr<bool>(lg, "params",  true);
        }
    }
    // tx 段已删除
    return cfg;
}
} // namespace cp