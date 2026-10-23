cmake_minimum_required(VERSION 4.0)

project(
  AnimeHubDesktop
  VERSION 0.3.0
  DESCRIPTION "Native AnimeHub desktop application"
  HOMEPAGE_URL "https://github.com/duardor968/animehub-desktop"
  LANGUAGES CXX
)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(ANIMEHUB_ENABLE_CLANG_TIDY "Run clang-tidy while compiling" OFF)
option(ANIMEHUB_BUNDLE_ENGINES "Download and verify aria2 and mpv next to the executable" ON)

find_package(Qt6 6.11 REQUIRED COMPONENTS Core Gui Network Sql Qml Quick QuickControls2 Widgets)

# MSVC exposes the evolving C++26 mode as /std:c++latest. CMake 4.4 does not
# map CXX26 to that flag yet, so keep the requested standard exact and provide
# the missing compiler mapping locally.
if(MSVC)
  set(CMAKE_CXX26_STANDARD_COMPILE_OPTION "/std:c++latest")
  set(CMAKE_CXX26_EXTENSION_COMPILE_OPTION "/std:c++latest")
  set(CMAKE_CXX_STANDARD_LATEST 26)
  list(APPEND CMAKE_CXX_COMPILE_FEATURES cxx_std_26)
endif()

set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
qt_standard_project_setup(REQUIRES 6.11)

if(ANIMEHUB_ENABLE_CLANG_TIDY)
  find_program(CLANG_TIDY_EXECUTABLE NAMES clang-tidy REQUIRED)
  set(ANIMEHUB_CLANG_TIDY_COMMAND
    "${CLANG_TIDY_EXECUTABLE};--extra-arg-before=/std:c++latest;--extra-arg-before=/Zc:__cplusplus"
  )
endif()

function(animehub_configure_target target)
  if(MSVC)
    target_compile_options(${target} PRIVATE
      /W4 /permissive- /Zc:__cplusplus /utf-8 /external:anglebrackets /external:W0
      /wd4702) # unreachable code: reported inside Qt's templates
  else()
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
  endif()
  target_compile_definitions(${target} PRIVATE
    QT_NO_CAST_FROM_ASCII
    QT_NO_URL_CAST_FROM_STRING
    QT_NO_NARROWING_CONVERSIONS_IN_CONNECT
  )
  if(ANIMEHUB_ENABLE_CLANG_TIDY)
    set_target_properties(${target} PROPERTIES CXX_CLANG_TIDY "${ANIMEHUB_CLANG_TIDY_COMMAND}")
  endif()
endfunction()

add_subdirectory(src/core)

if(ANIMEHUB_BUNDLE_ENGINES AND WIN32)
  include(cmake/Engines.cmake)
  animehub_stage_engines("${PROJECT_BINARY_DIR}/engines")
endif()

qt_add_executable(animehub-desktop WIN32
  src/app/anime_view.cpp
  src/app/anime_view.h
  src/app/app_controller.cpp
  src/app/app_controller.h
  src/app/app_settings.cpp
  src/app/app_settings.h
  src/app/card_model.cpp
  src/app/card_model.h
  src/app/catalog_view.cpp
  src/app/catalog_view.h
  src/app/display.cpp
  src/app/display.h
  src/app/download_series_model.cpp
  src/app/download_series_model.h
  src/app/downloads_view.cpp
  src/app/downloads_view.h
  src/app/episode_model.cpp
  src/app/episode_model.h
  src/app/glyph_provider.cpp
  src/app/glyph_provider.h
  src/app/jdownloader_view.cpp
  src/app/jdownloader_view.h
  src/app/library_model.cpp
  src/app/library_model.h
  src/app/remote_view.cpp
  src/app/remote_view.h
  src/app/main.cpp
  src/app/schedule.cpp
  src/app/schedule.h
  src/app/shell.cpp
  src/app/shell.h
)
if(WIN32)
  target_sources(animehub-desktop PRIVATE src/app/animehub.rc)
endif()

set_source_files_properties(qml/Theme.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

qt_add_qml_module(animehub-desktop
  URI AnimeHub
  VERSION 1.0
  QML_FILES
    qml/Main.qml
    qml/Theme.qml
    qml/CloseDialog.qml
    qml/SettingsDialog.qml
    qml/RemoteDevicesDialog.qml
    qml/anime/AnimeHeader.qml
    qml/anime/AnimePage.qml
    qml/anime/CompactBar.qml
    qml/anime/ConfirmDialog.qml
    qml/anime/EpisodeMenu.qml
    qml/anime/EpisodeRow.qml
    qml/anime/EpisodesBar.qml
    qml/anime/Pager.qml
    qml/anime/PrimaryActions.qml
    qml/anime/ProgressBand.qml
    qml/anime/RangeDialog.qml
    qml/anime/SelectionBar.qml
    qml/catalog/CatalogPage.qml
    qml/components/ActionMenu.qml
    qml/components/Glyph.qml
    qml/components/MenuEntry.qml
    qml/components/ModalDialog.qml
    qml/components/PageTab.qml
    qml/components/Poster.qml
    qml/components/PosterCard.qml
    qml/components/ProgressLine.qml
    qml/components/Shelf.qml
    qml/components/Toast.qml
    qml/downloads/DownloadBar.qml
    qml/downloads/DownloadsHeader.qml
    qml/downloads/DownloadsPage.qml
    qml/downloads/EpisodeLine.qml
    qml/downloads/SectionHeading.qml
    qml/downloads/SeriesRow.qml
    qml/downloads/SpeedGraph.qml
    qml/library/FranchisePage.qml
    qml/library/FranchiseCandidatesDialog.qml
    qml/library/LibraryRemovalDialog.qml
    qml/library/FeaturedAnime.qml
    qml/library/LibraryHome.qml
    qml/library/LibraryList.qml
    qml/library/LibraryPage.qml
    qml/library/SearchBox.qml
  RESOURCES
    assets/animehub.png
)

target_include_directories(animehub-desktop PRIVATE src/app)
if(WIN32)
  target_link_libraries(animehub-desktop PRIVATE dwmapi)
endif()
target_link_libraries(animehub-desktop
  PRIVATE
    animehub-core
    Qt6::Gui
    Qt6::Qml
    Qt6::Quick
    Qt6::QuickControls2
    Qt6::Widgets
)
animehub_configure_target(animehub-desktop)
set_target_properties(animehub-desktop PROPERTIES OUTPUT_NAME "AnimeHub")

if(WIN32 AND CMAKE_BUILD_TYPE STREQUAL "Release")
  include(cmake/Deploy.cmake)
  animehub_deploy_runtime(animehub-desktop)
endif()

qt_add_executable(animehub-library-import
  src/tools/library_import.cpp
  tests/support/noninteractive_guard.cpp
)
target_link_libraries(animehub-library-import PRIVATE animehub-core Qt6::Core Qt6::Sql)
animehub_configure_target(animehub-library-import)
set_target_properties(animehub-library-import PROPERTIES WIN32_EXECUTABLE FALSE)

include(CTest)
if(BUILD_TESTING)
  add_subdirectory(tests)
endif()
