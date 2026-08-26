add_executable(stgr_tray main.cpp tray.rc)
target_include_directories(stgr_tray PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(stgr_tray PRIVATE
    stgr_common
    stgr_config
    stgr_devices
    shell32
    advapi32
    ole32
)
target_compile_definitions(stgr_tray PRIVATE ${STGR_DEFINES})
set_target_properties(stgr_tray PROPERTIES OUTPUT_NAME "STGRTray")
# GUI app: wWinMain entry point, no console window.
set_target_properties(stgr_tray PROPERTIES LINK_FLAGS "/SUBSYSTEM:WINDOWS")