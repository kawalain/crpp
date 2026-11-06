if(WIN32)
  add_executable(crpp
    main.cpp window.cpp startup.cpp bridge.cpp shot.cpp
    area_connection.cpp area_home.cpp area_room.cpp area_sound.cpp area_preferences.cpp area_os.cpp area_account.cpp
    area_update.cpp)
  # Releases use MSVC, whose linker embeds this. A GCC build runs unaware of DPI,
  # which Windows scales for it; it exists to keep the code portable.
  if(MSVC)
    target_sources(crpp PRIVATE crpp.manifest)
  endif()
  target_link_libraries(crpp PRIVATE crpp_ui crpp_client crpp_store_windows crpp_assets_windows crpp_assets_built_in crpp_input_raw crpp_sound_wasapi crpp_transport_winhttp imm32)
  # Per-Monitor v2 starts at Windows 10 1703, which the UI's font loading needs too.
  target_compile_definitions(crpp PRIVATE _CRT_SECURE_NO_WARNINGS _WIN32_WINNT=0x0A00)
  crpp_target_defaults(crpp)
  # A development build runs from an assets folder next to the exe. A release is
  # the exe alone, and fetches them into the user's data folder.
  add_custom_command(TARGET crpp POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${PROJECT_SOURCE_DIR}/assets $<TARGET_FILE_DIR:crpp>/assets)

  add_executable(crpp_gui_sound_prefs_test tests/sound_prefs_test.cpp)
  target_link_libraries(crpp_gui_sound_prefs_test PRIVATE crpp_ui crpp_sound crpp_store)
  target_include_directories(crpp_gui_sound_prefs_test PRIVATE ${PROJECT_SOURCE_DIR}/tests/support)
  crpp_target_defaults(crpp_gui_sound_prefs_test)
  add_test(NAME gui.sound_prefs COMMAND crpp_gui_sound_prefs_test)
endif()
