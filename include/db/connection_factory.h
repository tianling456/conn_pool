//
// Created by aobo on 2026/10/30.
//

#ifndef CONN_POOL_DB_CONNECTION_FACTORY_H
#define CONN_POOL_DB_CONNECTION_FACTORY_H
#include "config/yaml_config.h"
#include "db/db_connection.h"
#include <memory>

namespace cp {

    class IConnectionFactory {
    public:
        virtual ~IConnectionFactory() = default;
        virtual std::unique_ptr<IDbConnection> create(const PoolConfig& cfg) = 0;
    };

    std::unique_ptr<IConnectionFactory> makeConnectionFactory(const std::string& driver);

} // namespace cp

#endif //CONN_POOL_DB_CONNECTION_FACTORY_H
