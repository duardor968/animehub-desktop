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
  # qt_standard_project_setup makes every executable a GUI one, and QtTest's plain log does not
  # reach a pipe: console tests with TAP output let ctest show why one failed.
  set_target_properties(${name} PROPERTIES WIN32_EXECUTABLE FALSE)
  add_test(NAME ${name} COMMAND ${name} -o -,tap)
  set_tests_properties(${name} PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endfunction()

animehub_add_test(api-json-test api_json_test.cpp)
animehub_add_test(api-client-test api_client_test.cpp)
animehub_add_test(library-repository-test library_repository_test.cpp)
animehub_add_test(mega-test mega_test.cpp)
animehub_add_test(myjd-test myjd_test.cpp)

# QML components on their own, as Qt Quick Test cases, in the style the app uses.
find_package(Qt6 6.11 REQUIRED COMPONENTS QuickTest)
qt_add_executable(qml-components-test qml_components_test.cpp)
target_link_libraries(qml-components-test PRIVATE Qt6::QuickTest)
target_compile_definitions(qml-components-test PRIVATE
  QUICK_TEST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/qml"
)
animehub_configure_target(qml-components-test)
set_target_properties(qml-components-test PROPERTIES WIN32_EXECUTABLE FALSE)
add_test(NAME qml-components-test COMMAND qml-components-test -o -,tap)
set_tests_properties(qml-components-test PROPERTIES ENVIRONMENT
  "QT_QPA_PLATFORM=offscreen;QT_QUICK_CONTROLS_STYLE=FluentWinUI3;QT_QUICK_BACKEND=software"
)
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
