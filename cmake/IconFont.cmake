# The icon font on Linux. Segoe Fluent Icons comes with Windows 11 and Microsoft does not allow
# redistributing it; Uno Fluent Icons (Apache-2.0) draws the same code points in the same style.
# The font and its license are pinned by commit and SHA-256 and embedded at :/fonts.

include("${CMAKE_CURRENT_LIST_DIR}/Pinned.cmake")

set(ANIMEHUB_ICON_FONT_COMMIT "f2a721469ab98f39b369961cb24339d7a90e39e8")
set(ANIMEHUB_ICON_FONT_URL
  "https://raw.githubusercontent.com/unoplatform/uno.fonts/${ANIMEHUB_ICON_FONT_COMMIT}")
set(ANIMEHUB_ICON_FONT_SHA256 "4b81f01d293bf23093c79f9c4588444dd97cde006220f8fc01488dd530c11006")
set(ANIMEHUB_ICON_FONT_LICENSE_SHA256
  "7afed34064d93afd447bb2ef72398f68789792e302b0792d1bcc3217b204f88e")

# Also leaves the font's license in <build>/icon-font for the package.
function(animehub_add_icon_font target)
  file(MAKE_DIRECTORY "${ANIMEHUB_ENGINE_CACHE}")
  animehub_fetch_pinned("uno-fluentui-assets-${ANIMEHUB_ICON_FONT_COMMIT}.ttf"
    "${ANIMEHUB_ICON_FONT_URL}/webfonts/Uno%20Fluent%20Icons/uno-fluentui-assets.ttf"
    "${ANIMEHUB_ICON_FONT_SHA256}" font)
  animehub_fetch_pinned("uno-fonts-${ANIMEHUB_ICON_FONT_COMMIT}-LICENSE.md"
    "${ANIMEHUB_ICON_FONT_URL}/LICENSE.md" "${ANIMEHUB_ICON_FONT_LICENSE_SHA256}" license)
  set(staged "${PROJECT_BINARY_DIR}/icon-font")
  file(MAKE_DIRECTORY "${staged}")
  file(COPY_FILE "${font}" "${staged}/uno-fluentui-assets.ttf" ONLY_IF_DIFFERENT)
  file(COPY_FILE "${license}" "${staged}/LICENSE.md" ONLY_IF_DIFFERENT)
  qt_add_resources(${target} icon_font
    PREFIX "/fonts"
    BASE "${staged}"
    FILES "${staged}/uno-fluentui-assets.ttf"
  )
endfunction()
