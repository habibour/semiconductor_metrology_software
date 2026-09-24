# Qt lives only in this directory (dependency rule, PRD 6.2). Everything the
# panel needs from the machine comes through ssim_machine and MachineApi.
set(CMAKE_AUTOMOC ON)

add_library(ssim_qt_panel STATIC
    event_bridge.cpp
    main_window.cpp
    settings_dialog.cpp
    wafer_map_widget.cpp
)

target_include_directories(ssim_qt_panel PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(ssim_qt_panel PUBLIC ssim_machine Qt6::Widgets Qt6::Concurrent)
# ssim_core has a config field called `slots` (PRD 8.1), which collides with
# Qt's `slots` keyword macro, so the keywords are switched off (Q_SIGNALS etc).
target_compile_definitions(ssim_qt_panel PUBLIC QT_NO_KEYWORDS)

ssim_apply_warnings(ssim_qt_panel)
ssim_apply_sanitizers(ssim_qt_panel)

add_executable(equipment_qt main.cpp)
target_link_libraries(equipment_qt PRIVATE ssim_qt_panel)

ssim_apply_warnings(equipment_qt)
ssim_apply_sanitizers(equipment_qt)
