cmake_minimum_required(VERSION 3.22)

project(obs-spine-player VERSION 0.2.0 LANGUAGES C)

include(GNUInstallDirs)

find_package(libobs CONFIG REQUIRED)

enable_testing()
find_program(NODE_EXECUTABLE node)

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/spine-runtime.cmake")
spine_add_raw_runtime(3.7)
spine_add_raw_runtime(4.0)
spine_add_raw_runtime(4.1)
spine_add_backend(VERSION 3.7 PREFIX sp37_ SOURCES src/native/backend.c)
spine_add_backend(VERSION 4.0 PREFIX sp40_ SOURCES src/native/backend.c)
spine_add_backend(VERSION 4.1 PREFIX sp41_ SOURCES src/native/backend.c)

add_library(obs-spine-player MODULE)
target_sources(
  obs-spine-player
  PRIVATE
    src/plugin-main.c
    src/animation-catalog.c
    src/animation-catalog.h
    src/browser-bridge.c
    src/browser-bridge.h
    src/cursor-input.c
    src/cursor-input.h
    src/level-gate.c
    src/level-gate.h
    src/skeleton-binary.c
    src/skeleton-binary.h
    src/spine-source.c
    src/spine-source.h
    src/native/atlas-image.c
    src/native/atlas-image.h
    src/native/skeleton-version.c
    src/native/skeleton-version.h
    src/native/draw-list.c
    src/native/draw-list.h
    src/native/spine-backend.h
    src/native/framing.c
    src/native/framing.h
    src/native/load-gate.h
    src/native/native-renderer.c
    src/native/native-renderer.h
    src/native/skeleton-desc.h
    src/native/mouth-plan.c
    src/native/mouth-plan.h
    src/native/state-machine.c
    src/native/state-machine.h
    src/native/eye-tracker.c
    src/native/eye-tracker.h
    src/native/antialiasing.c
    src/native/antialiasing.h
)
target_include_directories(obs-spine-player PRIVATE src src/native)
target_link_libraries(obs-spine-player PRIVATE OBS::libobs spine-backend-37 spine-backend-40 spine-backend-41)
if(WIN32)
  target_link_libraries(obs-spine-player PRIVATE user32)
elseif(APPLE)
  find_library(CORE_GRAPHICS_FRAMEWORK CoreGraphics REQUIRED)
  target_link_libraries(obs-spine-player PRIVATE "${CORE_GRAPHICS_FRAMEWORK}")
elseif(UNIX)
  find_package(X11 REQUIRED)
  target_link_libraries(obs-spine-player PRIVATE X11::X11)
endif()
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
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/characters")
  install(DIRECTORY characters/ DESTINATION "${OBS_PLUGIN_DATA_DESTINATION}/characters")
endif()

