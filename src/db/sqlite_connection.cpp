//
// Created by aobo on 2026/11/17.
//

#ifdef CP_HAS_SQLITE
#include "db/sqlite_connection.h"
#include <sqlite3.h>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <string>

namespace cp {
namespace {

struct StmtDel { void operator()(sqlite3_stmt* s) const { if (s) sqlite3_finalize(s); } };
using StmtPtr = std::unique_ptr<sqlite3_stmt, StmtDel>;

[[noreturn]] void throwSqlite(sqlite3* db, int rc, const char* what) {
    std::string msg = db ? sqlite3_errmsg(db) : sqlite3_errstr(rc);
    throw std::runtime_error(std::string(what) + ": " + msg);
}

void checkRc(sqlite3* db, int rc, const char* what) {
    if (rc != SQLITE_OK && rc != SQLITE_DONE && rc != SQLITE_ROW)
        throwSqlite(db, rc, what);
}

void bindParams(sqlite3* db, sqlite3_stmt* stmt,
                const std::vector<ParamValue>& params) {
    for (size_t i = 0; i < params.size(); ++i) {
        int idx = static_cast<int>(i + 1);
        int rc = SQLITE_OK;
        if (std::holds_alternative<std::monostate>(params[i]))
            rc = sqlite3_bind_null(stmt, idx);
        else if (auto p = std::get_if<std::int64_t>(&params[i]))
            rc = sqlite3_bind_int64(stmt, idx, static_cast<sqlite3_int64>(*p));
        else if (auto p = std::get_if<std::uint64_t>(&params[i]))
            rc = sqlite3_bind_int64(stmt, idx, static_cast<sqlite3_int64>(*p));
        else if (auto p = std::get_if<double>(&params[i]))
            rc = sqlite3_bind_double(stmt, idx, *p);
        else if (auto p = std::get_if<std::string>(&params[i]))
            rc = sqlite3_bind_text(stmt, idx, p->c_str(),
                                   static_cast<int>(p->size()), SQLITE_TRANSIENT);
        if (rc != SQLITE_OK) throwSqlite(db, rc, "sqlite3_bind");
    }
}

std::string sanitizeIdent(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s)
        if (std::isalnum(static_cast<unsigned char>(c)) || c == '_') out += c;
    return out;
}

std::string resolvePath(const PoolConfig& cfg) {
    if (cfg.database.empty())
        throw std::runtime_error("sqlite: 'database' must be a file path");
    // :memory: 在连接池模式下会失效（每连接独立库），显式拒绝
    if (cfg.database == ":memory:" || cfg.database.rfind("file::memory:", 0) == 0)
        throw std::runtime_error(
            "sqlite: ':memory:' cannot be used with connection pool "
            "(each connection would be an isolated in-memory DB); "
            "use a file path instead");
    return cfg.database;
}

} // namespace

struct SqliteConnection::Impl {
    sqlite3*      db = nullptr;
    bool          inTx = false;
    bool          readOnly = false;
    std::uint64_t id = 0;
    ~Impl() { if (db) sqlite3_close_v2(db); }
};

SqliteConnection::SqliteConnection(const PoolConfig& cfg)
    : impl_(std::make_unique<Impl>()) {

    std::string path = resolvePath(cfg);

    // FULLMUTEX = 串行化模式，同一连接多线程安全
    int flags = SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX;
    int rc = sqlite3_open_v2(path.c_str(), &impl_->db, flags, nullptr);
    if (rc != SQLITE_OK) {
        std::string err = impl_->db ? sqlite3_errmsg(impl_->db) : "unknown";
        if (impl_->db) { sqlite3_close_v2(impl_->db); impl_->db = nullptr; }
        throw std::runtime_error("sqlite open: " + err);
    }

    // 写冲突时内部重试（默认 0 = 立即报 SQLITE_BUSY）
    int busyMs = static_cast<int>(
        cfg.connectionTimeoutMs > 0 ? cfg.connectionTimeoutMs : 5000);
    sqlite3_busy_timeout(impl_->db, busyMs);

    // WAL 模式：一写多读并发，显著提升并发读性能
    // 对非 WAL 生效的文件执行一次即可（幂等）
    sqlite3_exec(impl_->db, "PRAGMA journal_mode=WAL",      nullptr, nullptr, nullptr);
    sqlite3_exec(impl_->db, "PRAGMA synchronous=NORMAL",    nullptr, nullptr, nullptr);
    sqlite3_exec(impl_->db, "PRAGMA foreign_keys=ON",       nullptr, nullptr, nullptr);

    impl_->id = reinterpret_cast<std::uint64_t>(impl_->db);
}

