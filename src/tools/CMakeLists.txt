add_executable(stgr_scan scan_main.cpp)
target_link_libraries(stgr_scan PRIVATE stgr_common stgr_config stgr_plugins ole32 shell32)
target_compile_definitions(stgr_scan PRIVATE ${STGR_DEFINES})
set_target_properties(stgr_scan PROPERTIES OUTPUT_NAME "STGRScan")

if(TARGET stgr_apo_reg)
    add_executable(stgr_admin admin_main.cpp)
    target_link_libraries(stgr_admin PRIVATE stgr_common stgr_apo_reg ole32 shell32 advapi32)
    target_compile_definitions(stgr_admin PRIVATE ${STGR_DEFINES})
    set_target_properties(stgr_admin PROPERTIES OUTPUT_NAME "STGRAdmin")

    # STGRAdmin requires elevation.
    set_target_properties(stgr_admin PROPERTIES
        LINK_FLAGS "/MANIFESTUAC:\"level='requireAdministrator' uiAccess='false'\"")
endif()