if(NODE_EXECUTABLE)
  file(GLOB PLAYER_TEST_FILES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/tests/*.test.js")
  add_test(NAME player-state-tests COMMAND "${NODE_EXECUTABLE}" --test ${PLAYER_TEST_FILES})
endif()

add_executable(atlas-image-tests tests/atlas-image-test.c src/native/atlas-image.c)
target_include_directories(atlas-image-tests PRIVATE src/native tests)
target_compile_features(atlas-image-tests PRIVATE c_std_11)
if(UNIX)
  target_link_libraries(atlas-image-tests PRIVATE m)
endif()
add_test(NAME atlas-image-tests COMMAND atlas-image-tests)

add_executable(skeleton-version-tests tests/skeleton-version-test.c src/native/skeleton-version.c src/skeleton-binary.c)
target_include_directories(skeleton-version-tests PRIVATE src src/native tests)
target_compile_features(skeleton-version-tests PRIVATE c_std_11)
target_compile_definitions(skeleton-version-tests PRIVATE TEST_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures")
add_test(NAME skeleton-version-tests COMMAND skeleton-version-tests)

# One backend test executable per Spine version, sharing tests/backend-test.c.
function(spine_add_backend_test tag version)
  add_executable(backend-${tag}-tests tests/backend-test.c src/native/atlas-image.c src/native/draw-list.c src/skeleton-binary.c)
  target_include_directories(backend-${tag}-tests PRIVATE src/native tests tests/fixtures)
  target_link_libraries(backend-${tag}-tests PRIVATE spine-backend-${tag})
  target_compile_features(backend-${tag}-tests PRIVATE c_std_11)
  target_compile_definitions(
    backend-${tag}-tests
    PRIVATE
      TEST_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures"
      TEST_OUTPUT_DIR="${CMAKE_CURRENT_BINARY_DIR}/backend-${tag}"
      FIXTURE_VERSION_DIR="spine${tag}"
      REF_HEADER="spine${tag}/reference.h"
      BACKEND_ENTRY=spine_backend_${tag}
      BACKEND_NAME="${version}"
  )
  file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/backend-${tag}")
  if(UNIX)
    target_link_libraries(backend-${tag}-tests PRIVATE m pthread)
  endif()
  add_test(NAME backend-${tag}-tests COMMAND backend-${tag}-tests)
endfunction()
spine_add_backend_test(37 3.7)
spine_add_backend_test(40 4.0)
spine_add_backend_test(41 4.1)

add_executable(framing-tests tests/framing-test.c src/native/framing.c)
target_include_directories(framing-tests PRIVATE src/native tests)
target_compile_features(framing-tests PRIVATE c_std_11)
if(UNIX)
  target_link_libraries(framing-tests PRIVATE m)
endif()
add_test(NAME framing-tests COMMAND framing-tests)

add_executable(mouth-plan-tests tests/mouth-plan-test.c src/native/mouth-plan.c)
target_include_directories(mouth-plan-tests PRIVATE src/native tests)
target_compile_features(mouth-plan-tests PRIVATE c_std_11)
if(UNIX)
  target_link_libraries(mouth-plan-tests PRIVATE m)
endif()
add_test(NAME mouth-plan-tests COMMAND mouth-plan-tests)

add_executable(state-machine-tests tests/state-machine-test.c src/native/state-machine.c src/native/mouth-plan.c)
target_include_directories(state-machine-tests PRIVATE src/native tests)
target_compile_features(state-machine-tests PRIVATE c_std_11)
if(UNIX)
  target_link_libraries(state-machine-tests PRIVATE m)
endif()
add_test(NAME state-machine-tests COMMAND state-machine-tests)

add_executable(eye-tracker-tests tests/eye-tracker-test.c src/native/eye-tracker.c)
target_include_directories(eye-tracker-tests PRIVATE src/native tests)
target_compile_features(eye-tracker-tests PRIVATE c_std_11)
if(UNIX)
  target_link_libraries(eye-tracker-tests PRIVATE m)
endif()
add_test(NAME eye-tracker-tests COMMAND eye-tracker-tests)

add_executable(antialiasing-tests tests/antialiasing-test.c src/native/antialiasing.c)
target_include_directories(antialiasing-tests PRIVATE src/native tests)
target_compile_features(antialiasing-tests PRIVATE c_std_11)
if(UNIX)
  target_link_libraries(antialiasing-tests PRIVATE m)
endif()
add_test(NAME antialiasing-tests COMMAND antialiasing-tests)

add_executable(level-gate-tests tests/level-gate-test.c src/level-gate.c)
target_include_directories(level-gate-tests PRIVATE src)
target_compile_features(level-gate-tests PRIVATE c_std_11)
if(UNIX)
  target_link_libraries(level-gate-tests PRIVATE m)
endif()
add_test(NAME level-gate-tests COMMAND level-gate-tests)

add_executable(skeleton-binary-tests tests/skeleton-binary-test.c src/skeleton-binary.c)
target_include_directories(skeleton-binary-tests PRIVATE src)
target_compile_features(skeleton-binary-tests PRIVATE c_std_11)
add_test(NAME skeleton-binary-tests COMMAND skeleton-binary-tests)

add_executable(animation-catalog-tests tests/animation-catalog-test.c src/animation-catalog.c src/skeleton-binary.c)
target_include_directories(animation-catalog-tests PRIVATE src)
target_link_libraries(animation-catalog-tests PRIVATE OBS::libobs)
target_compile_features(animation-catalog-tests PRIVATE c_std_11)
target_compile_definitions(
  animation-catalog-tests
  PRIVATE
    TEST_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures"
)
add_test(NAME animation-catalog-tests COMMAND animation-catalog-tests)
