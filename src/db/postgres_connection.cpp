//
// Created by aobo on 2026/11/15.
//

#ifdef CP_HAS_POSTGRES
#include "db/postgres_connection.h"
#include <cctype>
#include <cstdio>
#include <cstring>
#include <libpq-fe.h>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
namespace cp {
namespace {

struct PgResultDel { void operator()(PGresult* r) const { if (r) PQclear(r); } };
using PgResultPtr = std::unique_ptr<PGresult, PgResultDel>;

// 把 XML 生成的 "?" 占位符替换成 PG 的 "$1,$2..."
// 跳过单引号字符串、双引号标识符、行注释、块注释
std::string convertPlaceholders(const std::string& sql) {
    std::string out;
    out.reserve(sql.size() + 16);
    int  idx = 0;
    bool inSingle = false, inDouble = false;
    bool inLine = false, inBlock = false;
    size_t i = 0;
    while (i < sql.size()) {
        char c = sql[i];

        if (inLine) {
            out += c;
            if (c == '\n') inLine = false;
            ++i; continue;
        }
        if (inBlock) {
            out += c;
            if (c == '*' && i + 1 < sql.size() && sql[i + 1] == '/') {
                out += '/'; i += 2; inBlock = false; continue;
            }
            ++i; continue;
        }
        if (inSingle) {
            out += c;
            if (c == '\'') {
                if (i + 1 < sql.size() && sql[i + 1] == '\'') {
                    out += '\''; i += 2; continue;
                }
                inSingle = false;
            }
            ++i; continue;
        }
        if (inDouble) {
            out += c;
            if (c == '"') {
                if (i + 1 < sql.size() && sql[i + 1] == '"') {
                    out += '"'; i += 2; continue;
                }
                inDouble = false;
            }
            ++i; continue;
        }
        if (c == '-' && i + 1 < sql.size() && sql[i + 1] == '-') {
            out += "--"; i += 2; inLine = true; continue;
        }
        if (c == '/' && i + 1 < sql.size() && sql[i + 1] == '*') {
            out += "/*"; i += 2; inBlock = true; continue;
        }
        if (c == '\'') { out += c; inSingle = true; ++i; continue; }
        if (c == '"')  { out += c; inDouble = true; ++i; continue; }
        if (c == '?')  {
            out += '$';
            out += std::to_string(++idx);
            ++i; continue;
        }
        out += c;
        ++i;
    }
    return out;
}

void throwResult(const PGresult* r, const char* what) {
    std::string err = r ? PQresultErrorMessage(r) : "null result";
    throw std::runtime_error(std::string(what) + ": " + err);
}

void checkStatus(const PGresult* r, const char* what) {
    if (!r) throw std::runtime_error(std::string(what) + ": null result");
    auto st = PQresultStatus(r);
    if (st == PGRES_TUPLES_OK || st == PGRES_COMMAND_OK) return;
    throwResult(r, what);
}

std::string execScalar(PGconn* c, const char* sql) {
    PgResultPtr r(PQexec(c, sql));
    if (!r || PQresultStatus(r.get()) != PGRES_TUPLES_OK) return {};
    if (PQntuples(r.get()) == 0) return {};
    if (PQgetisnull(r.get(), 0, 0)) return {};
    return PQgetvalue(r.get(), 0, 0);
}

std::string sanitizeIdent(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s)
        if (std::isalnum((unsigned char)c) || c == '_') out += c;
    return out;
}

// 把 ParamValue 转成 C 字符串给 PQexecParams
struct ParamBuffer {
    std::vector<std::string>       storage;
    std::vector<const char*>       values;

    explicit ParamBuffer(const std::vector<ParamValue>& params)
        : storage(params.size()), values(params.size(), nullptr) {
        for (size_t i = 0; i < params.size(); ++i) {
            if (std::holds_alternative<std::monostate>(params[i])) {
                values[i] = nullptr;
            } else if (auto p = std::get_if<std::int64_t>(&params[i])) {
                storage[i] = std::to_string(*p);
                values[i]  = storage[i].c_str();
            } else if (auto p = std::get_if<std::uint64_t>(&params[i])) {
                storage[i] = std::to_string(*p);
                values[i]  = storage[i].c_str();
            } else if (auto p = std::get_if<double>(&params[i])) {
                char buf[64];
                std::snprintf(buf, sizeof(buf), "%.17g", *p);
                storage[i] = buf;
                values[i]  = storage[i].c_str();
            } else if (auto p = std::get_if<std::string>(&params[i])) {
                storage[i] = *p;
                values[i]  = storage[i].c_str();
            }
        }
    }
};

} // namespace

