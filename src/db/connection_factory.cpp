//
// Created by aobo on 2026/11/15.
//
#include "db/connection_factory.h"
#include <stdexcept>
#include <vector>

#ifdef CP_HAS_MYSQL
#include "db/mysql_connection.h"
#endif
#ifdef CP_HAS_POSTGRES
#include "db/postgres_connection.h"
#endif
#ifdef CP_HAS_ORACLE
#include "db/oracle_connection.h"
#endif
#ifdef CP_HAS_SQLITE
#include "db/sqlite_connection.h"
#endif

// #include <stdexcept>
// #include <mutex>

namespace cp {
    // namespace {
        // std::unordered_map<std::string, FactoryCreator>& registry() {
        //     static std::unordered_map<std::string, FactoryCreator> m;
        //     return m;
        // }
        // std::mutex& registryMutex() {
        //     static std::mutex m;
        //     return m;
        // }
    // }

    // void registerConnectionFactory(const std::string& driver, FactoryCreator creator) {
    //     std::lock_guard<std::mutex> lk(registryMutex());
    //     registry()[driver] = std::move(creator);
    // }
    //
    // std::unique_ptr<IConnectionFactory> makeConnectionFactory(const std::string& driver) {
    //     std::lock_guard<std::mutex> lk(registryMutex());
    //     auto it = registry().find(driver);
    //     if (it == registry().end())
    //         throw std::runtime_error("unsupported driver: " + driver
    //                                  + " (did you forget to register it?)");
    //     return it->second();
    // }



    namespace {
        // 把 driver 名归一化（大小写 / 别名）
        std::string normalize(const std::string& d) {
            std::string s;
            s.reserve(d.size());
            for (char c : d) s += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            return s;
        }
    }

    bool isDriverAvailable(const std::string& driver) {
        auto d = normalize(driver);
#ifdef CP_HAS_MYSQL
        if (d == "mysql" || d == "mariadb") return true;
#endif
#ifdef CP_HAS_POSTGRES
        if (d == "postgresql" || d == "postgres" || d == "pg") return true;
#endif
#ifdef CP_HAS_ORACLE
        if (d == "oracle" || d == "oracledb") return true;
#endif
#ifdef CP_HAS_SQLITE
        if (d == "sqlite" || d == "sqlite3") return true;
#endif
        return false;
    }

    std::string listAvailableDrivers() {
        std::string out;
#ifdef CP_HAS_MYSQL
        out += "mysql ";
#endif
#ifdef CP_HAS_POSTGRES
        out += "postgresql ";
#endif
#ifdef CP_HAS_ORACLE
        out += "oracle ";
#endif
#ifdef CP_HAS_SQLITE
        out += "sqlite ";
#endif
        if (!out.empty()) out.pop_back();
        if (out.empty()) out = "(none)";
        return out;
    }

std::unique_ptr<IConnectionFactory> makeConnectionFactory(const std::string& driver) {
        auto d = normalize(driver);

#ifdef CP_HAS_MYSQL
        if (d == "mysql" || d == "mariadb")
            return makeMysqlConnectionFactory();
#endif
#ifdef CP_HAS_POSTGRES
        if (d == "postgresql" || d == "postgres" || d == "pg")
            return makePostgresConnectionFactory();
#endif
#ifdef CP_HAS_ORACLE
        if (d == "oracle" || d == "oracledb")
            return makeOracleConnectionFactory();
#endif
#ifdef CP_HAS_SQLITE
        if (d == "sqlite" || d == "sqlite3")
            return makeSqliteConnectionFactory();
#endif

        throw std::runtime_error(
            "unsupported driver: '" + driver + "' (available: " + listAvailableDrivers()
            + "). Rebuild with the corresponding -dev package installed.");
    }

} // namespace cp