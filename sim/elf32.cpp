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
#include "elf32_defs.hpp"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef MAX
#define MAX(x, y) ((x) > (y) ? (x) : (y))
#endif

#define NO_ADDRESS ((void*)(-1))

static int elf32_parse(const char* filename, Elf32Info* info);

int elf32_load(const char* filename, Elf32Info* info) {
  info->base_address = NULL;
  info->text_address = 0;
  info->max_address = 0;
  return elf32_parse(filename, info);
}

int elf32_load_at(const char* filename, Elf32Info* info, void* addr) {
  info->base_address = addr;
  info->text_address = 0;
  info->max_address = 0;
  return elf32_parse(filename, info);
}

int elf32_stat(const char* filename, Elf32Info* info) {
  info->base_address = NO_ADDRESS;
  info->text_address = 0;
  info->max_address = 0;
  return elf32_parse(filename, info);
}

int elf32_parse(const char* filename, Elf32Info* info) {
  Elf32_Ehdr elf_header;
  Elf32_Shdr sec_header;
  FILE* fp;
  size_t br;

  fp = fopen(filename, "rb");
  uint8_t* base_mem = (uint8_t*)(info->base_address);

  info->text_address = 0;

  if (fp == nullptr) {
    return ELF32_FILE_NOT_FOUND;
  }

  // Read elf header.
  br = fread(&elf_header, 1, sizeof(elf_header), fp);
  if (br != sizeof(elf_header)) {
    return ELF32_READ_ERROR;
  }

  // Sanity check.
  if (elf_header.e_ehsize != sizeof(elf_header)) {
    return ELF32_HEADER_SIZE_MISMATCH;
  }

  // Sanity check.
  if (elf_header.e_shentsize != sizeof(Elf32_Shdr)) {
    return ELF32_HEADER_SIZE_MISMATCH;
  }

  // Read all section headers.
  for (int i = 0; i < elf_header.e_shnum; ++i) {
    if (fseek(fp, elf_header.e_shoff + i * sizeof(sec_header), SEEK_SET) != 0) {
      return ELF32_READ_ERROR;
    }

    br = fread(&sec_header, 1, sizeof(sec_header), fp);
    if (br != sizeof(sec_header)) {
      return ELF32_READ_ERROR;
    }

    // The sections we are interested in are the ALLOC sections.
    if (!(sec_header.sh_flags & SHF_ALLOC)) {
      continue;
    }

    // I assume that the first PROGBITS section is the text segment.
    // TODO: Verify using the name of the section (but requires to load the strings table,
    // painful...).
    if (sec_header.sh_type == SHT_PROGBITS && info->text_address == 0) {
      info->text_address = sec_header.sh_addr;
    }

    // Update max address.
    info->max_address = MAX(info->max_address, sec_header.sh_addr + sec_header.sh_size);

    // PROGBIT, INI_ARRAY and FINI_ARRAY need to be loaded.
    if (sec_header.sh_type == SHT_PROGBITS || sec_header.sh_type == SHT_INIT_ARRAY ||
        sec_header.sh_type == SHT_FINI_ARRAY) {
      if (info->base_address != NO_ADDRESS) {
        if (fseek(fp, sec_header.sh_offset, SEEK_SET) != 0) {
          return ELF32_READ_ERROR;
        }

        br = fread(base_mem + sec_header.sh_addr, 1, sec_header.sh_size, fp);
        if (br != sec_header.sh_size) {
          return ELF32_READ_ERROR;
        }
      }
    }

    // NOBITS need to be cleared.
    if (sec_header.sh_type == SHT_NOBITS && info->base_address != NO_ADDRESS) {
      memset(base_mem + sec_header.sh_addr, 0, sec_header.sh_size);
    }
  }
  fclose(fp);

  return ELF32_OK;
}
