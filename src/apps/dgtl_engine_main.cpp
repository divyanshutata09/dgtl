/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * Project:     DGTL (Deep General-purpose Transformation Layer)
 * Owner:       Divyanshu Tata
 * Repository:  github.com/TataG-Systems/DGTL
 *
 * Component:   DGTL Sovereign Engine Frontend (dgtl-engine)
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
#include <vector>
#include <span>

#if defined(_WIN32) || defined(_WIN64)
    #include <windows.h>
#else
    #include <sys/mman.h>
    #include <unistd.h>
#endif

import dgtl.tools.common;
import dgtl.execution;
import dgtl.execution.cache;
import dgtl.execution.dispatcher;
import dgtl.execution.loader;
import dgtl.object.file;
import dgtl.object.readers.elf;
import dgtl.object.readers.pe;
import dgtl.runtime.bulletins;

namespace fs = std::filesystem;

namespace dgtl::cli::engine {

struct EngineOptions {
    std::string input_file;
    std::string entry_symbol = "DGTL_kernel_entry";
    size_t cache_size_mb = 64;
    size_t benchmark_iterations = 1;
    bool verbose = false;
    bool stats = false;
    bool dry_run = false;
};

static void print_version() {
    std::cout << "dgtl-engine: DGTL Sovereign JIT Engine v2.1.0 (TataG Systems)\n"
              << "Standards: C++26 / Zero-Overhead Native Execution Coordinator\n";
}

static void print_usage(std::string_view prog_name) {
    std::cout << "DGTL Sovereign JIT Execution Engine (dgtl-engine)\n"
              << "Direct-to-memory JIT execution coordinator and native code runner\n\n"
              << "USAGE:\n"
              << "  " << prog_name << " [options] <input_file>\n\n"
              << "OPTIONS:\n"
              << "  -e, --entry=<symbol>     Entry point function symbol (default: 'DGTL_kernel_entry' or 'main')\n"
              << "  -c, --cache-size=<mb>    Execution code cache capacity in MB (default: 64)\n"
              << "  -b, --bench=<iters>      Benchmark kernel execution across N iterations\n"
              << "  -s, --stats              Display performance telemetry and cache hit ratio\n"
              << "  -d, --dry-run            Verify and load code into cache without jumping to entry\n"
              << "  -v, --verbose            Display detailed dispatch diagnostics\n"
              << "  --version                Display engine version information\n"
              << "  -h, --help               Display this help message\n\n"
              << "EXAMPLES:\n"
              << "  " << prog_name << " kernel.bin\n"
              << "  " << prog_name << " -e compute_sum --stats math_kernel.o\n"
              << "  " << prog_name << " --bench=100000 fast_pipeline.bin\n";
}

enum class ParseStatus {
    Success,
    HelpOrVersion,
    Error
};

ParseStatus parse_args(const std::vector<std::string>& args, std::string_view prog_name, EngineOptions& opts) {
    if (args.empty()) {
        print_usage(prog_name);
        return ParseStatus::Error;
    }

    for (size_t i = 0; i < args.size(); ++i) {
        std::string_view arg = args[i];

        if (arg == "-h" || arg == "--help") {
            print_usage(prog_name);
            return ParseStatus::HelpOrVersion;
        }
        if (arg == "--version") {
            print_version();
            return ParseStatus::HelpOrVersion;
        }
        if (arg == "-v" || arg == "--verbose") {
            opts.verbose = true;
            continue;
        }
        if (arg == "-s" || arg == "--stats") {
            opts.stats = true;
            continue;
        }
        if (arg == "-d" || arg == "--dry-run") {
            opts.dry_run = true;
            continue;
        }
        if (arg == "-e") {
            if (i + 1 < args.size()) {
                opts.entry_symbol = args[++i];
                continue;
            }
            std::cerr << "error[dgtl-engine]: -e requires an entry symbol name\n";
            return ParseStatus::Error;
        }
        if (arg.rfind("-e=", 0) == 0 || arg.rfind("--entry=", 0) == 0) {
            auto pos = arg.find('=');
            opts.entry_symbol = std::string(arg.substr(pos + 1));
            continue;
        }
        if (arg == "-c" || arg == "--cache-size") {
            if (i + 1 < args.size()) {
                opts.cache_size_mb = std::stoull(args[++i]);
                continue;
            }
            std::cerr << "error[dgtl-engine]: -c requires a cache size in MB\n";
            return ParseStatus::Error;
        }
        if (arg.rfind("-c=", 0) == 0 || arg.rfind("--cache-size=", 0) == 0) {
            auto pos = arg.find('=');
            opts.cache_size_mb = std::stoull(std::string(arg.substr(pos + 1)));
            continue;
        }
        if (arg == "-b" || arg == "--bench") {
            if (i + 1 < args.size()) {
                opts.benchmark_iterations = std::stoull(args[++i]);
                continue;
            }
            std::cerr << "error[dgtl-engine]: -b requires iteration count\n";
            return ParseStatus::Error;
        }
        if (arg.rfind("-b=", 0) == 0 || arg.rfind("--bench=", 0) == 0) {
            auto pos = arg.find('=');
            opts.benchmark_iterations = std::stoull(std::string(arg.substr(pos + 1)));
            continue;
        }

        if (arg.front() == '-') {
            std::cerr << "error[dgtl-engine]: unrecognized option '" << arg << "'\n";
            return ParseStatus::Error;
        }

        if (opts.input_file.empty()) {
            opts.input_file = std::string(arg);
        } else {
            std::cerr << "error[dgtl-engine]: multiple input files specified: '" << arg << "'\n";
            return ParseStatus::Error;
        }
    }

    if (opts.input_file.empty()) {
        std::cerr << "error[dgtl-engine]: no input file specified\n";
        return ParseStatus::Error;
    }

    return ParseStatus::Success;
}

int run_engine(const EngineOptions& opts) {
    if (!fs::exists(opts.input_file)) {
        std::cerr << "error[dgtl-engine]: input file not found: " << opts.input_file << "\n";
        return 2;
    }

    std::ifstream file(opts.input_file, std::ios::binary);
    if (!file) {
        std::cerr << "error[dgtl-engine]: cannot read file: " << opts.input_file << "\n";
        return 2;
    }

    std::vector<uint8_t> buffer((std::istreambuf_iterator<char>(file)),
                                 std::istreambuf_iterator<char>());
    file.close();

    if (buffer.empty()) {
        std::cerr << "error[dgtl-engine]: input file is empty\n";
        return 3;
    }

    if (opts.verbose) {
        std::cout << "[dgtl-engine] Read " << buffer.size() << " bytes from " << opts.input_file << "\n";
    }

    std::vector<uint8_t> executable_code;
    size_t entry_offset = 0;

    dgtl::object::ObjectFile obj;
    dgtl::object::readers::ELFObjectReader elf_reader;
    dgtl::object::readers::PEObjectReader pe_reader;

    if (elf_reader.read(buffer.data(), buffer.size(), obj)) {
        if (opts.verbose) {
            std::cout << "[dgtl-engine] Identified input as ELF64 object\n";
        }
        const auto* text_sec = obj.find_section(".text");
        if (!text_sec || text_sec->bytes.empty()) {
            std::cerr << "error[dgtl-engine]: ELF object does not contain a valid .text section\n";
            return 4;
        }
        executable_code = text_sec->bytes;

        for (const auto& sec : obj.sections()) {
            if (sec.name == ".rodata") {
                if (opts.verbose) std::cout << "[dgtl-engine] Loaded ELF .rodata: " << sec.bytes.size() << " bytes\n";
            } else if (sec.name == ".data") {
                if (opts.verbose) std::cout << "[dgtl-engine] Loaded ELF .data: " << sec.bytes.size() << " bytes\n";
            } else if (sec.is_bss() || sec.name == ".bss") {
                if (opts.verbose) std::cout << "[dgtl-engine] Reserved ELF .bss zero-fill memory: " << sec.size() << " bytes\n";
            }
        }

        const auto* sym = obj.find_symbol(opts.entry_symbol);
        if (sym) {
            entry_offset = sym->value;
        } else if (const auto* main_sym = obj.find_symbol("main")) {
            entry_offset = main_sym->value;
        }
    } else if (pe_reader.read(buffer.data(), buffer.size(), obj)) {
        if (opts.verbose) {
            std::cout << "[dgtl-engine] Identified input as PE/COFF image ("
                      << (obj.format() == dgtl::object::ObjectFormat::PE ? "PE executable" : "COFF object") << ")\n";
        }
        const auto* text_sec = obj.find_section(".text");
        if (!text_sec || text_sec->bytes.empty()) {
            std::cerr << "error[dgtl-engine]: PE/COFF binary does not contain a valid .text section\n";
            return 4;
        }
        executable_code = text_sec->bytes;

        for (const auto& sec : obj.sections()) {
            if (sec.name == ".rdata") {
                if (opts.verbose) std::cout << "[dgtl-engine] Loaded PE/COFF .rdata: " << sec.bytes.size() << " bytes\n";
            } else if (sec.name == ".data") {
                if (opts.verbose) std::cout << "[dgtl-engine] Loaded PE/COFF .data: " << sec.bytes.size() << " bytes\n";
            } else if (sec.is_bss() || sec.name == ".bss") {
                if (opts.verbose) std::cout << "[dgtl-engine] Reserved PE/COFF .bss zero-fill memory: " << sec.size() << " bytes\n";
            }
        }

        const auto* sym = obj.find_symbol(opts.entry_symbol);
        if (sym) {
            entry_offset = (obj.format() == dgtl::object::ObjectFormat::PE && sym->value >= text_sec->address)
                               ? (sym->value - text_sec->address)
                               : sym->value;
        } else if (const auto* main_sym = obj.find_symbol("main")) {
            entry_offset = (obj.format() == dgtl::object::ObjectFormat::PE && main_sym->value >= text_sec->address)
                               ? (main_sym->value - text_sec->address)
                               : main_sym->value;
        }
    } else if (buffer.size() >= 32 && (*reinterpret_cast<const uint32_t*>(buffer.data()) == 0xFEEDFACF || *reinterpret_cast<const uint32_t*>(buffer.data()) == 0xCFFAEDFE)) {
        const int32_t cputype = *reinterpret_cast<const int32_t*>(buffer.data() + 4);
        std::string arch = (cputype == 0x0100000C) ? "arm64" : ((cputype == 0x01000007) ? "x86_64" : "macho");
        if (opts.verbose) {
            std::cout << "[dgtl-engine] Identified input as Mach-O 64 container (" << arch << ")\n";
        }
        const uint32_t ncmds = *reinterpret_cast<const uint32_t*>(buffer.data() + 16);
        size_t cursor = 32;
        for (uint32_t c = 0; c < ncmds && cursor + 8 <= buffer.size(); ++c) {
            const uint32_t cmd = *reinterpret_cast<const uint32_t*>(buffer.data() + cursor);
            const uint32_t cmdsize = *reinterpret_cast<const uint32_t*>(buffer.data() + cursor + 4);
            if (cmd == 0x19 /* LC_SEGMENT_64 */ && cursor + 72 <= buffer.size()) {
                const uint32_t nsects = *reinterpret_cast<const uint32_t*>(buffer.data() + cursor + 64);
                size_t sect_cursor = cursor + 72;
                for (uint32_t s = 0; s < nsects && sect_cursor + 80 <= buffer.size(); ++s) {
                    char sname[17] = {0};
                    std::memcpy(sname, buffer.data() + sect_cursor, 16);
                    if (std::strcmp(sname, "__text") == 0) {
                        uint32_t text_off = *reinterpret_cast<const uint32_t*>(buffer.data() + sect_cursor + 48);
                        uint64_t text_sz  = *reinterpret_cast<const uint64_t*>(buffer.data() + sect_cursor + 40);
                        if (text_off + text_sz <= buffer.size()) {
                            executable_code.assign(buffer.begin() + text_off, buffer.begin() + text_off + text_sz);
                        }
                    }
                    sect_cursor += 80;
                }
            }
            cursor += cmdsize;
        }
        if (executable_code.empty()) {
            executable_code = std::move(buffer);
        }
    } else if (buffer.size() >= 8 && buffer[0] == 0x00 && buffer[1] == 'a' && buffer[2] == 's' && buffer[3] == 'm') {
        if (opts.verbose) {
            std::cout << "[dgtl-engine] Identified input as WebAssembly (WASM) binary module\n";
        }
        executable_code = std::move(buffer);
    } else {
        if (opts.verbose) {
            std::cout << "[dgtl-engine] Processing input as raw executable native code stream\n";
        }
        executable_code = std::move(buffer);
    }

    // Initialize Sovereign Execution Coordinator & Cache (lean cache in dry-run mode)
    const size_t cache_bytes = opts.dry_run
        ? std::max<size_t>(64 * 1024, executable_code.size() * 2)
        : (opts.cache_size_mb * 1024 * 1024);
    dgtl::execution::ExecutionCoordinator coordinator(cache_bytes);

    // Register with native code cache (W^X compliant executable memory)
    if (!coordinator.cache().store(opts.entry_symbol, executable_code.data(), executable_code.size(), true)) {
        std::cerr << "error[dgtl-engine]: failed to cache code into executable memory\n";
        return 5;
    }

    if (opts.dry_run) {
        std::cout << "[dgtl-engine] Dry run successful: code verified and cached ("
                  << executable_code.size() << " bytes). Skipping execution.\n";
        return 0;
    }

    const void* code_base = coordinator.cache().retrieve(opts.entry_symbol);
    if (!code_base) {
        std::cerr << "error[dgtl-engine]: failed to retrieve entry from code cache\n";
        return 5;
    }

    const uint8_t* entry_ptr = static_cast<const uint8_t*>(code_base) + entry_offset;

    if (opts.verbose) {
        std::cout << "[dgtl-engine] Dispatching entry point at " 
                  << static_cast<const void*>(entry_ptr) << "...\n";
    }

    int64_t return_val = 0;
    const auto start_time = std::chrono::high_resolution_clock::now();

    for (size_t i = 0; i < opts.benchmark_iterations; ++i) {
        return_val = coordinator.execute_native(entry_ptr);
    }

    const auto end_time = std::chrono::high_resolution_clock::now();
    const auto elapsed_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end_time - start_time).count();

