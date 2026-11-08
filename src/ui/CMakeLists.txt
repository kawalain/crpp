if(WIN32)
  add_library(crpp_ui STATIC
    gfx.cpp svg.cpp textfield.cpp text_units.cpp read_selection.cpp clipboard.cpp png.cpp surface.cpp controls.cpp cursor_sheet.cpp cursor_image.cpp window_frame.cpp skin_canvas.cpp skin_choice.cpp app.cpp
    setup_screen.cpp prompt_screen.cpp home_screen.cpp room_screen.cpp chat_panel.cpp notice_card.cpp dialogs.cpp toasts.cpp emoji_picker.cpp
    offline_popup.cpp connection_feedback.cpp
    settings_panel.cpp settings_rows.cpp settings_sound_tab.cpp settings_display_tab.cpp settings_notify_tab.cpp settings_system_tab.cpp
    settings_account_tab.cpp tip.cpp profile_card.cpp member_menu.cpp context_menu.cpp chat_logic.cpp
    update_popup.cpp changelog_view.cpp
    home_screen_skin.cpp prompt_screen_skin.cpp window_frame_skin.cpp room_screen_skin.cpp chat_panel_skin.cpp settings_skin.cpp)
  target_include_directories(crpp_ui PUBLIC include)
  target_link_libraries(crpp_ui PUBLIC crpp_session crpp_update crpp_locale crpp_skin d2d1 dwrite windowscodecs ole32 user32 gdi32)
  target_compile_definitions(crpp_ui PUBLIC NOMINMAX WIN32_LEAN_AND_MEAN)
  crpp_target_defaults(crpp_ui)

  # Every UI test draws from the official packs in the source tree.
  add_library(crpp_ui_test_skins INTERFACE)
  target_compile_definitions(crpp_ui_test_skins INTERFACE CRPP_TEST_SKINS="${PROJECT_SOURCE_DIR}/skins")

  add_executable(crpp_ui_textfield_test tests/textfield_test.cpp)
  target_link_libraries(crpp_ui_textfield_test PRIVATE crpp_ui)
  target_include_directories(crpp_ui_textfield_test PRIVATE ${PROJECT_SOURCE_DIR}/tests/support)
  crpp_target_defaults(crpp_ui_textfield_test)
  add_test(NAME ui.textfield COMMAND crpp_ui_textfield_test)

  add_executable(crpp_ui_read_selection_test tests/read_selection_test.cpp)
  target_link_libraries(crpp_ui_read_selection_test PRIVATE crpp_ui)
  target_include_directories(crpp_ui_read_selection_test PRIVATE ${PROJECT_SOURCE_DIR}/tests/support)
  crpp_target_defaults(crpp_ui_read_selection_test)
  add_test(NAME ui.read_selection COMMAND crpp_ui_read_selection_test)

  add_executable(crpp_ui_context_menu_test tests/context_menu_test.cpp)
  target_link_libraries(crpp_ui_context_menu_test PRIVATE crpp_ui crpp_ui_test_skins)
  target_include_directories(crpp_ui_context_menu_test PRIVATE ${PROJECT_SOURCE_DIR}/tests/support tests)
  crpp_target_defaults(crpp_ui_context_menu_test)
  add_test(NAME ui.context_menu COMMAND crpp_ui_context_menu_test)

  add_executable(crpp_ui_flow_test tests/flow_test.cpp)
  target_link_libraries(crpp_ui_flow_test PRIVATE crpp_ui crpp_ui_test_skins)
  target_include_directories(crpp_ui_flow_test PRIVATE ${PROJECT_SOURCE_DIR}/tests/support tests)
  crpp_target_defaults(crpp_ui_flow_test)
  add_test(NAME ui.flow COMMAND crpp_ui_flow_test)

  add_executable(crpp_ui_connection_test tests/connection_test.cpp)
  target_link_libraries(crpp_ui_connection_test PRIVATE crpp_ui crpp_ui_test_skins)
  target_include_directories(crpp_ui_connection_test PRIVATE ${PROJECT_SOURCE_DIR}/tests/support tests)
  crpp_target_defaults(crpp_ui_connection_test)
  add_test(NAME ui.connection COMMAND crpp_ui_connection_test)

  add_executable(crpp_ui_room_test tests/room_test.cpp)
  target_link_libraries(crpp_ui_room_test PRIVATE crpp_ui crpp_ui_test_skins)
  target_include_directories(crpp_ui_room_test PRIVATE ${PROJECT_SOURCE_DIR}/tests/support tests)
  target_compile_definitions(crpp_ui_room_test PRIVATE CRPP_TEST_ASSETS="${PROJECT_SOURCE_DIR}/assets")
  crpp_target_defaults(crpp_ui_room_test)
  add_test(NAME ui.room COMMAND crpp_ui_room_test)

  add_executable(crpp_ui_home_test tests/home_test.cpp)
  target_link_libraries(crpp_ui_home_test PRIVATE crpp_ui crpp_ui_test_skins)
  target_include_directories(crpp_ui_home_test PRIVATE ${PROJECT_SOURCE_DIR}/tests/support tests)
  crpp_target_defaults(crpp_ui_home_test)
  add_test(NAME ui.home COMMAND crpp_ui_home_test)

  add_executable(crpp_ui_sound_tab_test tests/sound_tab_test.cpp)
  target_link_libraries(crpp_ui_sound_tab_test PRIVATE crpp_ui crpp_ui_test_skins)
  target_include_directories(crpp_ui_sound_tab_test PRIVATE ${PROJECT_SOURCE_DIR}/tests/support tests)
  crpp_target_defaults(crpp_ui_sound_tab_test)
  add_test(NAME ui.sound_tab COMMAND crpp_ui_sound_tab_test)

  add_executable(crpp_ui_settings_test tests/settings_test.cpp)
  target_link_libraries(crpp_ui_settings_test PRIVATE crpp_ui crpp_ui_test_skins)
  target_include_directories(crpp_ui_settings_test PRIVATE ${PROJECT_SOURCE_DIR}/tests/support tests)
  crpp_target_defaults(crpp_ui_settings_test)
  add_test(NAME ui.settings COMMAND crpp_ui_settings_test)

  add_executable(crpp_ui_prompt_test tests/prompt_test.cpp)
  target_link_libraries(crpp_ui_prompt_test PRIVATE crpp_ui crpp_ui_test_skins)
  target_include_directories(crpp_ui_prompt_test PRIVATE ${PROJECT_SOURCE_DIR}/tests/support tests)
  crpp_target_defaults(crpp_ui_prompt_test)
  add_test(NAME ui.prompt COMMAND crpp_ui_prompt_test)

  add_executable(crpp_ui_skin_home_test tests/skin_home_test.cpp)
  target_link_libraries(crpp_ui_skin_home_test PRIVATE crpp_ui crpp_ui_test_skins)
  target_include_directories(crpp_ui_skin_home_test PRIVATE ${PROJECT_SOURCE_DIR}/tests/support tests)
  crpp_target_defaults(crpp_ui_skin_home_test)
  add_test(NAME ui.skin_home COMMAND crpp_ui_skin_home_test)

  add_executable(crpp_ui_skin_choice_test tests/skin_choice_test.cpp)
  target_link_libraries(crpp_ui_skin_choice_test PRIVATE crpp_ui crpp_ui_test_skins)
  target_include_directories(crpp_ui_skin_choice_test PRIVATE ${PROJECT_SOURCE_DIR}/tests/support)
  crpp_target_defaults(crpp_ui_skin_choice_test)
  add_test(NAME ui.skin_choice COMMAND crpp_ui_skin_choice_test)

  add_executable(crpp_ui_skin_cloud_test tests/skin_cloud_test.cpp)
  target_link_libraries(crpp_ui_skin_cloud_test PRIVATE crpp_ui crpp_ui_test_skins)
  target_include_directories(crpp_ui_skin_cloud_test PRIVATE ${PROJECT_SOURCE_DIR}/tests/support tests)
  crpp_target_defaults(crpp_ui_skin_cloud_test)
  add_test(NAME ui.skin_cloud COMMAND crpp_ui_skin_cloud_test)

  # crpp_skin, which checks a pack as the program loads it: see skins/README.md. The library is crpp_skin already.
  add_executable(crpp_skin_tool tools/skin_tool.cpp)
  set_target_properties(crpp_skin_tool PROPERTIES OUTPUT_NAME crpp_skin)
  target_link_libraries(crpp_skin_tool PRIVATE crpp_ui)
  crpp_target_defaults(crpp_skin_tool)
  add_test(NAME ui.skin_check_retro COMMAND crpp_skin_tool check ${PROJECT_SOURCE_DIR}/skins/retro)
  add_test(NAME ui.skin_check_cloud COMMAND crpp_skin_tool check ${PROJECT_SOURCE_DIR}/skins/cloud)

  add_executable(crpp_ui_update_test tests/update_ui_test.cpp)
  target_link_libraries(crpp_ui_update_test PRIVATE crpp_ui crpp_ui_test_skins)
  target_include_directories(crpp_ui_update_test PRIVATE ${PROJECT_SOURCE_DIR}/tests/support tests)
  crpp_target_defaults(crpp_ui_update_test)
  add_test(NAME ui.update COMMAND crpp_ui_update_test)
endif()
