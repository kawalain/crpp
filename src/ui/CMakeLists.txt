if(WIN32)
  add_library(crpp_ui STATIC
    gfx.cpp svg.cpp textfield.cpp png.cpp surface.cpp controls.cpp app.cpp
    setup_screen.cpp home_screen.cpp room_screen.cpp chat_panel.cpp notice_card.cpp dialogs.cpp toasts.cpp emoji_picker.cpp
    settings_panel.cpp settings_sound_tab.cpp settings_display_tab.cpp settings_notify_tab.cpp settings_system_tab.cpp
    settings_account_tab.cpp)
  target_include_directories(crpp_ui PUBLIC include)
  target_link_libraries(crpp_ui PUBLIC d2d1 dwrite windowscodecs ole32 user32 gdi32)
  target_compile_definitions(crpp_ui PUBLIC NOMINMAX WIN32_LEAN_AND_MEAN)
  crpp_target_defaults(crpp_ui)

  add_executable(crpp_ui_flow_test tests/flow_test.cpp)
  target_link_libraries(crpp_ui_flow_test PRIVATE crpp_ui)
  target_include_directories(crpp_ui_flow_test PRIVATE ${PROJECT_SOURCE_DIR}/tests/support tests)
  crpp_target_defaults(crpp_ui_flow_test)
  add_test(NAME ui.flow COMMAND crpp_ui_flow_test)
endif()
