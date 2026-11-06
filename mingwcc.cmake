set(TOOLCHAIN_PREFIX "x86_64-w64-mingw32")

set(CMAKE_SYSTEM_NAME Windows)

# The C++ compiler needs the posix thread model (std::thread). Debian and Ubuntu install it as
# <prefix>-g++-posix; Arch, Fedora and MSYS2 ship only <prefix>-g++, which uses that model already.
find_program(MINGW_CXX_COMPILER NAMES ${TOOLCHAIN_PREFIX}-g++-posix ${TOOLCHAIN_PREFIX}-g++)
if(NOT MINGW_CXX_COMPILER)
	message(FATAL_ERROR "No ${TOOLCHAIN_PREFIX}-g++-posix or ${TOOLCHAIN_PREFIX}-g++ found")
endif()
set(CMAKE_CXX_COMPILER "${MINGW_CXX_COMPILER}")
set(CMAKE_C_COMPILER "${TOOLCHAIN_PREFIX}-gcc")
set(CMAKE_OBJCOPY "${TOOLCHAIN_PREFIX}-objcopy")
set(CMAKE_STRIP "${TOOLCHAIN_PREFIX}-strip")
set(CMAKE_SIZE "${TOOLCHAIN_PREFIX}-size")
set(CMAKE_AR "${TOOLCHAIN_PREFIX}-ar")
set(ASSEMBLER "${TOOLCHAIN_PREFIX}-as")
set(CMAKE_RC_COMPILER "${TOOLCHAIN_PREFIX}-windres")


# The target environment, plus the SDL development packages in Libs/ (CMakeLists.txt), which
# the ONLY modes below would otherwise look for under the target root.
set(CMAKE_FIND_ROOT_PATH /usr/${TOOLCHAIN_PREFIX} ${CMAKE_CURRENT_LIST_DIR}/Libs)

# adjust the default behavior of the find commands:
# search headers and libraries in the target environment
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)

# search programs in the host environment
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)

# Add the path to your toolchain version of SDL2
set(SDL2_PATH /usr/${TOOLCHAIN_PREFIX})