struct PostgresConnection::Impl {
    PGconn* h = nullptr;
    bool    inTx = false;
    bool    autoCommit = true;
    ~Impl() { if (h) PQfinish(h); }
};

PostgresConnection::PostgresConnection(const PoolConfig& cfg)
    : impl_(std::make_unique<Impl>()) {

    // 用 PQconnectdbParams 避免连接串转义问题
    std::string portStr    = std::to_string(cfg.port);
    std::string timeoutStr = std::to_string(
        std::max(1, static_cast<int>(cfg.connectionTimeoutMs / 1000)));

    const char* keys[] = { "host", "port", "user", "password", "dbname",
                           "connect_timeout", nullptr };
    const char* vals[] = { cfg.host.c_str(), portStr.c_str(),
                           cfg.username.c_str(), cfg.password.c_str(),
                           cfg.database.c_str(), timeoutStr.c_str(),
                           nullptr };

    impl_->h = PQconnectdbParams(keys, vals, 0);
    if (!impl_->h || PQstatus(impl_->h) != CONNECTION_OK) {
        std::string err = impl_->h ? PQerrorMessage(impl_->h) : "PQconnectdbParams failed";
        if (impl_->h) { PQfinish(impl_->h); impl_->h = nullptr; }
        throw std::runtime_error("pg connect: " + err);
    }
}

PostgresConnection::~PostgresConnection() = default;

bool PostgresConnection::ping() {
    if (!impl_->h) return false;
    if (PQstatus(impl_->h) != CONNECTION_OK) return false;
    PgResultPtr r(PQexec(impl_->h, "SELECT 1"));
    return r && PQresultStatus(r.get()) == PGRES_TUPLES_OK;
}

ResultSet PostgresConnection::query(const std::string& sql,
                                    const std::vector<ParamValue>& params) {
    std::string pgSql = convertPlaceholders(sql);
    ParamBuffer buf(params);

    PgResultPtr res(PQexecParams(impl_->h, pgSql.c_str(),
                                 static_cast<int>(params.size()),
                                 nullptr, buf.values.data(),
                                 nullptr, nullptr, /*text*/ 0));
    checkStatus(res.get(), "pg query");

    ResultSet rows;
    int nrows = PQntuples(res.get());
    int ncols = PQnfields(res.get());
    rows.reserve(static_cast<size_t>(nrows));
    for (int r = 0; r < nrows; ++r) {
        ResultRow row;
        for (int c = 0; c < ncols; ++c) {
            const char* name = PQfname(res.get(), c);
            if (PQgetisnull(res.get(), r, c))
                row[name] = std::nullopt;
            else
                row[name] = std::string(PQgetvalue(res.get(), r, c));
        }
        rows.push_back(std::move(row));
    }
    return rows;
}

std::uint64_t PostgresConnection::update(const std::string& sql,
                                         const std::vector<ParamValue>& params) {
    std::string pgSql = convertPlaceholders(sql);
    ParamBuffer buf(params);

    PgResultPtr res(PQexecParams(impl_->h, pgSql.c_str(),
                                 static_cast<int>(params.size()),
                                 nullptr, buf.values.data(),
                                 nullptr, nullptr, 0));
    checkStatus(res.get(), "pg update");

    const char* n = PQcmdTuples(res.get());
    if (n && *n) {
        try { return std::stoull(n); } catch (...) {}
    }
    return 0;
}

bool PostgresConnection::exec(const std::string& sql,
                              const std::vector<ParamValue>& params) {
    if (params.empty()) {
        PgResultPtr r(PQexec(impl_->h, sql.c_str()));
        checkStatus(r.get(), "pg exec");
        return true;
    }
    update(sql, params);
    return true;
}

void PostgresConnection::begin() {
    PgResultPtr r(PQexec(impl_->h, "BEGIN"));
    checkStatus(r.get(), "pg begin");
    impl_->inTx = true;
    impl_->autoCommit = false;
}
void PostgresConnection::commit() {
    PgResultPtr r(PQexec(impl_->h, "COMMIT"));
    checkStatus(r.get(), "pg commit");
    impl_->inTx = false;
    impl_->autoCommit = true;
}
void PostgresConnection::rollback() {
    PgResultPtr r(PQexec(impl_->h, "ROLLBACK"));
    checkStatus(r.get(), "pg rollback");
    impl_->inTx = false;
    impl_->autoCommit = true;
}
void PostgresConnection::setAutoCommit(bool on) {
    if (on && impl_->inTx)        commit();
    else if (!on && !impl_->inTx) begin();
    else                          impl_->autoCommit = on;
}
bool PostgresConnection::getAutoCommit() { return impl_->autoCommit; }

