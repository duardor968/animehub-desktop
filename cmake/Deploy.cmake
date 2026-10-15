# Makes the Release build folder a complete installation: Qt libraries, plugins and QML modules
# next to AnimeHub.exe (windeployqt), plus the MSVC runtime, so it runs without Qt or a Visual C++
# redistributable installed. scripts/package.ps1 packs it into the release zip and installer.

function(animehub_deploy_runtime target)
  get_target_property(qmake Qt6::qmake IMPORTED_LOCATION)
  get_filename_component(qt_bin "${qmake}" DIRECTORY)
  find_program(ANIMEHUB_WINDEPLOYQT windeployqt HINTS "${qt_bin}" NO_DEFAULT_PATH REQUIRED)

  # The MSVC runtime the compiler links against, located by CMake itself.
  set(CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS_SKIP TRUE)
  set(CMAKE_INSTALL_UCRT_LIBRARIES FALSE)
  include(InstallRequiredSystemLibraries)

  set(unused)
  foreach(path IN ITEMS qmltooling generic networkinformation
               sqldrivers/qsqlibase.dll sqldrivers/qsqlmimer.dll sqldrivers/qsqloci.dll
               sqldrivers/qsqlodbc.dll sqldrivers/qsqlpsql.dll
               tls/qcertonlybackend.dll tls/qopensslbackend.dll
               imageformats/qicns.dll imageformats/qtga.dll imageformats/qtiff.dll
               imageformats/qwbmp.dll
               qml/QtQuick/Controls/Imagine qml/QtQuick/Controls/Material
               qml/QtQuick/Controls/Universal qml/QtQuick/Controls/Windows qml/QtQuick/NativeStyle
               Qt6QuickControls2Imagine.dll Qt6QuickControls2ImagineStyleImpl.dll
               Qt6QuickControls2Material.dll Qt6QuickControls2MaterialStyleImpl.dll
               Qt6QuickControls2Universal.dll Qt6QuickControls2UniversalStyleImpl.dll
               Qt6QuickControls2WindowsStyleImpl.dll Qt6Quick3DUtils.dll
               concrt140.dll msvcp140_atomic_wait.dll msvcp140_codecvt_ids.dll)
    list(APPEND unused "$<TARGET_FILE_DIR:${target}>/${path}")
  endforeach()

  add_custom_command(TARGET ${target} POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E env QT_COMMAND_LINE_PARSER_NO_GUI_MESSAGE_BOXES=1
            "${ANIMEHUB_WINDEPLOYQT}"
            --release
            --qmldir "${PROJECT_SOURCE_DIR}/qml"
            --no-translations
            --no-system-d3d-compiler
            --no-system-dxc-compiler
            --no-opengl-sw
            --no-compiler-runtime
            --no-ffmpeg
            --verbose 0
            "$<TARGET_FILE:${target}>"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different ${CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS}
            "$<TARGET_FILE_DIR:${target}>"
    # windeployqt 6.11 ignores --skip-plugin-types and --exclude-plugins: drop what AnimeHub does
    # not use (QML debugging, touch, reachability, other databases, OpenSSL, unused images, the
    # Controls styles other than FluentWinUI3 and the Fusion and Basic it builds on, and the parts
    # of the MSVC runtime nothing links).
    COMMAND "${CMAKE_COMMAND}" -E rm -rf ${unused}
    COMMENT "Deploying the Qt and MSVC runtime next to AnimeHub.exe"
    VERBATIM
  )
endfunction()
