include(FetchContent)
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.15.2
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

include(GoogleTest)
include(Testing)

ssim_add_unit_test(ssim_smoke_test SOURCES unit/smoke_test.cpp)

ssim_add_unit_test(ssim_core_clock_test SOURCES unit/core/clock_test.cpp LIBS ssim_core)
ssim_add_unit_test(ssim_core_queue_test SOURCES unit/core/queue_test.cpp LIBS ssim_core)
ssim_add_unit_test(ssim_core_event_bus_test SOURCES unit/core/event_bus_test.cpp LIBS ssim_core)
ssim_add_unit_test(ssim_core_logger_test SOURCES unit/core/logger_test.cpp LIBS ssim_core)
ssim_add_unit_test(ssim_core_config_test SOURCES unit/core/config_test.cpp LIBS ssim_core)
ssim_add_unit_test(ssim_core_state_machine_test SOURCES unit/core/state_machine_test.cpp LIBS ssim_core)
ssim_add_unit_test(ssim_core_alarms_test SOURCES unit/core/alarms_test.cpp LIBS ssim_core)
ssim_add_unit_test(ssim_core_controller_test SOURCES unit/core/controller_test.cpp LIBS ssim_core)
ssim_add_unit_test(ssim_core_ui_rules_test SOURCES unit/core/ui_rules_test.cpp LIBS ssim_core)
ssim_add_unit_test(ssim_core_machine_api_test SOURCES unit/core/machine_api_test.cpp LIBS ssim_core)

ssim_add_unit_test(ssim_hw_wafer_model_test SOURCES unit/hw/wafer_model_test.cpp LIBS ssim_hw)
ssim_add_unit_test(ssim_hw_stage_sim_test SOURCES unit/hw/stage_sim_test.cpp LIBS ssim_hw)
ssim_add_unit_test(ssim_hw_laser_sensor_sim_test SOURCES unit/hw/laser_sensor_sim_test.cpp LIBS ssim_hw)
ssim_add_unit_test(ssim_hw_fault_injector_test SOURCES unit/hw/fault_injector_test.cpp LIBS ssim_hw)
ssim_add_unit_test(ssim_hw_scan_thread_test SOURCES unit/hw/scan_thread_test.cpp LIBS ssim_hw)

ssim_add_unit_test(ssim_analysis_edge_exclusion_test SOURCES unit/analysis/edge_exclusion_test.cpp LIBS ssim_analysis)
ssim_add_unit_test(ssim_analysis_outlier_test SOURCES unit/analysis/outlier_test.cpp LIBS ssim_analysis)
ssim_add_unit_test(ssim_analysis_line_fit_test SOURCES unit/analysis/line_fit_test.cpp LIBS ssim_analysis)
ssim_add_unit_test(ssim_analysis_combine_test SOURCES unit/analysis/combine_test.cpp LIBS ssim_analysis)
ssim_add_unit_test(ssim_analysis_stoney_test SOURCES unit/analysis/stoney_test.cpp LIBS ssim_analysis ssim_hw)
ssim_add_unit_test(ssim_analysis_uncertainty_test SOURCES unit/analysis/uncertainty_test.cpp LIBS ssim_analysis)
ssim_add_unit_test(ssim_analysis_quality_gates_test SOURCES unit/analysis/quality_gates_test.cpp LIBS ssim_analysis)
ssim_add_unit_test(ssim_analysis_wafer_map_test SOURCES unit/analysis/wafer_map_test.cpp LIBS ssim_analysis)
ssim_add_unit_test(ssim_analysis_fit_pool_test SOURCES unit/analysis/fit_pool_test.cpp LIBS ssim_analysis)
ssim_add_unit_test(ssim_analysis_writers_test SOURCES unit/analysis/writers_test.cpp LIBS ssim_analysis)

# Integration test: invokes the real equipment_cli binary as a subprocess
# (FR-CLI-1/3), so it needs the binary's path baked in rather than linking
# against it.
add_executable(ssim_cli_demo_test integration/cli_demo_test.cpp)
target_link_libraries(ssim_cli_demo_test PRIVATE GTest::gtest_main nlohmann_json::nlohmann_json)
target_compile_definitions(ssim_cli_demo_test PRIVATE
    EQUIPMENT_CLI_PATH="$<TARGET_FILE:equipment_cli>")
add_dependencies(ssim_cli_demo_test equipment_cli)
ssim_apply_warnings(ssim_cli_demo_test)
ssim_apply_sanitizers(ssim_cli_demo_test)
gtest_discover_tests(ssim_cli_demo_test)

ssim_add_unit_test(ssim_machine_runtime_test SOURCES integration/machine_runtime_test.cpp LIBS ssim_machine)

# Qt panel smoke test: runs the real window on Qt's offscreen platform, so it
# needs no display. Only built with -DSSIM_BUILD_QT=ON.
if(SSIM_BUILD_QT)
    add_executable(ssim_qt_panel_smoke_test unit/app_qt/panel_smoke_test.cpp)
    set_target_properties(ssim_qt_panel_smoke_test PROPERTIES AUTOMOC ON)
    target_link_libraries(ssim_qt_panel_smoke_test PRIVATE ssim_qt_panel Qt6::Test)
    ssim_apply_warnings(ssim_qt_panel_smoke_test)
    ssim_apply_sanitizers(ssim_qt_panel_smoke_test)
    add_test(NAME QtPanelSmoke COMMAND ssim_qt_panel_smoke_test)
    set_tests_properties(QtPanelSmoke PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
        TIMEOUT 120)
