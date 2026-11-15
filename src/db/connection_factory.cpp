//
// Created by aobo on 2026/11/15.
//
#include "db/connection_factory.h"
#include <stdexcept>
#include <mutex>

namespace cp {
    namespace {
        std::unordered_map<std::string, FactoryCreator>& registry() {
            static std::unordered_map<std::string, FactoryCreator> m;
            return m;
        }
        std::mutex& registryMutex() {
            static std::mutex m;
            return m;
        }
    }

    void registerConnectionFactory(const std::string& driver, FactoryCreator creator) {
        std::lock_guard<std::mutex> lk(registryMutex());
        registry()[driver] = std::move(creator);
    }

    std::unique_ptr<IConnectionFactory> makeConnectionFactory(const std::string& driver) {
        std::lock_guard<std::mutex> lk(registryMutex());
        auto it = registry().find(driver);
        if (it == registry().end())
            throw std::runtime_error("unsupported driver: " + driver
                                     + " (did you forget to register it?)");
        return it->second();
    }

} // namespace cp