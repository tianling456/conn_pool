//
// Created by aobo on 2026/11/11.
//

#ifndef CONN_POOL_ROUTING_POLICY_H
#define CONN_POOL_ROUTING_POLICY_H

#include "tx/transaction.h"
#include <string>

namespace cp {
    class IRoutingPolicy {
    public:
        virtual ~IRoutingPolicy() = default;
        virtual std::string selectPool(const TransactionDefinition& def) = 0;
    };
    class ReadWriteRouting : public IRoutingPolicy {
    public:
        ReadWriteRouting(std::string w, std::string r) : w_(std::move(w)), r_(std::move(r)) {}
        std::string selectPool(const TransactionDefinition& def) override {
            return def.readOnly ? r_ : w_;
        }
    private:
        std::string w_, r_;
    };
} // namespace cp


#endif //CONN_POOL_ROUTING_POLICY_H
