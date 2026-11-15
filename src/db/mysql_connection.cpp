//
// Created by aobo on 2026/10/30.
//
// 头文件
// 实现
#include "db/mysql_connection.h"
#include "db/connection_factory.h"
#include <mysql/mysql.h>
#include <cctype>
#include <cstring>
#include <memory>
#include <stdexcept>

namespace cp {
namespace {

struct StmtDel { void operator()(MYSQL_STMT* s) const { if (s) mysql_stmt_close(s); } };
struct ResDel  { void operator()(MYSQL_RES*  r) const { if (r) mysql_free_result(r); } };
using StmtPtr = std::unique_ptr<MYSQL_STMT, StmtDel>;
using ResPtr  = std::unique_ptr<MYSQL_RES,  ResDel>;

[[noreturn]] void throwStmt(MYSQL_STMT* s, const char* what) {
    throw std::runtime_error(std::string(what) + ": " + mysql_stmt_error(s));
}

class ParamBinder {
public:
    ParamBinder(MYSQL_STMT* stmt, const std::vector<ParamValue>& params) {
        binds_.resize(params.size());
        for (size_t i = 0; i < params.size(); ++i) fill(binds_[i], params[i]);
        if (!params.empty() && mysql_stmt_bind_param(stmt, binds_.data()) != 0)
            throwStmt(stmt, "bind_param");
    }
private:
    static void fill(MYSQL_BIND& b, const ParamValue& p) {
        std::memset(&b, 0, sizeof(b));
        if (std::holds_alternative<std::monostate>(p)) { b.buffer_type = MYSQL_TYPE_NULL; return; }
        if (auto v = std::get_if<std::int64_t>(&p)) {
            b.buffer_type = MYSQL_TYPE_LONGLONG; b.buffer = const_cast<std::int64_t*>(v);
        } else if (auto v = std::get_if<std::uint64_t>(&p)) {
            b.buffer_type = MYSQL_TYPE_LONGLONG; b.buffer = const_cast<std::uint64_t*>(v);
            b.is_unsigned = 1;
        } else if (auto v = std::get_if<double>(&p)) {
            b.buffer_type = MYSQL_TYPE_DOUBLE; b.buffer = const_cast<double*>(v);
        } else if (auto v = std::get_if<std::string>(&p)) {
            b.buffer_type = MYSQL_TYPE_STRING;
            b.buffer = const_cast<char*>(v->data());
            b.buffer_length = v->size();
        }
    }
    std::vector<MYSQL_BIND> binds_;
};

std::string execScalar(MYSQL* h, const char* sql) {
    if (mysql_query(h, sql) != 0) throw std::runtime_error(mysql_error(h));
    ResPtr r(mysql_store_result(h));
    if (!r) return {};
    MYSQL_ROW row = mysql_fetch_row(r.get());
    return (row && row[0]) ? std::string(row[0]) : std::string{};
}

} // namespace

struct MysqlConnection::Impl {
    MYSQL* h = nullptr;
    ~Impl() { if (h) mysql_close(h); }
};

MysqlConnection::MysqlConnection(const PoolConfig& cfg) : impl_(std::make_unique<Impl>()) {
    impl_->h = mysql_init(nullptr);
    if (!impl_->h) throw std::runtime_error("mysql_init failed");
    unsigned t = static_cast<unsigned>(cfg.connectionTimeoutMs / 1000);
    mysql_options(impl_->h, MYSQL_OPT_CONNECT_TIMEOUT, &t);
    if (!mysql_real_connect(impl_->h, cfg.host.c_str(), cfg.username.c_str(),
                            cfg.password.c_str(),
                            cfg.database.empty() ? nullptr : cfg.database.c_str(),
                            cfg.port, nullptr, 0)) {
        std::string err = mysql_error(impl_->h);
        mysql_close(impl_->h); impl_->h = nullptr;
        throw std::runtime_error("mysql connect failed: " + err);
    }
}
MysqlConnection::~MysqlConnection() = default;

bool MysqlConnection::ping() { return impl_->h && mysql_ping(impl_->h) == 0; }

ResultSet MysqlConnection::query(const std::string& sql,
                                 const std::vector<ParamValue>& params) {
    StmtPtr stmt(mysql_stmt_init(impl_->h));
    if (!stmt) throwStmt(stmt.get(), "stmt_init");
    if (mysql_stmt_prepare(stmt.get(), sql.data(), sql.size()) != 0)
        throwStmt(stmt.get(), "prepare");
    ParamBinder binder(stmt.get(), params);
    if (mysql_stmt_execute(stmt.get()) != 0) throwStmt(stmt.get(), "execute");

    ResPtr meta(mysql_stmt_result_metadata(stmt.get()));
    if (!meta) return {};

    unsigned n = mysql_num_fields(meta.get());
    MYSQL_FIELD* fields = mysql_fetch_fields(meta.get());
    std::vector<MYSQL_BIND>     out(n);
    std::vector<unsigned long>  lens(n);
    std::vector<char>           isNull(n, 0);
    std::vector<std::string>    bufs(n);

    for (unsigned i = 0; i < n; ++i) {
        bufs[i].resize(1024);
        std::memset(&out[i], 0, sizeof(MYSQL_BIND));
        out[i].buffer_type   = MYSQL_TYPE_STRING;
        out[i].buffer        = bufs[i].data();
        out[i].buffer_length = bufs[i].size();
        out[i].length        = &lens[i];
        out[i].is_null       = reinterpret_cast<decltype(out[i].is_null)>(&isNull[i]);
    }
    if (mysql_stmt_bind_result(stmt.get(), out.data()) != 0) throwStmt(stmt.get(), "bind_result");
    if (mysql_stmt_store_result(stmt.get()) != 0)           throwStmt(stmt.get(), "store_result");

    ResultSet rows;
    while (true) {
        int rc = mysql_stmt_fetch(stmt.get());
        if (rc == MYSQL_NO_DATA) break;
        if (rc == 1)            throwStmt(stmt.get(), "fetch");
        ResultRow row;
        for (unsigned i = 0; i < n; ++i) {
            if (isNull[i]) { row[fields[i].name] = std::nullopt; continue; }
            unsigned long len = lens[i];
            std::string val;
            if (len > bufs[i].size()) {
                val.resize(len);
                MYSQL_BIND& b = out[i];
                b.buffer        = val.data();
                b.buffer_length = len;
                if (mysql_stmt_fetch_column(stmt.get(), &b, i, 0) != 0)
                    throwStmt(stmt.get(), "fetch_column");
            } else val.assign(bufs[i].data(), len);
            row[fields[i].name] = std::move(val);
        }
        rows.push_back(std::move(row));
    }
    return rows;
}

std::uint64_t MysqlConnection::update(const std::string& sql,
                                      const std::vector<ParamValue>& params) {
    StmtPtr stmt(mysql_stmt_init(impl_->h));
    if (!stmt) throwStmt(stmt.get(), "stmt_init");
    if (mysql_stmt_prepare(stmt.get(), sql.data(), sql.size()) != 0)
        throwStmt(stmt.get(), "prepare");
    ParamBinder binder(stmt.get(), params);
    if (mysql_stmt_execute(stmt.get()) != 0) throwStmt(stmt.get(), "execute");
    return mysql_stmt_affected_rows(stmt.get());
}

bool MysqlConnection::exec(const std::string& sql,
                           const std::vector<ParamValue>& params) {
    if (params.empty()) {
        if (mysql_query(impl_->h, sql.c_str()) != 0)
            throw std::runtime_error(std::string("exec: ") + mysql_error(impl_->h));
        return true;
    }
    return update(sql, params) >= 0;
}

void MysqlConnection::begin() {
    if (mysql_autocommit(impl_->h, 0) != 0 ||
        mysql_query(impl_->h, "START TRANSACTION") != 0)
        throw std::runtime_error(std::string("begin: ") + mysql_error(impl_->h));
}
void MysqlConnection::commit() {
    if (mysql_commit(impl_->h) != 0)
        throw std::runtime_error(std::string("commit: ") + mysql_error(impl_->h));
    mysql_autocommit(impl_->h, 1);
}
void MysqlConnection::rollback() {
    if (mysql_rollback(impl_->h) != 0)
        throw std::runtime_error(std::string("rollback: ") + mysql_error(impl_->h));
    mysql_autocommit(impl_->h, 1);
}
void MysqlConnection::setAutoCommit(bool on) {
    if (mysql_autocommit(impl_->h, on ? 1 : 0) != 0)
        throw std::runtime_error("setAutoCommit: " + std::string(mysql_error(impl_->h)));
}
bool MysqlConnection::getAutoCommit() { return mysql_autocommit(impl_->h, 2) == 1; }

void MysqlConnection::setIsolationLevel(IsolationLevel level) {
    const char* sql = nullptr;
    switch (level) {
        case IsolationLevel::READ_UNCOMMITTED:
            sql = "SET SESSION TRANSACTION ISOLATION LEVEL READ UNCOMMITTED"; break;
        case IsolationLevel::READ_COMMITTED:
            sql = "SET SESSION TRANSACTION ISOLATION LEVEL READ COMMITTED";   break;
        case IsolationLevel::REPEATABLE_READ:
            sql = "SET SESSION TRANSACTION ISOLATION LEVEL REPEATABLE READ";  break;
        case IsolationLevel::SERIALIZABLE:
            sql = "SET SESSION TRANSACTION ISOLATION LEVEL SERIALIZABLE";     break;
        default: return;
    }
    if (mysql_query(impl_->h, sql) != 0)
        throw std::runtime_error("setIsolationLevel: " + std::string(mysql_error(impl_->h)));
}
IsolationLevel MysqlConnection::getIsolationLevel() {
    std::string v = execScalar(impl_->h, "SELECT @@session.transaction_isolation");
    for (auto& c : v) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    if (v.find("READ-UNCOMMITTED") != std::string::npos) return IsolationLevel::READ_UNCOMMITTED;
    if (v.find("READ-COMMITTED")   != std::string::npos) return IsolationLevel::READ_COMMITTED;
    if (v.find("REPEATABLE-READ")  != std::string::npos) return IsolationLevel::REPEATABLE_READ;
    if (v.find("SERIALIZABLE")     != std::string::npos) return IsolationLevel::SERIALIZABLE;
    return IsolationLevel::DEFAULT;
}
void MysqlConnection::setReadOnly(bool ro) {
    const char* sql = ro ? "SET SESSION TRANSACTION READ ONLY"
                         : "SET SESSION TRANSACTION READ WRITE";
    if (mysql_query(impl_->h, sql) != 0)
        throw std::runtime_error("setReadOnly: " + std::string(mysql_error(impl_->h)));
}
bool MysqlConnection::getReadOnly() {
    return execScalar(impl_->h, "SELECT @@session.transaction_read_only") == "1";
}

void MysqlConnection::savepoint(const std::string& name) {
    if (mysql_query(impl_->h, ("SAVEPOINT `" + name + "`").c_str()) != 0)
        throw std::runtime_error("savepoint: " + std::string(mysql_error(impl_->h)));
}
void MysqlConnection::rollbackToSavepoint(const std::string& name) {
    if (mysql_query(impl_->h, ("ROLLBACK TO SAVEPOINT `" + name + "`").c_str()) != 0)
        throw std::runtime_error("rollbackToSavepoint: " + std::string(mysql_error(impl_->h)));
}
void MysqlConnection::releaseSavepoint(const std::string& name) {
    mysql_query(impl_->h, ("RELEASE SAVEPOINT `" + name + "`").c_str());
}

std::uint64_t MysqlConnection::connectionId() const {
    return impl_->h ? mysql_thread_id(impl_->h) : 0;
}
void MysqlConnection::killConnection(std::uint64_t id) {
    std::string sql = "KILL CONNECTION " + std::to_string(id);
    if (mysql_query(impl_->h, sql.c_str()) != 0)
        throw std::runtime_error("kill: " + std::string(mysql_error(impl_->h)));
}

class MysqlConnectionFactory : public IConnectionFactory {
public:
    std::unique_ptr<IDbConnection> create(const PoolConfig& cfg) override {
        return std::make_unique<MysqlConnection>(cfg);
    }
};

    std::unique_ptr<IConnectionFactory> createMysqlFactory() {
        return std::make_unique<MysqlConnectionFactory>();
    }
    CP_REGISTER_DRIVER("mysql", createMysqlFactory)
} // namespace cp