cmake_minimum_required(VERSION 3.16)
project(connpool LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ===== 必需依赖 =====
find_package(yaml-cpp REQUIRED)
find_package(pugixml REQUIRED)
find_package(PkgConfig REQUIRED)
# 多线程：编译 + 链接都正确
find_package(Threads REQUIRED)

file(GLOB_RECURSE SRC src/*.cpp)
list(REMOVE_ITEM SRC ${CMAKE_SOURCE_DIR}/src/main.cpp)

add_library(connpool ${SRC})
target_include_directories(connpool PUBLIC include)
target_link_libraries(connpool PUBLIC yaml-cpp pugixml::pugixml pthread)

set(CP_DRIVERS "")
set(CP_DRIVERS_MISSING "")

# ===== 可选：MySQL =====
pkg_check_modules(MYSQLCLIENT QUIET mysqlclient)
if(MYSQLCLIENT_FOUND)
    target_compile_definitions(connpool PRIVATE CP_HAS_MYSQL=1)
    target_include_directories(connpool PRIVATE ${MYSQLCLIENT_INCLUDE_DIRS})
    target_link_libraries(connpool PUBLIC ${MYSQLCLIENT_LIBRARIES})
    message(STATUS "  [driver] mysql     = ENABLED")
    set(CP_DRIVERS "${CP_DRIVERS} mysql")
else()
    message(STATUS "  [driver] mysql     = DISABLED (libmysqlclient-dev not installed)")
    set(CP_DRIVERS_MISSING "${CP_DRIVERS_MISSING} mysql")
endif()

# ===== 可选：PostgreSQL =====
pkg_check_modules(LIBPQ QUIET libpq)
if(LIBPQ_FOUND)
    target_compile_definitions(connpool PRIVATE CP_HAS_POSTGRES=1)
    target_include_directories(connpool PRIVATE ${LIBPQ_INCLUDE_DIRS})
    target_link_libraries(connpool PUBLIC ${LIBPQ_LIBRARIES})
    message(STATUS "  [driver] postgresql = ENABLED")
    set(CP_DRIVERS "${CP_DRIVERS} postgresql")
else()
    message(STATUS "  [driver] postgresql = DISABLED (libpq-dev not installed)")
    set(CP_DRIVERS_MISSING "${CP_DRIVERS_MISSING} postgresql")
endif()

# ===== 可选：SQLite =====
find_package(SQLite3 QUIET)
if(SQLite3_FOUND)
    target_compile_definitions(connpool PRIVATE CP_HAS_SQLITE=1)
    target_link_libraries(connpool PUBLIC SQLite::SQLite3)
    message(STATUS "  [driver] sqlite     = ENABLED")
    set(CP_DRIVERS "${CP_DRIVERS} sqlite")
else()
    message(STATUS "  [driver] sqlite     = DISABLED (libsqlite3-dev not installed)")
    set(CP_DRIVERS_MISSING "${CP_DRIVERS_MISSING} sqlite")
endif()

# ===== 可选：Oracle =====
set(ORACLE_HOME "$ENV{ORACLE_HOME}" CACHE PATH "Oracle Instant Client home")
if(ORACLE_HOME AND EXISTS "${ORACLE_HOME}/sdk/include/oci.h")
    find_library(OCIEI_LIB NAMES clntsh PATHS "${ORACLE_HOME}" NO_DEFAULT_PATH QUIET)
    if(OCIEI_LIB)
        target_compile_definitions(connpool PRIVATE CP_HAS_ORACLE=1)
        target_include_directories(connpool PRIVATE "${ORACLE_HOME}/sdk/include")
        target_link_libraries(connpool PUBLIC "${OCIEI_LIB}")
        message(STATUS "  [driver] oracle     = ENABLED")
        set(CP_DRIVERS "${CP_DRIVERS} oracle")
    else()
        message(STATUS "  [driver] oracle     = DISABLED (libclntsh.so not found in $ORACLE_HOME)")
        set(CP_DRIVERS_MISSING "${CP_DRIVERS_MISSING} oracle")
    endif()
else()
    message(STATUS "  [driver] oracle     = DISABLED (ORACLE_HOME unset or oci.h missing)")
    set(CP_DRIVERS_MISSING "${CP_DRIVERS_MISSING} oracle")
endif()

message(STATUS "== Drivers enabled:${CP_DRIVERS}")
message(STATUS "== Drivers missing:${CP_DRIVERS_MISSING}")

# 传给源码一份列表，方便运行时打印
string(STRIP "${CP_DRIVERS}" CP_DRIVERS_STR)
target_compile_definitions(connpool PRIVATE CP_DRIVERS_STR="${CP_DRIVERS_STR}")

# 编译选项：按 target、按编译器、按构建类型精确控制
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(connpool PRIVATE
            -Wall
            -Wextra
            -Wno-unused-parameter
            $<$<CONFIG:Release>:-O2>
            $<$<CONFIG:Debug>:-O0 -g>
    )
endif()

add_executable(conn_pool src/main.cpp)
target_link_libraries(conn_pool PRIVATE connpool)