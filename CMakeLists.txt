cmake_minimum_required(VERSION 4.0)

project(
  AnimeHubDesktop
  VERSION 0.1.0
  DESCRIPTION "Native AnimeHub desktop application"
  LANGUAGES CXX
)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(ANIMEHUB_ENABLE_CLANG_TIDY "Run clang-tidy while compiling" OFF)

find_package(Qt6 6.11 REQUIRED COMPONENTS Core Gui Qml Quick QuickControls2 Test)

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

qt_add_executable(animehub-desktop
  WIN32
  src/main.cpp
  src/app_info.h
)

qt_add_qml_module(animehub-desktop
  URI AnimeHub
  VERSION 1.0
  QML_FILES
    qml/Main.qml
)

target_link_libraries(animehub-desktop
  PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Qml
    Qt6::Quick
    Qt6::QuickControls2
)

if(MSVC)
  target_compile_options(animehub-desktop PRIVATE /W4 /permissive- /Zc:__cplusplus)
else()
  target_compile_options(animehub-desktop PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(ANIMEHUB_ENABLE_CLANG_TIDY)
  find_program(CLANG_TIDY_EXECUTABLE NAMES clang-tidy REQUIRED)
  set(ANIMEHUB_CLANG_TIDY_COMMAND
    "${CLANG_TIDY_EXECUTABLE};--extra-arg-before=/std:c++latest;--extra-arg-before=/Zc:__cplusplus"
  )
  set_target_properties(animehub-desktop PROPERTIES CXX_CLANG_TIDY "${ANIMEHUB_CLANG_TIDY_COMMAND}")
endif()

set_target_properties(animehub-desktop PROPERTIES
  OUTPUT_NAME "AnimeHub"
  WIN32_EXECUTABLE TRUE
  MACOSX_BUNDLE TRUE
  MACOSX_BUNDLE_BUNDLE_NAME "AnimeHub"
  MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
)

include(CTest)
if(BUILD_TESTING)
  add_subdirectory(tests)
endif()

install(TARGETS animehub-desktop
  BUNDLE DESTINATION .
  RUNTIME DESTINATION bin
)
