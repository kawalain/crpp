add_library(crpp_update STATIC update.cpp notes.cpp)
target_include_directories(crpp_update PUBLIC include)
target_link_libraries(crpp_update PUBLIC crpp_store)
crpp_target_defaults(crpp_update)

function(crpp_update_test name)
  add_executable(crpp_update_${name}_test tests/${name}_test.cpp)
  target_link_libraries(crpp_update_${name}_test PRIVATE crpp_update)
  target_include_directories(crpp_update_${name}_test PRIVATE ${PROJECT_SOURCE_DIR}/tests/support)
  crpp_target_defaults(crpp_update_${name}_test)
  add_test(NAME update.${name} COMMAND crpp_update_${name}_test)
endfunction()

crpp_update_test(update)
crpp_update_test(notes)

# What the build decides: the version, whether this is a release, and the key.
set(CRPP_VERSION "0.0.0" CACHE STRING "The version this build reports; the release workflow sets it from the tag")
option(CRPP_OFFICIAL_BUILD "A build for release: asks GitHub for updates and needs the real public key" OFF)
set(CRPP_UPDATE_TEST_KEY "" CACHE STRING "A throwaway public key for trying the update flow; refused in an official build")

set(key_file ${CMAKE_CURRENT_SOURCE_DIR}/release_public_key.txt)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${key_file})
file(READ ${key_file} CRPP_UPDATE_KEY_HEX)
string(STRIP "${CRPP_UPDATE_KEY_HEX}" CRPP_UPDATE_KEY_HEX)
if(CRPP_UPDATE_KEY_HEX STREQUAL "PLACEHOLDER")
  set(CRPP_UPDATE_KEY_HEX "")
else()
  string(LENGTH "${CRPP_UPDATE_KEY_HEX}" key_length)
  if(NOT CRPP_UPDATE_KEY_HEX MATCHES "^[0-9a-f]+$" OR NOT key_length EQUAL 128)
    message(FATAL_ERROR "${key_file} must hold PLACEHOLDER or a public key of 128 lowercase hex digits")
  endif()
endif()

if(CRPP_OFFICIAL_BUILD)
  if(CRPP_UPDATE_KEY_HEX STREQUAL "")
    message(FATAL_ERROR "An official build needs the real public key, and ${key_file} still holds the placeholder. "
                        "See docs/signing-key.md.")
  endif()
  if(NOT CRPP_UPDATE_TEST_KEY STREQUAL "")
    message(FATAL_ERROR "An official build must not carry a test key (CRPP_UPDATE_TEST_KEY).")
  endif()
  if(NOT CRPP_VERSION MATCHES "^0\\.(0|[1-9][0-9]*)\\.(0|[1-9][0-9]*)$")
    message(FATAL_ERROR "An official build needs a version 0.y.z, not '${CRPP_VERSION}'.")
  endif()
  set(CRPP_OFFICIAL_VALUE true)
else()
  set(CRPP_OFFICIAL_VALUE false)
  if(NOT CRPP_UPDATE_TEST_KEY STREQUAL "")
    set(CRPP_UPDATE_KEY_HEX "${CRPP_UPDATE_TEST_KEY}")
  endif()
endif()
configure_file(built_in.cpp.in built_in.cpp @ONLY)
# The commit changes without CMake running, so a step of its own keeps it current.
set(CRPP_BUILD_HEADER ${CMAKE_BINARY_DIR}/generated/crpp_build.h)
set(CRPP_BUILD_HEADER ${CRPP_BUILD_HEADER} PARENT_SCOPE)
set(CRPP_VERSION ${CRPP_VERSION} PARENT_SCOPE)
add_custom_target(crpp_build_id
  COMMAND ${CMAKE_COMMAND} -DSOURCE=${PROJECT_SOURCE_DIR} -DVERSION=${CRPP_VERSION} -DOUT=${CRPP_BUILD_HEADER}
          -P ${PROJECT_SOURCE_DIR}/cmake/build_id.cmake
  BYPRODUCTS ${CRPP_BUILD_HEADER} VERBATIM)
add_library(crpp_update_built_in STATIC ${CMAKE_CURRENT_BINARY_DIR}/built_in.cpp)
add_dependencies(crpp_update_built_in crpp_build_id)
target_include_directories(crpp_update_built_in PRIVATE ${CMAKE_BINARY_DIR}/generated)
target_link_libraries(crpp_update_built_in PUBLIC crpp_update)
crpp_target_defaults(crpp_update_built_in)

if(WIN32)
  add_library(crpp_update_windows STATIC windows_platform.cpp)
  target_link_libraries(crpp_update_windows PUBLIC crpp_update winhttp bcrypt)
  target_compile_definitions(crpp_update_windows PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN _WIN32_WINNT=0x0A00)
  crpp_target_defaults(crpp_update_windows)

  # Checks a finished release against the key built in, before it is published.
  add_executable(crpp_update_verify tools/verify_release.cpp)
  target_link_libraries(crpp_update_verify PRIVATE crpp_update_windows crpp_update_built_in)
  crpp_target_defaults(crpp_update_verify)

  # Stands in for the app in the replacement test; the real exe is never touched.
  add_executable(crpp_update_fake_app tests/fake_app.cpp)
  target_link_libraries(crpp_update_fake_app PRIVATE crpp_update_windows)
  target_compile_definitions(crpp_update_fake_app PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
  crpp_target_defaults(crpp_update_fake_app)

  add_executable(crpp_update_windows_platform_test tests/windows_platform_test.cpp)
  target_link_libraries(crpp_update_windows_platform_test PRIVATE crpp_update_windows ws2_32)
  target_include_directories(crpp_update_windows_platform_test PRIVATE
                             ${PROJECT_SOURCE_DIR}/tests/support ${PROJECT_SOURCE_DIR}/src/transport/tests)
  target_compile_definitions(crpp_update_windows_platform_test PRIVATE
                             NOMINMAX _WIN32_WINNT=0x0A00 CRPP_FAKE_APP="$<TARGET_FILE:crpp_update_fake_app>")
  add_dependencies(crpp_update_windows_platform_test crpp_update_fake_app)
  crpp_target_defaults(crpp_update_windows_platform_test)
  add_test(NAME update.windows_platform COMMAND crpp_update_windows_platform_test)
endif()