void PostgresConnection::setIsolationLevel(IsolationLevel level) {
    const char* lv = nullptr;
    switch (level) {
        case IsolationLevel::READ_UNCOMMITTED: lv = "READ UNCOMMITTED"; break;
        case IsolationLevel::READ_COMMITTED:   lv = "READ COMMITTED";   break;
        case IsolationLevel::REPEATABLE_READ:  lv = "REPEATABLE READ";  break;
        case IsolationLevel::SERIALIZABLE:     lv = "SERIALIZABLE";     break;
        default: return;
    }
    std::string sql = std::string("SET SESSION CHARACTERISTICS AS TRANSACTION ISOLATION LEVEL ") + lv;
    PgResultPtr r(PQexec(impl_->h, sql.c_str()));
    checkStatus(r.get(), "pg setIsolationLevel");
}

IsolationLevel PostgresConnection::getIsolationLevel() {
    std::string v = execScalar(impl_->h, "SHOW transaction_isolation");
    for (auto& c : v) c = static_cast<char>(std::toupper((unsigned char)c));
    if (v == "READ UNCOMMITTED") return IsolationLevel::READ_UNCOMMITTED;
    if (v == "READ COMMITTED")   return IsolationLevel::READ_COMMITTED;
    if (v == "REPEATABLE READ")  return IsolationLevel::REPEATABLE_READ;
    if (v == "SERIALIZABLE")     return IsolationLevel::SERIALIZABLE;
    return IsolationLevel::DEFAULT;
}

void PostgresConnection::setReadOnly(bool ro) {
    const char* sql = ro
        ? "SET SESSION CHARACTERISTICS AS TRANSACTION READ ONLY"
        : "SET SESSION CHARACTERISTICS AS TRANSACTION READ WRITE";
    PgResultPtr r(PQexec(impl_->h, sql));
    checkStatus(r.get(), "pg setReadOnly");
}
bool PostgresConnection::getReadOnly() {
    std::string v = execScalar(impl_->h, "SHOW transaction_read_only");
    for (auto& c : v) c = static_cast<char>(std::tolower((unsigned char)c));
    return v == "on";
}

void PostgresConnection::savepoint(const std::string& name) {
    std::string sql = "SAVEPOINT " + sanitizeIdent(name);
    PgResultPtr r(PQexec(impl_->h, sql.c_str()));
    checkStatus(r.get(), "pg savepoint");
}
void PostgresConnection::rollbackToSavepoint(const std::string& name) {
    std::string sql = "ROLLBACK TO SAVEPOINT " + sanitizeIdent(name);
    PgResultPtr r(PQexec(impl_->h, sql.c_str()));
    checkStatus(r.get(), "pg rollbackToSavepoint");
}
void PostgresConnection::releaseSavepoint(const std::string& name) {
    std::string sql = "RELEASE SAVEPOINT " + sanitizeIdent(name);
    PQexec(impl_->h, sql.c_str());   // 尽力而为，失败忽略
}

std::uint64_t PostgresConnection::connectionId() const {
    if (!impl_->h) return 0;
    return static_cast<std::uint64_t>(PQbackendPID(impl_->h));
}

void PostgresConnection::killConnection(std::uint64_t id) {
    // 用当前连接执行 pg_terminate_backend
    std::string sql = "SELECT pg_terminate_backend(" + std::to_string(id) + ")";
    PgResultPtr r(PQexec(impl_->h, sql.c_str()));
    checkStatus(r.get(), "pg killConnection");
}

class PostgresConnectionFactory : public IConnectionFactory {
public:
    std::unique_ptr<IDbConnection> create(const PoolConfig& cfg) override {
        return std::make_unique<PostgresConnection>(cfg);
    }
};

// std::unique_ptr<IConnectionFactory> createPostgresFactory() {
//     return std::make_unique<PostgresConnectionFactory>();
// }

std::unique_ptr<IConnectionFactory> makePostgresConnectionFactory() {
    return std::make_unique<PostgresConnectionFactory>();
}

// CP_REGISTER_DRIVER("postgresql", createPostgresFactory)
// CP_REGISTER_DRIVER("postgres",   createPostgresFactory)   // 别名
// CP_REGISTER_DRIVER("pg",         createPostgresFactory)
// CP_REGISTER_DRIVER("postgre",    createPostgresFactory)
} // namespace cp
#endif//CP_HAS_POSTGRES