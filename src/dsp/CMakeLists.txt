add_library(stgr_dsp STATIC
    engine.cpp
)
target_include_directories(stgr_dsp PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(stgr_dsp PUBLIC ${STGR_DEFINES})
target_link_libraries(stgr_dsp PUBLIC stgr_common)
