find_package(Qt6 6.11 REQUIRED COMPONENTS Test)

qt_add_library(animehub-test-support STATIC
  support/http_test_server.cpp
  support/http_test_server.h
)
target_include_directories(animehub-test-support PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/support")
target_link_libraries(animehub-test-support PUBLIC Qt6::Core Qt6::Network Qt6::Test)
animehub_configure_target(animehub-test-support)

# One executable per suite keeps failures and timings readable in ctest.
function(animehub_add_test name)
  qt_add_executable(${name} ${ARGN})
  target_link_libraries(${name} PRIVATE animehub-core animehub-test-support Qt6::Test)
  target_compile_definitions(${name} PRIVATE
    ANIMEHUB_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
  )
  animehub_configure_target(${name})
  add_test(NAME ${name} COMMAND ${name})
  set_tests_properties(${name} PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endfunction()

animehub_add_test(api-json-test api_json_test.cpp)
animehub_add_test(api-client-test api_client_test.cpp)
animehub_add_test(library-repository-test library_repository_test.cpp)
animehub_add_test(mega-test mega_test.cpp)
if(ANIMEHUB_BUNDLE_ENGINES)
  animehub_add_test(aria2-test aria2_test.cpp)
  animehub_add_test(download-manager-test download_manager_test.cpp)
  animehub_add_test(player-test player_test.cpp)
  foreach(test IN ITEMS aria2-test download-manager-test player-test)
    target_compile_definitions(${test} PRIVATE
      ANIMEHUB_ARIA2_EXECUTABLE="${PROJECT_BINARY_DIR}/engines/aria2/aria2c.exe"
      ANIMEHUB_MPV_EXECUTABLE="${PROJECT_BINARY_DIR}/engines/mpv/mpv.exe"
    )
  endforeach()
endif()
