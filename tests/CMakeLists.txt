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
