//
// Created by aobo on 2026/11/17.
//
#include "db/oracle_connection.h"
#include "db/connection_factory.h"
#include <oci.h>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace cp {
namespace {

// ==================== RAII：OCI 句柄 ====================
template <ub4 HTYPE>
struct OciHandle {
    dvoid* h = nullptr;
    OciHandle() = default;
    explicit OciHandle(dvoid* p) : h(p) {}
    OciHandle(const OciHandle&) = delete;
    OciHandle& operator=(const OciHandle&) = delete;
    OciHandle(OciHandle&& o) noexcept : h(o.h) { o.h = nullptr; }
    OciHandle& operator=(OciHandle&& o) noexcept {
        if (this != &o) { reset(); h = o.h; o.h = nullptr; }
        return *this;
    }
    ~OciHandle() { reset(); }
    void reset() {
        if (h) { OCIHandleFree(h, HTYPE); h = nullptr; }
    }
    dvoid** addr() { return &h; }
    dvoid*  get()  const { return h; }
    explicit operator bool() const { return h != nullptr; }
};
using EnvHandle   = OciHandle<OCI_HTYPE_ENV>;
using ErrHandle   = OciHandle<OCI_HTYPE_ERROR>;
using SrvHandle   = OciHandle<OCI_HTYPE_SERVER>;
using SvcHandle   = OciHandle<OCI_HTYPE_SVCCTX>;
using AuthHandle  = OciHandle<OCI_HTYPE_SESSION>;
using StmtHandle  = OciHandle<OCI_HTYPE_STMT>;

// ==================== 错误处理 ====================
[[noreturn]] void throwOci(OCIError* err, sword status, const char* what) {
    char buf[1024] = {0};
    sb4 code = 0;
    if (err) OCIErrorGet(err, 1, nullptr, &code, (OraText*)buf, sizeof(buf), OCI_HTYPE_ERROR);
    char full[1200];
    std::snprintf(full, sizeof(full), "%s: ORA-%05d: %s", what, code, buf);
    throw std::runtime_error(full);
}

void checkOci(OCIError* err, sword status, const char* what) {
    if (status == OCI_SUCCESS || status == OCI_SUCCESS_WITH_INFO) return;
    throwOci(err, status, what);
}

// ==================== 占位符转换 ? → :1 :2 ====================
// Oracle 支持位置绑定，也支持命名绑定。这里用位置绑定 OCIBindByPos，
// 把 XML 里的 "?" 统一替换成 ":1" ":2"...（跳过字符串/注释中的 ?）
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
            out += ':';
            out += std::to_string(++idx);
            ++i; continue;
        }
        out += c;
        ++i;
    }
    return out;
}

// ==================== 参数存储：生命周期要覆盖 OCIStmtExecute ====================
struct OracleParam {
    // 实际存储：用字符串统一转换，绑定为 SQLT_STR，Oracle 会按列类型隐式转换
    std::string    str;
    sb2            ind = 0;        // 0=非 NULL, -1=NULL
    ub2            len = 0;
};

class ParamArray {
public:
    explicit ParamArray(const std::vector<ParamValue>& params)
        : storage_(params.size()) {
        for (size_t i = 0; i < params.size(); ++i) {
            auto& s = storage_[i];
            if (std::holds_alternative<std::monostate>(params[i])) {
                s.ind = -1;
                s.len = 0;
            } else if (auto p = std::get_if<std::int64_t>(&params[i])) {
                s.str = std::to_string(*p);
                s.len = (ub2)s.str.size();
            } else if (auto p = std::get_if<std::uint64_t>(&params[i])) {
                s.str = std::to_string(*p);
                s.len = (ub2)s.str.size();
            } else if (auto p = std::get_if<double>(&params[i])) {
                char buf[64];
                std::snprintf(buf, sizeof(buf), "%.17g", *p);
                s.str = buf;
                s.len = (ub2)s.str.size();
            } else if (auto p = std::get_if<std::string>(&params[i])) {
                s.str = *p;
                s.len = (ub2)s.str.size();
            }
        }
    }
    OracleParam& operator[](size_t i) { return storage_[i]; }
    size_t size() const { return storage_.size(); }

private:
    std::vector<OracleParam> storage_;
};

