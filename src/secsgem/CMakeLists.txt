# SECS/GEM module (optional: -DSSIM_ENABLE_SECSGEM=ON). Depends on ssim_core
# and Asio only, never on ssim_hw, ssim_analysis or Qt (PRD 6.2).
find_package(Threads REQUIRED)

# Asio is header-only and has no CMakeLists of its own, so it is only
# downloaded and its include directory used.
FetchContent_MakeAvailable(asio)

add_library(ssim_secsgem STATIC
    secs2/item.cpp
    secs2/codec.cpp
    secs2/text_dump.cpp
    secs2/message_factory.cpp
    hsms/frame.cpp
    hsms/frame_decoder.cpp
    hsms/connection_state.cpp
    hsms/data_message.cpp
    hsms/session.cpp
    hsms/server.cpp
)

target_include_directories(ssim_secsgem PUBLIC
    "${CMAKE_SOURCE_DIR}/include"
)

target_link_libraries(ssim_secsgem
    PUBLIC ssim_core
    PRIVATE Threads::Threads
)

# Asio as a SYSTEM include so -Werror and /WX do not fire inside its headers.
# Only .cpp files include it; no public ssim header does.
target_include_directories(ssim_secsgem SYSTEM PUBLIC "${asio_SOURCE_DIR}/asio/include")
target_compile_definitions(ssim_secsgem PUBLIC ASIO_STANDALONE ASIO_NO_DEPRECATED)
if(WIN32)
    target_compile_definitions(ssim_secsgem PUBLIC _WIN32_WINNT=0x0A00)
    target_link_libraries(ssim_secsgem PUBLIC ws2_32 mswsock)
endif()

# Standard-library hardening turns an out-of-range vector/string/span access
# into a trap. It stands in for AddressSanitizer on machines where the
# sanitizer runtime does not work, and costs little in non-Release builds.
target_compile_definitions(ssim_secsgem PUBLIC
    $<$<AND:$<CONFIG:Debug,RelWithDebInfo>,$<CXX_COMPILER_ID:AppleClang,Clang>>:_LIBCPP_HARDENING_MODE=_LIBCPP_HARDENING_MODE_EXTENSIVE>
    $<$<AND:$<CONFIG:Debug,RelWithDebInfo>,$<CXX_COMPILER_ID:GNU>>:_GLIBCXX_ASSERTIONS>
)

ssim_apply_warnings(ssim_secsgem)
ssim_apply_sanitizers(ssim_secsgem)
