# Symbols for Crash reports: an MSVC Release build writes a PDB beside each exe, which a Release
# publishes, so that anyone can read a report's module+offset as a function and a line
# (crpp_symbolize). The code stays the same: /DEBUG would turn /OPT:REF and /OPT:ICF off, so they are
# named after it. The exe names its PDB by file name only (/PDBALTPATH), not by a path of the machine
# that built it. Measured in docs/research/crash-reports.md.
if(NOT MSVC)
  return()
endif()

# /Z7: the debug information goes into each object, so no PDB server is needed while compiling.
set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "$<$<CONFIG:Release>:Embedded>")
add_link_options("$<$<CONFIG:Release>:/DEBUG;/OPT:REF;/OPT:ICF;/INCREMENTAL:NO;/PDBALTPATH:%_PDB%>")
