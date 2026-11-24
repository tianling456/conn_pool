#include "pool/pool_manager.h"
#include <stdexcept>

namespace cp {

    // ConnectionPoolManager::ConnectionPoolManager(AppConfig cfg) : cfg_(std::move(cfg)) {
    //     for (const auto& pc : cfg_.datasource.pools) {
    //         if (pools_.count(pc.name))
    //             throw std::runtime_error("duplicate pool name: " + pc.name);
    //         auto factory = std::shared_ptr<IConnectionFactory>(makeConnectionFactory(pc.driver));
    //         pools_.emplace(pc.name,
    //             std::make_shared<ConnectionPool>(pc, std::move(factory)));
    //     }
    //     if (pools_.empty()) throw std::runtime_error("no pools configured");
    //     defaultPool_ = cfg_.datasource.defaultPool;
    //     if (defaultPool_.empty() || !pools_.count(defaultPool_))
    //         defaultPool_ = pools_.begin()->first;
    // }
    //
    // std::shared_ptr<ConnectionPool>
    // ConnectionPoolManager::getPool(const std::string& name) {
    //     auto key = name.empty() ? defaultPool_ : name;
    //     auto it  = pools_.find(key);
    //     if (it == pools_.end()) throw std::runtime_error("pool not found: " + key);
    //     return it->second;
    // }
    //
    // void ConnectionPoolManager::setTraceSink(std::shared_ptr<ITraceSink> s) {
    //     for (auto& [_, p] : pools_) p->setTraceSink(s);
    // }
    //
    // ConnectionPoolManager::~ConnectionPoolManager() {
    //     for (auto& [_, p] : pools_) p->close();
    // }


ConnectionPoolManager::ConnectionPoolManager(AppConfig cfg, Options opts)
    : cfg_(std::move(cfg)) {

    for (const auto& pc : cfg_.datasource.pools) {
        if (pools_.count(pc.name) || skipped_.count(pc.name)) {
            std::fprintf(stderr, "[WARN] duplicate pool name '%s', skipped\n", pc.name.c_str());
            continue;
        }
        try {
            auto factory = std::shared_ptr<IConnectionFactory>(
                makeConnectionFactory(pc.driver).release());
            pools_.emplace(pc.name,
                std::make_shared<ConnectionPool>(pc, std::move(factory)));
            std::fprintf(stderr, "[INFO] pool '%s' (driver=%s) loaded\n",
                         pc.name.c_str(), pc.driver.c_str());
        } catch (const std::exception& e) {
            if (!opts.skipUnavailable) throw;
            std::fprintf(stderr, "[WARN] pool '%s' (driver=%s) skipped: %s\n",
                         pc.name.c_str(), pc.driver.c_str(), e.what());
            skipped_[pc.name] = e.what();
        }
    }

    if (pools_.empty()) {
        throw std::runtime_error(
            "no usable pool loaded. available drivers: " + listAvailableDrivers()
            + ". install the missing -dev package or set ORACLE_HOME, then rebuild.");
    }

    defaultPool_ = cfg_.datasource.defaultPool;
    if (defaultPool_.empty() || !pools_.count(defaultPool_)) {
        if (!defaultPool_.empty() && skipped_.count(defaultPool_)) {
            std::fprintf(stderr,
                "[WARN] default-pool '%s' was skipped, fallback to '%s'\n",
                defaultPool_.c_str(), pools_.begin()->first.c_str());
        }
        defaultPool_ = pools_.begin()->first;
    }
}

std::shared_ptr<ConnectionPool>
ConnectionPoolManager::getPool(const std::string& name) {
    auto key = name.empty() ? defaultPool_ : name;
    auto it  = pools_.find(key);
    if (it != pools_.end()) return it->second;

    if (skipped_.count(key)) {
        throw std::runtime_error(
            "pool '" + key + "' is not available: " + skipped_.at(key));
    }
    throw std::runtime_error("pool not found: " + key);
}

void ConnectionPoolManager::setTraceSink(std::shared_ptr<ITraceSink> s) {
    for (auto& [_, p] : pools_) p->setTraceSink(s);
}

std::vector<std::string> ConnectionPoolManager::loadedPoolNames() const {
    std::vector<std::string> r;
    r.reserve(pools_.size());
    for (auto& [name, _] : pools_) r.push_back(name);
    return r;
}

ConnectionPoolManager::~ConnectionPoolManager() {
    for (auto& [_, p] : pools_) p->close();
}

} // namespace cp