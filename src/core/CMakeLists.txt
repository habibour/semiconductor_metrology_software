find_package(Threads REQUIRED)

add_library(ssim_core STATIC
    config.cpp
    event_bus.cpp
    logger.cpp
    process_state_machine.cpp
    command.cpp
    alarms.cpp
    controller.cpp
)

target_include_directories(ssim_core PUBLIC
    "${CMAKE_SOURCE_DIR}/include"
)

target_link_libraries(ssim_core
    PUBLIC nlohmann_json::nlohmann_json
    PRIVATE Threads::Threads
)

ssim_apply_warnings(ssim_core)
ssim_apply_sanitizers(ssim_core)
