//
// Created by aobo on 2026/11/11.
//

#ifndef CONN_POOL_TRANSACTION_CONTEXT_H
#define CONN_POOL_TRANSACTION_CONTEXT_H
#include "tx/transaction.h"

namespace cp {
    class TransactionContext {
    public:
        static std::shared_ptr<TransactionStatus> get();
        static void set(std::shared_ptr<TransactionStatus> s);
        static void clear();
        class Scoped {
        public:
            explicit Scoped(std::shared_ptr<TransactionStatus> s);
            ~Scoped();
        private:
            std::shared_ptr<TransactionStatus> saved_;
        };
    };
} // namespace cp

#endif //CONN_POOL_TRANSACTION_CONTEXT_H
