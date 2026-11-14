//
// Created by aobo on 2026/11/8.
//

#ifndef CONN_POOL_CONNECTION_BATCH_H
#define CONN_POOL_CONNECTION_BATCH_H


#include "db/db_connection.h"
#include <memory>
#include <vector>

namespace cp {
    class ConnectionPool;

    class ConnectionBatch {
    public:
        explicit ConnectionBatch(std::shared_ptr<ConnectionPool> pool)
            : pool_(std::move(pool)) {}
        ~ConnectionBatch();

        ConnectionBatch(const ConnectionBatch&)            = delete;
        ConnectionBatch& operator=(const ConnectionBatch&) = delete;

        void add(std::shared_ptr<IDbConnection> conn) {
            if (conn) conns_.push_back(std::move(conn));
        }
        size_t size() const { return conns_.size(); }
        void clear();

        template <typename Fn>
        auto transact(Fn&& fn) -> decltype(fn(std::declval<IDbConnection&>())) {
            if (conns_.empty())
                conns_.push_back(pool_->getConnection());
            auto* c = conns_[0].get();
            c->begin();
            try {
                auto r = fn(*c);
                c->commit();
                return r;
            } catch (...) {
                try { c->rollback(); } catch (...) {}
                throw;
            }
        }

    private:
        std::shared_ptr<ConnectionPool> pool_;
        std::vector<std::shared_ptr<IDbConnection>> conns_;
    };

} // namespace cp

#endif //CONN_POOL_CONNECTION_BATCH_H