endif()

# SECS/GEM module tests (only when the module is built).
if(SSIM_ENABLE_SECSGEM)
    ssim_add_unit_test(ssim_secs2_codec_roundtrip_test SOURCES unit/secsgem/secs2/codec_roundtrip_test.cpp LIBS ssim_secsgem)
    ssim_add_unit_test(ssim_secs2_codec_reject_test SOURCES unit/secsgem/secs2/codec_reject_test.cpp LIBS ssim_secsgem)
    ssim_add_unit_test(ssim_secs2_codec_fuzz_test SOURCES unit/secsgem/secs2/codec_fuzz_test.cpp LIBS ssim_secsgem)
    ssim_add_unit_test(ssim_secs2_text_dump_test SOURCES unit/secsgem/secs2/text_dump_test.cpp LIBS ssim_secsgem)
    ssim_add_unit_test(ssim_secs2_message_factory_test SOURCES unit/secsgem/secs2/message_factory_test.cpp LIBS ssim_secsgem)
    ssim_add_unit_test(ssim_hsms_frame_test SOURCES unit/secsgem/hsms/frame_test.cpp LIBS ssim_secsgem)
    ssim_add_unit_test(ssim_hsms_connection_state_test SOURCES unit/secsgem/hsms/connection_state_test.cpp LIBS ssim_secsgem)
    ssim_add_unit_test(ssim_hsms_timers_test SOURCES unit/secsgem/hsms/timers_test.cpp LIBS ssim_secsgem)
    ssim_add_unit_test(ssim_hsms_session_test SOURCES unit/secsgem/hsms/session_test.cpp LIBS ssim_secsgem)
    ssim_add_unit_test(ssim_hsms_server_test SOURCES integration/hsms_server_test.cpp LIBS ssim_secsgem)
    ssim_add_unit_test(ssim_gem_messages_test SOURCES unit/secsgem/gem/messages_test.cpp LIBS ssim_secsgem)
    ssim_add_unit_test(ssim_gem_service_test SOURCES unit/secsgem/gem/gem_service_test.cpp LIBS ssim_secsgem)
    ssim_add_unit_test(ssim_hostsim_pattern_test SOURCES unit/host_sim/pattern_test.cpp LIBS ssim_hostsim)
    ssim_add_unit_test(ssim_hostsim_script_test SOURCES unit/host_sim/script_test.cpp LIBS ssim_hostsim)

    # XT-SECSGEM-1 (FR-TOOL-2): an independent open-source host (the Python
    # `secsgem` library) talks to a real `equipment_cli serve`. Registered only
    # when the chosen Python can import secsgem, and it says so when it cannot,
    # so a missing library is visible instead of a silent skip.
    #   pip install secsgem==0.3.0
    #   cmake ... -DSSIM_INTEROP_PYTHON=/path/to/python-with-secsgem
    set(SSIM_INTEROP_PYTHON "" CACHE FILEPATH "Python with the secsgem package, for the XT-SECSGEM-1 interop test")
    if(NOT SSIM_INTEROP_PYTHON)
        find_package(Python3 COMPONENTS Interpreter QUIET)
        if(Python3_FOUND)
            set(SSIM_INTEROP_PYTHON "${Python3_EXECUTABLE}")
        endif()
    endif()
    set(_ssim_have_secsgem_py FALSE)
    if(SSIM_INTEROP_PYTHON)
        execute_process(COMMAND "${SSIM_INTEROP_PYTHON}" -c "import secsgem"
                        RESULT_VARIABLE _ssim_secsgem_rc OUTPUT_QUIET ERROR_QUIET)
        if(_ssim_secsgem_rc EQUAL 0)
            set(_ssim_have_secsgem_py TRUE)
        endif()
    endif()
    if(_ssim_have_secsgem_py)
        add_test(NAME XT-SECSGEM-1
                 COMMAND "${SSIM_INTEROP_PYTHON}" "${CMAKE_SOURCE_DIR}/scripts/interop_secsgem.py"
                         --cli $<TARGET_FILE:equipment_cli>
                         --config "${CMAKE_SOURCE_DIR}/config/demo_alarm.json")
        set_tests_properties(XT-SECSGEM-1 PROPERTIES TIMEOUT 300)
    else()
        message(STATUS "XT-SECSGEM-1 is NOT registered: no Python with the secsgem package "
                       "(pip install secsgem==0.3.0, then -DSSIM_INTEROP_PYTHON=<python>)")
    endif()

    # Scenario tests: real machine, real socket, the actual scenarios/*.scn files.
    add_executable(ssim_scenario_test scenario/scenario_test.cpp)
    target_link_libraries(ssim_scenario_test PRIVATE GTest::gtest_main ssim_hostsim ssim_machine)
    target_compile_definitions(ssim_scenario_test PRIVATE SSIM_SCENARIO_DIR="${CMAKE_SOURCE_DIR}/scenarios")
    ssim_apply_warnings(ssim_scenario_test)
    ssim_apply_sanitizers(ssim_scenario_test)
    gtest_discover_tests(ssim_scenario_test DISCOVERY_TIMEOUT 60)
endif()
