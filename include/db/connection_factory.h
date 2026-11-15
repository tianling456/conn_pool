//
// Created by aobo on 2026/10/30.
//

#ifndef CONN_POOL_DB_CONNECTION_FACTORY_H
#define CONN_POOL_DB_CONNECTION_FACTORY_H
#include "config/yaml_config.h"
#include "db/db_connection.h"
#include <functional>
#include <memory>
#include <string>

namespace cp {

    class IConnectionFactory {
    public:
        virtual ~IConnectionFactory() = default;
        virtual std::unique_ptr<IDbConnection> create(const PoolConfig& cfg) = 0;
    };

    using FactoryCreator = std::function<std::unique_ptr<IConnectionFactory>()>;

    // ---- 注册表 API ----
    void registerConnectionFactory(const std::string& driver, FactoryCreator creator);
    std::unique_ptr<IConnectionFactory> makeConnectionFactory(const std::string& driver);

    // ---- 自动注册辅助宏（用 __COUNTER__ 保证名字唯一） ----
#define CP_REGISTER_DRIVER_IMPL(DRIVER_NAME, FACTORY_FN, UNIQUE_ID)          \
    namespace {                                                              \
        struct DriverRegistrar_##UNIQUE_ID {                                 \
            DriverRegistrar_##UNIQUE_ID() {                                  \
                ::cp::registerConnectionFactory(DRIVER_NAME, FACTORY_FN);    \
            }                                                                \
        } g_driverRegistrar_##UNIQUE_ID;                                     \
    }

#define CP_REGISTER_DRIVER_EXPAND(DRIVER_NAME, FACTORY_FN, UNIQUE_ID) \
    CP_REGISTER_DRIVER_IMPL(DRIVER_NAME, FACTORY_FN, UNIQUE_ID)

#define CP_REGISTER_DRIVER(DRIVER_NAME, FACTORY_FN) \
    CP_REGISTER_DRIVER_EXPAND(DRIVER_NAME, FACTORY_FN, __COUNTER__)

} // namespace cp

#endif //CONN_POOL_DB_CONNECTION_FACTORY_H
