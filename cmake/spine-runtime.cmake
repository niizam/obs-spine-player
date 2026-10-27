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

# Prefixed runtime plus its version adapter, as one static library. Every source is
# compiled with the generated rename header forced in, and a CTest check verifies
# that nothing in the library defines an external symbol without the prefix.
function(spine_add_backend)
  cmake_parse_arguments(SPINE "" "VERSION;PREFIX" "SOURCES" ${ARGN})
  string(REPLACE "." "" tag "${SPINE_VERSION}")
  set(target spine-backend-${tag})
  set(prefix_header "${CMAKE_CURRENT_SOURCE_DIR}/src/native/spine${tag}-prefix.h")
  spine_runtime_sources(${SPINE_VERSION} runtime_sources)

  add_library(${target} STATIC ${runtime_sources} ${SPINE_SOURCES})
  target_include_directories(${target} PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src/native")
  # src/ of the runtime exposes its private Json.h, used to pre-validate JSON exports.
  target_include_directories(${target} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/third_party/spine-c-${SPINE_VERSION}/include"
                                               "${CMAKE_CURRENT_SOURCE_DIR}/third_party/spine-c-${SPINE_VERSION}/src"
                                               "${CMAKE_CURRENT_SOURCE_DIR}/src")
  target_compile_features(${target} PRIVATE c_std_11)
  target_compile_definitions(${target} PRIVATE SPINE_BACKEND_VERSION=${tag})
  set_target_properties(${target} PROPERTIES POSITION_INDEPENDENT_CODE ON)
  if(MSVC)
    target_compile_options(${target} PRIVATE "/FI${prefix_header}")
    # Upstream code is built quietly; the adapter is ours and keeps the project's level.
    set_source_files_properties(${runtime_sources} TARGET_DIRECTORY ${target} PROPERTIES COMPILE_OPTIONS "/W0")
    set_source_files_properties(${SPINE_SOURCES} TARGET_DIRECTORY ${target} PROPERTIES COMPILE_OPTIONS "/W4")
  else()
    target_compile_options(${target} PRIVATE -include "${prefix_header}")
    set_source_files_properties(${runtime_sources} TARGET_DIRECTORY ${target} PROPERTIES COMPILE_OPTIONS "-w")
    set_source_files_properties(${SPINE_SOURCES} TARGET_DIRECTORY ${target} PROPERTIES COMPILE_OPTIONS "-Wall;-Wextra;-Wpedantic")
  endif()

  if(NODE_EXECUTABLE)
    if(MSVC)
      get_filename_component(compiler_dir "${CMAKE_C_COMPILER}" DIRECTORY)
      find_program(SPINE_SYMBOL_TOOL dumpbin HINTS "${compiler_dir}")
    else()
      set(SPINE_SYMBOL_TOOL "${CMAKE_NM}")
    endif()
    if(SPINE_SYMBOL_TOOL)
      add_test(
        NAME spine${tag}-prefix-check
        COMMAND "${NODE_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/check-spine-prefix.js" --tool "${SPINE_SYMBOL_TOOL}"
                --prefix ${SPINE_PREFIX} --allow spine_backend_${tag} "$<TARGET_FILE:${target}>"
      )
    else()
      message(WARNING "No dumpbin/nm found; spine${tag}-prefix-check is not registered")
    endif()
  endif()
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
