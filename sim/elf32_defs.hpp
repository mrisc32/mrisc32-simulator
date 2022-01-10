//--------------------------------------------------------------------------------------------------
// Copyright (c) 2022 Marcus Geelnard
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
// Minimal replacement for system elf.h.
//
// Inspired by https://nes.osdn.jp/man/projects/es/elf.h.html
//--------------------------------------------------------------------------------------------------

#ifndef SIM_ELF32_DEFS_HPP_
#define SIM_ELF32_DEFS_HPP_

#include <cstdint>

//--------------------------------------------------------------------------------------------------
// ELF header
//--------------------------------------------------------------------------------------------------

struct Elf32_Ehdr {
  uint8_t e_ident[16];
  uint16_t e_type;
  uint16_t e_machine;
  uint32_t e_version;
  uint32_t e_entry;
  uint32_t e_phoff;
  uint32_t e_shoff;
  uint32_t e_flags;
  uint16_t e_ehsize;
  uint16_t e_phentsize;
  uint16_t e_phnum;
  uint16_t e_shentsize;
  uint16_t e_shnum;
  uint16_t e_shstrndx;
};

// Elf32_Ehdr.e_machine
#define EM_MRISC32 0xc001

//--------------------------------------------------------------------------------------------------
// Section header
//--------------------------------------------------------------------------------------------------

struct Elf32_Shdr {
  uint32_t sh_name;
  uint32_t sh_type;
  uint32_t sh_flags;
  uint32_t sh_addr;
  uint32_t sh_offset;
  uint32_t sh_size;
  uint32_t sh_link;
  uint32_t sh_info;
  uint32_t sh_addralign;
  uint32_t sh_entsize;
};

// Elf32_Shdr.sh_type
#define SHT_PROGBITS 1
#define SHT_NOBITS 8
#define SHT_INIT_ARRAY 14
#define SHT_FINI_ARRAY 15

// Elf32_Shdr.sh_flags
#define SHF_ALLOC 0x2

#endif  // SIM_ELF32_DEFS_HPP_
