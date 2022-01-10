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

#include <stdint.h>

typedef uint32_t elf32_addr;

typedef struct {
  void* base_address;       ///< Base memory address (NULL on normal operation).
  elf32_addr text_address;  ///< The address of the text segment.
  elf32_addr max_address;   ///< The maximum address of a segment.
} Elf32Info;

#define ELF32_OK 0
#define ELF32_FILE_NOT_FOUND 1
#define ELF32_HEADER_SIZE_MISMATCH 2
#define ELF32_READ_ERROR 3

/// @brief Loads an ELF executable to RAM.
/// @param[in] filename the name of the file that contains the ELF executable.
/// @param[out] info a pointer to an Elf32Info. On exit, base_adress is NULL, text_address contains
/// the starting address of the text segment, and max_address the maximum address used by the
/// segments.
/// @return ELF32_OK or an error code.
int elf32_load(const char* filename, Elf32Info* info);

/// @brief Loads an ELF executable to RAM at a specified address.
/// @details Used by programs that convert ELF executables to other formats.
/// @param[in] filename the name of the file that contains the ELF executable.
/// @param[out] info a pointer to an Elf32Info. On exit, base_adress is NULL, text_address contains
/// the starting address of the text segment, and max_address the maximum address used by the
/// segments.
/// @param[in] addr the address where to load the ELF segments.
/// @return ELF32_OK or an error code.
int elf32_load_at(const char* filename, Elf32Info* info, void* addr);

/// @brief Analyzes an ELF executable.
/// @param[in] filename the name of the file that contains the ELF executable.
/// @param[out] info a pointer to an Elf32Info. On exit, base_adress is NULL, text_address contains
/// the starting address of the text segment, and max_address the maximum address used by the
/// segments.
/// @return ELF32_OK or an error code.
int elf32_stat(const char* filename, Elf32Info* info);

#endif  // SIM_ELF32_HPP_
