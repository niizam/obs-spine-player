# Builds vendored spine-c runtimes. Each Spine version family gets its own copy of
# spine-c whose external symbols are renamed by a generated forced-include header,
# so several versions can be linked into the one plugin module.

function(spine_runtime_sources version out_var)
  file(GLOB sources CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/spine-c-${version}/src/spine/*.c")
  set(${out_var} ${sources} PARENT_SCOPE)
endfunction()

function(spine_runtime_compile_options target)
  if(MSVC)
    target_compile_options(${target} PRIVATE /W0)
  else()
    target_compile_options(${target} PRIVATE -w)
  endif()
  target_compile_features(${target} PRIVATE c_std_11)
  set_target_properties(${target} PROPERTIES POSITION_INDEPENDENT_CODE ON)
endfunction()

# Unprefixed build of a vendored runtime. Only used to dump its symbol table when
# regenerating the prefix header (see tools/generate-spine-prefix.js).
function(spine_add_raw_runtime version)
  spine_runtime_sources(${version} sources)
  string(REPLACE "." "" tag "${version}")
  add_library(spine-c-${tag}-raw STATIC EXCLUDE_FROM_ALL ${sources})
  target_include_directories(spine-c-${tag}-raw PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/third_party/spine-c-${version}/include")
  spine_runtime_compile_options(spine-c-${tag}-raw)
endfunction()
