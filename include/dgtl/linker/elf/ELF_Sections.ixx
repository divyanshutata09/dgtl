/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * Project:     DGTL (Deep General-purpose Transformation Layer)
 * Owner:       Divyanshu Tata
 * Repository:  github.com/TataG-Systems/DGTL
 *
 * This software is the proprietary property of TataG Systems.
 * Unauthorized copying, modification, or distribution is strictly prohibited.
 */

module;

#include <cstdint>
#include <string_view>

export module dgtl.linker.elf.sections;

import dgtl.object.section;

export namespace dgtl::linker::elf {

struct SectionDescriptor {
    const char* name;
    uint64_t alignment;
    uint32_t flags;
    uint32_t type;
};

#pragma pack(push, 1)
struct Elf64_Ehdr {
    unsigned char e_ident[16];
    uint16_t e_type;
    uint16_t e_machine;
    uint32_t e_version;
    uint64_t e_entry;
    uint64_t e_phoff;
    uint64_t e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize;
    uint16_t e_phentsize;
    uint16_t e_phnum;
    uint16_t e_shentsize;
    uint16_t e_shnum;
    uint16_t e_shstrndx;
};

struct Elf64_Phdr {
    uint32_t p_type;
    uint32_t p_flags;
    uint64_t p_offset;
    uint64_t p_vaddr;
    uint64_t p_paddr;
    uint64_t p_filesz;
    uint64_t p_memsz;
    uint64_t p_align;
};

struct Elf64_Shdr {
    uint32_t sh_name;
    uint32_t sh_type;
    uint64_t sh_flags;
    uint64_t sh_addr;
    uint64_t sh_offset;
    uint64_t sh_size;
    uint32_t sh_link;
    uint32_t sh_info;
    uint64_t sh_addralign;
    uint64_t sh_entsize;
};

struct Elf64_Sym {
    uint32_t st_name;
    unsigned char st_info;
    unsigned char st_other;
    uint16_t st_shndx;
    uint64_t st_value;
    uint64_t st_size;
};

struct Elf64_Rela {
    uint64_t r_offset;
    uint64_t r_info;
    int64_t  r_addend;
};
#pragma pack(pop)

inline constexpr uint32_t SHT_NULL     = 0;
inline constexpr uint32_t SHT_PROGBITS = 1;
inline constexpr uint32_t SHT_SYMTAB   = 2;
inline constexpr uint32_t SHT_STRTAB   = 3;
inline constexpr uint32_t SHT_RELA     = 4;
inline constexpr uint32_t SHT_NOBITS   = 8;
inline constexpr uint32_t SHT_REL      = 9;
inline constexpr uint32_t SHT_DYNSYM   = 11;

inline constexpr uint64_t SHF_WRITE     = 0x1;
inline constexpr uint64_t SHF_ALLOC     = 0x2;
inline constexpr uint64_t SHF_EXECINSTR = 0x4;
inline constexpr uint64_t SHF_INFO_LINK = 0x40;

inline constexpr uint8_t  STB_LOCAL    = 0;
inline constexpr uint8_t  STB_GLOBAL   = 1;
inline constexpr uint8_t  STT_FUNC     = 2;
inline constexpr uint8_t  STT_SECTION  = 3;

inline constexpr const char* ELFMAG    = "\177ELF";
inline constexpr size_t   SELFMAG      = 4;
inline constexpr uint8_t  ELFCLASS64   = 2;
inline constexpr uint8_t  ELFDATA2LSB  = 1;
inline constexpr uint32_t EV_CURRENT   = 1;

inline constexpr uint16_t ET_REL       = 1;
inline constexpr uint16_t ET_EXEC      = 2;
inline constexpr uint16_t EM_X86_64    = 62;
inline constexpr uint32_t PT_LOAD      = 1;
inline constexpr uint32_t PF_X         = 1;
inline constexpr uint32_t PF_R         = 4;

[[nodiscard]] inline constexpr uint8_t elf64_st_info(uint8_t bind, uint8_t type) noexcept {
    return static_cast<uint8_t>(((bind) << 4) + ((type) & 0x0F));
}

[[nodiscard]] inline constexpr uint64_t elf64_r_info(uint32_t sym, uint32_t type) noexcept {
    return (static_cast<uint64_t>(sym) << 32) + static_cast<uint64_t>(type);
}

#ifndef ELF64_ST_INFO
#define ELF64_ST_INFO(bind, type) (dgtl::linker::elf::elf64_st_info((bind), (type)))
#endif
#ifndef ELF64_R_INFO
#define ELF64_R_INFO(sym, type) (dgtl::linker::elf::elf64_r_info((sym), (type)))
#endif

} // namespace dgtl::linker::elf

export namespace dgtl::backend::linker::elf {
    using namespace dgtl::linker::elf;
} // namespace dgtl::backend::linker::elf



