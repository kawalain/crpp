# Run-time hardening of every target. Measured in docs/research/hardening.md.
#   MSVC: the STL checks the bounds of operator[], front(), back() and the like and ends the process
#   at a bad one (_MSVC_STL_HARDENING); Control Flow Guard checks indirect calls (/guard:cf); and
#   the exe declares itself compatible with CET shadow stacks (/CETCOMPAT). A failed check of the STL
#   calls abort() rather than __fastfail, so that the crash handler (src/crash) can write a report
#   of it; the check comes before memory is touched, so what the report reads is still whole.
#   GCC: libstdc++'s assertions, the same checks as MSVC's STL hardening.
# CRPP_HARDEN=OFF exists only to measure what they cost.
option(CRPP_HARDEN "Bounds-checked standard library, Control Flow Guard and CET" ON)
if(NOT CRPP_HARDEN)
  return()
endif()

if(MSVC)
  add_compile_definitions(_MSVC_STL_HARDENING=1 _MSVC_STL_USE_ABORT_AS_DOOM_FUNCTION)
  add_compile_options(/guard:cf)
  add_link_options(/guard:cf /CETCOMPAT)
else()
  add_compile_definitions(_GLIBCXX_ASSERTIONS)
endif()
