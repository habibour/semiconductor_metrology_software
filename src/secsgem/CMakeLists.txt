# SECS/GEM module (optional: -DSSIM_ENABLE_SECSGEM=ON). Depends on ssim_core
# only, never on ssim_hw, ssim_analysis or Qt (PRD 6.2). Asio is added with the
# server (it is the only part that needs sockets).
find_package(Threads REQUIRED)

add_library(ssim_secsgem STATIC
    secs2/item.cpp
    secs2/codec.cpp
)

target_include_directories(ssim_secsgem PUBLIC
    "${CMAKE_SOURCE_DIR}/include"
)

target_link_libraries(ssim_secsgem
    PUBLIC ssim_core
    PRIVATE Threads::Threads
)

# Standard-library hardening turns an out-of-range vector/string/span access
# into a trap. It stands in for AddressSanitizer on machines where the
# sanitizer runtime does not work, and costs little in non-Release builds.
target_compile_definitions(ssim_secsgem PUBLIC
    $<$<AND:$<CONFIG:Debug,RelWithDebInfo>,$<CXX_COMPILER_ID:AppleClang,Clang>>:_LIBCPP_HARDENING_MODE=_LIBCPP_HARDENING_MODE_EXTENSIVE>
    $<$<AND:$<CONFIG:Debug,RelWithDebInfo>,$<CXX_COMPILER_ID:GNU>>:_GLIBCXX_ASSERTIONS>
)

ssim_apply_warnings(ssim_secsgem)
ssim_apply_sanitizers(ssim_secsgem)