    std::cout << "[dgtl-engine] Execution completed. Return code: " << return_val << "\n";

    if (opts.stats || opts.verbose || opts.benchmark_iterations > 1) {
        const double total_ms = elapsed_ns / 1000000.0;
        const double avg_us = (elapsed_ns / 1000.0) / static_cast<double>(opts.benchmark_iterations);

        std::cout << "\n=======================================================\n"
                  << "        DGTL SOVEREIGN ENGINE TELEMETRY & STATS        \n"
                  << "=======================================================\n"
                  << "  Iterations Executed    : " << opts.benchmark_iterations << "\n"
                  << "  Total Execution Time   : " << std::fixed << std::setprecision(3) << total_ms << " ms\n"
                  << "  Average Time / Kernel  : " << std::fixed << std::setprecision(3) << avg_us << " us\n"
                  << "  Code Size              : " << executable_code.size() << " bytes\n"
                  << "  Cache Hit Ratio        : " << std::fixed << std::setprecision(1) 
                  << (coordinator.cache().stats().hit_ratio() * 100.0) << " %\n"
                  << "  Cache Entries          : " << coordinator.cache().stats().entries << "\n"
                  << "=======================================================\n";
    }

    return static_cast<int>(return_val);
}

} // namespace dgtl::cli::engine

int dgtl_engine_tool_main(std::span<const std::string_view> raw_args, std::string_view invoked_as) {
    std::string prog_name = invoked_as.empty() ? "dgtl-engine" : std::string(invoked_as);
    std::vector<std::string> expanded = dgtl::tools::ArgExpander::expand(raw_args);

    dgtl::cli::engine::EngineOptions opts;
    auto status = dgtl::cli::engine::parse_args(expanded, prog_name, opts);
    if (status == dgtl::cli::engine::ParseStatus::HelpOrVersion) {
        return 0;
    }
    if (status == dgtl::cli::engine::ParseStatus::Error) {
        return 1;
    }
    return dgtl::cli::engine::run_engine(opts);
}
