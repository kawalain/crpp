# CRPP_SANITIZE=ON builds every target with AddressSanitizer, and with GCC or Clang also with
# UndefinedBehaviorSanitizer, so the tests find out-of-bounds access, use after free and undefined
# behaviour. A diagnostic build only, in a folder of its own: never a release.
option(CRPP_SANITIZE "Build with AddressSanitizer (and UBSan where the compiler has it)" OFF)

# Lets every test in `dir` and below find MSVC's ASan runtime. Called once all tests are added.
function(crpp_sanitize_tests dir)
  if(NOT CRPP_SANITIZE OR NOT MSVC)
    return()
  endif()
  get_property(tests DIRECTORY "${dir}" PROPERTY TESTS)
  foreach(test IN LISTS tests)
    set_property(TEST "${test}" DIRECTORY "${dir}" APPEND PROPERTY
                 ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${crpp_asan_runtime_dir}")
  endforeach()
  get_property(subdirs DIRECTORY "${dir}" PROPERTY SUBDIRECTORIES)
  foreach(subdir IN LISTS subdirs)
    crpp_sanitize_tests("${subdir}")
  endforeach()
endfunction()

if(NOT CRPP_SANITIZE)
  return()
endif()

if(MSVC)
  # MSVC has no UBSan. ASan rules out incremental linking and edit-and-continue, which a Ninja
  # build does not use; whole-program optimisation is left off to keep reports exact.
  add_compile_options(/fsanitize=address /Zi)
  add_link_options(/DEBUG /INCREMENTAL:NO)
  set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE OFF)
  # The runtime is a DLL beside cl.exe, which only a developer prompt has on PATH.
  cmake_path(GET CMAKE_CXX_COMPILER PARENT_PATH crpp_asan_runtime_dir)
else()
  add_compile_options(-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer)
  add_link_options(-fsanitize=address,undefined)
endif()
