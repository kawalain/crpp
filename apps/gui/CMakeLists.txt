if(WIN32)
  add_executable(crpp main.cpp)
  target_link_libraries(crpp PRIVATE crpp_ui crpp_client crpp_store_windows crpp_transport_winhttp imm32)
  target_compile_definitions(crpp PRIVATE _CRT_SECURE_NO_WARNINGS)
  crpp_target_defaults(crpp)
  # The emoji and word lists are loaded from an assets folder next to the exe.
  add_custom_command(TARGET crpp POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${PROJECT_SOURCE_DIR}/assets $<TARGET_FILE_DIR:crpp>/assets)
endif()
