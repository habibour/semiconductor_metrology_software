# Applies the project's "warnings are errors" policy (CLAUDE.md §6.1) to one target.
function(ssim_apply_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /WX /permissive-)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Werror)
    endif()
endfunction()
