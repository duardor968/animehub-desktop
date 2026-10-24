find_package(Qt6 6.11 REQUIRED COMPONENTS Test)

qt_add_library(animehub-test-support STATIC
  support/http_test_server.cpp
  support/http_test_server.h
)
target_include_directories(animehub-test-support PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/support")
target_link_libraries(animehub-test-support PUBLIC Qt6::Core Qt6::Network Qt6::Test)
animehub_configure_target(animehub-test-support)

# Offscreen tests find the system fonts through fontconfig on Linux; Windows points Qt at them.
set(ANIMEHUB_TEST_ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software")
if(WIN32)
  list(APPEND ANIMEHUB_TEST_ENVIRONMENT "QT_QPA_FONTDIR=C:/Windows/Fonts")
endif()

# One executable per suite keeps failures and timings readable in ctest.
function(animehub_add_test name)
  qt_add_executable(${name} ${ARGN} support/noninteractive_guard.cpp)
  target_link_libraries(${name} PRIVATE animehub-core animehub-test-support Qt6::Test)
  target_compile_definitions(${name} PRIVATE
    ANIMEHUB_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
  )
  animehub_configure_target(${name})
  # qt_standard_project_setup makes every executable a GUI one, and QtTest's plain log does not
  # reach a pipe: console tests with TAP output let ctest show why one failed.
  set_target_properties(${name} PROPERTIES WIN32_EXECUTABLE FALSE)
  add_test(NAME ${name} COMMAND ${name} -o -,tap)
  set_tests_properties(${name} PROPERTIES ENVIRONMENT "${ANIMEHUB_TEST_ENVIRONMENT}")
endfunction()

animehub_add_test(api-json-test api_json_test.cpp)
animehub_add_test(autostart-test autostart_test.cpp)
animehub_add_test(api-client-test api_client_test.cpp)
animehub_add_test(library-repository-test library_repository_test.cpp)
animehub_add_test(mega-test mega_test.cpp)
animehub_add_test(myjd-test myjd_test.cpp)
animehub_add_test(jdownloader-test jdownloader_test.cpp)
animehub_add_test(remote-service-test remote_service_test.cpp)
animehub_add_test(remote-api-integration-test remote_api_integration_test.cpp)
animehub_add_test(library-model-test library_model_test.cpp ../src/app/library_model.cpp ../src/app/card_model.cpp ../src/app/display.cpp ../src/app/schedule.cpp)
target_include_directories(library-model-test PRIVATE ../src)
target_link_libraries(library-model-test PRIVATE Qt6::Qml)
animehub_add_test(catalog-view-test catalog_view_test.cpp ../src/app/catalog_view.cpp ../src/app/library_model.cpp ../src/app/card_model.cpp ../src/app/display.cpp ../src/app/schedule.cpp)
target_include_directories(catalog-view-test PRIVATE ../src)
target_link_libraries(catalog-view-test PRIVATE Qt6::Qml)
animehub_add_test(app-settings-test app_settings_test.cpp ../src/app/app_settings.cpp)
target_include_directories(app-settings-test PRIVATE ../src)
target_link_libraries(app-settings-test PRIVATE Qt6::Qml)
animehub_add_test(downloads-view-test downloads_view_test.cpp ../src/app/downloads_view.cpp ../src/app/download_series_model.cpp ../src/app/display.cpp ../src/app/shell.cpp)
target_include_directories(downloads-view-test PRIVATE ../src)
target_link_libraries(downloads-view-test PRIVATE Qt6::Qml Qt6::Gui)
if(WIN32)
  target_link_libraries(downloads-view-test PRIVATE dwmapi)
elseif(LINUX)
  target_link_libraries(downloads-view-test PRIVATE Qt6::DBus)
endif()
animehub_add_test(anime-view-test anime_view_test.cpp ../src/app/anime_view.cpp ../src/app/episode_model.cpp ../src/app/card_model.cpp ../src/app/display.cpp ../src/app/schedule.cpp ../src/app/shell.cpp ../src/app/app_settings.cpp)
target_include_directories(anime-view-test PRIVATE ../src)
target_link_libraries(anime-view-test PRIVATE Qt6::Qml Qt6::Gui)
if(WIN32)
  target_link_libraries(anime-view-test PRIVATE dwmapi)
elseif(LINUX)
  target_link_libraries(anime-view-test PRIVATE Qt6::DBus)
endif()

# QML components on their own, as Qt Quick Test cases, in the style the app uses.
find_package(Qt6 6.11 REQUIRED COMPONENTS QuickTest)
qt_add_executable(qml-components-test qml_components_test.cpp support/noninteractive_guard.cpp)
target_link_libraries(qml-components-test PRIVATE Qt6::QuickTest)
target_compile_definitions(qml-components-test PRIVATE
  QUICK_TEST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/qml"
)
animehub_configure_target(qml-components-test)
set_target_properties(qml-components-test PROPERTIES WIN32_EXECUTABLE FALSE)
add_test(NAME qml-components-test COMMAND qml-components-test -o -,tap)
set_tests_properties(qml-components-test PROPERTIES ENVIRONMENT
  "${ANIMEHUB_TEST_ENVIRONMENT};QT_QUICK_CONTROLS_STYLE=FluentWinUI3"
)
# The real engines: the bundled ones on Windows, the system's on Linux.
if(WIN32 AND ANIMEHUB_BUNDLE_ENGINES)
  set(ANIMEHUB_TEST_ARIA2 "${PROJECT_BINARY_DIR}/engines/aria2/aria2c.exe")
  set(ANIMEHUB_TEST_MPV "${PROJECT_BINARY_DIR}/engines/mpv/mpv.exe")
elseif(LINUX)
  find_program(ANIMEHUB_TEST_ARIA2 aria2c REQUIRED)
  find_program(ANIMEHUB_TEST_MPV mpv REQUIRED)
endif()
if(ANIMEHUB_TEST_ARIA2)
  animehub_add_test(aria2-test aria2_test.cpp)
  animehub_add_test(download-manager-test download_manager_test.cpp)
  animehub_add_test(player-test player_test.cpp)
  foreach(test IN ITEMS aria2-test download-manager-test player-test)
    target_compile_definitions(${test} PRIVATE
      ANIMEHUB_ARIA2_EXECUTABLE="${ANIMEHUB_TEST_ARIA2}"
      ANIMEHUB_MPV_EXECUTABLE="${ANIMEHUB_TEST_MPV}"
    )
  endforeach()
endif()

if(LINUX)
  animehub_add_test(platform-linux-test platform_linux_test.cpp)
endif()
