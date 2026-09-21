# Applies the sanitizer named by SSIM_SANITIZER (empty, "address", "thread" or
# "undefined") to one target. Thread and address sanitizers cannot be combined
# in the same build, which is why they use separate presets/build directories
# (CLAUDE.md §8).
function(ssim_apply_sanitizers target)
    if(NOT SSIM_SANITIZER OR SSIM_SANITIZER STREQUAL "")
        return()
    endif()

    if(MSVC)
        if(NOT SSIM_SANITIZER STREQUAL "address")
            message(FATAL_ERROR "MSVC only supports SSIM_SANITIZER=address")
        endif()
        target_compile_options(${target} PRIVATE /fsanitize=address)
    else()
        target_compile_options(${target} PRIVATE -fsanitize=${SSIM_SANITIZER} -fno-omit-frame-pointer -g)
        target_link_options(${target} PRIVATE -fsanitize=${SSIM_SANITIZER})
    endif()
endfunction()
