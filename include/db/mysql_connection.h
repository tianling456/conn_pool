//
// Created by aobo on 2026/10/30.
//

#ifndef CONN_POOL_MYSQL_CONNECTION_H
#define CONN_POOL_MYSQL_CONNECTION_H

#include "db/db_connection.h"
#include "config/yaml_config.h"
#include <memory>

#include "connection_factory.h"

namespace cp {

    class MysqlConnection : public IDbConnection {
    public:
        explicit MysqlConnection(const PoolConfig& cfg);
        ~MysqlConnection() override;

        bool ping() override;
        ResultSet query(const std::string& sql, const std::vector<ParamValue>& params) override;
        std::uint64_t update(const std::string& sql, const std::vector<ParamValue>& params) override;
        bool exec(const std::string& sql, const std::vector<ParamValue>& params) override;

        void begin() override;
        void commit() override;
        void rollback() override;
        void setAutoCommit(bool on) override;
        bool getAutoCommit() override;

        void           setIsolationLevel(IsolationLevel level) override;
        IsolationLevel getIsolationLevel() override;
        void           setReadOnly(bool ro) override;
        bool           getReadOnly() override;

        void savepoint(const std::string& name) override;
        void rollbackToSavepoint(const std::string& name) override;
        void releaseSavepoint(const std::string& name) override;

        std::uint64_t connectionId() const override;
        void          killConnection(std::uint64_t id) override;

    private:
        struct Impl;
        std::unique_ptr<Impl> impl_;
    };
    // ========= 新增：对外只暴露 MySQL 工厂入口 =========
    std::unique_ptr<IConnectionFactory> makeMysqlConnectionFactory();
} // namespace cp

#endif //CONN_POOL_MYSQL_CONNECTION_H
