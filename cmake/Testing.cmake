# Reduces the per-test-executable boilerplate (warnings, sanitizers, CTest
# discovery) that would otherwise be repeated once per file under tests/unit/.
function(ssim_add_unit_test name)
    cmake_parse_arguments(ARG "" "" "SOURCES;LIBS" ${ARGN})

    add_executable(${name} ${ARG_SOURCES})
    target_link_libraries(${name} PRIVATE GTest::gtest_main ${ARG_LIBS})
    ssim_apply_warnings(${name})
    ssim_apply_sanitizers(${name})
    gtest_discover_tests(${name})
endfunction()
