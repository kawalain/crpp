# The Crash report: its text, the breadcrumbs it ends with, and the files and events it is made
# from. Pure; the handlers that write it are Windows' and live beside it.
add_library(crpp_crash STATIC writer.cpp report.cpp)
target_include_directories(crpp_crash PUBLIC include)
crpp_target_defaults(crpp_crash)

add_executable(crpp_crash_report_test tests/report_test.cpp)
target_link_libraries(crpp_crash_report_test PRIVATE crpp_crash)
target_include_directories(crpp_crash_report_test PRIVATE ${PROJECT_SOURCE_DIR}/tests/support)
crpp_target_defaults(crpp_crash_report_test)
add_test(NAME crash.report COMMAND crpp_crash_report_test)

if(WIN32)
  add_library(crpp_crash_windows STATIC windows.cpp)
  target_link_libraries(crpp_crash_windows PUBLIC crpp_crash PRIVATE crpp_win)
  target_compile_definitions(crpp_crash_windows PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN _WIN32_WINNT=0x0A00)
  crpp_target_defaults(crpp_crash_windows)

  add_executable(crpp_crash_handler_test tests/handler_test.cpp)
  target_link_libraries(crpp_crash_handler_test PRIVATE crpp_crash_windows crpp_win wevtapi)
  target_include_directories(crpp_crash_handler_test PRIVATE ${PROJECT_SOURCE_DIR}/tests/support)
  target_compile_definitions(crpp_crash_handler_test PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN _WIN32_WINNT=0x0A00)
  crpp_target_defaults(crpp_crash_handler_test)
  add_test(NAME crash.handler COMMAND crpp_crash_handler_test)
  # Reads the Application log, so it runs where there is one, apart from the others.
  add_test(NAME crash.event_log COMMAND crpp_crash_handler_test --event-log)
endif()
