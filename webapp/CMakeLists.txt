# MicroOcpp webapp module (HTTP dashboard + REST; no EVSE simulation)
# Built as a static library and linked by examples/pc_web

cmake_minimum_required(VERSION 3.15)

get_filename_component(MO_WEBAPP_ROOT "${CMAKE_CURRENT_SOURCE_DIR}" ABSOLUTE)
get_filename_component(MO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)

find_program(NODE_EXECUTABLE node)
if(NODE_EXECUTABLE)
    add_custom_command(
        OUTPUT ${MO_WEBAPP_ROOT}/file_data.c
        COMMAND ${NODE_EXECUTABLE}
                ${MO_ROOT}/lib/mongoose/resources/html2c.js
                ${MO_WEBAPP_ROOT}/root/dashboard.html
                -o ${MO_WEBAPP_ROOT}/file_data.c
        DEPENDS
            ${MO_WEBAPP_ROOT}/root/dashboard.html
            ${MO_WEBAPP_ROOT}/root/dashboard.js
            ${MO_WEBAPP_ROOT}/root/uPlot.iife.min.js
            ${MO_WEBAPP_ROOT}/root/uPlot.min.css
            ${MO_ROOT}/lib/mongoose/resources/html2c.js
        COMMENT "Packing webapp/root/dashboard.html into file_data.c"
        VERBATIM
    )
else()
    message(WARNING "node not found: webapp/file_data.c will not be auto-regenerated")
endif()

add_library(MicroOcppWebApp STATIC
    api.cpp
    dashboard.cpp
    sim_log.cpp
    connector_backend.cpp
    file_data.c
)

target_include_directories(MicroOcppWebApp PUBLIC
    ${MO_WEBAPP_ROOT}
    ${MO_ROOT}/lib/mongoose
)

target_compile_definitions(MicroOcppWebApp PUBLIC
    MO_NETLIB=MO_NETLIB_MONGOOSE
)

target_link_libraries(MicroOcppWebApp PUBLIC
    MicroOcpp
    MicroOcppMongoose
)
