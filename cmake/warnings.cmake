# Warning policy shared by every module. Call crpp_target_defaults(<target>).
function(crpp_target_defaults target)
  if(MSVC)
    target_compile_options(${target} PRIVATE /W4 /permissive- /utf-8)
  else()
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
  endif()
endfunction()
