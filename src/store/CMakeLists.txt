add_library(crpp_store STATIC json.cpp)
target_include_directories(crpp_store PUBLIC include)
crpp_target_defaults(crpp_store)

function(crpp_store_test name)
  add_executable(crpp_store_${name}_test tests/${name}_test.cpp)
  target_link_libraries(crpp_store_${name}_test PRIVATE crpp_store)
  target_include_directories(crpp_store_${name}_test PRIVATE ${PROJECT_SOURCE_DIR}/tests/support)
  crpp_target_defaults(crpp_store_${name}_test)
  add_test(NAME store.${name} COMMAND crpp_store_${name}_test)
endfunction()

crpp_store_test(json)
