qt_add_library(animehub-core STATIC
  api/api_client.cpp
  api/api_client.h
  api/api_json.cpp
  api/api_json.h
  api/api_types.cpp
  api/api_types.h
  api/response_cache.cpp
  api/response_cache.h
  downloads/click_n_load.cpp
  downloads/click_n_load.h
  downloads/aria2.cpp
  downloads/aria2.h
  downloads/download_manager.cpp
  downloads/download_manager.h
  downloads/download_repository.cpp
  downloads/download_repository.h
  downloads/download_types.h
  downloads/mega.cpp
  downloads/mega.h
  downloads/myjdownloader.cpp
  downloads/myjdownloader.h
  downloads/pixeldrain.cpp
  downloads/pixeldrain.h
  library/library_service.cpp
  library/library_service.h
  library/library_repository.cpp
  library/library_repository.h
  library/library_types.cpp
  library/library_types.h
  playback/player.cpp
  playback/player.h
  platform/aes.h
  platform/autostart.h
  platform/credentials.h
  platform/process_job.h
  platform/trash.h
  storage/database.cpp
  storage/database.h
  util/file_names.cpp
  util/file_names.h
)

if(WIN32)
  target_sources(animehub-core PRIVATE
    platform/aes_win.cpp platform/autostart_win.cpp platform/credentials_win.cpp
    platform/process_job_win.cpp platform/trash_win.cpp)
  target_link_libraries(animehub-core PRIVATE bcrypt advapi32)
else()
  message(FATAL_ERROR "AnimeHub Desktop needs a platform AES backend for this OS.")
endif()

target_include_directories(animehub-core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(animehub-core
  PUBLIC
    Qt6::Core
    Qt6::Network
    Qt6::Sql
)
target_compile_definitions(animehub-core PUBLIC
  ANIMEHUB_VERSION="${PROJECT_VERSION}"
)
animehub_configure_target(animehub-core)
