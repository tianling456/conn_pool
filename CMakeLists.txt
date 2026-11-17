cmake_minimum_required(VERSION 3.16)
project(conn_pool LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -pthread")


# 新增：查找 Oracle Instant Client
# 注意：Oracle 没有 CMake config，手动指定路径或使用环境变量
set(ORACLE_HOME "$ENV{ORACLE_HOME}" CACHE PATH "Oracle Instant Client home")
if(NOT ORACLE_HOME)
    message(FATAL_ERROR "请设置 ORACLE_HOME 环境变量")
endif()
set(ORACLE_INCLUDE_DIR "${ORACLE_HOME}/sdk/include")
set(ORACLE_LIB_DIR "${ORACLE_HOME}")


find_library(OCIEI_LIB NAMES clntsh PATHS ${ORACLE_LIB_DIR} REQUIRED)
find_library(OCIEI_SHARED NAMES clntsh.so.23.1 PATHS ${ORACLE_LIB_DIR})

file(GLOB_RECURSE SRC src/*.cpp)
list(REMOVE_ITEM SRC ${CMAKE_SOURCE_DIR}/src/main.cpp)

find_package(yaml-cpp REQUIRED)
find_package(PkgConfig REQUIRED)
find_package(pugixml REQUIRED)

find_package(SQLite3 REQUIRED)

pkg_check_modules(MYSQL REQUIRED mysqlclient)
pkg_check_modules(LIBPQ   REQUIRED libpq)

add_library(connpool ${SRC}
        src/db/oracle_connection.cpp
        src/db/sqlite_connection.cpp)

target_include_directories(connpool PUBLIC include ${MYSQL_INCLUDE_DIRS} ${LIBPQ_INCLUDE_DIRS} ${ORACLE_INCLUDE_DIR})
target_link_libraries(connpool PUBLIC yaml-cpp pugixml::pugixml ${MYSQL_LIBRARIES} ${LIBPQ_LIBRARIES} ${OCIEI_LIB} pthread)

target_link_libraries(connpool PUBLIC SQLite::SQLite3)

add_executable(conn_pool src/main.cpp
        include/db/oracle_connection.h
        include/db/sqlite_connection.h)
if(APPLE)
    target_link_libraries(conn_pool PRIVATE -Wl,-force_load connpool)
elseif(WIN32)
    target_link_libraries(conn_pool PRIVATE -WHOLEARCHIVE:connpool)
else()
    target_link_libraries(conn_pool PRIVATE
            -Wl,--whole-archive connpool -Wl,--no-whole-archive)
endif()
