# Registration helpers (shared between the APO DLL and the elevated helper).
add_library(stgr_apo_reg STATIC
    apo_registration.cpp
)
target_include_directories(stgr_apo_reg PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(stgr_apo_reg PUBLIC ${STGR_DEFINES})
target_link_libraries(stgr_apo_reg PUBLIC stgr_common)
target_link_libraries(stgr_apo_reg PRIVATE advapi32)

add_library(stgr_apo SHARED
    apo_dll.cpp
    stgr_apo.cpp
)

target_include_directories(stgr_apo PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# The APO must use the static CRT (audiodg does not redistribute the dynamic
# CRT) and must not embed a manifest (protected audio dg requirements).
set_target_properties(stgr_apo PROPERTIES
    OUTPUT_NAME "stgr_apo"
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin/$<CONFIG>)

set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
target_compile_options(stgr_apo PRIVATE /W3)
target_link_options(stgr_apo PRIVATE /MANIFEST:NO)

target_link_libraries(stgr_apo PRIVATE
    stgr_common
    stgr_dsp
    stgr_config
    stgr_devices
    stgr_bridge
    stgr_apo_reg
    ole32
    uuid
    propsys
    shell32
)

target_compile_definitions(stgr_apo PRIVATE ${STGR_DEFINES} STGR_APO_EXPORTS)