// 把 OCI 字符串绑定到每个 ":N" 占位符
void bindAll(OCIStmt* stmt, OCIError* err, ParamArray& pa) {
    for (size_t i = 0; i < pa.size(); ++i) {
        OCIBind* bind = nullptr;
        sword s = OCIBindByPos(stmt, &bind, err,
                               (ub4)(i + 1),
                               (dvoid*)pa[i].str.data(),
                               (sb4)pa[i].str.size() + 1,
                               SQLT_STR,
                               (dvoid*)&pa[i].ind,
                               &pa[i].len,
                               nullptr, 0, nullptr, OCI_DEFAULT);
        checkOci(err, s, "OCIBindByPos");
    }
}

// 执行 + 把 result set 转成 ResultSet
ResultSet fetchResult(OCIStmt* stmt, OCIError* err, bool wantRows) {
    ResultSet rows;
    if (!wantRows) return rows;

    // 获取列数
    ub4 colCount = 0;
    checkOci(err, OCIAttrGet(stmt, OCI_HTYPE_STMT, &colCount, nullptr,
                             OCI_ATTR_PARAM_COUNT, err), "OCIAttrGet(colCount)");
    if (colCount == 0) return rows;

    // 每列用一个 4KB 缓冲（不够时 OCI 会报 ORA-01406，生产可改为 define piecewise）
    constexpr ub4 kBufSize = 4096;
    struct ColBuffer {
        std::vector<char> data = std::vector<char>(kBufSize);
        ub2  len = 0;
        sb2  ind = 0;
        std::string name;
    };
    std::vector<ColBuffer> cols(colCount);

    for (ub4 c = 0; c < colCount; ++c) {
        OCIError* colErr = err;
        // 获取列名
        OCIDefine* def = nullptr;
        checkOci(err, OCIDefineByPos(stmt, &def, err,
                                     c + 1,
                                     cols[c].data.data(),
                                     kBufSize,
                                     SQLT_STR,
                                     &cols[c].ind,
                                     &cols[c].len,
                                     nullptr,
                                     OCI_DEFAULT),
                 "OCIDefineByPos");

        OCIParam* param = nullptr;
        checkOci(err, OCIParamGet(stmt, OCI_HTYPE_STMT, err, (dvoid**)&param,
                                  c + 1), "OCIParamGet");
        OraText* name = nullptr;
        ub4 nameLen = 0;
        checkOci(err, OCIAttrGet(param, OCI_DTYPE_PARAM, &name, &nameLen,
                                 OCI_ATTR_NAME, err), "OCIAttrGet(name)");
        cols[c].name.assign((char*)name, nameLen);
        OCIDescriptorFree(param, OCI_DTYPE_PARAM);
    }

    // fetch
    while (true) {
        sword s = OCIStmtFetch2(stmt, err, 1, OCI_FETCH_NEXT, 0, OCI_DEFAULT);
        if (s == OCI_NO_DATA) break;
        checkOci(err, s, "OCIStmtFetch2");
        ResultRow row;
        for (ub4 c = 0; c < colCount; ++c) {
            if (cols[c].ind == -1) row[cols[c].name] = std::nullopt;
            else row[cols[c].name] = std::string(cols[c].data.data(),
                                                 (size_t)cols[c].len);
        }
        rows.push_back(std::move(row));
    }
    return rows;
}

// 把 dsn 拼成 OCI 能识别的 (DESCRIPTION=...)
std::string buildConnectString(const PoolConfig& cfg) {
    if (!cfg.host.empty() && cfg.port != 0 && !cfg.database.empty())
        return "(DESCRIPTION=(ADDRESS=(PROTOCOL=TCP)(HOST=" + cfg.host +
               ")(PORT=" + std::to_string(cfg.port) + "))" +
               "(CONNECT_DATA=(SERVICE_NAME=" + cfg.database + ")))";
    return cfg.database;   // 回退：直接用 tnsnames.ora 里的别名
}

std::string sanitizeIdent(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s)
        if (std::isalnum((unsigned char)c) || c == '_') out += c;
    return out;
}

} // namespace

// ==================== Impl ====================
struct OracleConnection::Impl {
    EnvHandle  env;
    ErrHandle  err;
    SrvHandle  srv;
    SvcHandle  svc;
    AuthHandle auth;

    bool inTx = false;
    bool readOnly = false;
    IsolationLevel isoLevel = IsolationLevel::DEFAULT;
};

