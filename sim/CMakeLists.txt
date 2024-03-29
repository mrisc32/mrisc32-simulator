#---------------------------------------------------------------------------------------------------
# Copyright (c) 2018 Marcus Geelnard
#
# This software is provided 'as-is', without any express or implied warranty. In no event will the
# authors be held liable for any damages arising from the use of this software.
#
# Permission is granted to anyone to use this software for any purpose, including commercial
# applications, and to alter it and redistribute it freely, subject to the following restrictions:
#
#  1. The origin of this software must not be misrepresented; you must not claim that you wrote
#     the original software. If you use this software in a product, an acknowledgment in the
#     product documentation would be appreciated but is not required.
#
#  2. Altered source versions must be plainly marked as such, and must not be misrepresented as
#     being the original software.
#
#  3. This notice may not be removed or altered from any source distribution.
#---------------------------------------------------------------------------------------------------

cmake_minimum_required (VERSION 3.14)
project (mrisc32-sim)

add_subdirectory(third_party)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(MR32SIM_SRC mr32sim.cpp
                config.cpp
                config.hpp
                elf32.cpp
                elf32.hpp
                cpu.cpp
                cpu.hpp
                cpu_simple.cpp
                cpu_simple.hpp
                gpu.cpp
                gpu.hpp
                packed_float.hpp
                perf_symbols.cpp
                perf_symbols.hpp
                ram.cpp
                ram.hpp
                syscalls.cpp
                syscalls.hpp)
set(MR32SIM_LIBS glfw
                 glad
                 ${CMAKE_DL_LIBS})

# We need C++ threads.
find_package(Threads REQUIRED)
list(APPEND MR32SIM_LIBS ${CMAKE_THREAD_LIBS_INIT})

add_executable(mr32sim ${MR32SIM_SRC})
target_include_directories(mr32sim PRIVATE .)
target_link_libraries(mr32sim ${MR32SIM_LIBS})

# Installation.
install(TARGETS mr32sim)

# This ensures that required MSVC runtime libraries are installed.
include(InstallRequiredSystemLibraries)
