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

# MACOSX_BUNDLE: on macOS this makes the target a real .app bundle instead of
# a bare Unix executable. Without it, `equipment_qt` still runs fine from the
# command line, but there is nothing for macdeployqt to bundle Qt's frameworks
# into, so release.yml's Release build (Day 6) has nothing to attach to the
# GitHub Release — found by a real failed Release run, not by inspection.
add_executable(equipment_qt MACOSX_BUNDLE main.cpp)
target_link_libraries(equipment_qt PRIVATE ssim_qt_panel)
if(APPLE)
    set_target_properties(equipment_qt PROPERTIES
        MACOSX_BUNDLE_BUNDLE_NAME "StressScan-Sim"
        MACOSX_BUNDLE_GUI_IDENTIFIER "io.github.habibour.stressscan-sim"
        MACOSX_BUNDLE_BUNDLE_VERSION "0.1.0"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "0.1.0"
    )
endif()

ssim_apply_warnings(equipment_qt)
ssim_apply_sanitizers(equipment_qt)
