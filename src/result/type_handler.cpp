#include "result/type_handler.h"
#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>

namespace cp {
    TypeHandlerRegistry& TypeHandlerRegistry::instance() {
        static TypeHandlerRegistry inst;
        return inst;
    }
    TypeHandlerRegistry::TypeHandlerRegistry() {
        registerHandler<std::int64_t>(
            [](auto& v) { return ParamValue(std::int64_t(v)); },
            [](const ColumnValue& c) { return conv::toInt64(c).value_or(0); });
        registerHandler<std::uint64_t>(
            [](auto& v) { return ParamValue(std::uint64_t(v)); },
            [](const ColumnValue& c) { return conv::toUInt64(c).value_or(0); });
        registerHandler<double>(
            [](auto& v) { return ParamValue(v); },
            [](const ColumnValue& c) { return conv::toDouble(c).value_or(0); });
        registerHandler<bool>(
            [](auto& v) { return ParamValue(std::int64_t(v ? 1 : 0)); },
            [](const ColumnValue& c) { return conv::toBool(c).value_or(false); });
        registerHandler<std::string>(
            [](auto& v) { return ParamValue(v); },
            [](const ColumnValue& c) { return conv::toString(c).value_or(""); });

        using TP = std::chrono::system_clock::time_point;
        registerHandler<TP>(
            [](const TP& v) {
                auto t = std::chrono::system_clock::to_time_t(v);
                std::tm tm{};
            #if defined(_WIN32)
                localtime_s(&tm, &t);
            #else
                localtime_r(&t, &tm);
            #endif
                char buf[32];
                std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm);
                return ParamValue(std::string(buf));
            },
            [](const ColumnValue& c) -> TP {
                if (!c) return {};
                std::tm tm{};
                std::istringstream ss(*c);
                ss >> std::get_time(&tm, "%Y-%m-%d %H:%M:%S");
                if (ss.fail()) return {};
                return std::chrono::system_clock::from_time_t(std::mktime(&tm));
            });
    }
} // namespace cp