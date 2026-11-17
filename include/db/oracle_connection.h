//
// Created by aobo on 2026/11/17.
//

#ifndef CONN_POOL_ORACLE_CONNECTION_H
#define CONN_POOL_ORACLE_CONNECTION_H

#include "config/yaml_config.h"
#include "db/db_connection.h"
#include <memory>

namespace cp {

    class OracleConnection : public IDbConnection {
    public:
        explicit OracleConnection(const PoolConfig& cfg);
        ~OracleConnection() override;

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

    std::unique_ptr<IConnectionFactory> makeOracleConnectionFactory();

} // namespace cp

#endif //CONN_POOL_ORACLE_CONNECTION_H
