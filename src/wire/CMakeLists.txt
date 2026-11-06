add_library(crpp_wire STATIC
  input_frame.cpp
  json_reader.cpp
  server_message.cpp
  client_message.cpp)
target_include_directories(crpp_wire PUBLIC include)
crpp_target_defaults(crpp_wire)

foreach(name input_frame server_message client_message)
  add_executable(crpp_wire_${name}_test tests/${name}_test.cpp)
  target_link_libraries(crpp_wire_${name}_test PRIVATE crpp_wire)
  target_include_directories(crpp_wire_${name}_test PRIVATE ${PROJECT_SOURCE_DIR}/tests/support)
  crpp_target_defaults(crpp_wire_${name}_test)
  add_test(NAME wire.${name} COMMAND crpp_wire_${name}_test)
endforeach()
