//
// Created by aobo on 2026/11/11.
//
#include "tx/transaction_context.h"
#include <vector>

namespace cp {
    namespace { thread_local std::vector<std::shared_ptr<TransactionStatus>> g_stack; }

    std::shared_ptr<TransactionStatus> TransactionContext::get() {
        return g_stack.empty() ? nullptr : g_stack.back();
    }
    void TransactionContext::set(std::shared_ptr<TransactionStatus> s) { g_stack.push_back(std::move(s)); }
    void TransactionContext::clear() { if (!g_stack.empty()) g_stack.pop_back(); }
    TransactionContext::Scoped::Scoped(std::shared_ptr<TransactionStatus> s) : saved_(get()) {
        g_stack.clear();
        if (s) g_stack.push_back(std::move(s));
    }
    TransactionContext::Scoped::~Scoped() {
        g_stack.clear();
        if (saved_) g_stack.push_back(std::move(saved_));
    }
} // namespace cp