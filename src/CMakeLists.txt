file(GLOB_RECURSE app_sources
    ${CMAKE_CURRENT_SOURCE_DIR}/*.c
    ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp
)

set(idf_bluedroid_api_dir
    $ENV{IDF_PATH}/components/bt/host/bluedroid/api/include/api
)
set(idf_bt_target_include_dir
    $ENV{IDF_PATH}/components/bt/include/${IDF_TARGET}/include
)

idf_component_register(
    SRCS ${app_sources}
    INCLUDE_DIRS
        .
        ../include
        ${idf_bluedroid_api_dir}
        ${idf_bt_target_include_dir}
    REQUIRES
        bt
)