OracleConnection::OracleConnection(const PoolConfig& cfg)
    : impl_(std::make_unique<Impl>()) {

    // 1. 环境句柄
    sword s = OCIEnvCreate(impl_->env.addr(), OCI_THREADED | OCI_OBJECT,
                           nullptr, nullptr, nullptr, nullptr, 0, nullptr);
    checkOci(nullptr, s, "OCIEnvCreate");
    // 错误句柄
    s = OCIHandleAlloc(impl_->env.get(), impl_->err.addr(), OCI_HTYPE_ERROR, 0, nullptr);
    checkOci(impl_->err.get(), s, "OCIHandleAlloc(ERROR)");

    // 2. 服务器句柄 + attach
    s = OCIHandleAlloc(impl_->env.get(), impl_->srv.addr(), OCI_HTYPE_SERVER, 0, nullptr);
    checkOci(impl_->err.get(), s, "OCIHandleAlloc(SERVER)");

    std::string dsn = buildConnectString(cfg);
    s = OCIServerAttach(impl_->srv.get(), impl_->err.get(),
                        (OraText*)dsn.c_str(), (sb4)dsn.size(), OCI_DEFAULT);
    checkOci(impl_->err.get(), s, "OCIServerAttach");

    // 3. 服务上下文句柄 + 绑定 server
    s = OCIHandleAlloc(impl_->env.get(), impl_->svc.addr(), OCI_HTYPE_SVCCTX, 0, nullptr);
    checkOci(impl_->err.get(), s, "OCIHandleAlloc(SVCCTX)");
    s = OCIAttrSet(impl_->svc.get(), OCI_HTYPE_SVCCTX,
                   impl_->srv.get(), 0, OCI_ATTR_SERVER, impl_->err.get());
    checkOci(impl_->err.get(), s, "OCIAttrSet(SERVER)");

    // 4. 会话句柄 + 登录
    s = OCIHandleAlloc(impl_->env.get(), impl_->auth.addr(), OCI_HTYPE_SESSION, 0, nullptr);
    checkOci(impl_->err.get(), s, "OCIHandleAlloc(SESSION)");
    s = OCIAttrSet(impl_->auth.get(), OCI_HTYPE_SESSION,
                   (dvoid*)cfg.username.c_str(), (ub4)cfg.username.size(),
                   OCI_ATTR_USERNAME, impl_->err.get());
    checkOci(impl_->err.get(), s, "OCIAttrSet(USERNAME)");
    s = OCIAttrSet(impl_->auth.get(), OCI_HTYPE_SESSION,
                   (dvoid*)cfg.password.c_str(), (ub4)cfg.password.size(),
                   OCI_ATTR_PASSWORD, impl_->err.get());
    checkOci(impl_->err.get(), s, "OCIAttrSet(PASSWORD)");

    s = OCISessionBegin(impl_->svc.get(), impl_->err.get(),
                        impl_->auth.get(), OCI_CRED_RDBMS, OCI_DEFAULT);
    checkOci(impl_->err.get(), s, "OCISessionBegin");

    s = OCIAttrSet(impl_->svc.get(), OCI_HTYPE_SVCCTX,
                   impl_->auth.get(), 0, OCI_ATTR_SESSION, impl_->err.get());
    checkOci(impl_->err.get(), s, "OCIAttrSet(SESSION)");
}

OracleConnection::~OracleConnection() {
    if (!impl_) return;
    if (impl_->svc.get() && impl_->auth.get())
        OCISessionEnd(impl_->svc.get(), impl_->err.get(), impl_->auth.get(), OCI_DEFAULT);
    if (impl_->srv.get())
        OCIServerDetach(impl_->srv.get(), impl_->err.get(), OCI_DEFAULT);
}

// ==================== ping ====================
bool OracleConnection::ping() {
    if (!impl_->svc.get()) return false;
    StmtHandle stmt;
    if (OCIHandleAlloc(impl_->env.get(), stmt.addr(), OCI_HTYPE_STMT, 0, nullptr) != OCI_SUCCESS)
        return false;
    const char* sql = "SELECT 1 FROM DUAL";
    sword s = OCIStmtPrepare(stmt.get(), impl_->err.get(),
                             (OraText*)sql, (ub4)std::strlen(sql),
                             OCI_NTV_SYNTAX, OCI_DEFAULT);
    if (s != OCI_SUCCESS) return false;
    s = OCIStmtExecute(impl_->svc.get(), stmt.get(), impl_->err.get(),
                       0, 0, nullptr, nullptr, OCI_DEFAULT);
    return s == OCI_SUCCESS || s == OCI_SUCCESS_WITH_INFO;
}

