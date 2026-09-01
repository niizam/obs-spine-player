cmake_minimum_required(VERSION 3.22)

project(obs-spine-player VERSION 0.1.0 LANGUAGES C)

include(GNUInstallDirs)

find_package(libobs CONFIG REQUIRED)

add_library(obs-spine-player MODULE)
target_sources(
  obs-spine-player
  PRIVATE
    src/plugin-main.c
    src/animation-catalog.c
    src/animation-catalog.h
    src/browser-bridge.c
    src/browser-bridge.h
    src/level-gate.c
    src/level-gate.h
    src/spine-source.c
    src/spine-source.h
)
target_link_libraries(obs-spine-player PRIVATE OBS::libobs)
if(UNIX)
  target_link_libraries(obs-spine-player PRIVATE m)
endif()
target_compile_features(obs-spine-player PRIVATE c_std_11)
target_compile_definitions(obs-spine-player PRIVATE PLUGIN_VERSION="${PROJECT_VERSION}")

if(MSVC)
  target_compile_options(obs-spine-player PRIVATE /W4)
else()
  target_compile_options(obs-spine-player PRIVATE -Wall -Wextra -Wpedantic)
endif()

set_target_properties(obs-spine-player PROPERTIES PREFIX "")

if(WIN32)
  set(OBS_PLUGIN_BINARY_DESTINATION "obs-plugins/64bit")
  set(OBS_PLUGIN_DATA_DESTINATION "data/obs-plugins/obs-spine-player")
elseif(APPLE)
  set(OBS_PLUGIN_BINARY_DESTINATION "obs-plugins")
  set(OBS_PLUGIN_DATA_DESTINATION "data/obs-plugins/obs-spine-player")
else()
  set(OBS_PLUGIN_BINARY_DESTINATION "${CMAKE_INSTALL_LIBDIR}/obs-plugins")
  set(OBS_PLUGIN_DATA_DESTINATION "${CMAKE_INSTALL_DATADIR}/obs/obs-plugins/obs-spine-player")
endif()

install(
  TARGETS obs-spine-player
  LIBRARY DESTINATION "${OBS_PLUGIN_BINARY_DESTINATION}"
  RUNTIME DESTINATION "${OBS_PLUGIN_BINARY_DESTINATION}"
)
install(DIRECTORY data/ DESTINATION "${OBS_PLUGIN_DATA_DESTINATION}")
install(DIRECTORY characters/ DESTINATION "${OBS_PLUGIN_DATA_DESTINATION}/characters")

enable_testing()
find_program(NODE_EXECUTABLE node)
if(NODE_EXECUTABLE)
  file(GLOB PLAYER_TEST_FILES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/tests/*.test.js")
  add_test(NAME player-state-tests COMMAND "${NODE_EXECUTABLE}" --test ${PLAYER_TEST_FILES})
endif()

add_executable(level-gate-tests tests/level-gate-test.c src/level-gate.c)
target_include_directories(level-gate-tests PRIVATE src)
target_compile_features(level-gate-tests PRIVATE c_std_11)
if(UNIX)
  target_link_libraries(level-gate-tests PRIVATE m)
endif()
add_test(NAME level-gate-tests COMMAND level-gate-tests)

add_executable(animation-catalog-tests tests/animation-catalog-test.c src/animation-catalog.c)
target_include_directories(animation-catalog-tests PRIVATE src)
target_link_libraries(animation-catalog-tests PRIVATE OBS::libobs)
target_compile_features(animation-catalog-tests PRIVATE c_std_11)
target_compile_definitions(
  animation-catalog-tests
  PRIVATE
    TEST_CHARACTER_DIR="${CMAKE_CURRENT_SOURCE_DIR}/characters"
    TEST_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures"
)
add_test(NAME animation-catalog-tests COMMAND animation-catalog-tests)
