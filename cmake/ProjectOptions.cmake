# Warnings and floating-point settings shared by every target.
#
# -ffp-contract=off: compilers disagree on when to fuse a*b+c into one FMA
# (GCC fuses by default, Clang only within expressions, MSVC not unless asked),
# which makes Monte Carlo results differ across platforms. Turning contraction
# off keeps results identical across GCC, Clang and MSVC.
function(ccold_set_project_options target)
  if(MSVC)
    target_compile_options(${target} PRIVATE /W4 /permissive- /utf-8 /fp:precise)
    if(CCOLD_WARNINGS_AS_ERRORS)
      target_compile_options(${target} PRIVATE /WX)
    endif()
  else()
    target_compile_options(${target} PRIVATE
      -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow
      -Wnon-virtual-dtor -Wold-style-cast -Wcast-align
      -Wdouble-promotion -Wimplicit-fallthrough
      -ffp-contract=off)
    if(CCOLD_WARNINGS_AS_ERRORS)
      target_compile_options(${target} PRIVATE -Werror)
    endif()
    if(CCOLD_SANITIZE)
      target_compile_options(${target} PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=all)
      target_link_options(${target} PRIVATE -fsanitize=address,undefined)
    endif()
  endif()
endfunction()
