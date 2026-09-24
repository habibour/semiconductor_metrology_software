add_executable(equipment_cli main.cpp)

target_link_libraries(equipment_cli PRIVATE ssim_machine)

ssim_apply_warnings(equipment_cli)
ssim_apply_sanitizers(equipment_cli)
