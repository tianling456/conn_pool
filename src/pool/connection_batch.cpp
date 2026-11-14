//
// Created by aobo on 2026/11/8.
//
#include "pool/connection_batch.h"
#include "pool/connection_pool.h"

namespace cp {

    ConnectionBatch::~ConnectionBatch() { clear(); }

    void ConnectionBatch::clear() {
        if (pool_ && !conns_.empty()) pool_->releaseMany(conns_);
        else conns_.clear();
    }

} // namespace cp