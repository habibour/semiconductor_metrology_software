add_library(ssim_hw STATIC
    fault_injector.cpp
    hardware_factory.cpp
    laser_sensor_sim.cpp
    scan_thread.cpp
    stage_sim.cpp
    wafer_model.cpp
)

target_include_directories(ssim_hw PUBLIC
    "${CMAKE_SOURCE_DIR}/include"
)

target_link_libraries(ssim_hw PUBLIC ssim_core)

ssim_apply_warnings(ssim_hw)
ssim_apply_sanitizers(ssim_hw)
