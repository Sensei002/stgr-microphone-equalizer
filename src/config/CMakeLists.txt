add_library(stgr_config STATIC
    schema.cpp
)
target_include_directories(stgr_config PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(stgr_config PUBLIC ${STGR_DEFINES})
target_link_libraries(stgr_config PUBLIC stgr_common)