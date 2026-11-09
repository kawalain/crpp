add_library(crpp_files STATIC tree.cpp)
target_include_directories(crpp_files PUBLIC include)
crpp_target_defaults(crpp_files)

add_executable(crpp_files_tree_test tests/tree_test.cpp)
target_link_libraries(crpp_files_tree_test PRIVATE crpp_files)
target_include_directories(crpp_files_tree_test PRIVATE ${PROJECT_SOURCE_DIR}/tests/support)
crpp_target_defaults(crpp_files_tree_test)
add_test(NAME files.tree COMMAND crpp_files_tree_test)
