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

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#ifndef MAX
#  define MAX(x,y) ((x)>(y)?(x):(y))
#endif

#define NO_ADDRESS ((void*)(-1))

/****************************************************************************/

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

/****************************************************************************/

/* Borrowed from /usr/include/elf.h of a Linux system */

typedef uint16_t Elf32_Half;     /* Type for a 16-bit quantity.  */
typedef uint32_t Elf32_Word;     /* Type for unsigned 32-bit quantities.  */
typedef	int32_t  Elf32_Sword;    /* Type for signed 32-bit quantities.  */
typedef uint64_t Elf32_Xword;    /* Type for unsigned 64-bit quantities.  */
typedef	int64_t  Elf32_Sxword;   /* Type for signed 64-bit quantities.  */
typedef uint32_t Elf32_Addr;     /* Type of addresses.  */
typedef uint32_t Elf32_Off;      /* Type of file offsets.  */
typedef uint16_t Elf32_Section;  /* Type for section indices, which are 16-bit quantities.  */
typedef Elf32_Half Elf32_Versym; /* Type for version symbol information.  */

#define EI_NIDENT (16)
typedef struct
{
  unsigned char	e_ident[EI_NIDENT];	/* Magic number and other info */
  Elf32_Half	e_type;			/* Object file type */
  Elf32_Half	e_machine;		/* Architecture */
  Elf32_Word	e_version;		/* Object file version */
  Elf32_Addr	e_entry;		/* Entry point virtual address */
  Elf32_Off	e_phoff;		/* Program header table file offset */
  Elf32_Off	e_shoff;		/* Section header table file offset */
  Elf32_Word	e_flags;		/* Processor-specific flags */
  Elf32_Half	e_ehsize;		/* ELF header size in bytes */
  Elf32_Half	e_phentsize;		/* Program header table entry size */
  Elf32_Half	e_phnum;		/* Program header table entry count */
  Elf32_Half	e_shentsize;		/* Section header table entry size */
  Elf32_Half	e_shnum;		/* Section header table entry count */
  Elf32_Half	e_shstrndx;		/* Section header string table index */
} Elf32_Ehdr;


typedef struct
{
  Elf32_Word	sh_name;		/* Section name (string tbl index) */
  Elf32_Word	sh_type;		/* Section type */
  Elf32_Word	sh_flags;		/* Section flags */
  Elf32_Addr	sh_addr;		/* Section virtual addr at execution */
  Elf32_Off	sh_offset;		/* Section file offset */
  Elf32_Word	sh_size;		/* Section size in bytes */
  Elf32_Word	sh_link;		/* Link to another section */
  Elf32_Word	sh_info;		/* Additional section information */
  Elf32_Word	sh_addralign;		/* Section alignment */
  Elf32_Word	sh_entsize;		/* Entry size if section holds table */
} Elf32_Shdr;

/* Section header type */
#define SHT_NULL	  0		/* Section header table entry unused */
#define SHT_PROGBITS	  1		/* Program data */
#define SHT_NOBITS	  8		/* Program space with no data (bss) */
#define SHT_INIT_ARRAY	  14		/* Array of constructors */
#define SHT_FINI_ARRAY	  15		/* Array of destructors */

/* Section header flags */
#define SHF_ALLOC	     (1 << 1)	/* Occupies memory during execution */

/****************************************************************************/

int elf32_parse(const char* filename, Elf32Info* info) {
  Elf32_Ehdr elf_header;
  Elf32_Shdr sec_header;
  FILE* fp;
  size_t br;
   
  fp = fopen(filename, "rb");
  uint8_t* base_mem = (uint8_t*)(info->base_address);
  
  info->text_address = 0;

  if(fp == nullptr) {
    return ELF32_FILE_NOT_FOUND;
  }

  /* read elf header */
  br = fread(&elf_header, 1, sizeof(elf_header), fp);
  if(br != sizeof(elf_header)) {
     return ELF32_READ_ERROR;
  }

  /* sanity check */
  if(elf_header.e_ehsize != sizeof(elf_header)) {
     return ELF32_HEADER_SIZE_MISMATCH;
  }

  /* sanity check */  
  if(elf_header.e_shentsize != sizeof(Elf32_Shdr)) {
     return ELF32_HEADER_SIZE_MISMATCH;
  }
  
  /* read all section headers */  
  for(int i=0; i<elf_header.e_shnum; ++i) {
    
     if(fseek(fp, elf_header.e_shoff + i*sizeof(sec_header), SEEK_SET) != 0) {
	return ELF32_READ_ERROR;	  
     }

     br = fread(&sec_header, 1, sizeof(sec_header), fp);
     if(br != sizeof(sec_header)) {
	return ELF32_READ_ERROR;
     }
    
     /* The sections we are interested in are the ALLOC sections. */
     if(!(sec_header.sh_flags & SHF_ALLOC)) {
	continue;
     }
     
     /* I assume that the first PROGBITS section is the text segment */
     /* TODO: verify using the name of the section (but requires to  */
     /* load the strings table, painful...)                          */ 
     if(sec_header.sh_type == SHT_PROGBITS && info->text_address == 0) {
	info->text_address = sec_header.sh_addr;
     }

     /* Update max address */ 
     info->max_address = MAX(
        info->max_address, 
        sec_header.sh_addr + sec_header.sh_size
     );

     /* PROGBIT, INI_ARRAY and FINI_ARRAY need to be loaded. */
     if(
	sec_header.sh_type == SHT_PROGBITS ||
	sec_header.sh_type == SHT_INIT_ARRAY ||
	sec_header.sh_type == SHT_FINI_ARRAY
     ) {
	if(info->base_address != NO_ADDRESS) {
           if(fseek(fp, sec_header.sh_offset, SEEK_SET) != 0) {
	      return ELF32_READ_ERROR;
           }

           br = fread(base_mem + sec_header.sh_addr, 1, sec_header.sh_size, fp);
           if(br != sec_header.sh_size) {
	    return ELF32_READ_ERROR;
	  }
	}
     }

     /* NOBITS need to be cleared. */    
     if(sec_header.sh_type == SHT_NOBITS && info->base_address != NO_ADDRESS) {	
	memset(base_mem + sec_header.sh_addr, 0, sec_header.sh_size);
     }
  }  
  fclose(fp);
  
  return ELF32_OK;
}
