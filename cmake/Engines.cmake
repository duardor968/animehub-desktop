# Downloads, verifies and stages the bundled engines (aria2 and mpv) next to the executable.
# Every archive is pinned by URL and SHA-256; a mismatch fails the configure step. Archives are
# cached in ANIMEHUB_ENGINE_CACHE so presets and fresh configures reuse them offline.

set(ANIMEHUB_ENGINE_CACHE "${PROJECT_SOURCE_DIR}/out/engine-cache" CACHE PATH
  "Where pinned engine archives are cached")

set(ANIMEHUB_ARIA2_VERSION "1.37.0")
set(ANIMEHUB_ARIA2_ARCHIVE "aria2-1.37.0-win-64bit-build1.zip")
set(ANIMEHUB_ARIA2_URL "https://github.com/aria2/aria2/releases/download/release-1.37.0/${ANIMEHUB_ARIA2_ARCHIVE}")
set(ANIMEHUB_ARIA2_SHA256 "67d015301eef0b612191212d564c5bb0a14b5b9c4796b76454276a4d28d9b288")
set(ANIMEHUB_ARIA2_EXE_SHA256 "be2099c214f63a3cb4954b09a0becd6e2e34660b886d4c898d260febfe9d70c2")

set(ANIMEHUB_MPV_VERSION "0.41.0")
set(ANIMEHUB_MPV_COMMIT "41f6a645068483470267271e1d09966ca3b9f413")
set(ANIMEHUB_MPV_ARCHIVE "mpv-v0.41.0-x86_64-w64-mingw32.zip")
set(ANIMEHUB_MPV_URL "https://github.com/mpv-player/mpv/releases/download/v0.41.0/${ANIMEHUB_MPV_ARCHIVE}")
set(ANIMEHUB_MPV_SHA256 "a49811c0752c108b8260636f9c6f6fcb97406641c98b30f1e7b500dfb20177de")
set(ANIMEHUB_MPV_LICENSES
  "LICENSE.GPL|edaef632cbb643e4e7a221717a6c441a4c1a7c918e6e4d56debc3d8739b233f6"
  "Copyright|bfe9ee4cceabcb8ecbfadf208d04156f73d801e6a57369a5606bb8341e204a23"
)

function(animehub_fetch_pinned file_name url sha256 out_path)
  set(path "${ANIMEHUB_ENGINE_CACHE}/${file_name}")
  if(EXISTS "${path}")
    file(SHA256 "${path}" actual)
    if(NOT actual STREQUAL sha256)
      message(STATUS "Discarding ${file_name}: its SHA-256 does not match the pin")
      file(REMOVE "${path}")
    endif()
  endif()
  if(NOT EXISTS "${path}")
    message(STATUS "Downloading ${url}")
    file(DOWNLOAD "${url}" "${path}.partial"
      EXPECTED_HASH SHA256=${sha256}
      TLS_VERIFY ON
      STATUS status)
    list(GET status 0 code)
    if(NOT code EQUAL 0)
      file(REMOVE "${path}.partial")
      message(FATAL_ERROR "Could not download ${url}: ${status}")
    endif()
    file(RENAME "${path}.partial" "${path}")
  endif()
  set(${out_path} "${path}" PARENT_SCOPE)
endfunction()

function(animehub_stage_engines destination)
  set(stamp_value "${ANIMEHUB_ARIA2_SHA256};${ANIMEHUB_MPV_SHA256};${ANIMEHUB_MPV_LICENSES};strip-debug")
  set(stamp "${destination}/.pins")
  if(EXISTS "${stamp}")
    file(READ "${stamp}" previous)
    if(previous STREQUAL stamp_value AND EXISTS "${destination}/aria2/aria2c.exe"
       AND EXISTS "${destination}/mpv/mpv.exe")
      return()
    endif()
  endif()

  file(MAKE_DIRECTORY "${ANIMEHUB_ENGINE_CACHE}")
  file(REMOVE_RECURSE "${destination}")
  set(work "${CMAKE_CURRENT_BINARY_DIR}/engine-staging")
  file(REMOVE_RECURSE "${work}")

  # aria2: a single static executable plus its licenses.
  animehub_fetch_pinned("${ANIMEHUB_ARIA2_ARCHIVE}" "${ANIMEHUB_ARIA2_URL}" "${ANIMEHUB_ARIA2_SHA256}" aria2_zip)
  file(ARCHIVE_EXTRACT INPUT "${aria2_zip}" DESTINATION "${work}/aria2")
  set(aria2_dir "${work}/aria2/aria2-1.37.0-win-64bit-build1")
  file(SHA256 "${aria2_dir}/aria2c.exe" aria2_exe_sha)
  if(NOT aria2_exe_sha STREQUAL ANIMEHUB_ARIA2_EXE_SHA256)
    message(FATAL_ERROR "aria2c.exe does not match its pinned SHA-256")
  endif()
  file(COPY "${aria2_dir}/aria2c.exe" "${aria2_dir}/COPYING" "${aria2_dir}/LICENSE.OpenSSL"
       "${aria2_dir}/AUTHORS" DESTINATION "${destination}/aria2")

  # mpv: the official release wraps the build in a second zip.
  animehub_fetch_pinned("${ANIMEHUB_MPV_ARCHIVE}" "${ANIMEHUB_MPV_URL}" "${ANIMEHUB_MPV_SHA256}" mpv_zip)
  file(ARCHIVE_EXTRACT INPUT "${mpv_zip}" DESTINATION "${work}/mpv-outer")
  file(GLOB inner "${work}/mpv-outer/*.zip")
  list(LENGTH inner inner_count)
  if(NOT inner_count EQUAL 1)
    message(FATAL_ERROR "Unexpected layout in ${ANIMEHUB_MPV_ARCHIVE}")
  endif()
  file(ARCHIVE_EXTRACT INPUT "${inner}" DESTINATION "${work}/mpv")
  file(GLOB mpv_files "${work}/mpv/*.exe" "${work}/mpv/*.com" "${work}/mpv/*.dll")
  # The build embeds its debug information, over a third of its size, which playing never uses.
  # MinGW's strip removes it: Qt's installer puts one in Tools; GitHub's runners have one on PATH.
  file(GLOB qt_mingw_bins "${QT6_INSTALL_PREFIX}/../../Tools/mingw*/bin")
  find_program(ANIMEHUB_STRIP strip HINTS ${qt_mingw_bins} REQUIRED)
  execute_process(COMMAND "${ANIMEHUB_STRIP}" --strip-debug ${mpv_files} COMMAND_ERROR_IS_FATAL ANY)
  file(COPY ${mpv_files} DESTINATION "${destination}/mpv")
  foreach(entry IN LISTS ANIMEHUB_MPV_LICENSES)
    string(REPLACE "|" ";" parts "${entry}")
    list(GET parts 0 name)
    list(GET parts 1 sha)
    animehub_fetch_pinned("mpv-${ANIMEHUB_MPV_COMMIT}-${name}.txt"
      "https://raw.githubusercontent.com/mpv-player/mpv/${ANIMEHUB_MPV_COMMIT}/${name}" "${sha}" license)
    file(COPY_FILE "${license}" "${destination}/mpv/${name}.txt")
  endforeach()

  file(REMOVE_RECURSE "${work}")
  file(WRITE "${stamp}" "${stamp_value}")
endfunction()
