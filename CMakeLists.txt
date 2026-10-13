cmake_minimum_required(VERSION 3.16)
project(distributedTaskSystemFinal LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(PostgreSQL REQUIRED)
find_package(Threads REQUIRED)

add_library(common STATIC src/common.cpp)
target_include_directories(common PUBLIC include)

add_library(store STATIC src/store.cpp)
target_include_directories(store PUBLIC include)
target_link_libraries(store PUBLIC PostgreSQL::PostgreSQL)

foreach(binary IN ITEMS scheduler api worker)
  add_executable(${binary} src/${binary}_main.cpp)
  target_link_libraries(${binary} PRIVATE common Threads::Threads)
endforeach()

target_link_libraries(scheduler PRIVATE store)
target_link_libraries(api PRIVATE store)

enable_testing()

add_executable(unit_tests tests/unit.cpp)
target_link_libraries(unit_tests PRIVATE common)

add_test(NAME unit_tests COMMAND unit_tests)

target_compile_options(unit_tests PRIVATE -UNDEBUG)