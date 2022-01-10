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
// This a fork of lite_elf.c by Bruno Levy.
//--------------------------------------------------------------------------------------------------

#include "elf32.hpp"

#include "config.hpp"
#include "elf32_defs.hpp"

#include <algorithm>
#include <fstream>
#include <iomanip>
#include <iostream>

namespace elf32 {
namespace {
bool read_to_ram(std::ifstream& f, uint32_t addr, uint32_t bytes, ram_t& ram) {
  auto end_addr = addr + bytes;
  while (addr != end_addr && f.good()) {
    uint8_t byte;
    f.read(reinterpret_cast<char*>(&byte), 1);
    if (f.bad()) {
      return false;
    }
    ram.store8(addr, byte);
    ++addr;
  }
  return true;
}

void clear_ram(uint32_t addr, uint32_t bytes, ram_t& ram) {
  for (uint32_t k = 0; k < bytes; ++k) {
    ram.store8(addr, 0);
    ++addr;
  }
}
}  // namespace

status_t load(const char* file_name, ram_t& ram, info_t& info) {
  info.text_address = 0;
  info.max_address = 0;

  std::ifstream f(file_name, std::fstream::in | std::fstream::binary);
  if (f.bad()) {
    return status_t::FILE_NOT_FOUND;
  }

  // Read elf header.
  Elf32_Ehdr elf_header;
  f.read(reinterpret_cast<char*>(&elf_header), sizeof(elf_header));
  if (f.bad()) {
    return status_t::READ_ERROR;
  }

  // Sanity check.
  if (elf_header.e_ehsize != sizeof(elf_header)) {
    return status_t::HEADER_SIZE_MISMATCH;
  }

  // Sanity check.
  if (elf_header.e_shentsize != sizeof(Elf32_Shdr)) {
    return status_t::HEADER_SIZE_MISMATCH;
  }

  // Read all section headers.
  for (int i = 0; i < elf_header.e_shnum; ++i) {
    Elf32_Shdr sec_header;
    f.seekg(elf_header.e_shoff + i * sizeof(sec_header));
    if (f.bad()) {
      return status_t::READ_ERROR;
    }

    f.read(reinterpret_cast<char*>(&sec_header), sizeof(sec_header));
    if (f.bad()) {
      return status_t::READ_ERROR;
    }

    // The sections we are interested in are the ALLOC sections.
    if (!(sec_header.sh_flags & SHF_ALLOC)) {
      continue;
    }

    // I assume that the first PROGBITS section is the text segment.
    // TODO: Verify using the name of the section (but requires to load the strings table,
    // painful...).
    if (sec_header.sh_type == SHT_PROGBITS && info.text_address == 0) {
      info.text_address = sec_header.sh_addr;
    }

    // Update max address.
    info.max_address = std::max(info.max_address, sec_header.sh_addr + sec_header.sh_size);

    // PROGBIT, INI_ARRAY and FINI_ARRAY need to be loaded.
    if (sec_header.sh_type == SHT_PROGBITS || sec_header.sh_type == SHT_INIT_ARRAY ||
        sec_header.sh_type == SHT_FINI_ARRAY) {
      f.seekg(sec_header.sh_offset);
      if (f.bad()) {
        return status_t::READ_ERROR;
      }

      if (!read_to_ram(f, sec_header.sh_addr, sec_header.sh_size, ram)) {
        return status_t::READ_ERROR;
      }
    }

    // NOBITS need to be cleared.
    if (sec_header.sh_type == SHT_NOBITS) {
      clear_ram(sec_header.sh_addr, sec_header.sh_size, ram);
    }
  }
  f.close();

  if (config_t::instance().verbose()) {
    std::cout << "Read ELF32 executable " << file_name << " into RAM @ 0x" << std::hex
              << std::setw(8) << std::setfill('0') << info.text_address << "\n";
    std::cout << std::resetiosflags(std::ios::hex);
  }

  return status_t::OK;
}

}  // namespace elf32
