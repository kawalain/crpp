add_library(crpp_wire STATIC input_frame.cpp)
target_include_directories(crpp_wire PUBLIC include)
crpp_target_defaults(crpp_wire)

add_executable(crpp_wire_test tests/input_frame_test.cpp)
target_link_libraries(crpp_wire_test PRIVATE crpp_wire)
target_include_directories(crpp_wire_test PRIVATE ${PROJECT_SOURCE_DIR}/tests/support)
crpp_target_defaults(crpp_wire_test)
add_test(NAME wire.input_frame COMMAND crpp_wire_test)
