#include "pool/pool_manager.h"
#include <stdexcept>

namespace cp {

    ConnectionPoolManager::ConnectionPoolManager(AppConfig cfg) : cfg_(std::move(cfg)) {
        for (const auto& pc : cfg_.datasource.pools) {
            if (pools_.count(pc.name))
                throw std::runtime_error("duplicate pool name: " + pc.name);
            auto factory = std::shared_ptr<IConnectionFactory>(makeConnectionFactory(pc.driver));
            pools_.emplace(pc.name,
                std::make_shared<ConnectionPool>(pc, std::move(factory)));
        }
        if (pools_.empty()) throw std::runtime_error("no pools configured");
        defaultPool_ = cfg_.datasource.defaultPool;
        if (defaultPool_.empty() || !pools_.count(defaultPool_))
            defaultPool_ = pools_.begin()->first;
    }

    std::shared_ptr<ConnectionPool>
    ConnectionPoolManager::getPool(const std::string& name) {
        auto key = name.empty() ? defaultPool_ : name;
        auto it  = pools_.find(key);
        if (it == pools_.end()) throw std::runtime_error("pool not found: " + key);
        return it->second;
    }

    void ConnectionPoolManager::setTraceSink(std::shared_ptr<ITraceSink> s) {
        for (auto& [_, p] : pools_) p->setTraceSink(s);
    }

    ConnectionPoolManager::~ConnectionPoolManager() {
        for (auto& [_, p] : pools_) p->close();
    }

} // namespace cp