# Downloads a file pinned by URL and SHA-256 into ANIMEHUB_ENGINE_CACHE, so presets and fresh
# configures reuse it offline; a mismatch fails the configure step.

set(ANIMEHUB_ENGINE_CACHE "${PROJECT_SOURCE_DIR}/out/engine-cache" CACHE PATH
  "Where pinned engine archives are cached")

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
