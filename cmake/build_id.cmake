# Run on every build: writes the commit into a header, touching it only when it changed.
#   cmake -DSOURCE=<repo> -DVERSION=<x.y.z> -DOUT=<crpp_build.h> -P build_id.cmake
set(hash "unknown")
find_package(Git QUIET)
if(GIT_FOUND)
  execute_process(COMMAND ${GIT_EXECUTABLE} rev-parse --short=7 HEAD WORKING_DIRECTORY ${SOURCE}
                  OUTPUT_VARIABLE commit OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET RESULT_VARIABLE failed)
  if(NOT failed AND commit)
    set(hash "${commit}")
    execute_process(COMMAND ${GIT_EXECUTABLE} status --porcelain WORKING_DIRECTORY ${SOURCE}
                    OUTPUT_VARIABLE dirty OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
    if(dirty)
      string(APPEND hash ".dirty")
    endif()
  endif()
endif()
set(text "#define CRPP_BUILD_HASH \"${hash}\"\n#define CRPP_PRODUCT_VERSION \"${VERSION}+${hash}\"\n")
if(EXISTS ${OUT})
  file(READ ${OUT} old)
endif()
if(NOT "${old}" STREQUAL "${text}")
  file(WRITE ${OUT} "${text}")
endif()
