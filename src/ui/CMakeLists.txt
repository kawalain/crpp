if(WIN32)
  add_library(crpp_ui STATIC
    gfx.cpp svg.cpp textfield.cpp png.cpp surface.cpp controls.cpp app.cpp
    setup_screen.cpp home_screen.cpp room_screen.cpp chat_panel.cpp notice_card.cpp settings_panel.cpp)
  target_include_directories(crpp_ui PUBLIC include)
  target_link_libraries(crpp_ui PUBLIC d2d1 dwrite windowscodecs ole32 user32 gdi32)
  target_compile_definitions(crpp_ui PUBLIC NOMINMAX WIN32_LEAN_AND_MEAN)
  crpp_target_defaults(crpp_ui)
endif()
