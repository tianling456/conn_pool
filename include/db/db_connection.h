//
// Created by aobo on 2026/10/30.
//

#ifndef UNTITLED_DB_CONNECTION_H
#define UNTITLED_DB_CONNECTION_H

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

namespace cp {

    enum class IsolationLevel {
        DEFAULT, READ_UNCOMMITTED, READ_COMMITTED, REPEATABLE_READ, SERIALIZABLE
    };

    using ParamValue  = std::variant<std::monostate, std::int64_t, std::uint64_t,
                                     double, std::string>;
    using ColumnValue = std::optional<std::string>;
    using ResultRow   = std::unordered_map<std::string, ColumnValue>;
    using ResultSet   = std::vector<ResultRow>;

    class IDbConnection {
    public:
        virtual ~IDbConnection() = default;

        virtual bool ping() = 0;
        virtual ResultSet query(const std::string& sql,
                                const std::vector<ParamValue>& params) = 0;
        virtual std::uint64_t update(const std::string& sql,
                                     const std::vector<ParamValue>& params) = 0;
        virtual bool exec(const std::string& sql,
                          const std::vector<ParamValue>& params) = 0;

        virtual void begin()                = 0;
        virtual void commit()               = 0;
        virtual void rollback()             = 0;
        virtual void setAutoCommit(bool on) = 0;
        virtual bool getAutoCommit()        = 0;

        virtual void           setIsolationLevel(IsolationLevel level) = 0;
        virtual IsolationLevel getIsolationLevel()                     = 0;
        virtual void           setReadOnly(bool ro)                    = 0;
        virtual bool           getReadOnly()                           = 0;

        virtual void savepoint(const std::string& name)           = 0;
        virtual void rollbackToSavepoint(const std::string& name) = 0;
        virtual void releaseSavepoint(const std::string& name)    = 0;

        virtual std::uint64_t connectionId() const        = 0;
        virtual void          killConnection(std::uint64_t id) = 0;
    };

} // namespace cp

#endif //UNTITLED_DB_CONNECTION_H