SqliteConnection::~SqliteConnection() = default;

// ==================== ping ====================
bool SqliteConnection::ping() {
    if (!impl_->db) return false;
    sqlite3_stmt* raw = nullptr;
    if (sqlite3_prepare_v2(impl_->db, "SELECT 1", -1, &raw, nullptr) != SQLITE_OK)
        return false;
    StmtPtr stmt(raw);
    int rc = sqlite3_step(stmt.get());
    return rc == SQLITE_ROW || rc == SQLITE_DONE;
}

// ==================== query ====================
ResultSet SqliteConnection::query(const std::string& sql,
                                  const std::vector<ParamValue>& params) {
    sqlite3_stmt* raw = nullptr;
    int rc = sqlite3_prepare_v2(impl_->db, sql.c_str(),
                                static_cast<int>(sql.size()), &raw, nullptr);
    if (rc != SQLITE_OK) throwSqlite(impl_->db, rc, "sqlite prepare");
    StmtPtr stmt(raw);

    bindParams(impl_->db, stmt.get(), params);

    ResultSet rows;
    int ncols = sqlite3_column_count(stmt.get());
    while ((rc = sqlite3_step(stmt.get())) == SQLITE_ROW) {
        ResultRow row;
        for (int c = 0; c < ncols; ++c) {
            const char* name = sqlite3_column_name(stmt.get(), c);
            if (sqlite3_column_type(stmt.get(), c) == SQLITE_NULL) {
                row[name] = std::nullopt;
            } else {
                const unsigned char* txt = sqlite3_column_text(stmt.get(), c);
                int n = sqlite3_column_bytes(stmt.get(), c);
                row[name] = std::string(reinterpret_cast<const char*>(txt),
                                        static_cast<size_t>(n));
            }
        }
        rows.push_back(std::move(row));
    }
    if (rc != SQLITE_DONE) throwSqlite(impl_->db, rc, "sqlite step(query)");
    return rows;
}

// ==================== update ====================
std::uint64_t SqliteConnection::update(const std::string& sql,
                                       const std::vector<ParamValue>& params) {
    sqlite3_stmt* raw = nullptr;
    int rc = sqlite3_prepare_v2(impl_->db, sql.c_str(),
                                static_cast<int>(sql.size()), &raw, nullptr);
    if (rc != SQLITE_OK) throwSqlite(impl_->db, rc, "sqlite prepare");
    StmtPtr stmt(raw);

    bindParams(impl_->db, stmt.get(), params);

    rc = sqlite3_step(stmt.get());
    if (rc != SQLITE_DONE && rc != SQLITE_ROW)
        throwSqlite(impl_->db, rc, "sqlite step(update)");

    return static_cast<std::uint64_t>(sqlite3_changes(impl_->db));
}

// ==================== exec ====================
bool SqliteConnection::exec(const std::string& sql,
                            const std::vector<ParamValue>& params) {
    if (params.empty()) {
        char* errMsg = nullptr;
        int rc = sqlite3_exec(impl_->db, sql.c_str(), nullptr, nullptr, &errMsg);
        if (rc != SQLITE_OK) {
            std::string e = errMsg ? errMsg : "unknown";
            if (errMsg) sqlite3_free(errMsg);
            throw std::runtime_error("sqlite exec: " + e);
        }
        return true;
    }
    update(sql, params);
    return true;
}

