//
// Created by aobo on 2026/11/15.
//

#ifndef CONN_POOL_POSTGRES_CONNECTION_H
#define CONN_POOL_POSTGRES_CONNECTION_H

#include "config/yaml_config.h"
#include "db/db_connection.h"
#include <memory>

#include "connection_factory.h"

namespace cp {

    class PostgresConnection : public IDbConnection {
    public:
        explicit PostgresConnection(const PoolConfig& cfg);
        ~PostgresConnection() override;

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

    // 工厂入口，供 makeConnectionFactory 调用
    std::unique_ptr<IConnectionFactory> makePostgresConnectionFactory();

} // namespace cp

#endif //CONN_POOL_POSTGRES_CONNECTION_H
