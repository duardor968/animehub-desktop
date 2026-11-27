# Standalone zero-Qt Win32 updater helper (~150-250 KB, zero network code).
# Verifies staged SHA-256 via Windows CNG BCrypt, swaps files atomically on the same volume,
# waits for AnimeHub.exe post-update healthcheck event, and rolls back binaries + SQLite DB on crash.

configure_file(
  "${CMAKE_CURRENT_SOURCE_DIR}/updater.rc.in"
  "${CMAKE_CURRENT_BINARY_DIR}/updater.rc"
  @ONLY
)
set_source_files_properties("${CMAKE_CURRENT_BINARY_DIR}/updater.rc" PROPERTIES
  OBJECT_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/updater.manifest"
)

add_executable(animehub-updater WIN32
  main.cpp
  "${CMAKE_CURRENT_BINARY_DIR}/updater.rc"
)

set_target_properties(animehub-updater PROPERTIES
  OUTPUT_NAME "AnimeHubUpdater"
  CXX_STANDARD 26
  CXX_STANDARD_REQUIRED ON
  CXX_EXTENSIONS OFF
  AUTOMOC OFF
  AUTOUIC OFF
  AUTORCC OFF
)

if(MSVC)
  target_compile_options(animehub-updater PRIVATE
    /W4 /permissive- /Zc:__cplusplus /utf-8 /EHsc
  )
  target_link_options(animehub-updater PRIVATE /MANIFEST:NO)
else()
  target_compile_options(animehub-updater PRIVATE -Wall -Wextra -Wpedantic)
endif()

target_compile_definitions(animehub-updater PRIVATE
  UNICODE
  _UNICODE
  WIN32_LEAN_AND_MEAN
  NOMINMAX
)

target_link_libraries(animehub-updater PRIVATE
  bcrypt
  advapi32
  shell32
)
