add_library(stgr_plugins STATIC
    vst2_host.cpp
    vst3_host.cpp
    plugin_loader.cpp
)
target_include_directories(stgr_plugins PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(stgr_plugins PUBLIC ${STGR_DEFINES})
target_link_libraries(stgr_plugins PUBLIC stgr_common stgr_config)

if(STGR_ENABLE_VST3 AND STGR_VST3_SDK_DIR)
    target_compile_definitions(stgr_plugins PUBLIC STGR_HAVE_VST3=1)
    target_include_directories(stgr_plugins PUBLIC ${STGR_VST3_SDK_DIR})
endif()

# The version API (GetFileVersionInfoSizeW etc.) is used when scanning
# plugins for file metadata.
target_link_libraries(stgr_plugins PRIVATE version)