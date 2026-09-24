add_library(ssim_machine STATIC
    machine_runtime.cpp
)

target_include_directories(ssim_machine PUBLIC
    "${CMAKE_SOURCE_DIR}/include"
)

target_link_libraries(ssim_machine PUBLIC ssim_core ssim_hw ssim_analysis)

ssim_apply_warnings(ssim_machine)
ssim_apply_sanitizers(ssim_machine)
