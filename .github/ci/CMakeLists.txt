cmake_minimum_required(VERSION 3.28)

if(NOT OBS_PLUGIN_TEMPLATE_DIR)
  message(FATAL_ERROR "OBS_PLUGIN_TEMPLATE_DIR must point to an obs-plugintemplate checkout")
endif()

file(TO_CMAKE_PATH "${OBS_PLUGIN_TEMPLATE_DIR}" OBS_PLUGIN_TEMPLATE_DIR)
list(APPEND CMAKE_MODULE_PATH "${OBS_PLUGIN_TEMPLATE_DIR}/cmake/common")
include("${OBS_PLUGIN_TEMPLATE_DIR}/cmake/common/bootstrap.cmake" NO_POLICY_SCOPE)

project(${_name} VERSION ${_version} LANGUAGES C)

if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Windows")
  list(APPEND CMAKE_MODULE_PATH "${OBS_PLUGIN_TEMPLATE_DIR}/cmake/windows")
elseif(CMAKE_HOST_SYSTEM_NAME STREQUAL "Linux")
  list(APPEND CMAKE_MODULE_PATH "${OBS_PLUGIN_TEMPLATE_DIR}/cmake/linux")
else()
  message(FATAL_ERROR "The CI build wrapper only supports Linux and Windows")
endif()

include(defaults)

enable_testing()
add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/../.." obs-spine-player)
