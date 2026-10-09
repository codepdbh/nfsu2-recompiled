cmake_minimum_required(VERSION 3.22)
project(NFSU2AndroidBootstrap C CXX)
if(NOT ANDROID OR NOT ANDROID_ABI STREQUAL "arm64-v8a")
    message(FATAL_ERROR "This target requires Android arm64-v8a")
endif()
add_library(native_app_glue STATIC "${ANDROID_NDK}/sources/android/native_app_glue/android_native_app_glue.c")
target_include_directories(native_app_glue PUBLIC "${ANDROID_NDK}/sources/android/native_app_glue")
get_filename_component(NFS_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../../../.." ABSOLUTE)
set(PCRECOMP "${NFS_ROOT}/../pcrecomp")
set(DXVK_SOURCE "${NFS_ROOT}/build/dxvk-source" CACHE PATH "Prepared DXVK native source")
set(DXVK_ANDROID_STATIC ON CACHE BOOL "Link into application" FORCE)
add_subdirectory("${NFS_ROOT}/ports/dxvk_android" "${CMAKE_CURRENT_BINARY_DIR}/dxvk")
set(GEN_DIR "${NFS_ROOT}/src/recomp/gen")
file(GLOB GEN_SOURCES CONFIGURE_DEPENDS "${GEN_DIR}/recomp_*.c")
if(NOT GEN_SOURCES)
    message(FATAL_ERROR "Generate the local game C first; see the project README")
endif()
# Very large translation units leave one compiler working long after all other
# jobs finish. Split only those units, preserving complete functions and headers.
set(ANDROID_GEN_SOURCES)
foreach(source IN LISTS GEN_SOURCES)
    file(SIZE "${source}" source_bytes)
    if(source_bytes GREATER 67108864)
        find_package(Python3 REQUIRED COMPONENTS Interpreter)
        set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${source}" "${NFS_ROOT}/scripts/split_android_sources.py")
        execute_process(COMMAND "${Python3_EXECUTABLE}" "${NFS_ROOT}/scripts/split_android_sources.py"
            "${source}" "${CMAKE_CURRENT_BINARY_DIR}/split_guest"
            RESULT_VARIABLE split_status OUTPUT_VARIABLE split_sources OUTPUT_STRIP_TRAILING_WHITESPACE)
        if(NOT split_status EQUAL 0)
            message(FATAL_ERROR "Failed to split lifted Android source: ${source}")
        endif()
        string(REPLACE "\n" ";" split_sources "${split_sources}")
        list(APPEND ANDROID_GEN_SOURCES ${split_sources})
    else()
        list(APPEND ANDROID_GEN_SOURCES "${source}")
    endif()
endforeach()
set_property(GLOBAL PROPERTY JOB_POOLS guest_compile=4)
add_library(nfs_guest OBJECT ${ANDROID_GEN_SOURCES})
set_property(TARGET nfs_guest PROPERTY JOB_POOL_COMPILE guest_compile)
target_include_directories(nfs_guest PRIVATE "${PCRECOMP}/runtime/recomp32" "${GEN_DIR}")
# RECOMP_LOCAL_REGS: guest registers live in locals of each lifted function
# (written back at every call and return) so they stay in ARM64 registers.
# RECOMP_MEM_QUAL=: guest memory accesses are not volatile; -fno-strict-aliasing
# keeps the untyped guest memory correct, and lifted loops still make an opaque
# call per back-edge. -fwrapv: x86 integer arithmetic wraps. Both validated by
# scripts/difftest_real.py --cc clang-o2 --local-regs (same result as MSVC).
# RECOMP_X87_WINDOW: the x87 stack is a sliding window (push/pop move one
# index instead of shifting 16 values). Every unit of the library must agree,
# so nfsu2_android (runtime, overrides, widescreen) defines it too.
target_compile_definitions(nfs_guest PRIVATE RECOMP_STRICT_ICALL RECOMP_LOCAL_REGS RECOMP_X87_WINDOW "RECOMP_MEM_QUAL=")
# Optimize the ARM64 game while avoiding aliasing assumptions in x86 accesses;
# retain unwind tables so unsupported HLE paths can exit to the host cleanly.
target_compile_options(nfs_guest PRIVATE -O2 -w -fwrapv -fno-strict-aliasing -fexceptions -ffp-contract=off
    -include "${CMAKE_CURRENT_SOURCE_DIR}/arm64_guest.h")
# The ~27,700 lifted functions are internal to the library: hidden visibility
# turns their cross-file calls from PLT stubs into direct calls, and ThinLTO
# (as in the MW port) can inline small game functions across the generated
# files, which also removes their guest register save/reload. No debug info:
# it only slows the build of 9M generated lines.
option(NFS_GUEST_LTO "ThinLTO over the lifted game code" ON)
target_compile_options(nfs_guest PRIVATE -g0 -fvisibility=hidden)
if(NFS_GUEST_LTO)
    target_compile_options(nfs_guest PRIVATE -flto=thin)
endif()
# Custom Vulkan drivers (Turnip, ...) imported in the launcher, as in the MW/Carbon ports.
# libadrenotools is pinned in third_party/ (BSD-2-Clause); no driver is bundled.
add_subdirectory("${NFS_ROOT}/third_party/libadrenotools" "${CMAKE_CURRENT_BINARY_DIR}/adrenotools")
add_library(nfsu2_diagnostics SHARED gpu_probe.cpp vulkan_driver.cpp)
target_link_libraries(nfsu2_diagnostics PRIVATE adrenotools android log dl)
target_compile_options(nfsu2_diagnostics PRIVATE -O2 -Wall -Wextra -Werror -Wno-missing-field-initializers)
add_library(nfsu2_android SHARED main.cpp guest_runtime.cpp d3d9_bridge.cpp vulkan_driver.cpp "${NFS_ROOT}/src/runtime/overrides.c" "${NFS_ROOT}/src/runtime/widescreen.c" $<TARGET_OBJECTS:nfs_guest>)
target_include_directories(nfsu2_android SYSTEM PRIVATE "${DXVK_SOURCE}/include/native"
  "${DXVK_SOURCE}/include/native/windows" "${DXVK_SOURCE}/include/native/directx")
target_include_directories(nfsu2_android PRIVATE "${PCRECOMP}/runtime/recomp32" "${PCRECOMP}/runtime/win32hle" "${NFS_ROOT}/src/runtime")
target_compile_definitions(nfsu2_android PRIVATE RECOMP_STRICT_ICALL RECOMP_X87_WINDOW)
target_compile_features(nfsu2_android PRIVATE cxx_std_17)
target_compile_definitions(nfsu2_android PRIVATE VK_USE_PLATFORM_ANDROID_KHR)
target_compile_options(nfsu2_android PRIVATE -O2 -fno-strict-aliasing -ffp-contract=off -Wall -Wextra -Werror -Wno-missing-field-initializers)
target_link_options(nfsu2_android PRIVATE "-uANativeActivity_onCreate")
target_link_libraries(nfsu2_android PRIVATE native_app_glue android log vulkan dxvk_android aaudio adrenotools dl)
if(NFS_GUEST_LTO)
    target_link_options(nfsu2_android PRIVATE -flto=thin "-Wl,--thinlto-jobs=4"
        "-Wl,--thinlto-cache-dir=${CMAKE_BINARY_DIR}/thinlto-cache")
endif()
# linkernsbypass has data symbols named like libdl's functions; exporting them breaks
# the system loader when it opens the vendor namespace (MW port note).
# Only adrenotools' archives: excluding ALL would also hide native_app_glue's
# ANativeActivity_onCreate, which NativeActivity looks up by name.
foreach(target nfsu2_android nfsu2_diagnostics)
    target_link_options(${target} PRIVATE "-Wl,--exclude-libs,libadrenotools.a:liblinkernsbypass.a" "-Wl,-z,max-page-size=16384")
endforeach()
target_link_options(hook_impl PRIVATE "-Wl,--exclude-libs,ALL")
foreach(target adrenotools main_hook file_redirect_hook gsl_alloc_hook hook_impl)
    target_link_options(${target} PRIVATE "-Wl,-z,max-page-size=16384")
endforeach()
# adrenotools opens these from nativeLibraryDir at run time.
add_dependencies(nfsu2_android main_hook file_redirect_hook gsl_alloc_hook hook_impl)
