add_library(stgr_common STATIC
    paths.cpp
    log.cpp
)
target_include_directories(stgr_common PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(stgr_common PUBLIC ${STGR_DEFINES})