// ==================== query ====================
ResultSet OracleConnection::query(const std::string& sql,
                                  const std::vector<ParamValue>& params) {
    std::string ociSql = convertPlaceholders(sql);
    StmtHandle stmt;
    checkOci(impl_->err.get(),
             OCIHandleAlloc(impl_->env.get(), stmt.addr(), OCI_HTYPE_STMT, 0, nullptr),
             "OCIHandleAlloc(STMT)");

    checkOci(impl_->err.get(),
             OCIStmtPrepare(stmt.get(), impl_->err.get(),
                            (OraText*)ociSql.c_str(), (ub4)ociSql.size(),
                            OCI_NTV_SYNTAX, OCI_DEFAULT),
             "OCIStmtPrepare");

    ParamArray pa(params);
    bindAll(stmt.get(), impl_->err.get(), pa);

    sword s = OCIStmtExecute(impl_->svc.get(), stmt.get(), impl_->err.get(),
                             0, 0, nullptr, nullptr, OCI_DEFAULT);
    checkOci(impl_->err.get(), s, "OCIStmtExecute(query)");

    return fetchResult(stmt.get(), impl_->err.get(), /*wantRows=*/true);
}

// ==================== update ====================
std::uint64_t OracleConnection::update(const std::string& sql,
                                       const std::vector<ParamValue>& params) {
    std::string ociSql = convertPlaceholders(sql);
    StmtHandle stmt;
    checkOci(impl_->err.get(),
             OCIHandleAlloc(impl_->env.get(), stmt.addr(), OCI_HTYPE_STMT, 0, nullptr),
             "OCIHandleAlloc(STMT)");
    checkOci(impl_->err.get(),
             OCIStmtPrepare(stmt.get(), impl_->err.get(),
                            (OraText*)ociSql.c_str(), (ub4)ociSql.size(),
                            OCI_NTV_SYNTAX, OCI_DEFAULT),
             "OCIStmtPrepare");

    ParamArray pa(params);
    bindAll(stmt.get(), impl_->err.get(), pa);

    sword s = OCIStmtExecute(impl_->svc.get(), stmt.get(), impl_->err.get(),
                             1, 0, nullptr, nullptr, OCI_DEFAULT);
    checkOci(impl_->err.get(), s, "OCIStmtExecute(update)");

    ub4 affected = 0;
    OCIAttrGet(stmt.get(), OCI_HTYPE_STMT, &affected, nullptr,
               OCI_ATTR_ROW_COUNT, impl_->err.get());
    return (std::uint64_t)affected;
}

// ==================== exec ====================
bool OracleConnection::exec(const std::string& sql,
                            const std::vector<ParamValue>& params) {
    std::string ociSql = convertPlaceholders(sql);
    StmtHandle stmt;
    checkOci(impl_->err.get(),
             OCIHandleAlloc(impl_->env.get(), stmt.addr(), OCI_HTYPE_STMT, 0, nullptr),
             "OCIHandleAlloc(STMT)");
    checkOci(impl_->err.get(),
             OCIStmtPrepare(stmt.get(), impl_->err.get(),
                            (OraText*)ociSql.c_str(), (ub4)ociSql.size(),
                            OCI_NTV_SYNTAX, OCI_DEFAULT),
             "OCIStmtPrepare");

    ParamArray pa(params);
    bindAll(stmt.get(), impl_->err.get(), pa);

    // iters = 1 表示执行一次（DDL 也可以）
    sword s = OCIStmtExecute(impl_->svc.get(), stmt.get(), impl_->err.get(),
                             1, 0, nullptr, nullptr, OCI_DEFAULT);
    checkOci(impl_->err.get(), s, "OCIStmtExecute(exec)");
    return true;
}

// ==================== 事务 ====================
void OracleConnection::begin() {
    // Oracle 隐式事务：不需要 BEGIN，第一条 DML 就开启事务
    // 这里只做标记，供 setAutoCommit/getAutoCommit 查询
    impl_->inTx = true;
}

