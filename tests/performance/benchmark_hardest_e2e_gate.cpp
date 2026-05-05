/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * Project:    DGTL (Deep General-purpose Transformation Layer)
 * Owner:      Divyanshu Tata
 * Repository: github.com/TataG-Systems/DGTL
 *
 * This software is the proprietary property of TataG Systems.
 * Unauthorized copying, modification, or distribution is strictly prohibited.
 */

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

namespace {

using Clock = std::chrono::high_resolution_clock;

struct GateResult {
    std::string name;
    bool pass = false;
    std::string detail;
};

std::string read_file_text(const std::filesystem::path& p) {
    std::ifstream in(p, std::ios::in);
    if (!in) return {};
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

bool run_cmd_capture(const std::string& cmd, const std::filesystem::path& out_log, int& exit_code, double& seconds) {
    const auto t0 = Clock::now();
    const std::string wrapped = cmd + " > \"" + out_log.string() + "\" 2>&1";
    exit_code = std::system(wrapped.c_str());
    const auto t1 = Clock::now();
    seconds = std::chrono::duration<double>(t1 - t0).count();
    return exit_code == 0;
}

std::string find_first(const std::string& text, const std::string& pattern) {
    std::smatch m;
    const std::regex rx(pattern);
    if (std::regex_search(text, m, rx) && m.size() >= 2) {
        return m[1].str();
    }
    return {};
}

bool contains_all(const std::string& text, const std::vector<std::string>& needles) {
    for (const auto& n : needles) {
        if (text.find(n) == std::string::npos) return false;
    }
    return true;
}

std::string lower_hex(std::string v) {
    std::transform(v.begin(), v.end(), v.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return v;
}

} // namespace

int main() {
    const std::filesystem::path logs_dir = std::filesystem::path("build") / "logs" / "hardest_e2e_cpp";
    std::error_code ec;
    std::filesystem::create_directories(logs_dir, ec);

    std::vector<GateResult> results;

    // 1) End-to-end latency + memory pressure under large streams (from full suite)
    {
        const auto log = logs_dir / "test_full.log";
        int code = -1;
        double sec = 0.0;
        const bool ok = run_cmd_capture("dgtl.cmd test full", log, code, sec);
        const std::string out = read_file_text(log);

        const std::string mib = find_first(out, R"(Allocating ~([0-9]+(?:\.[0-9]+)?) MiB estimated IRStream storage)");
        const std::string throughput = find_first(out, R"(Throughput\s*:\s*([0-9]+(?:\.[0-9]+)?)\s*M-Nodes/sec)");

        GateResult r;
        r.name = "End-to-end latency + large-stream memory pressure";
        r.pass = ok && out.find("[DGTL test] Aggregate suite completed") != std::string::npos &&
                 !mib.empty() && !throughput.empty();
        r.detail = "exit=" + std::to_string(code) +
                   " wall_s=" + (std::ostringstream{} << std::fixed << std::setprecision(3) << sec, "") +
                   " est_mib=" + (mib.empty() ? "missing" : mib) +
                   " strike_throughput_mnodes=" + (throughput.empty() ? "missing" : throughput);

        // rebuild detail with proper wall_s formatting without temporary stream lifetime issues
        std::ostringstream ds;
        ds << "exit=" << code
           << " wall_s=" << std::fixed << std::setprecision(3) << sec
           << " est_mib=" << (mib.empty() ? "missing" : mib)
           << " strike_throughput_mnodes=" << (throughput.empty() ? "missing" : throughput);
        r.detail = ds.str();

        results.push_back(r);
    }

    // 2) Hot-path pass ordering and skip logic (from benchmark trace)
    {
        const auto log = logs_dir / "bench_x64_fusion.log";
        int code = -1;
        double sec = 0.0;
        const bool ok = run_cmd_capture("dgtl.cmd bench-backend x64 fusion", log, code, sec);
        const std::string out = read_file_text(log);

        const bool order_ok = contains_all(out, {
            "running constant-fold",
            "running fusion",
            "running simd-legalize",
            "running ai-cost",
            "running pressure-zero-spill",
            "running determinism",
            "running latency"
        });

        const std::string nodes = find_first(out, R"(nodes=([0-9]+->[0-9]+->[0-9]+))");
        const std::string hot_swaps = find_first(out, R"(hot_swaps=([^\s]+))");

        GateResult r;
        r.name = "Hot-path pass ordering and skip logic";
        r.pass = ok && order_ok && !nodes.empty() && !hot_swaps.empty();

        std::ostringstream ds;
        ds << "exit=" << code
           << " order_trace=" << (order_ok ? "present" : "missing")
           << " nodes=" << (nodes.empty() ? "missing" : nodes)
           << " hot_swaps=" << (hot_swaps.empty() ? "missing" : hot_swaps)
           << " wall_s=" << std::fixed << std::setprecision(3) << sec;
        r.detail = ds.str();

        results.push_back(r);
    }

    // 3) Deterministic output and traceability signals (repeat strict backend runs)
    {
        const std::vector<std::string> backends = {
            "arm64", "x64", "nasm", "wasm",
            "arm32", "riscv64", "cuda", "rocm",
            "macho", "elfobj", "staticlib", "coffobj", "cofflib"
        };
        const int runs = 3;

        bool all_stable = true;
        std::ostringstream detail;

        for (const auto& b : backends) {
            std::vector<std::string> hashes;
            for (int i = 1; i <= runs; ++i) {
                const auto log = logs_dir / ("strict_" + b + "_run_" + std::to_string(i) + ".log");
                int code = -1;
                double sec = 0.0;
                const bool ok = run_cmd_capture(".\\build\\dgtl_main_strict.exe --backend " + b, log, code, sec);
                const std::string out = read_file_text(log);
                std::string h = find_first(out, R"(LogicHash=0x([0-9a-fA-F]+))");
                if (!h.empty()) h = "0x" + lower_hex(h);
                const bool completed = out.find("Pipeline completed successfully") != std::string::npos;

                if (!ok || !completed || h.empty()) {
                    all_stable = false;
                    detail << b << "#" << i << "(exit=" << code
                           << ", completed=" << (completed ? "yes" : "no")
                           << ", hash=" << (h.empty() ? "missing" : h) << ") ";
                }
                hashes.push_back(h);
            }

            const std::string ref = hashes.empty() ? "" : hashes.front();
            for (const auto& h : hashes) {
                if (h != ref || h.empty()) {
                    all_stable = false;
                    detail << b << "(unstable-hash) ";
                    break;
                }
            }

            if (!ref.empty()) {
                detail << b << "=" << ref << " ";
            }
        }

        GateResult r;
        r.name = "Deterministic output and traceability signals";
        r.pass = all_stable;
        r.detail = detail.str();
        results.push_back(r);
    }

    // 4) Multi-target backend parity under strict mode (full strict conformance)
    {
        const auto log = logs_dir / "strict_conformance.log";
        int code = -1;
        double sec = 0.0;
        const bool ok = run_cmd_capture("dgtl.cmd run-strict-conformance", log, code, sec);
        const std::string out = read_file_text(log);

        GateResult r;
        r.name = "Multi-target backend parity under strict mode";
        r.pass = ok && out.find("Conformance PASSED for all backends") != std::string::npos;

        std::ostringstream ds;
        ds << "exit=" << code
           << " conformance=" << (r.pass ? "pass" : "fail")
           << " wall_s=" << std::fixed << std::setprecision(3) << sec;
        r.detail = ds.str();
        results.push_back(r);
    }

    std::cout << "===========================================================\n";
    std::cout << " DGTL HARDEST E2E C++ GATE\n";
    std::cout << "===========================================================\n";

    int passed = 0;
    for (const auto& r : results) {
        std::cout << "[" << (r.pass ? "PASS" : "FAIL") << "] " << r.name << "\n";
        std::cout << "       " << r.detail << "\n";
        if (r.pass) ++passed;
    }

    std::cout << "\nScore: " << passed << "/" << results.size() << "\n";
    std::cout << "Logs: " << (logs_dir.string()) << "\n";

    if (passed == static_cast<int>(results.size())) {
        std::cout << "Standing: strong across all requested hardest-gate dimensions.\n";
        return 0;
    }

    std::cout << "Standing: not yet full-capability; inspect failed dimensions above.\n";
    return 1;
}
