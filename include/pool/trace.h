//
// Created by aobo on 2026/11/8.
//

#ifndef CONN_POOL_TRACE_H
#define CONN_POOL_TRACE_H

#include <chrono>
#include <cstdio>
#include <string>

namespace cp {

    class ITraceSink {
    public:
        virtual ~ITraceSink() = default;

        virtual void onBorrow(const std::string& pool,
                              std::chrono::nanoseconds waitTime) = 0;
        virtual void onRelease(const std::string& pool,
                               std::chrono::nanoseconds waitTime,
                               std::chrono::nanoseconds holdTime) = 0;
        virtual void onTransactionEnd(const std::string& /*name*/, int /*txId*/,
                                      std::chrono::nanoseconds /*elapsed*/,
                                      bool /*committed*/) {}
    };

    class LogSlowTraceSink : public ITraceSink {
    public:
        explicit LogSlowTraceSink(std::chrono::milliseconds threshold)
            : threshold_(threshold) {}

        void onBorrow(const std::string& pool,
                      std::chrono::nanoseconds waitTime) override {
            if (waitTime >= threshold_) {
                std::fprintf(stderr, "[TRACE] borrow pool=%s wait=%lldus\n",
                    pool.c_str(),
                    (long long)std::chrono::duration_cast<
                        std::chrono::microseconds>(waitTime).count());
            }
        }
        void onRelease(const std::string& pool,
                       std::chrono::nanoseconds waitTime,
                       std::chrono::nanoseconds holdTime) override {
            if (holdTime >= threshold_) {
                std::fprintf(stderr, "[TRACE] release pool=%s wait=%lldus hold=%lldus\n",
                    pool.c_str(),
                    (long long)std::chrono::duration_cast<
                        std::chrono::microseconds>(waitTime).count(),
                    (long long)std::chrono::duration_cast<
                        std::chrono::microseconds>(holdTime).count());
            }
        }

    private:
        std::chrono::milliseconds threshold_;
    };

} // namespace cp

#endif //CONN_POOL_TRACE_H
