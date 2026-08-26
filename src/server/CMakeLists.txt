add_executable(stgr_server main.cpp)
target_link_libraries(stgr_server PRIVATE
    stgr_common
    stgr_config
    stgr_plugins
    stgr_bridge
    ole32
    shell32
)
target_compile_definitions(stgr_server PRIVATE ${STGR_DEFINES})
set_target_properties(stgr_server PROPERTIES OUTPUT_NAME "STGRAudioServer")