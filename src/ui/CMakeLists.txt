if(WIN32)
  add_library(crpp_ui STATIC
    gfx.cpp svg.cpp textfield.cpp png.cpp surface.cpp controls.cpp app.cpp
    setup_screen.cpp prompt_screen.cpp home_screen.cpp room_screen.cpp chat_panel.cpp notice_card.cpp dialogs.cpp toasts.cpp emoji_picker.cpp
    offline_popup.cpp connection_feedback.cpp
    settings_panel.cpp settings_rows.cpp settings_sound_tab.cpp settings_display_tab.cpp settings_notify_tab.cpp settings_system_tab.cpp
    settings_account_tab.cpp tip.cpp profile_card.cpp member_menu.cpp chat_logic.cpp
    update_popup.cpp changelog_view.cpp)
  target_include_directories(crpp_ui PUBLIC include)
  target_link_libraries(crpp_ui PUBLIC crpp_session crpp_update d2d1 dwrite windowscodecs ole32 user32 gdi32)
  target_compile_definitions(crpp_ui PUBLIC NOMINMAX WIN32_LEAN_AND_MEAN)
  crpp_target_defaults(crpp_ui)

  add_executable(crpp_ui_flow_test tests/flow_test.cpp)
  target_link_libraries(crpp_ui_flow_test PRIVATE crpp_ui)
  target_include_directories(crpp_ui_flow_test PRIVATE ${PROJECT_SOURCE_DIR}/tests/support tests)
  crpp_target_defaults(crpp_ui_flow_test)
  add_test(NAME ui.flow COMMAND crpp_ui_flow_test)

  add_executable(crpp_ui_connection_test tests/connection_test.cpp)
  target_link_libraries(crpp_ui_connection_test PRIVATE crpp_ui)
  target_include_directories(crpp_ui_connection_test PRIVATE ${PROJECT_SOURCE_DIR}/tests/support tests)
  crpp_target_defaults(crpp_ui_connection_test)
  add_test(NAME ui.connection COMMAND crpp_ui_connection_test)

  add_executable(crpp_ui_room_test tests/room_test.cpp)
  target_link_libraries(crpp_ui_room_test PRIVATE crpp_ui)
  target_include_directories(crpp_ui_room_test PRIVATE ${PROJECT_SOURCE_DIR}/tests/support tests)
  target_compile_definitions(crpp_ui_room_test PRIVATE CRPP_TEST_ASSETS="${PROJECT_SOURCE_DIR}/assets")
  crpp_target_defaults(crpp_ui_room_test)
  add_test(NAME ui.room COMMAND crpp_ui_room_test)

  add_executable(crpp_ui_home_test tests/home_test.cpp)
  target_link_libraries(crpp_ui_home_test PRIVATE crpp_ui)
  target_include_directories(crpp_ui_home_test PRIVATE ${PROJECT_SOURCE_DIR}/tests/support tests)
  crpp_target_defaults(crpp_ui_home_test)
  add_test(NAME ui.home COMMAND crpp_ui_home_test)

  add_executable(crpp_ui_sound_tab_test tests/sound_tab_test.cpp)
  target_link_libraries(crpp_ui_sound_tab_test PRIVATE crpp_ui)
  target_include_directories(crpp_ui_sound_tab_test PRIVATE ${PROJECT_SOURCE_DIR}/tests/support tests)
  crpp_target_defaults(crpp_ui_sound_tab_test)
  add_test(NAME ui.sound_tab COMMAND crpp_ui_sound_tab_test)

  add_executable(crpp_ui_settings_test tests/settings_test.cpp)
  target_link_libraries(crpp_ui_settings_test PRIVATE crpp_ui)
  target_include_directories(crpp_ui_settings_test PRIVATE ${PROJECT_SOURCE_DIR}/tests/support tests)
  crpp_target_defaults(crpp_ui_settings_test)
  add_test(NAME ui.settings COMMAND crpp_ui_settings_test)

  add_executable(crpp_ui_prompt_test tests/prompt_test.cpp)
  target_link_libraries(crpp_ui_prompt_test PRIVATE crpp_ui)
  target_include_directories(crpp_ui_prompt_test PRIVATE ${PROJECT_SOURCE_DIR}/tests/support tests)
  crpp_target_defaults(crpp_ui_prompt_test)
  add_test(NAME ui.prompt COMMAND crpp_ui_prompt_test)

  add_executable(crpp_ui_update_test tests/update_ui_test.cpp)
  target_link_libraries(crpp_ui_update_test PRIVATE crpp_ui)
  target_include_directories(crpp_ui_update_test PRIVATE ${PROJECT_SOURCE_DIR}/tests/support tests)
  crpp_target_defaults(crpp_ui_update_test)
  add_test(NAME ui.update COMMAND crpp_ui_update_test)
endif()
