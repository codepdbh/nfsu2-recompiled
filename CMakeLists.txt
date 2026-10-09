cmake_minimum_required(VERSION 3.20)
project(NFSU2Recompiled C)

# Windows bring-up host: 32-bit, MSVC (native32's bridge is MSVC x86 inline asm).
if(NOT MSVC OR NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
  message(FATAL_ERROR "Build with MSVC x86 (source scripts/vsenv.sh x86; cmake -G Ninja)")
endif()

set(PCRECOMP "${CMAKE_SOURCE_DIR}/../pcrecomp" CACHE PATH "pcrecomp checkout (with patches/pcrecomp applied)")
set(GEN_DIR "${CMAKE_SOURCE_DIR}/src/recomp/gen")
set(NFS_GEN_OPT "/O1" CACHE STRING "optimisation for the generated C")
set(NFS_GEN_HEAVY_OPT "/O1" CACHE STRING "optimisation for the very large generated TUs")
set(NFS_GEN_HEAVY_LINES 300000 CACHE STRING "a generated TU with more lines than this is 'heavy'")

file(GLOB GEN_SOURCES "${GEN_DIR}/recomp_*.c")
if(NOT GEN_SOURCES)
  message(FATAL_ERROR "No generated code in ${GEN_DIR}: run scripts/run_lift.py lift first")
endif()

set(RT "${PCRECOMP}/runtime")
set(COMMON_DEFS RECOMP_FLAT_MEMORY RECOMP_STRICT_ICALL RECOMP_NATIVE_RANGE _CRT_SECURE_NO_WARNINGS)
set(COMMON_INC "${RT}/recomp32" "${RT}/native32" "${RT}/hybrid" "${CMAKE_SOURCE_DIR}/src/runtime"
               "${CMAKE_SOURCE_DIR}/src/platform" "${GEN_DIR}")

# ---- generated code ----
add_library(nfs_gen OBJECT ${GEN_SOURCES})
target_compile_definitions(nfs_gen PRIVATE ${COMMON_DEFS})
target_include_directories(nfs_gen PRIVATE ${COMMON_INC})
target_compile_options(nfs_gen PRIVATE /w /bigobj /GS- /fp:precise)
foreach(src ${GEN_SOURCES})
  file(STRINGS "${src}" _lines)
  list(LENGTH _lines _n)
  if(_n GREATER NFS_GEN_HEAVY_LINES)
    set_source_files_properties("${src}" PROPERTIES COMPILE_OPTIONS "${NFS_GEN_HEAVY_OPT}")
  else()
    set_source_files_properties("${src}" PROPERTIES COMPILE_OPTIONS "${NFS_GEN_OPT}")
  endif()
endforeach()

# ---- host ----
add_executable(NFSU2-Recompiled
  src/host/main.c
  src/host/profiler.c
  src/runtime/nfs_runtime.c
  src/runtime/overrides.c src/runtime/widescreen.c
  src/runtime/widescreen.c
  src/runtime/d3d9_trace.c
  src/runtime/dinput_trace.c
  src/runtime/native_range.c
  ${RT}/hybrid/hybrid.c
  src/shims/win32_shims.c
  src/platform/nfs_log.c
  ${RT}/native32/native32.c
  ${RT}/recomp32/image_loader.c
  ${RT}/recomp32/recomp_trace.c
  $<TARGET_OBJECTS:nfs_gen>)
target_compile_definitions(NFSU2-Recompiled PRIVATE ${COMMON_DEFS})
target_include_directories(NFSU2-Recompiled PRIVATE ${COMMON_INC})
target_compile_options(NFSU2-Recompiled PRIVATE /W3 /Zi)
set_source_files_properties(src/host/main.c src/runtime/nfs_runtime.c src/runtime/overrides.c src/runtime/widescreen.c
  src/shims/win32_shims.c src/platform/nfs_log.c PROPERTIES COMPILE_OPTIONS /O2)
# Base away from the guest image (0x400000-0x932000), no ASLR for a stable
# layout, a large stack because every guest call is a host call.
target_link_options(NFSU2-Recompiled PRIVATE
  /BASE:0x60000000 /DYNAMICBASE:NO /LARGEADDRESSAWARE /STACK:0x100000
  /DEBUG /SUBSYSTEM:CONSOLE /INCREMENTAL:NO)
target_link_libraries(NFSU2-Recompiled PRIVATE kernel32 user32 winmm dbghelp)
