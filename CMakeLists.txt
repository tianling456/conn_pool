cmake_minimum_required(VERSION 3.16)
project(conn_pool LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -pthread")

find_package(yaml-cpp REQUIRED)
find_package(PkgConfig REQUIRED)
find_package(pugixml REQUIRED)
pkg_check_modules(MYSQL REQUIRED mysqlclient)

add_library(connpool
        src/config/yaml_config.cpp
        src/db/mysql_connection.cpp
        src/pool/connection_pool.cpp
        src/pool/connection_batch.cpp
        src/pool/pool_manager.cpp
        src/xml/xml_types.cpp
        src/xml/expr_evaluator.cpp
        src/xml/bound_sql_builder.cpp
        src/xml/mapper_parser.cpp
        src/xml/xml_mapper_registry.cpp
        src/result/type_handler.cpp
        src/interceptor/interceptor_factory.cpp
        src/interceptor/query_cache_interceptor.cpp
        src/sql/sql_executor.cpp
        src/tx/transaction_context.cpp
        src/tx/transaction_manager.cpp
        src/tx/transaction_watchdog.cpp
        src/tx/tx_sql_executor.cpp
        src/pool/connection_batch.cpp
)

target_include_directories(connpool PUBLIC include ${MYSQL_INCLUDE_DIRS})
target_link_libraries(connpool PUBLIC yaml-cpp pugixml::pugixml ${MYSQL_LIBRARIES} pthread)

add_executable(conn_pool src/main.cpp
        include/interceptor/interceptor_factory.h
        include/result/type_handler.h
        src/result/type_handler.cpp
        include/result/reflection_registry.h
        include/interceptor/query_cache_interceptor.h
        include/tx/tx_sql_executor.h
        include/tx/transaction_template.h
        include/pool/mpmc_queue.h
        include/pool/histogram.h
        include/pool/pool_stats.h
        include/pool/trace.h
        include/pool/connection_batch.h
        include/result/xml_result_mapper.h
        include/tx/transaction.h
        include/tx/transaction_context.h
        src/tx/transaction_context.cpp
        include/tx/routing_policy.h
        include/tx/transaction_watchdog.h
        src/tx/transaction_watchdog.cpp
        include/tx/transaction_manager.h
        src/tx/transaction_manager.cpp
        include/sql/sql_logger.h)
target_link_libraries(conn_pool PRIVATE connpool)