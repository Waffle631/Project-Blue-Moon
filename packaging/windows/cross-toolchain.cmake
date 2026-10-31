# Cross build of the Windows port from Linux (docs/specs/2026-10-30-spec-8-alpha.md §4 A1):
#   cmake -S gpu -B out/gpu-win -G Ninja -DCMAKE_TOOLCHAIN_FILE=$PWD/packaging/windows/cross-toolchain.cmake
# (an absolute path: CMake resolves a relative one from the build folder)
# tools/cross_windows_env.sh fills out/cross/ (TSUKI_CROSS_DIR overrides it): llvm-mingw is the compiler (clang,
# libc++, UCRT, as MSYS2 CLANG64) and sysroot/clang64 holds MSYS2's CLANG64 packages, the libraries of a native
# build. Programs the build runs (glslang for the host shaders, Python) are Linux's: CMAKE_FIND_ROOT_PATH_MODE_PROGRAM
# NEVER, while libraries, headers and CMake packages come only from the sysroot.
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)
if (DEFINED ENV{TSUKI_CROSS_DIR})
    set(TSUKI_CROSS_DIR $ENV{TSUKI_CROSS_DIR})
else()
    get_filename_component(TSUKI_CROSS_DIR "${CMAKE_CURRENT_LIST_DIR}/../../out/cross" ABSOLUTE)
endif()
set(TSUKI_MINGW ${TSUKI_CROSS_DIR}/llvm-mingw)
set(TSUKI_SYSROOT ${TSUKI_CROSS_DIR}/sysroot)
if (NOT EXISTS ${TSUKI_MINGW}/bin/x86_64-w64-mingw32-clang OR NOT EXISTS ${TSUKI_SYSROOT}/clang64/lib)
    message(FATAL_ERROR "No cross toolchain in ${TSUKI_CROSS_DIR}: run tools/cross_windows_env.sh first")
endif()

set(CMAKE_C_COMPILER ${TSUKI_MINGW}/bin/x86_64-w64-mingw32-clang)
set(CMAKE_CXX_COMPILER ${TSUKI_MINGW}/bin/x86_64-w64-mingw32-clang++)
set(CMAKE_RC_COMPILER ${TSUKI_MINGW}/bin/x86_64-w64-mingw32-windres)
set(CMAKE_AR ${TSUKI_MINGW}/bin/x86_64-w64-mingw32-ar CACHE FILEPATH "")
set(CMAKE_RANLIB ${TSUKI_MINGW}/bin/x86_64-w64-mingw32-ranlib CACHE FILEPATH "")

set(CMAKE_FIND_ROOT_PATH ${TSUKI_SYSROOT}/clang64 ${TSUKI_MINGW}/x86_64-w64-mingw32)
set(CMAKE_PREFIX_PATH ${TSUKI_SYSROOT}/clang64)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
# MSYS2's clang searches /clang64/include and /clang64/lib by default, and some CMake packages rely on it (Zydis
# links a bare -lZycore): the sysroot's counterparts are made default too.
set(CMAKE_C_STANDARD_INCLUDE_DIRECTORIES ${TSUKI_SYSROOT}/clang64/include)
set(CMAKE_CXX_STANDARD_INCLUDE_DIRECTORIES ${TSUKI_SYSROOT}/clang64/include)
foreach(kind EXE SHARED MODULE)
    set(CMAKE_${kind}_LINKER_FLAGS_INIT -L${TSUKI_SYSROOT}/clang64/lib)
endforeach()
# MSYS2's headers default to Windows 10 (_WIN32_WINNT 0xA00, so NTDDI_VERSION is the newest) and llvm-mingw's to
# Windows 7: without this, CreateFileMapping2 (io_file.cpp, src/runtime_memory.c) is not declared.
set(CMAKE_C_FLAGS_INIT -D_WIN32_WINNT=0xA00)
set(CMAKE_CXX_FLAGS_INIT -D_WIN32_WINNT=0xA00)

# The .pc files say prefix=/clang64: the host pkg-config reads only the sysroot's and puts it in front of each path.
set(ENV{PKG_CONFIG_PATH} "")
set(ENV{PKG_CONFIG_LIBDIR} ${TSUKI_SYSROOT}/clang64/lib/pkgconfig:${TSUKI_SYSROOT}/clang64/share/pkgconfig)
set(ENV{PKG_CONFIG_SYSROOT_DIR} ${TSUKI_SYSROOT})
