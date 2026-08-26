add_library(stgr_bridge STATIC
    dummy.cpp
    shm.h
)
target_include_directories(stgr_bridge PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(stgr_bridge PUBLIC ${STGR_DEFINES})
target_link_libraries(stgr_bridge PUBLIC stgr_common)