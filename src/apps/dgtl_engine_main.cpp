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

#if defined(_WIN32) || defined(_WIN64)
    #include <windows.h>
#else
    #include <sys/mman.h>
    #include <unistd.h>
#endif

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

void print_version() {
    std::cout << "dgtl-engine: DGTL Sovereign JIT Engine v2.1.0 (TataG Systems)\n"
              << "Standards: C++26 / Zero-Overhead Native Execution Coordinator\n";
}

void print_usage(std::string_view prog_name) {
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

std::optional<EngineOptions> parse_args(int argc, char* argv[]) {
    if (argc < 2) {
        print_usage(argv[0]);
        return std::nullopt;
    }

    EngineOptions opts;
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
        if (arg == "-s" || arg == "--stats") {
            opts.stats = true;
            continue;
        }
        if (arg == "-d" || arg == "--dry-run") {
            opts.dry_run = true;
            continue;
        }
        if (arg == "-e") {
            if (i + 1 < argc) {
                opts.entry_symbol = argv[++i];
                continue;
            }
            std::cerr << "error[dgtl-engine]: -e requires an entry symbol name\n";
            return std::nullopt;
        }
        if (arg.rfind("-e=", 0) == 0 || arg.rfind("--entry=", 0) == 0) {
            auto pos = arg.find('=');
            opts.entry_symbol = std::string(arg.substr(pos + 1));
            continue;
        }
        if (arg == "-c" || arg == "--cache-size") {
            if (i + 1 < argc) {
                opts.cache_size_mb = std::stoull(argv[++i]);
                continue;
            }
            std::cerr << "error[dgtl-engine]: -c requires a cache size in MB\n";
            return std::nullopt;
        }
        if (arg.rfind("-c=", 0) == 0 || arg.rfind("--cache-size=", 0) == 0) {
            auto pos = arg.find('=');
            opts.cache_size_mb = std::stoull(std::string(arg.substr(pos + 1)));
            continue;
        }
        if (arg == "-b" || arg == "--bench") {
            if (i + 1 < argc) {
                opts.benchmark_iterations = std::stoull(argv[++i]);
                continue;
            }
            std::cerr << "error[dgtl-engine]: -b requires iteration count\n";
            return std::nullopt;
        }
        if (arg.rfind("-b=", 0) == 0 || arg.rfind("--bench=", 0) == 0) {
            auto pos = arg.find('=');
            opts.benchmark_iterations = std::stoull(std::string(arg.substr(pos + 1)));
            continue;
        }

        if (arg.front() == '-') {
            std::cerr << "error[dgtl-engine]: unrecognized option '" << arg << "'\n";
            return std::nullopt;
        }

        if (opts.input_file.empty()) {
            opts.input_file = std::string(arg);
        } else {
            std::cerr << "error[dgtl-engine]: multiple input files specified: '" << arg << "'\n";
            return std::nullopt;
        }
    }

    if (opts.input_file.empty()) {
        std::cerr << "error[dgtl-engine]: no input file specified\n";
        return std::nullopt;
    }

    return opts;
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

int main(int argc, char* argv[]) {
    auto opts = dgtl::cli::engine::parse_args(argc, argv);
    if (!opts.has_value()) {
        return 1;
    }
    return dgtl::cli::engine::run_engine(*opts);
}
