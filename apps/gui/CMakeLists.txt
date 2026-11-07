if(WIN32)
  # The Windows subsystem: no console opens with the window.
  add_executable(crpp WIN32
    main.cpp window.cpp startup.cpp bridge.cpp shot.cpp
    area_connection.cpp area_home.cpp area_room.cpp area_sound.cpp area_preferences.cpp area_os.cpp area_account.cpp
    area_update.cpp tray.cpp autostart_windows.cpp account_controller.cpp)
  # Releases use MSVC, whose linker embeds this. A GCC build runs unaware of DPI,
  # which Windows scales for it; it exists to keep the code portable.
  # Icon and file properties. The numeric fields take digits only, so a test version
  # that is not plain x.y.z reads 0.0.0 there.
  set(CRPP_VERSION_MAJOR 0)
  set(CRPP_VERSION_MINOR 0)
  set(CRPP_VERSION_PATCH 0)
  if(CRPP_VERSION MATCHES "^([0-9]+)\.([0-9]+)\.([0-9]+)$")
    set(CRPP_VERSION_MAJOR ${CMAKE_MATCH_1})
    set(CRPP_VERSION_MINOR ${CMAKE_MATCH_2})
    set(CRPP_VERSION_PATCH ${CMAKE_MATCH_3})
  endif()
  set(CRPP_ICON_FILE ${CMAKE_CURRENT_SOURCE_DIR}/icon/cloudroom.ico)
  configure_file(crpp.rc.in crpp.rc @ONLY)
  target_sources(crpp PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/crpp.rc)
  set_source_files_properties(${CMAKE_CURRENT_BINARY_DIR}/crpp.rc PROPERTIES
    OBJECT_DEPENDS "${CRPP_ICON_FILE};${CRPP_BUILD_HEADER}")
  target_include_directories(crpp PRIVATE ${CMAKE_BINARY_DIR}/generated)
  add_dependencies(crpp crpp_build_id)
  if(MSVC)
    target_sources(crpp PRIVATE crpp.manifest)
    target_link_options(crpp PRIVATE /ENTRY:mainCRTStartup)  # the entry point stays main
    # A release is one exe, often run from Downloads: imports come from System32 only
    # (LOAD_LIBRARY_SEARCH_SYSTEM32), not from a DLL left beside it.
    target_link_options(crpp PRIVATE /DEPENDENTLOADFLAG:0x800)
  endif()
  target_link_libraries(crpp PRIVATE crpp_ui crpp_client crpp_store_windows crpp_assets_windows crpp_assets_built_in crpp_update_windows crpp_update_built_in crpp_input_raw crpp_sound_wasapi crpp_transport_winhttp crpp_api_winhttp imm32)
  target_link_libraries(crpp PRIVATE dwmapi)
  # Per-Monitor v2 starts at Windows 10 1703, which the UI's font loading needs too.
  target_compile_definitions(crpp PRIVATE _CRT_SECURE_NO_WARNINGS _WIN32_WINNT=0x0A00)
  crpp_target_defaults(crpp)
  # A development build runs from an assets folder next to the exe. A release is
  # the exe alone, and fetches them into the user's data folder.
  add_custom_command(TARGET crpp POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${PROJECT_SOURCE_DIR}/assets $<TARGET_FILE_DIR:crpp>/assets)
  if(CRPP_FETCH_DEV_ASSETS)
    add_custom_command(TARGET crpp POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E make_directory ${CRPP_ASSET_CACHE}
      COMMAND ${CMAKE_COMMAND} -E copy_directory ${CRPP_ASSET_CACHE} $<TARGET_FILE_DIR:crpp>/assets)
  endif()

  # Not part of a normal build: see the tool's first lines.
  add_executable(crpp_make_icon EXCLUDE_FROM_ALL tools/make_icon.cpp)
  target_link_libraries(crpp_make_icon PRIVATE crpp_ui)
  target_include_directories(crpp_make_icon PRIVATE .)
  target_compile_definitions(crpp_make_icon PRIVATE _WIN32_WINNT=0x0A00)
  crpp_target_defaults(crpp_make_icon)

  add_executable(crpp_gui_sound_prefs_test tests/sound_prefs_test.cpp)
  target_link_libraries(crpp_gui_sound_prefs_test PRIVATE crpp_ui crpp_sound crpp_store)
  target_include_directories(crpp_gui_sound_prefs_test PRIVATE ${PROJECT_SOURCE_DIR}/tests/support)
  crpp_target_defaults(crpp_gui_sound_prefs_test)
  add_test(NAME gui.sound_prefs COMMAND crpp_gui_sound_prefs_test)

  add_executable(crpp_gui_parts_test tests/parts_test.cpp)
  target_link_libraries(crpp_gui_parts_test PRIVATE crpp_store)
  target_include_directories(crpp_gui_parts_test PRIVATE . ${PROJECT_SOURCE_DIR}/tests/support)
  crpp_target_defaults(crpp_gui_parts_test)
  add_test(NAME gui.parts COMMAND crpp_gui_parts_test ${PROJECT_SOURCE_DIR}/assets/sounds/notifications)

  add_executable(crpp_gui_os_test tests/os_test.cpp tray.cpp)
  target_link_libraries(crpp_gui_os_test PRIVATE crpp_ui)
  target_include_directories(crpp_gui_os_test PRIVATE . ${PROJECT_SOURCE_DIR}/tests/support)
  target_compile_definitions(crpp_gui_os_test PRIVATE _WIN32_WINNT=0x0A00)
  crpp_target_defaults(crpp_gui_os_test)
  add_test(NAME gui.os COMMAND crpp_gui_os_test)

  add_executable(crpp_gui_account_test tests/account_test.cpp account_controller.cpp)
  target_link_libraries(crpp_gui_account_test PRIVATE crpp_ui crpp_api)
  target_include_directories(crpp_gui_account_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR} ${PROJECT_SOURCE_DIR}/tests/support ${PROJECT_SOURCE_DIR}/src/ui/tests)
  crpp_target_defaults(crpp_gui_account_test)
  add_test(NAME gui.account COMMAND crpp_gui_account_test)
endif()
