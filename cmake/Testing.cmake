# Reduces the per-test-executable boilerplate (warnings, sanitizers, CTest
# discovery) that would otherwise be repeated once per file under tests/unit/.
function(ssim_add_unit_test name)
    cmake_parse_arguments(ARG "" "" "SOURCES;LIBS" ${ARGN})

    add_executable(${name} ${ARG_SOURCES})
    target_link_libraries(${name} PRIVATE GTest::gtest_main ${ARG_LIBS})
    ssim_apply_warnings(${name})
    ssim_apply_sanitizers(${name})
    # Discovery runs the freshly built test binary after linking. The default
    # 5 s limit fails the whole build when the machine is busy compiling many
    # tests in parallel, so it is raised.
    gtest_discover_tests(${name} DISCOVERY_TIMEOUT 60)
endfunction()
