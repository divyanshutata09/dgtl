/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * Project:     DGTL (Deep General-purpose Transformation Layer)
 * Owner:       Divyanshu Tata
 * Repository:  github.com/TataG-Systems/DGTL
 *
 * Component:   DGTL Standalone Linker Frontend (dgtl-link)
 * Standards:   C++26 / Clang 18+ / MSVC 19.40+ / GCC 14+
 *
 * This software is the proprietary property of TataG Systems.
 * Unauthorized copying, modification, or distribution is strictly prohibited.
 */

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

import dgtl.object.file;
import dgtl.object.section;
import dgtl.object.symbol;
import dgtl.object.relocation;
import dgtl.object.archive;
import dgtl.object.readers.elf;
import dgtl.object.readers.pe;
import dgtl.object.writers.pe;
import dgtl.object.writers.elf;
import dgtl.object.writers.macho;
import dgtl.object.writers.wasm;
import dgtl.linker.driver;
import dgtl.linker.driver_api;
import dgtl.linker.pe;
import dgtl.linker.elf;
import dgtl.linker.wasm;
import dgtl.linker.context;
import dgtl.linker.layout;
import dgtl.linker.symbols;
import dgtl.linker.entry_point;

namespace fs = std::filesystem;

namespace dgtl::cli::link {

enum class LinkFormat {
    Auto,
    PEExecutable,
    COFFObject,
    COFFArchive,
    ELFExecutable,
    ELFObject,
    StaticArchive,
    MachOObject,
    WASMBinary
};

struct LinkOptions {
    std::vector<std::string> input_files;
    std::string output_file;
    LinkFormat format = LinkFormat::Auto;
    std::string entry_symbol = "DGTL_kernel_entry";
    uint64_t image_base = 0;
    bool is_archive = false;
    bool verbose = false;
    bool time_report = false;
};

void print_version() {
    std::cout << "dgtl-link: DGTL Sovereign Binary Linker v2.0.0 (TataG Systems)\n"
              << "Standards: C++26 / Multi-Target Object & Executable Synthesis Engine\n";
}

void print_usage(std::string_view prog_name) {
    std::cout << "DGTL Sovereign Binary Linker (dgtl-link)\n"
              << "Direct-to-binary in-memory linker for PE/COFF, ELF64, Mach-O, and WASM\n\n"
              << "USAGE:\n"
              << "  " << prog_name << " [options] <input_files...>\n\n"
              << "OPTIONS:\n"
              << "  -o, --output=<path>     Output file path (required)\n"
              << "  -f, --format=<fmt>      Output binary format:\n"
              << "                            pe, coff, elf, macho, wasm, lib, archive\n"
              << "                            (default: auto-detected from output extension)\n"
              << "  -e, --entry=<symbol>    Entry point symbol (default: 'DGTL_kernel_entry' or 'main')\n"
              << "  -b, --base=<hex_addr>   Preferred image base address (e.g. 0x140000000)\n"
              << "  -a, --archive           Build static library archive (.lib / .a)\n"
              << "  -v, --verbose           Display detailed section layout and symbol mappings\n"
              << "  -t, --time              Measure and display link-time performance metrics\n"
              << "  --version               Display linker version information\n"
              << "  -h, --help              Display this help message\n\n"
              << "EXAMPLES:\n"
              << "  " << prog_name << " -o kernel.exe module1.o module2.o\n"
              << "  " << prog_name << " --format=pe -e main -o app.exe main.obj\n"
              << "  " << prog_name << " --archive -o libmath.lib math1.obj math2.obj\n";
}

LinkFormat detect_format_from_path(std::string_view path, bool is_archive) {
    fs::path p(path);
    std::string ext = p.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

    if (is_archive || ext == ".lib" || ext == ".a") {
        return (ext == ".lib") ? LinkFormat::COFFArchive : LinkFormat::StaticArchive;
    }
    if (ext == ".exe" || ext == ".dll") {
        return LinkFormat::PEExecutable;
    }
    if (ext == ".obj") {
        return LinkFormat::COFFObject;
    }
    if (ext == ".o") {
        return LinkFormat::ELFObject;
    }
    if (ext == ".wasm") {
        return LinkFormat::WASMBinary;
    }
    if (ext == ".dylib" || ext == ".macho") {
        return LinkFormat::MachOObject;
    }
    if (ext == ".elf") {
        return LinkFormat::ELFExecutable;
    }
#if defined(__linux__) || defined(__ELF__)
    return LinkFormat::ELFExecutable;
#elif defined(__APPLE__)
    return LinkFormat::MachOObject;
#else
    return LinkFormat::PEExecutable;
#endif
}

LinkFormat parse_format_str(std::string_view s) {
    std::string f(s);
    std::transform(f.begin(), f.end(), f.begin(), ::tolower);

    if (f == "pe" || f == "pe-exe" || f == "exe" || f == "pe64") return LinkFormat::PEExecutable;
    if (f == "coff" || f == "coff-obj" || f == "obj") return LinkFormat::COFFObject;
    if (f == "lib" || f == "coff-lib") return LinkFormat::COFFArchive;
    if (f == "elf" || f == "elf-exe" || f == "elf64") return LinkFormat::ELFExecutable;
    if (f == "elf-obj" || f == "o") return LinkFormat::ELFObject;
    if (f == "a" || f == "archive" || f == "staticlib") return LinkFormat::StaticArchive;
    if (f == "macho" || f == "dylib" || f == "macho64") return LinkFormat::MachOObject;
    if (f == "wasm") return LinkFormat::WASMBinary;
    return LinkFormat::Auto;
}

std::optional<LinkOptions> parse_args(int argc, char* argv[]) {
    if (argc < 2) {
        print_usage(argv[0]);
        return std::nullopt;
    }

    LinkOptions opts;
    for (int i = 1; i < argc; ++i) {
        std::string_view arg = argv[i];

        if (arg == "-h" || arg == "--help") {
            print_usage(argv[0]);
            return std::nullopt;
        }
        if (arg == "--version") {
            print_version();
            return std::nullopt;
        }
        if (arg == "-v" || arg == "--verbose") {
            opts.verbose = true;
            continue;
        }
        if (arg == "-t" || arg == "--time") {
            opts.time_report = true;
            continue;
        }
        if (arg == "-a" || arg == "--archive") {
            opts.is_archive = true;
            continue;
        }
        if (arg == "-o") {
            if (i + 1 < argc) {
                opts.output_file = argv[++i];
                continue;
            }
            std::cerr << "error[dgtl-link]: -o requires an output path\n";
            return std::nullopt;
        }
        if (arg.rfind("-o=", 0) == 0 || arg.rfind("--output=", 0) == 0) {
            auto pos = arg.find('=');
            opts.output_file = std::string(arg.substr(pos + 1));
            continue;
        }
        if (arg == "-f") {
            if (i + 1 < argc) {
                opts.format = parse_format_str(argv[++i]);
                continue;
            }
            std::cerr << "error[dgtl-link]: -f requires a format name\n";
            return std::nullopt;
        }
        if (arg.rfind("-f=", 0) == 0 || arg.rfind("--format=", 0) == 0) {
            auto pos = arg.find('=');
            opts.format = parse_format_str(arg.substr(pos + 1));
            continue;
        }
        if (arg == "-e") {
            if (i + 1 < argc) {
                opts.entry_symbol = argv[++i];
                continue;
            }
            std::cerr << "error[dgtl-link]: -e requires an entry symbol name\n";
            return std::nullopt;
        }
        if (arg.rfind("-e=", 0) == 0 || arg.rfind("--entry=", 0) == 0) {
            auto pos = arg.find('=');
            opts.entry_symbol = std::string(arg.substr(pos + 1));
            continue;
        }
        if (arg.rfind("-b=", 0) == 0 || arg.rfind("--base=", 0) == 0) {
            auto pos = arg.find('=');
            opts.image_base = std::stoull(std::string(arg.substr(pos + 1)), nullptr, 16);
            continue;
        }

        if (arg.front() == '-') {
            std::cerr << "error[dgtl-link]: unrecognized option '" << arg << "'\n";
            return std::nullopt;
        }

        opts.input_files.push_back(std::string(arg));
    }

    if (opts.input_files.empty()) {
        std::cerr << "error[dgtl-link]: no input files specified\n";
        return std::nullopt;
    }
    if (opts.output_file.empty()) {
        std::cerr << "error[dgtl-link]: no output file specified (use -o <path>)\n";
        return std::nullopt;
    }

    if (opts.format == LinkFormat::Auto) {
        opts.format = detect_format_from_path(opts.output_file, opts.is_archive);
    }

    return opts;
}

static std::string_view canonical_section_name(std::string_view name) {
    if (name == ".text" || name.starts_with(".text$")) return ".text";
    if (name == ".rdata" || name.starts_with(".rdata$") || name == ".rodata" || name.starts_with(".rodata$") ||
        name == ".idata" || name.starts_with(".idata$")) return ".rdata";
    if (name == ".data" || name.starts_with(".data$")) return ".data";
    if (name == ".bss" || name.starts_with(".bss$")) return ".bss";
    return name;
}

bool merge_object_file(dgtl::object::ObjectFile& target, const dgtl::object::ObjectFile& src) {
    std::unordered_map<std::string, uint64_t> section_offsets;
    std::unordered_map<std::string, uint32_t> section_target_idx;

    for (const auto& sec : src.sections()) {
        std::string canon_name(canonical_section_name(sec.name));
        auto* existing = target.find_section(canon_name);
        if (!existing) {
            uint32_t new_idx = static_cast<uint32_t>(target.sections().size());
            auto new_sec = sec;
            new_sec.name = canon_name;
            target.sections().push_back(std::move(new_sec));
            section_offsets[sec.name] = 0;
            section_target_idx[sec.name] = new_idx;
        } else {
            const uint64_t align_val = std::max<uint64_t>(16u, sec.alignment);
            if (sec.alignment > existing->alignment) {
                existing->alignment = sec.alignment;
            }

            if (sec.is_bss()) {
                const uint64_t rem = existing->bss_size % align_val;
                if (rem != 0) {
                    existing->bss_size += (align_val - rem);
                }
                const uint64_t base = existing->bss_size;
                existing->bss_size += sec.bss_size;
                section_offsets[sec.name] = base;
            } else {
                const uint64_t rem = existing->bytes.size() % align_val;
                if (rem != 0) {
                    existing->bytes.resize(existing->bytes.size() + (align_val - rem), 0);
                }
                const uint64_t base = existing->bytes.size();
                existing->append_bytes(sec.bytes.data(), sec.bytes.size());
                section_offsets[sec.name] = base;
            }

            for (size_t t_i = 0; t_i < target.sections().size(); ++t_i) {
                if (&target.sections()[t_i] == existing) {
                    section_target_idx[sec.name] = static_cast<uint32_t>(t_i);
                    break;
                }
            }
        }
    }
    for (const auto& sym : src.symbols()) {
        if (sym.is_defined()) {
            uint64_t shift = 0;
            uint32_t target_sec = 0;
            if (sym.section_index < src.sections().size()) {
                const auto& sec_name = src.sections()[sym.section_index].name;
                auto it = section_offsets.find(sec_name);
                if (it != section_offsets.end()) shift = it->second;
                auto it_idx = section_target_idx.find(sec_name);
                if (it_idx != section_target_idx.end()) target_sec = it_idx->second;
            }

            const bool is_section_symbol = sym.name.starts_with(".") ||
                                           sym.name == ".text" || sym.name == ".data" ||
                                           sym.name == ".rdata" || sym.name == ".bss";
            const std::string norm_name = is_section_symbol
                ? std::string(canonical_section_name(sym.name))
                : sym.name;

            auto* existing_sym = target.find_symbol(norm_name);
            if (!existing_sym) {
                auto new_sym = sym;
                new_sym.name = norm_name;
                new_sym.value = is_section_symbol ? 0 : (sym.value + shift);
                new_sym.section_index = target_sec;
                target.symbols().push_back(std::move(new_sym));
            } else if (is_section_symbol) {
                existing_sym->value = 0;
                existing_sym->section_index = target_sec;
            } else if (existing_sym->is_external() || !existing_sym->is_defined()) {
                *existing_sym = sym;
                existing_sym->value += shift;
                existing_sym->section_index = target_sec;
            }
        } else {
            if (!target.find_symbol(sym.name)) {
                target.symbols().push_back(sym);
            }
        }
    }
    for (const auto& r : src.relocations()) {
        uint64_t shift = 0;
        uint32_t target_sec = 0;
        if (r.section_index < src.sections().size()) {
            const auto& sec_name = src.sections()[r.section_index].name;
            auto it = section_offsets.find(sec_name);
            if (it != section_offsets.end()) shift = it->second;
            auto it_idx = section_target_idx.find(sec_name);
            if (it_idx != section_target_idx.end()) target_sec = it_idx->second;
        }
        auto new_r = r;
        new_r.offset += shift;
        new_r.section_index = target_sec;
        if (r.symbol_name.starts_with(".")) {
            auto it_sym_sec = section_offsets.find(r.symbol_name);
            if (it_sym_sec != section_offsets.end()) {
                new_r.addend += static_cast<int64_t>(it_sym_sec->second);
            }
            new_r.symbol_name = std::string(canonical_section_name(r.symbol_name));
        }
        target.relocations().push_back(std::move(new_r));
    }
    return true;
}

int run_link(const LinkOptions& opts) {
    const auto start_time = std::chrono::high_resolution_clock::now();

    if (opts.verbose) {
        std::cout << "[dgtl-link] Inputs: " << opts.input_files.size() << " file(s)\n";
        std::cout << "[dgtl-link] Target Output: " << opts.output_file << "\n";
        std::cout << "[dgtl-link] Entry Symbol: " << opts.entry_symbol << "\n";
    }

    if (opts.is_archive || opts.format == LinkFormat::StaticArchive || opts.format == LinkFormat::COFFArchive) {
        if (opts.verbose) {
            std::cout << "[dgtl-link] Building static archive from " << opts.input_files.size() << " object(s)...\n";
        }
        auto ar_res = dgtl::object::archive::UniversalArchiveWriter::create_static_archive(opts.output_file, opts.input_files);
        if (!ar_res.ok) {
            std::cerr << "error[dgtl-link]: failed to create static archive: " << ar_res.error_message << "\n";
            return 1;
        }
        if (opts.verbose) {
            std::cout << "[dgtl-link] Successfully wrote static archive: " << opts.output_file 
                      << " (" << ar_res.total_bytes << " bytes, " << ar_res.symbols_count << " symbols)\n";
        }
        if (opts.time_report) {
            const auto end_time = std::chrono::high_resolution_clock::now();
            const double ms = std::chrono::duration<double, std::milli>(end_time - start_time).count();
            std::cout << "[dgtl-link] Archive creation completed in " << std::fixed << std::setprecision(2) << ms << " ms\n";
        }
        return 0;
    }

    dgtl::object::ObjectFormat obj_fmt = dgtl::object::ObjectFormat::PE;
    if (opts.format == LinkFormat::COFFObject || opts.format == LinkFormat::COFFArchive) {
        obj_fmt = dgtl::object::ObjectFormat::COFF;
    } else if (opts.format == LinkFormat::ELFObject || opts.format == LinkFormat::ELFExecutable || opts.format == LinkFormat::StaticArchive) {
        obj_fmt = dgtl::object::ObjectFormat::ELF;
    } else if (opts.format == LinkFormat::MachOObject) {
        obj_fmt = dgtl::object::ObjectFormat::MachO;
    } else if (opts.format == LinkFormat::WASMBinary) {
        obj_fmt = dgtl::object::ObjectFormat::WASM;
    }

    dgtl::object::ObjectFile merged_obj(obj_fmt);
    dgtl::object::readers::ELFObjectReader elf_reader;
    dgtl::object::readers::PEObjectReader pe_reader;

    std::vector<dgtl::object::archive::UniversalArchiveReader> archives;

    for (const auto& in_path : opts.input_files) {
        if (!fs::exists(in_path)) {
            std::cerr << "error[dgtl-link]: input file not found: " << in_path << "\n";
            return 2;
        }

        // Detect static archives (.a / .lib / starts with !<arch>\n)
        bool is_archive_file = false;
        std::ifstream test_in(in_path, std::ios::binary);
        if (test_in) {
            char magic[8] = {0};
            test_in.read(magic, 8);
            if (test_in.gcount() == 8 && std::memcmp(magic, "!<arch>\n", 8) == 0) {
                is_archive_file = true;
            }
            test_in.close();
        }

        if (is_archive_file) {
            dgtl::object::archive::UniversalArchiveReader ar;
            if (ar.load_archive(in_path)) {
                if (opts.verbose) {
                    std::cout << "[dgtl-link] Loaded static archive: " << in_path 
                              << " (" << ar.members().size() << " members, "
                              << ar.symbol_map().size() << " symbols)\n";
                }
                archives.push_back(std::move(ar));
                continue;
            }
        }

        dgtl::object::ObjectFile current(obj_fmt);
        if (elf_reader.read_file(in_path, current)) {
            if (opts.verbose) {
                std::cout << "[dgtl-link] Loaded ELF object: " << in_path 
                          << " (" << current.sections().size() << " sections, "
                          << current.symbols().size() << " symbols)\n";
            }
            merge_object_file(merged_obj, current);
        } else if (pe_reader.read_file(in_path, current)) {
            if (opts.verbose) {
                std::cout << "[dgtl-link] Loaded PE/COFF object: " << in_path 
                          << " (" << current.sections().size() << " sections, "
                          << current.symbols().size() << " symbols)\n";
            }
            merge_object_file(merged_obj, current);
        } else {
            std::ifstream raw_in(in_path, std::ios::binary);
            if (!raw_in) {
                std::cerr << "error[dgtl-link]: failed to open input: " << in_path << "\n";
                return 2;
            }
            std::vector<uint8_t> raw_bytes((std::istreambuf_iterator<char>(raw_in)),
                                            std::istreambuf_iterator<char>());
            auto& text_sec = merged_obj.get_text_section();
            text_sec.append_bytes(raw_bytes.data(), raw_bytes.size());
            if (opts.verbose) {
                std::cout << "[dgtl-link] Appended raw code bytes from " << in_path 
                          << " (" << raw_bytes.size() << " bytes)\n";
            }
        }
    }

    // Iteratively resolve undefined external symbols from archives
    if (!archives.empty()) {
        std::unordered_set<std::string> loaded_keys;
        bool added_any = true;
        while (added_any) {
            added_any = false;
            std::unordered_set<std::string> undefined_syms;

            // 1. Check undefined symbols referenced by relocations
            for (const auto& rel : merged_obj.relocations()) {
                if (rel.symbol_name.empty()) continue;
                const auto* sym = merged_obj.find_symbol(rel.symbol_name);
                if (!sym || sym->is_external() || !sym->is_defined()) {
                    undefined_syms.insert(rel.symbol_name);
                }
            }

            // 2. Check declared external or undefined symbols in the symbol table
            for (const auto& sym : merged_obj.symbols()) {
                if (sym.is_external() || !sym.is_defined()) {
                    undefined_syms.insert(sym.name);
                }
            }

            // 3. Check requested entry symbol if not yet defined
            const auto* entry = merged_obj.find_symbol(opts.entry_symbol);
            if (!entry || !entry->is_defined()) {
                undefined_syms.insert(opts.entry_symbol);
            }

            if (undefined_syms.empty()) break;

            for (const auto& sym_name : undefined_syms) {
                for (size_t a_idx = 0; a_idx < archives.size(); ++a_idx) {
                    const auto& ar = archives[a_idx];
                    if (ar.has_symbol(sym_name)) {
                        uint32_t mem_off = ar.symbol_map().at(sym_name);
                        std::string key = std::to_string(a_idx) + ":" + std::to_string(mem_off);
                        if (loaded_keys.contains(key)) continue;
                        loaded_keys.insert(key);

                        dgtl::object::ObjectFile member_obj(obj_fmt);
                        if (ar.extract_object_file_for_symbol(sym_name, member_obj)) {
                            if (opts.verbose) {
                                std::cout << "[dgtl-link] Extracted archive member for symbol: " << sym_name << "\n";
                            }
                            merge_object_file(merged_obj, member_obj);
                            added_any = true;
                            break;
                        }
                    }
                }
            }
        }
    }

    if (!merged_obj.find_symbol(opts.entry_symbol)) {
        merged_obj.add_function_symbol(opts.entry_symbol, 0, merged_obj.get_text_section().size(), true);
    }

    std::vector<uint8_t> out_binary;
    dgtl::linker::LinkerDriver driver;
    dgtl::linker::OutputFormat driver_fmt = dgtl::linker::OutputFormat::PEExecutable;

    switch (opts.format) {
        case LinkFormat::PEExecutable:
            driver_fmt = dgtl::linker::OutputFormat::PEExecutable;
            break;
        case LinkFormat::COFFObject:
            driver_fmt = dgtl::linker::OutputFormat::COFFObject;
            break;
        case LinkFormat::COFFArchive:
            driver_fmt = dgtl::linker::OutputFormat::COFFArchive;
            break;
        case LinkFormat::ELFExecutable:
            driver_fmt = dgtl::linker::OutputFormat::ELFExecutable;
            break;
        case LinkFormat::ELFObject:
            driver_fmt = dgtl::linker::OutputFormat::ELFObject;
            break;
        case LinkFormat::StaticArchive:
            driver_fmt = dgtl::linker::OutputFormat::StaticArchive;
            break;
        case LinkFormat::MachOObject:
            driver_fmt = dgtl::linker::OutputFormat::MachOObject;
            break;
        case LinkFormat::WASMBinary:
            driver_fmt = dgtl::linker::OutputFormat::WASMBinary;
            break;
        default:
            driver_fmt = dgtl::linker::OutputFormat::PEExecutable;
            break;
    }

    if (opts.format == LinkFormat::PEExecutable) {
        dgtl::linker::pe::SovereignPELinkerOptions pe_opts;
        pe_opts.entry_symbol = opts.entry_symbol;
        if (opts.image_base != 0) {
            pe_opts.image_base = opts.image_base;
        }
        auto res = dgtl::linker::pe::SovereignPELinker::emit_standalone_binary(merged_obj, out_binary, pe_opts);
        if (!res.success) {
            std::cerr << "error[dgtl-link]: PE synthesis failed: " << res.message << "\n";
            return 3;
        }
    } else if (opts.format == LinkFormat::WASMBinary) {
        dgtl::linker::wasm::WASMBinaryLink wasm_linker;
        dgtl::linker::wasm::WASMLinkerOptions wasm_opts;
        wasm_opts.entry_symbol = opts.entry_symbol;
        if (!wasm_linker.link(merged_obj, out_binary, wasm_opts)) {
            std::cerr << "error[dgtl-link]: WASM binary synthesis failed\n";
            return 3;
        }
    } else if (opts.format == LinkFormat::ELFExecutable) {
        dgtl::linker::elf::ELFLinker elf_linker;
        uint64_t base = (opts.image_base != 0) ? opts.image_base : 0x400000;
        if (!elf_linker.link_executable(merged_obj, out_binary, base, opts.entry_symbol)) {
            std::cerr << "error[dgtl-link]: ELF executable synthesis failed\n";
            return 3;
        }
    } else {
        if (!driver.emit(driver_fmt, merged_obj, out_binary)) {
            std::cerr << "error[dgtl-link]: failed to synthesize binary output\n";
            return 3;
        }
    }

    fs::path out_path(opts.output_file);
    if (out_path.has_parent_path()) {
        fs::create_directories(out_path.parent_path());
    }

    std::ofstream out_stream(opts.output_file, std::ios::binary | std::ios::trunc);
    if (!out_stream) {
        std::cerr << "error[dgtl-link]: unable to create output file: " << opts.output_file << "\n";
        return 4;
    }

    out_stream.write(reinterpret_cast<const char*>(out_binary.data()), static_cast<std::streamsize>(out_binary.size()));
    out_stream.close();

    const auto end_time = std::chrono::high_resolution_clock::now();
    const auto elapsed_us = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time).count();

    std::cout << "[dgtl-link] Successfully linked " << opts.output_file 
              << " (" << out_binary.size() << " bytes)\n";

    if (opts.time_report || opts.verbose) {
        std::cout << "[dgtl-link] Link duration: " << std::fixed << std::setprecision(3)
                  << (elapsed_us / 1000.0) << " ms\n";
    }

    return 0;
}

} // namespace dgtl::cli::link

int main(int argc, char* argv[]) {
    auto opts = dgtl::cli::link::parse_args(argc, argv);
    if (!opts.has_value()) {
        return 1;
    }
    return dgtl::cli::link::run_link(*opts);
}
