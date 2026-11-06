if(WIN32)
  add_executable(crpp main.cpp)
  target_link_libraries(crpp PRIVATE crpp_ui crpp_client crpp_store_windows crpp_assets_windows crpp_assets_built_in crpp_input_raw crpp_sound_wasapi crpp_transport_winhttp imm32)
  target_compile_definitions(crpp PRIVATE _CRT_SECURE_NO_WARNINGS)
  crpp_target_defaults(crpp)
  # A development build runs from an assets folder next to the exe. A release is
  # the exe alone, and fetches them into the user's data folder.
  add_custom_command(TARGET crpp POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${PROJECT_SOURCE_DIR}/assets $<TARGET_FILE_DIR:crpp>/assets)
endif()
