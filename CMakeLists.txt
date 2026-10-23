cmake_minimum_required(VERSION 3.21)
project(courier_shifts CXX)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(DownloadUserver)

set(USERVER_FEATURE_POSTGRESQL ON CACHE BOOL "" FORCE)
set(USERVER_FEATURE_REDIS ON CACHE BOOL "" FORCE)
set(USERVER_FEATURE_GRPC OFF CACHE BOOL "" FORCE)
set(USERVER_FEATURE_MONGODB OFF CACHE BOOL "" FORCE)
set(USERVER_FEATURE_CLICKHOUSE OFF CACHE BOOL "" FORCE)
set(USERVER_FEATURE_KAFKA OFF CACHE BOOL "" FORCE)
set(USERVER_FEATURE_RABBITMQ OFF CACHE BOOL "" FORCE)
set(USERVER_FEATURE_MYSQL OFF CACHE BOOL "" FORCE)
set(USERVER_FEATURE_ROCKS OFF CACHE BOOL "" FORCE)
set(USERVER_FEATURE_YDB OFF CACHE BOOL "" FORCE)
set(USERVER_FEATURE_EASY OFF CACHE BOOL "" FORCE)
set(USERVER_FEATURE_OTLP OFF CACHE BOOL "" FORCE)
set(USERVER_FEATURE_SQLITE OFF CACHE BOOL "" FORCE)
set(USERVER_FEATURE_ODBC OFF CACHE BOOL "" FORCE)
set(USERVER_FEATURE_S3API OFF CACHE BOOL "" FORCE)

find_package(userver COMPONENTS core postgresql redis QUIET)
if(NOT userver_FOUND)
    download_userver(TRY_DIR third_party/userver VERSION 3.2)
endif()

userver_setup_environment()

add_library(${PROJECT_NAME}_objs OBJECT
    src/earnings.cpp
    src/handlers.cpp
)
target_include_directories(${PROJECT_NAME}_objs PUBLIC src)
target_link_libraries(${PROJECT_NAME}_objs PUBLIC userver::core userver::postgresql userver::redis)

add_executable(${PROJECT_NAME} src/main.cpp)
target_link_libraries(${PROJECT_NAME} PRIVATE ${PROJECT_NAME}_objs)

add_executable(${PROJECT_NAME}_unittest src/earnings_test.cpp)
target_link_libraries(${PROJECT_NAME}_unittest PRIVATE ${PROJECT_NAME}_objs userver::utest)
add_google_tests(${PROJECT_NAME}_unittest)

userver_testsuite_add_simple()
