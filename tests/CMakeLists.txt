include(GoogleTest)

add_executable(ssim_smoke_test unit/smoke_test.cpp)
target_link_libraries(ssim_smoke_test PRIVATE GTest::gtest_main)
ssim_apply_warnings(ssim_smoke_test)
ssim_apply_sanitizers(ssim_smoke_test)

gtest_discover_tests(ssim_smoke_test)
