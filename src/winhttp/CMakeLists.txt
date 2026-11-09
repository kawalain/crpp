# What the WinHTTP adapters of transport, api, assets and update share. Windows only.
if(WIN32)
  add_library(crpp_winhttp STATIC winhttp.cpp)
  target_include_directories(crpp_winhttp PUBLIC include)
  target_link_libraries(crpp_winhttp PUBLIC winhttp)
  target_compile_definitions(crpp_winhttp PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
  crpp_target_defaults(crpp_winhttp)
endif()
