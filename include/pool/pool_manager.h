#ifndef CONN_POOL_POOL_MANAGER_H
#define CONN_POOL_POOL_MANAGER_H



#include "config/yaml_config.h"
#include "pool/connection_pool.h"
#include <memory>
#include <string>
#include <unordered_map>

#include "trace.h"

namespace cp {

    class ConnectionPoolManager {
    public:
        explicit ConnectionPoolManager(AppConfig cfg);
        ~ConnectionPoolManager();

        std::shared_ptr<ConnectionPool> getPool(const std::string& name = "");
        const AppConfig& config() const { return cfg_; }
        void setTraceSink(std::shared_ptr<ITraceSink> s);

    private:
        AppConfig cfg_;
        std::string defaultPool_;
        std::unordered_map<std::string, std::shared_ptr<ConnectionPool>> pools_;
    };

} // namespace cp

#endif //CONN_POOL_POOL_MANAGER_H