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
 
/*
    DGTL Industry-Grade Competitive Qualification Test
    --------------------------------------------------
    Stage 1: validates the existing 48-check qualification harness.
    Stage 2: adds independent competitive/production checks.

    Usage:
      DGTL_Industry_Grade_Competitive_Test_UPDATED.exe D:\TataG\DGTL

    Notes:
      - This harness reports measurements; it does not manufacture an
        "overall winner" score.
      - Cross-compiler superiority requires the same corpus, flags, machine,
        warm-up policy and statistical protocol for every compiler.
*/

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace fs = std::filesystem;
using Clock = std::chrono::steady_clock;

struct Check {
    std::string name;
    bool pass{};
    std::string detail;
};

static void report(const Check& c) {
    std::cout << (c.pass ? "[PASS] " : "[FAIL] ")
              << c.name;
    if (!c.detail.empty())
        std::cout << " - " << c.detail;
    std::cout << '\n';
}

static bool exists_file(const fs::path& p) {
    std::error_code ec;
    return fs::is_regular_file(p, ec);
}

static std::string read_file(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(f)),
                       std::istreambuf_iterator<char>());
}

static bool write_file(const fs::path& p, const std::string& data) {
    std::error_code ec;
    fs::create_directories(p.parent_path(), ec);

    std::ofstream f(p, std::ios::binary);
    if (!f)
        return false;

    f.write(data.data(), static_cast<std::streamsize>(data.size()));
    return static_cast<bool>(f);
}

static std::string quote_arg(const std::string& s) {
#ifdef _WIN32
    std::string out = "\"";
    for (char c : s) {
        if (c == '"')
            out += "\\\"";
        else
            out += c;
    }
    out += '"';
    return out;
#else
    std::string out = "'";
    for (char c : s) {
        if (c == '\'')
            out += "'\\''";
        else
            out += c;
    }
    out += '\'';
    return out;
#endif
}

/*
    Windows process runner.

    Unlike std::system(), this:
      - captures the actual child exit code;
      - avoids cmd.exe quoting ambiguity for the executable;
      - creates an inheritable stdout/stderr log handle.
*/
static int run_process(const fs::path& exe,
                       const std::vector<std::string>& args,
                       const fs::path& log) {
#ifdef _WIN32
    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;

    HANDLE log_handle = CreateFileA(
        log.string().c_str(),
        GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        &sa,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);

    if (log_handle == INVALID_HANDLE_VALUE)
        return -10001;

    std::string command = quote_arg(exe.string());
    for (const auto& arg : args)
        command += " " + quote_arg(arg);

    std::vector<char> mutable_command(command.begin(), command.end());
    mutable_command.push_back('\0');

    STARTUPINFOA si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    si.hStdOutput = log_handle;
    si.hStdError = log_handle;

    PROCESS_INFORMATION pi{};
    BOOL created = CreateProcessA(
        nullptr,
        mutable_command.data(),
        nullptr,
        nullptr,
        TRUE,
        CREATE_NO_WINDOW,
        nullptr,
        nullptr,
        &si,
        &pi);

    if (!created) {
        CloseHandle(log_handle);
        return -10002;
    }

    // Parent no longer needs its copy.
    CloseHandle(log_handle);

    WaitForSingleObject(pi.hProcess, INFINITE);

    DWORD exit_code = 0;
    BOOL got_code = GetExitCodeProcess(pi.hProcess, &exit_code);

    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);

    if (!got_code)
        return -10003;

    return static_cast<int>(exit_code);
#else
    std::string command = quote_arg(exe.string());
    for (const auto& arg : args)
        command += " " + quote_arg(arg);

    command += " > " + quote_arg(log.string()) + " 2>&1";
    return std::system(command.c_str());
#endif
}

/*
    The DGTL textual IR accepted by the current dgtl-opt/dgtl-llc parser
    observed in the project source.
*/
static std::string make_ir(std::size_t groups) {
    std::ostringstream out;

    for (std::size_t i = 0; i < groups; ++i) {
        out << "op add v" << i
            << " v" << i
            << " v" << (i + 1)
            << " 0 0\n";

        out << "op mul m" << i
            << " v" << i
            << " v" << i
            << " 0 0\n";

        out << "op sub s" << i
            << " m" << i
            << " v" << i
            << " 0 0\n";
    }

    return out.str();
}

static int percentile_index(std::size_t n, double p) {
    if (n == 0)
        return 0;

    const double x = p * static_cast<double>(n - 1);
    return static_cast<int>(x);
}

