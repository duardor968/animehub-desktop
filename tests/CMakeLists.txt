qt_add_executable(animehub-desktop-tests
  app_info_test.cpp
  ../src/app_info.h
)

target_include_directories(animehub-desktop-tests PRIVATE ../src)
target_link_libraries(animehub-desktop-tests PRIVATE Qt6::Core Qt6::Test)

if(MSVC)
  target_compile_options(animehub-desktop-tests PRIVATE /W4 /permissive- /Zc:__cplusplus)
else()
  target_compile_options(animehub-desktop-tests PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(ANIMEHUB_ENABLE_CLANG_TIDY)
  set_target_properties(animehub-desktop-tests PROPERTIES CXX_CLANG_TIDY "${ANIMEHUB_CLANG_TIDY_COMMAND}")
endif()

add_test(NAME app-info COMMAND animehub-desktop-tests)
