# CMake toolchain file to cross-compile ECWolf for PS3 (PowerPC), assuming
# a ps3dev/PSL1GHT toolchain with a working SDL2 (e.g. built on top of
# SDL2_PSL1GHT: https://github.com/sergiou87/SDL2_PSL1GHT). Setting up
# that toolchain itself is not covered here -- adjust PS3DEV below to
# wherever yours is installed.
set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR powerpc64)
set(PS3DEV /usr/local/ps3dev-aqua)  # <-- adjust to your toolchain path

set(CMAKE_C_COMPILER   ${PS3DEV}/ppu/bin/ppu-gcc)
set(CMAKE_CXX_COMPILER ${PS3DEV}/ppu/bin/ppu-g++)
set(CMAKE_FIND_ROOT_PATH ${PS3DEV}/ppu ${PS3DEV}/portlibs/ppu)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

# Forces ps3_compat.h (stricmp/strnicmp, I_AM_PS3) into every translation
# unit -- newlib doesn't provide those names under that spelling.
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -include ${CMAKE_CURRENT_LIST_DIR}/ps3_compat.h")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -include ${CMAKE_CURRENT_LIST_DIR}/ps3_compat.h")

set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -lnet -lnetctl")

# PSL1GHT headers (sysutil, io, rsx, etc.) -- needed for the XMB exit
# callback.
include_directories(${PS3DEV}/ppu/include)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -O3 -mcpu=cell -fomit-frame-pointer")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O3 -mcpu=cell -fomit-frame-pointer")

# Pass -DECWOLF_DEBUG=ON to cmake to build the debug variant (adds
# controller-based god mode / level skip / max ammo -- see wl_play.cpp).
if(ECWOLF_DEBUG)
	add_definitions(-DECWOLF_DEBUG_BUILD)
endif()