void OracleConnection::commit() {
    if (!impl_->inTx) return;
    sword s = OCITransCommit(impl_->svc.get(), impl_->err.get(), OCI_DEFAULT);
    checkOci(impl_->err.get(), s, "OCITransCommit");
    impl_->inTx = false;
}

void OracleConnection::rollback() {
    if (!impl_->inTx) return;
    sword s = OCITransRollback(impl_->svc.get(), impl_->err.get(), OCI_DEFAULT);
    checkOci(impl_->err.get(), s, "OCITransRollback");
    impl_->inTx = false;
}

void OracleConnection::setAutoCommit(bool on) {
    // Oracle OCI 默认就是"自动提交"由应用控制：所有 DML 都需要显式 COMMIT
    // 如果要模拟 autoCommit=true，在每次 DML 后立即 commit
    // 但连接池模式下通常保持 autoCommit=false，由事务管理器控制
    if (on && impl_->inTx) commit();
}

bool OracleConnection::getAutoCommit() {
    return !impl_->inTx;
}

// ==================== 隔离级别 ====================
void OracleConnection::setIsolationLevel(IsolationLevel level) {
    // Oracle 只支持 READ_COMMITTED 和 SERIALIZABLE（READ_UNCOMMITTED/REPEATABLE_READ 不支持）
    // 用 SET TRANSACTION 语句，必须在一事务中的第一条语句前执行
    const char* stmt = nullptr;
    IsolationLevel mapped = level;
    switch (level) {
        case IsolationLevel::SERIALIZABLE:
            stmt = "SET TRANSACTION ISOLATION LEVEL SERIALIZABLE";
            break;
        case IsolationLevel::READ_COMMITTED:
        case IsolationLevel::DEFAULT:
            stmt = "SET TRANSACTION ISOLATION LEVEL READ COMMITTED";
            break;
        case IsolationLevel::READ_UNCOMMITTED:
        case IsolationLevel::REPEATABLE_READ:
            // Oracle 不支持，回退到 READ_COMMITTED
            stmt = "SET TRANSACTION ISOLATION LEVEL READ COMMITTED";
            mapped = IsolationLevel::READ_COMMITTED;
            break;
    }
    exec(stmt, {});
    impl_->isoLevel = mapped;
}

IsolationLevel OracleConnection::getIsolationLevel() {
    return impl_->isoLevel;
}

void OracleConnection::setReadOnly(bool ro) {
    const char* stmt = ro ? "SET TRANSACTION READ ONLY" : "SET TRANSACTION READ WRITE";
    exec(stmt, {});
    impl_->readOnly = ro;
}

bool OracleConnection::getReadOnly() {
    return impl_->readOnly;
}

// ==================== Savepoint ====================
void OracleConnection::savepoint(const std::string& name) {
    std::string sql = "SAVEPOINT " + sanitizeIdent(name);
    exec(sql, {});
}

void OracleConnection::rollbackToSavepoint(const std::string& name) {
    std::string sql = "ROLLBACK TO SAVEPOINT " + sanitizeIdent(name);
    exec(sql, {});
}

void OracleConnection::releaseSavepoint(const std::string& name) {
    // Oracle 没有 RELEASE SAVEPOINT 语法，savepoint 随事务结束自动消失
    // 保持接口一致，不抛错
    (void)name;
}

// ==================== 连接标识 ====================
std::uint64_t OracleConnection::connectionId() const {
    // 没有直接的 "connection id" 概念，可以用 sid+serial#
    // 这里只做占位，如果需要 kill 用 killConnection 走 ALTER SYSTEM
    return 0;
}

void OracleConnection::killConnection(std::uint64_t id) {
    // 用另一条连接执行 ALTER SYSTEM KILL SESSION 'sid,serial#'
    // 这里不做，留给外部管理线程
    (void)id;
}

// ==================== 工厂 ====================
class OracleConnectionFactory : public IConnectionFactory {
public:
    std::unique_ptr<IDbConnection> create(const PoolConfig& cfg) override {
        return std::make_unique<OracleConnection>(cfg);
    }
};

static std::unique_ptr<IConnectionFactory> createOracleFactory() {
    return std::make_unique<OracleConnectionFactory>();
}

CP_REGISTER_DRIVER("oracle", createOracleFactory)
CP_REGISTER_DRIVER("oracledb", createOracleFactory)

} // namespace cp