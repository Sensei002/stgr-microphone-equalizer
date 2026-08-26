add_library(stgr_devices STATIC
    device_manager.cpp
)
target_include_directories(stgr_devices PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(stgr_devices PUBLIC ${STGR_DEFINES})
target_link_libraries(stgr_devices PUBLIC stgr_common)
target_link_libraries(stgr_devices PRIVATE ole32 comsuppw uuid)