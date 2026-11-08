# How an MSVC Release build is made small. Measured in docs/research/binary-size.md: none of it
# showed in the CPU of mixing, loading packs or scrolling.
if(NOT MSVC)
  return()
endif()

# Optimise for size (/O1 instead of /O2), with each global in its own section for the linker to drop
# (/Gw), and no RTTI, which no code uses (/GR-).
string(REPLACE "/O2" "/O1" CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE}")
string(REPLACE "/Ob2" "" CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE}")
add_compile_options(/GR- $<$<CONFIG:Release>:/Gw> $<$<CONFIG:Release>:/Zc:inline>)
# Whole-program optimisation: /GL, and /LTCG for the libraries and the link.
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)

# The hybrid CRT: the C++ runtime stays inside the exe (/MT) but the Universal CRT is Windows' own,
# a part of the system since Windows 10, so its code is not copied in. As Microsoft's Windows App
# SDK builds itself (docs/Coding-Guidelines/HybridCRT.md in microsoft/WindowsAppSDK).
if(CMAKE_MSVC_RUNTIME_LIBRARY STREQUAL "MultiThreaded")
  add_link_options(/NODEFAULTLIB:libucrt.lib /DEFAULTLIB:ucrt.lib)
endif()
