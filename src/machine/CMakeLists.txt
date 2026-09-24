add_library(ssim_machine STATIC
    machine_runtime.cpp
)

target_include_directories(ssim_machine PUBLIC
    "${CMAKE_SOURCE_DIR}/include"
)

target_link_libraries(ssim_machine PUBLIC ssim_core ssim_hw ssim_analysis)

# The optional SECS/GEM link. The machine builds and runs without it (FR-MC-3).
if(SSIM_ENABLE_SECSGEM)
    target_link_libraries(ssim_machine PUBLIC ssim_secsgem)
    target_compile_definitions(ssim_machine PUBLIC SSIM_HAS_SECSGEM)
endif()

ssim_apply_warnings(ssim_machine)
ssim_apply_sanitizers(ssim_machine)
