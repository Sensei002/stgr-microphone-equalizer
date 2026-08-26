add_executable(stgr_gui gui.cpp gui.rc)
target_link_libraries(stgr_gui PRIVATE
    stgr_common
    stgr_config
    stgr_devices
    stgr_plugins
    stgr_bridge
    ole32
    shell32
    shlwapi
    comctl32
    uuid
    ntdll
)
target_compile_definitions(stgr_gui PRIVATE ${STGR_DEFINES})
set_target_properties(stgr_gui PROPERTIES OUTPUT_NAME "STGRMicrophoneEqualizer")
# GUI app: wWinMain entry point, no console window.
set_target_properties(stgr_gui PROPERTIES LINK_FLAGS "/SUBSYSTEM:WINDOWS")