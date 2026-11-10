# Run-time hardening of every target. Measured in docs/research/hardening.md.
#   MSVC: the STL checks the bounds of operator[], front(), back() and the like and ends the process
#   at a bad one (_MSVC_STL_HARDENING); Control Flow Guard checks indirect calls (/guard:cf); and
#   the exe declares itself compatible with CET shadow stacks (/CETCOMPAT).
#   GCC: libstdc++'s assertions, the same checks as MSVC's STL hardening.
# CRPP_HARDEN=OFF exists only to measure what they cost.
option(CRPP_HARDEN "Bounds-checked standard library, Control Flow Guard and CET" ON)
if(NOT CRPP_HARDEN)
  return()
endif()

if(MSVC)
  add_compile_definitions(_MSVC_STL_HARDENING=1)
  add_compile_options(/guard:cf)
  add_link_options(/guard:cf /CETCOMPAT)
else()
  add_compile_definitions(_GLIBCXX_ASSERTIONS)
endif()
