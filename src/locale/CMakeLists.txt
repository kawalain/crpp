add_library(crpp_locale STATIC locale.cpp keys.cpp ko.cpp en.cpp)
target_include_directories(crpp_locale PUBLIC include)
crpp_target_defaults(crpp_locale)

add_executable(crpp_locale_test tests/locale_test.cpp)
target_link_libraries(crpp_locale_test PRIVATE crpp_locale)
target_include_directories(crpp_locale_test PRIVATE ${PROJECT_SOURCE_DIR}/tests/support)
crpp_target_defaults(crpp_locale_test)
add_test(NAME locale COMMAND crpp_locale_test)
