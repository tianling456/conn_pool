#include "interceptor/interceptor_factory.h"
#include "interceptor/metrics_collector.h"
#include "interceptor/query_cache_interceptor.h"
#include "interceptor/slow_sql_logger.h"
#include <stdexcept>

namespace cp {
namespace {
long long parseInt(const InterceptorParams& p, const std::string& k, long long d) {
    auto it = p.find(k);
    if (it == p.end() || it->second.empty()) return d;
    return std::stoll(it->second);
}
}

InterceptorFactoryRegistry& InterceptorFactoryRegistry::instance() {
    static InterceptorFactoryRegistry inst;
    return inst;
}
InterceptorFactoryRegistry::InterceptorFactoryRegistry() {
    registerFactory("slow-sql-logger", [](const InterceptorParams& p) {
        auto ms = parseInt(p, "threshold-ms", 200);
        return std::make_shared<SlowSqlLogger>(std::chrono::milliseconds(ms));
    });
    registerFactory("query-cache", [](const InterceptorParams& p) {
        size_t cap = static_cast<size_t>(parseInt(p, "max-size", 1024));
        auto ttl   = std::chrono::seconds(parseInt(p, "ttl-seconds", 60));
        return std::make_shared<QueryCacheInterceptor>(cap, ttl);
    });
    registerFactory("metrics", [](const InterceptorParams&) {
        return std::make_shared<MetricsCollector>();
    });
}
void InterceptorFactoryRegistry::registerFactory(const std::string& n, InterceptorCreator c) {
    factories_[n] = std::move(c);
}
std::shared_ptr<SqlInterceptor>
InterceptorFactoryRegistry::create(const std::string& n,
                                   const InterceptorParams& p) const {
    auto it = factories_.find(n);
    if (it == factories_.end()) throw std::runtime_error("unknown interceptor: " + n);
    return it->second(p);
}
std::shared_ptr<InterceptorChain>
buildInterceptorChain(const std::vector<InterceptorDeclInput>& decls) {
    auto chain = std::make_shared<InterceptorChain>();
    for (auto& d : decls) {
        auto inst = InterceptorFactoryRegistry::instance().create(d.name, d.params);
        chain->add(std::move(inst), d.priority);
    }
    return chain;
}
} // namespace cp