// ==================== 事务 ====================
void SqliteConnection::begin() {
    if (impl_->inTx) return;
    // IMMEDIATE：一开始就取得写锁，避免读写事务升级时 SQLITE_BUSY
    int rc = sqlite3_exec(impl_->db, "BEGIN IMMEDIATE", nullptr, nullptr, nullptr);
    checkRc(impl_->db, rc, "sqlite BEGIN");
    impl_->inTx = true;
}

void SqliteConnection::commit() {
    if (!impl_->inTx) return;
    int rc = sqlite3_exec(impl_->db, "COMMIT", nullptr, nullptr, nullptr);
    checkRc(impl_->db, rc, "sqlite COMMIT");
    impl_->inTx = false;
}

void SqliteConnection::rollback() {
    if (!impl_->inTx) return;
    sqlite3_exec(impl_->db, "ROLLBACK", nullptr, nullptr, nullptr);
    impl_->inTx = false;
}

void SqliteConnection::setAutoCommit(bool on) {
    if (on && impl_->inTx)           commit();
    else if (!on && !impl_->inTx)    begin();
}
bool SqliteConnection::getAutoCommit() {
    return !impl_->inTx;
}

// ==================== 隔离级别 / 只读 ====================
void SqliteConnection::setIsolationLevel(IsolationLevel) {
    // SQLite 实际只有 Serializable 一种级别（写锁全局独占）
    // 其他级别忽略，不报错
}
IsolationLevel SqliteConnection::getIsolationLevel() {
    return IsolationLevel::SERIALIZABLE;
}

void SqliteConnection::setReadOnly(bool ro) {
    impl_->readOnly = ro;
    sqlite3_exec(impl_->db, ro ? "PRAGMA query_only=ON" : "PRAGMA query_only=OFF",
                 nullptr, nullptr, nullptr);
}
bool SqliteConnection::getReadOnly() {
    return impl_->readOnly;
}

// ==================== Savepoint ====================
void SqliteConnection::savepoint(const std::string& name) {
    std::string sql = "SAVEPOINT " + sanitizeIdent(name);
    int rc = sqlite3_exec(impl_->db, sql.c_str(), nullptr, nullptr, nullptr);
    checkRc(impl_->db, rc, "sqlite SAVEPOINT");
}
void SqliteConnection::rollbackToSavepoint(const std::string& name) {
    std::string sql = "ROLLBACK TO SAVEPOINT " + sanitizeIdent(name);
    int rc = sqlite3_exec(impl_->db, sql.c_str(), nullptr, nullptr, nullptr);
    checkRc(impl_->db, rc, "sqlite ROLLBACK TO SAVEPOINT");
}
void SqliteConnection::releaseSavepoint(const std::string& name) {
    std::string sql = "RELEASE SAVEPOINT " + sanitizeIdent(name);
    sqlite3_exec(impl_->db, sql.c_str(), nullptr, nullptr, nullptr);
}

// ==================== 连接标识 ====================
std::uint64_t SqliteConnection::connectionId() const {
    return impl_->id;
}
void SqliteConnection::killConnection(std::uint64_t) {
    // 嵌入式数据库无连接管理概念，忽略
}

// ==================== 工厂 ====================
class SqliteConnectionFactory : public IConnectionFactory {
public:
    std::unique_ptr<IDbConnection> create(const PoolConfig& cfg) override {
        return std::make_unique<SqliteConnection>(cfg);
    }
};

// std::unique_ptr<IConnectionFactory> createSqliteConnectionFactory() {
//     return std::make_unique<SqliteConnectionFactory>();
// }

std::unique_ptr<IConnectionFactory> makeSqliteConnectionFactory() {
    return std::make_unique<SqliteConnectionFactory>();
}

// CP_REGISTER_DRIVER("sqlite",  createSqliteFactory)
// CP_REGISTER_DRIVER("sqlite3", createSqliteFactory)

} // namespace cp

#endif//CP_HAS_SQLITE