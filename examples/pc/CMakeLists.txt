# matth-x/MicroOcpp — PC (Linux/WSL) example
# MIT License

cmake_minimum_required(VERSION 3.15)

project(MicroOcppPcExample VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED True)

# Repo root: examples/pc -> ../..
set(MO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../..")

add_compile_definitions(
    MO_PLATFORM=MO_PLATFORM_UNIX
    MO_NUMCONNECTORS=3
    MO_TRAFFIC_OUT
    MO_DBG_LEVEL=MO_DL_INFO
    MO_CUSTOM_CONSOLE
    MO_CUSTOM_CONSOLE_MAXMSGSIZE=4096
    MO_FILENAME_PREFIX="./mo_store/"
    MO_ENABLE_V201=1
    MO_ENABLE_MBEDTLS=1
    MO_ENABLE_TIMESTAMP_MILLISECONDS=1
    MO_MG_USE_VERSION=MO_MG_V715
)

add_executable(mo_pc_example
    main.c
    ${MO_ROOT}/lib/mongoose/mongoose.c
)

target_include_directories(mo_pc_example PRIVATE
    ${MO_ROOT}/lib/mongoose
)

# Core library (uses lib/ArduinoJson via root CMakeLists.txt)
add_subdirectory(${MO_ROOT} ${CMAKE_CURRENT_BINARY_DIR}/microocpp)
target_link_libraries(mo_pc_example PRIVATE MicroOcpp)

# MbedTLS (MO_ENABLE_MBEDTLS=1)
option(ENABLE_TESTING  "Build mbed TLS tests."    OFF)
option(ENABLE_PROGRAMS "Build mbed TLS programs." OFF)
add_subdirectory(${MO_ROOT}/lib/mbedtls ${CMAKE_CURRENT_BINARY_DIR}/mbedtls)
target_link_libraries(MicroOcpp PUBLIC mbedtls mbedcrypto mbedx509)
target_link_libraries(mo_pc_example PRIVATE mbedtls mbedcrypto mbedx509)
target_compile_definitions(mo_pc_example PRIVATE MG_TLS=MG_TLS_MBED)

# WebSocket adapter
add_subdirectory(${MO_ROOT}/lib/MicroOcppMongoose ${CMAKE_CURRENT_BINARY_DIR}/microocpp_mongoose)
target_link_libraries(mo_pc_example PRIVATE MicroOcppMongoose)
