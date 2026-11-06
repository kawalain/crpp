add_library(crpp_client STATIC client.cpp)
target_include_directories(crpp_client PUBLIC include)
target_link_libraries(crpp_client PUBLIC crpp_session crpp_transport Threads::Threads)
crpp_target_defaults(crpp_client)

if(WIN32)
  add_executable(crpp_client_test tests/client_test.cpp)
  target_link_libraries(crpp_client_test PRIVATE crpp_client crpp_transport_winhttp ws2_32 bcrypt)
  target_include_directories(crpp_client_test PRIVATE
    ${PROJECT_SOURCE_DIR}/tests/support ${PROJECT_SOURCE_DIR}/src/transport/tests)
  crpp_target_defaults(crpp_client_test)
  add_test(NAME client.end_to_end COMMAND crpp_client_test)
  set_tests_properties(client.end_to_end PROPERTIES TIMEOUT 120)
endif()
