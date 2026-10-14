cmake_minimum_required(VERSION 4.0)

project(
  AnimeHubDesktop
  VERSION 0.2.0
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
      /W4 /permissive- /Zc:__cplusplus /utf-8 /external:anglebrackets /external:W0)
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
  src/app/main.cpp
)

qt_add_qml_module(animehub-desktop
  URI AnimeHub
  VERSION 1.0
  QML_FILES
    qml/Main.qml
)

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

include(CTest)
if(BUILD_TESTING)
  add_subdirectory(tests)
endif()
