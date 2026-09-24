# host_sim: a scripted stand-in for the factory host (FR-HOST-1..3). Depends
# on ssim_secsgem only (PRD 6.2). The library holds the script language and
# the HSMS client so scenario tests can run scripts in-process; the executable
# is a thin main().
add_library(ssim_hostsim STATIC
    pattern.cpp
    script.cpp
    host_client.cpp
    runner.cpp
)

target_include_directories(ssim_hostsim PUBLIC "${CMAKE_SOURCE_DIR}/include")
target_link_libraries(ssim_hostsim PUBLIC ssim_secsgem)

ssim_apply_warnings(ssim_hostsim)
ssim_apply_sanitizers(ssim_hostsim)

add_executable(host_sim main.cpp)
target_link_libraries(host_sim PRIVATE ssim_hostsim)
ssim_apply_warnings(host_sim)
ssim_apply_sanitizers(host_sim)
