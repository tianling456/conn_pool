cmake_minimum_required(VERSION 3.16)
project(conn_pool LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -pthread")

file(GLOB_RECURSE SRC src/*.cpp)
list(REMOVE_ITEM SRC ${CMAKE_SOURCE_DIR}/src/main.cpp)

find_package(yaml-cpp REQUIRED)
find_package(PkgConfig REQUIRED)
find_package(pugixml REQUIRED)
pkg_check_modules(MYSQL REQUIRED mysqlclient)
pkg_check_modules(LIBPQ   REQUIRED libpq)

add_library(connpool ${SRC})

target_include_directories(connpool PUBLIC include ${MYSQL_INCLUDE_DIRS} ${LIBPQ_INCLUDE_DIRS})
target_link_libraries(connpool PUBLIC yaml-cpp pugixml::pugixml ${MYSQL_LIBRARIES} ${LIBPQ_LIBRARIES} pthread)

add_executable(conn_pool src/main.cpp)
if(APPLE)
    target_link_libraries(conn_pool PRIVATE -Wl,-force_load connpool)
elseif(WIN32)
    target_link_libraries(conn_pool PRIVATE -WHOLEARCHIVE:connpool)
else()
    target_link_libraries(conn_pool PRIVATE
            -Wl,--whole-archive connpool -Wl,--no-whole-archive)
endif()
