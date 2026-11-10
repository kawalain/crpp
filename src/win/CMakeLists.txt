# Owning COM pointers and Windows handles, for every Windows adapter. Header only, and free of
# Windows headers itself, so its test runs on any compiler.
add_library(crpp_win INTERFACE)
target_include_directories(crpp_win INTERFACE include)

add_executable(crpp_win_own_test tests/own_test.cpp)
target_link_libraries(crpp_win_own_test PRIVATE crpp_win)
target_include_directories(crpp_win_own_test PRIVATE ${PROJECT_SOURCE_DIR}/tests/support)
crpp_target_defaults(crpp_win_own_test)
add_test(NAME win.own COMMAND crpp_win_own_test)