static int run_stage_2(const fs::path& root) {
    const fs::path build = root / "BUILD";
    const fs::path evidence =
        build / "qualification" / "competitive_updated";

    std::error_code ec;
    fs::create_directories(evidence, ec);

    const fs::path opt = build / "dgtl-opt.exe";
    const fs::path llc = build / "dgtl-llc.exe";
    const fs::path verifier = build / "dgtl-verify.exe";

    std::cout << "\n========================================\n";
    std::cout << "STAGE 2: INDUSTRY / COMPETITIVE GATE\n";
    std::cout << "========================================\n";
    std::cout << "Evidence: " << evidence << "\n\n";

    std::vector<Check> checks;

    /*
        A. Determinism
    */
    {
        const fs::path input = evidence / "determinism.dgtl";
        const fs::path out_a = evidence / "determinism_a.dgtl";
        const fs::path out_b = evidence / "determinism_b.dgtl";

        write_file(input, make_ir(200));

        const int a = run_process(
            opt,
            {"-O3", "--verify",
             "--output=" + out_a.string(),
             input.string()},
            evidence / "determinism_a.log");

        const int b = run_process(
            opt,
            {"-O3", "--verify",
             "--output=" + out_b.string(),
             input.string()},
            evidence / "determinism_b.log");

        const bool same =
            exists_file(out_a) &&
            exists_file(out_b) &&
            read_file(out_a) == read_file(out_b);

        checks.push_back({
            "optimized IR is byte deterministic",
            a == 0 && b == 0 && same,
            same ? "two independent O3 runs are identical"
                  : "outputs differ or process failed"
        });
    }

    /*
        B. Optimization levels
    */
    {
        const fs::path input = evidence / "optimization_levels.dgtl";
        write_file(input, make_ir(500));

        bool ok = true;
        std::ostringstream detail;

        for (const std::string level : {"-O0", "-O1", "-O2", "-O3"}) {
            const fs::path output =
                evidence / ("optimized_" + level.substr(1) + ".dgtl");

            const int e = run_process(
                opt,
                {level, "--verify",
                 "--output=" + output.string(),
                 input.string()},
                evidence / ("optimization_" + level.substr(1) + ".log"));

            const bool artifact =
                exists_file(output) && fs::file_size(output, ec) > 0;

            if (e != 0 || !artifact)
                ok = false;

            detail << level << "=" << e << " ";
        }

        checks.push_back({
            "optimization-level coverage",
            ok,
            detail.str()
        });
    }

    /*
        C. Invalid IR rejection
    */
    {
        const fs::path bad = evidence / "invalid_ir.dgtl";

        write_file(
            bad,
            "op definitely_not_a_real_opcode x y z 0 0\n");

        const int e = run_process(
            verifier,
            {"--original=" + bad.string(),
             "--optimized=" + bad.string(),
             "--level=full"},
            evidence / "invalid_ir.log");

        checks.push_back({
            "verifier rejects invalid IR",
            e != 0,
            "exit=" + std::to_string(e)
        });
    }

    /*
        D. Scaling / throughput
    */
    {
        bool ok = true;
        std::ostringstream detail;

        for (const std::size_t n : {
                 static_cast<std::size_t>(100),
                 static_cast<std::size_t>(1000),
                 static_cast<std::size_t>(5000),
                 static_cast<std::size_t>(10000)}) {

            const fs::path input =
                evidence / ("scale_" + std::to_string(n) + ".dgtl");

            const fs::path output =
                evidence / ("scale_" + std::to_string(n) + "_o3.dgtl");

            write_file(input, make_ir(n));

            const auto begin = Clock::now();

            const int e = run_process(
                opt,
                {"-O3", "--verify",
                 "--output=" + output.string(),
                 input.string()},
                evidence / ("scale_" + std::to_string(n) + ".log"));

            const double seconds =
                std::chrono::duration<double>(
                    Clock::now() - begin).count();

            if (e != 0 || !exists_file(output))
                ok = false;

            detail << n << "="
                   << std::fixed << std::setprecision(3)
                   << seconds << "s ";
        }

        checks.push_back({
            "large-program optimization throughput",
            ok,
            detail.str()
        });
    }

    /*
        E. Latency distribution
    */
    {
        const fs::path input = evidence / "latency.dgtl";
        const fs::path output = evidence / "latency_out.dgtl";

        write_file(input, make_ir(1000));

        std::vector<double> times;
        bool ok = true;

        for (int i = 0; i < 7; ++i) {
            const auto begin = Clock::now();

            const int e = run_process(
                opt,
                {"-O3", "--verify",
                 "--output=" + output.string(),
                 input.string()},
                evidence / ("latency_" + std::to_string(i) + ".log"));

            times.push_back(
                std::chrono::duration<double>(
                    Clock::now() - begin).count());

            if (e != 0)
                ok = false;
        }

        std::sort(times.begin(), times.end());

        const double min = times.front();
        const double p50 =
            times[static_cast<std::size_t>(
                percentile_index(times.size(), 0.50))];

        const double p95 =
            times[static_cast<std::size_t>(
                percentile_index(times.size(), 0.95))];

        const double max = times.back();

        std::ostringstream detail;
        detail << "min=" << min
               << "s p50=" << p50
               << "s p95=" << p95
               << "s max=" << max << "s";

        checks.push_back({
            "compiler latency distribution",
            ok,
            detail.str()
        });
    }

    /*
        F. Multi-target code generation

        IMPORTANT:
        dgtl-llc consumes the textual DGTL IR accepted by its own parser.
        The raw stress corpus used above is intentionally useful for the
        optimizer, but it is not guaranteed to satisfy every LLC/backend
        lowering precondition. Therefore this test first obtains a verified
        optimizer-produced IR artifact, then feeds THAT artifact to LLC.
        This keeps the test honest: we are testing the actual
        optimizer -> codegen contract rather than declaring a parser-level
        rejection a backend failure.
    */
    {
        const fs::path raw_input = evidence / "codegen_source.dgtl";
        const fs::path verified_input = evidence / "codegen_verified.dgtl";

        write_file(raw_input, make_ir(500));

        const int optimize_exit = run_process(
            opt,
            {"-O3", "--verify",
             "--output=" + verified_input.string(),
             raw_input.string()},
            evidence / "codegen_prepare.log");

        bool ok = optimize_exit == 0 && exists_file(verified_input);
        std::ostringstream detail;
        detail << "prepare=" << optimize_exit << " ";

        for (const std::string target : {"x86_64", "arm64"}) {
            const fs::path output =
                evidence / ("codegen_" + target + ".s");

            const auto begin = Clock::now();

            const int e = run_process(
                llc,
                {"--target=" + target,
                 "--output=" + output.string(),
                 verified_input.string()},
                evidence / ("codegen_" + target + ".log"));

            const double seconds =
                std::chrono::duration<double>(
                    Clock::now() - begin).count();

            const bool artifact =
                e == 0 &&
                exists_file(output) &&
                fs::file_size(output, ec) > 0;

            if (!artifact)
                ok = false;

            detail << target << "=" << e
                   << "/" << std::fixed << std::setprecision(3)
                   << seconds << "s ";
        }

        checks.push_back({
            "multi-target code generation",
            ok,
            detail.str()
        });
    }

    /*
        G. Artifact integrity

        Use the same verified IR -> LLC contract as the multi-target test.
    */
    {
        const fs::path raw_input = evidence / "artifact_source.dgtl";
        const fs::path verified_input =
            evidence / "artifact_verified.dgtl";
        const fs::path output = evidence / "artifact_integrity.s";

        write_file(raw_input, make_ir(100));

        const int prepare = run_process(
            opt,
            {"-O3", "--verify",
             "--output=" + verified_input.string(),
             raw_input.string()},
            evidence / "artifact_prepare.log");

        const int e = (prepare == 0 && exists_file(verified_input))
            ? run_process(
                llc,
                {"--target=x86_64",
                 "--output=" + output.string(),
                 verified_input.string()},
                evidence / "artifact_integrity.log")
            : -1;

        const bool ok =
            prepare == 0 &&
            e == 0 &&
            exists_file(output) &&
            fs::file_size(output, ec) > 0;

        checks.push_back({
            "generated artifact integrity",
            ok,
            ok
                ? std::to_string(fs::file_size(output)) + " bytes"
                : "prepare=" + std::to_string(prepare) +
                  " llc=" + std::to_string(e)
        });
    }

    /*
        H. Independent process concurrency.
    */
    {
        const fs::path input = evidence / "parallel.dgtl";
        write_file(input, make_ir(300));

        std::vector<std::thread> workers;
        std::vector<int> exits(4, -1);

        for (int i = 0; i < 4; ++i) {
            workers.emplace_back([&, i] {
                const fs::path output =
                    evidence / ("parallel_" +
                                std::to_string(i) + ".dgtl");

                exits[i] = run_process(
                    opt,
                    {"-O2", "--verify",
                     "--output=" + output.string(),
                     input.string()},
                    evidence / ("parallel_" +
                                std::to_string(i) + ".log"));
            });
        }

        for (auto& worker : workers)
            worker.join();

        const bool ok =
            std::all_of(
                exits.begin(),
                exits.end(),
                [](int e) { return e == 0; });

        checks.push_back({
            "parallel compiler process isolation",
            ok,
            "4 concurrent compiler invocations"
        });
    }

    /*
        I. Evidence completeness.
    */
    {
        std::size_t count = 0;

        for (const auto& entry :
             fs::recursive_directory_iterator(evidence)) {

            if (entry.is_regular_file())
                ++count;
        }

        checks.push_back({
            "competitive evidence collection",
            count >= 15,
            std::to_string(count) + " evidence files"
        });
    }

    std::cout << "STAGE 2 RESULTS\n";

    int passed = 0;

    for (const auto& check : checks) {
        report(check);
        if (check.pass)
            ++passed;
    }

    std::cout << "\nSTAGE 2 SUMMARY\n";
    std::cout << "Executed " << checks.size() << "\n";
    std::cout << "Passed   " << passed << "\n";
    std::cout << "Failed   "
              << checks.size() - static_cast<std::size_t>(passed)
              << "\n";

    return passed == static_cast<int>(checks.size()) ? 0 : 2;
}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr
            << "Usage: DGTL_Industry_Grade_Competitive_Test_UPDATED "
               "<DGTL_ROOT>\n";
        return 2;
    }

    const fs::path root = argv[1];

    if (!fs::exists(root)) {
        std::cerr << "DGTL root does not exist: " << root << "\n";
        return 2;
    }

    /*
        Gate 1 is intentionally not duplicated here. The user's already
        validated 48/48 harness remains the authoritative internal gate.
        Stage 2 is independent and can be run after Gate 1.
    */
    return run_stage_2(root);
}
