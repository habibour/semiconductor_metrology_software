add_library(ssim_analysis STATIC
    edge_exclusion.cpp
    outlier_rejection.cpp
    line_fit.cpp
    combine.cpp
    stoney.cpp
    uncertainty.cpp
    quality_gates.cpp
    wafer_map.cpp
    fit_pool.cpp
    pipeline.cpp
    writers/json_writer.cpp
    writers/csv_writer.cpp
    writers/png_writer.cpp
)

target_include_directories(ssim_analysis PUBLIC
    "${CMAKE_SOURCE_DIR}/include"
)

# stb is header-only and not part of ssim_analysis's own public API — only
# png_writer.cpp includes it — so it stays a private include dir.
target_include_directories(ssim_analysis PRIVATE
    "${stb_SOURCE_DIR}"
)

target_link_libraries(ssim_analysis PUBLIC ssim_core)

ssim_apply_warnings(ssim_analysis)
ssim_apply_sanitizers(ssim_analysis)
