//--------------------------------------------------------------------------------------------------
// Copyright (c) 2022 Marcus Geelnard
// Copyright (c) 2020 Bruno Levy
//
// This software is provided 'as-is', without any express or implied warranty. In no event will the
// authors be held liable for any damages arising from the use of this software.
//
// Permission is granted to anyone to use this software for any purpose, including commercial
// applications, and to alter it and redistribute it freely, subject to the following restrictions:
//
//  1. The origin of this software must not be misrepresented; you must not claim that you wrote
//     the original software. If you use this software in a product, an acknowledgment in the
//     product documentation would be appreciated but is not required.
//
//  2. Altered source versions must be plainly marked as such, and must not be misrepresented as
//     being the original software.
//
//  3. This notice may not be removed or altered from any source distribution.
//--------------------------------------------------------------------------------------------------

//--------------------------------------------------------------------------------------------------
// This a fork of lite_elf.h by Bruno Levy. Original description:
//   A minimalistic ELF loader. Probably many things are missing.
//   Disclaimer: I do not understand everything here !
//   Bruno Levy, 12/2020
//--------------------------------------------------------------------------------------------------

#ifndef SIM_ELF32_HPP_
#define SIM_ELF32_HPP_

#include "ram.hpp"

#include <cstdint>

namespace elf32 {

struct info_t {
  uint32_t text_address;  ///< The address of the text segment.
  uint32_t max_address;   ///< The maximum address of a segment.
};

enum class status_t {
  OK,
  FILE_NOT_FOUND,
  HEADER_SIZE_MISMATCH,
  READ_ERROR,
};

/// @brief Loads an ELF executable to simulator RAM.
/// @param[in] file_name the name of the file that contains the ELF executable.
/// @param[in] ram the simulator RAM object to load the file into.
/// @param[out] info an info_t object. On exit, text_address contains the starting address of the
/// text segment, and max_address the maximum address used by the segments.
/// @return OK or an error code.
status_t load(const char* file_name, ram_t& ram, info_t& info);

}  // namespace elf32

#endif  // SIM_ELF32_HPP_
