#ifndef CONN_POOL_RETRY_H
#define CONN_POOL_RETRY_H



#include "config/yaml_config.h"
#include <chrono>
#include <stdexcept>
#include <thread>

namespace cp {

    class RetryTemplate {
    public:
        explicit RetryTemplate(const RetryConfig& cfg) : cfg_(cfg) {}

        template <typename Fn>
        auto execute(Fn&& fn) const -> decltype(fn()) {
            using namespace std::chrono;
            std::int64_t delay = cfg_.backoffMs < 0 ? 0 : cfg_.backoffMs;
            for (int attempt = 1; attempt <= cfg_.maxAttempts; ++attempt) {
                try { return fn(); }
                catch (...) {
                    if (attempt == cfg_.maxAttempts) throw;
                    if (delay > 0) std::this_thread::sleep_for(milliseconds(delay));
                    delay = static_cast<std::int64_t>(delay * cfg_.multiplier);
                    if (delay > cfg_.maxBackoffMs) delay = cfg_.maxBackoffMs;
                }
            }
            throw std::runtime_error("retry exhausted");
        }

    private:
        RetryConfig cfg_;
    };

} // namespace cp

#endif //CONN_POOL_RETRY_H