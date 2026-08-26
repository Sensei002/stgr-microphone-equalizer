add_executable(stgr_test_dsp test_dsp.cpp)
target_include_directories(stgr_test_dsp PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(stgr_test_dsp PRIVATE stgr_dsp stgr_config stgr_common)
target_compile_definitions(stgr_test_dsp PRIVATE ${STGR_DEFINES})

add_executable(stgr_test_config test_config.cpp)
target_include_directories(stgr_test_config PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(stgr_test_config PRIVATE stgr_config stgr_common)
target_compile_definitions(stgr_test_config PRIVATE ${STGR_DEFINES})

add_test(NAME test_dsp COMMAND stgr_test_dsp)
add_test(NAME test_config COMMAND stgr_test_config)