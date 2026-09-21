#include <gtest/gtest.h>

// Proves the CMake + FetchContent + GoogleTest + CTest chain works before any
// real ssim_core code exists.
TEST(Smoke, BuildAndTestToolchainWorks) {
    EXPECT_EQ(2 + 2, 4);
}
