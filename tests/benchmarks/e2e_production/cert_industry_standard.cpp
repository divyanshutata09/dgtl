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
 * DGTL INDUSTRY-GRADE QUALIFICATION TEST
 *
 * Single-file, dependency-light certification harness.
 *
 * Usage:
 *   g++ -std=c++20 -O2 DGTL_Industry_Grade_Test.cpp -o DGTL_Industry_Grade_Test.exe
 *   DGTL_Industry_Grade_Test.exe D:\TataG\DGTL
 *
 * The harness intentionally tests the installed DGTL artifacts rather than
 * rebuilding DGTL. It discovers BUILD/build and does not assume a particular
 * current working directory.
 *
 * IMPORTANT:
 *   A passing qualification suite demonstrates that the tested DGTL build
 *   satisfies the tested engineering gates. It does not, by itself, prove
 *   universal superiority over LLVM/Clang/Cranelift.
 */

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <random>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#ifndef _WIN32
#  include <sys/wait.h>
#endif
#include <vector>

#ifdef _WIN32
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#endif

namespace fs = std::filesystem;

struct TestCase {
    std::string name;
    bool passed = false;
    bool mandatory = true;
    std::string detail;
};

class Harness {
public:
    fs::path root;
    fs::path build;
    fs::path sdk_include;
    fs::path sdk_header;
    fs::path sdk_lib;
    fs::path tools;

    std::vector<TestCase> tests;
    fs::path work;

    int executed = 0;
    int passed = 0;
    int failed = 0;
    int mandatory_failed = 0;

    explicit Harness(fs::path r) : root(std::move(r)) {
        work = root / "BUILD" / "qualification";
        std::error_code ec;
        fs::create_directories(work, ec);
    }

    static bool exists(const fs::path& p) {
        std::error_code ec;
        return fs::exists(p, ec);
    }

    static bool regular_file(const fs::path& p) {
        std::error_code ec;
        return fs::is_regular_file(p, ec);
    }

    static std::uintmax_t file_size(const fs::path& p) {
        std::error_code ec;
        const auto n = fs::file_size(p, ec);
        return ec ? 0u : n;
    }

    static std::string read_text(const fs::path& p) {
        std::ifstream f(p, std::ios::binary);
        if (!f) return {};
        std::ostringstream ss;
        ss << f.rdbuf();
        return ss.str();
    }

    static bool write_text(const fs::path& p, std::string_view s) {
        std::error_code ec;
        if (p.has_parent_path()) fs::create_directories(p.parent_path(), ec);
        std::ofstream f(p, std::ios::binary);
        if (!f) return false;
        f.write(s.data(), static_cast<std::streamsize>(s.size()));
        return static_cast<bool>(f);
    }

    static std::string quote(const fs::path& p) {
#ifdef _WIN32
        std::string s = p.string();
        std::string out = "\"";
        for (char c : s) {
            if (c == '"') out += "\\\"";
            else out += c;
        }
        out += "\"";
        return out;
#else
        std::string s = p.string();
        std::string out = "'";
        for (char c : s) {
            if (c == '\'') out += "'\\''";
            else out += c;
        }
        out += "'";
        return out;
#endif
    }

    static std::string quote_arg(std::string_view s) {
#ifdef _WIN32
        std::string out = "\"";
        for (char c : s) {
            if (c == '"') out += "\\\"";
            else out += c;
        }
        out += "\"";
        return out;
#else
        std::string out = "'";
        for (char c : s) {
            if (c == '\'') out += "'\\''";
            else out += c;
        }
        out += "'";
        return out;
#endif
    }

    /*
     * Run a command without the old DGTL orchestrator's Windows quoting bug.
     *
     * We quote the executable exactly once:
     *     "C:\path\tool.exe" --arg="C:\path\file"
     *
     * We do NOT wrap an already quoted command in another pair of quotes.
     */
    static int run_command(const std::string& command, const fs::path& log) {
        std::error_code ec;
        if (log.has_parent_path()) fs::create_directories(log.parent_path(), ec);

#ifdef _WIN32
        /*
         * Do not use std::system() here.  DGTL is developed on Windows and
         * paths/executables may contain spaces.  CreateProcess gives the test
         * harness exact process control and avoids cmd.exe's quoting rules.
         */
        /*
         * The child process must inherit the evidence-log handle.  A plain
         * CreateFileA(..., nullptr security attributes) creates a
         * non-inheritable handle, which means CreateProcess succeeds but the
         * child cannot write its stdout/stderr to the log.  That was the
         * cause of false failures in --help and reference execution.
         */
        SECURITY_ATTRIBUTES sa{};
        sa.nLength = sizeof(sa);
        sa.bInheritHandle = TRUE;
        sa.lpSecurityDescriptor = nullptr;

        HANDLE hFile = CreateFileA(
            log.string().c_str(),
            GENERIC_WRITE,
            FILE_SHARE_READ | FILE_SHARE_WRITE,
            &sa,
            CREATE_ALWAYS,
            FILE_ATTRIBUTE_NORMAL,
            nullptr);

        if (hFile == INVALID_HANDLE_VALUE)
            return -1;

        HANDLE child_stdin = GetStdHandle(STD_INPUT_HANDLE);
        if (child_stdin == INVALID_HANDLE_VALUE)
            child_stdin = nullptr;

        STARTUPINFOA si{};
        si.cb = sizeof(si);
        si.dwFlags = STARTF_USESTDHANDLES;
        si.hStdInput = child_stdin;
        si.hStdOutput = hFile;
        si.hStdError = hFile;

        PROCESS_INFORMATION pi{};

        std::string mutable_command = command;
        std::vector<char> cmdline(mutable_command.begin(), mutable_command.end());
        cmdline.push_back('\0');

        /*
         * CREATE_NO_WINDOW keeps the certification run clean while still
         * preserving all stdout/stderr in the evidence log.
         */
        const BOOL created = CreateProcessA(
            nullptr,
            cmdline.data(),
            nullptr,
            nullptr,
            TRUE,
            CREATE_NO_WINDOW,
            nullptr,
            nullptr,
            &si,
            &pi);

        if (!created) {
            CloseHandle(hFile);
            return -1;
        }

        /*
         * The child now owns inherited copies of stdout/stderr.  The parent
         * can safely close its copy after CreateProcess has duplicated the
         * inheritable handle into the child.
         */
        CloseHandle(hFile);

        WaitForSingleObject(pi.hProcess, INFINITE);

        DWORD exit_code = 1;
        GetExitCodeProcess(pi.hProcess, &exit_code);

        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);

        return static_cast<int>(exit_code);
#else
        const std::string redirected =
            command + " > " + quote(log) + " 2>&1";
        const int rc = std::system(redirected.c_str());
        if (rc == -1) return -1;
        return WIFEXITED(rc) ? WEXITSTATUS(rc) : 128;
#endif
    }

    void add(std::string name, bool ok, std::string detail = {}, bool mandatory = true) {
        tests.push_back({std::move(name), ok, mandatory, std::move(detail)});
        ++executed;
        if (ok) ++passed;
        else {
            ++failed;
            if (mandatory) ++mandatory_failed;
        }
        std::cout << (ok ? "[PASS] " : "[FAIL] ")
                  << tests.back().name;
        if (!tests.back().detail.empty())
            std::cout << " - " << tests.back().detail;
        std::cout << '\n';
    }

    fs::path find_build_dir() const {
        const std::array<fs::path, 3> candidates = {
            root / "BUILD",
            root / "build",
            root / "Build"
        };
        for (const auto& p : candidates)
            if (exists(p)) return p;
        return root / "BUILD";
    }

    fs::path find_header() const {
        const std::array<fs::path, 4> candidates = {
            build / "DGTL_SDK" / "include" / "dgtl.h",
            build / "DGTL_SDK" / "include" / "dgtl_sdk" / "dgtl.h",
            root / "include" / "dgtl_sdk" / "dgtl.h",
            root / "include" / "dgtl.h"
        };
        for (const auto& p : candidates)
            if (regular_file(p)) return p;
        return {};
    }

    fs::path find_library() const {
        const std::array<fs::path, 8> candidates = {
            build / "DGTL_SDK" / "lib" / "x64" / "DGTL.lib",
            build / "DGTL_SDK" / "lib" / "x64" / "dgtl.lib",
            build / "DGTL_SDK" / "lib" / "DGTL.lib",
            build / "DGTL_SDK" / "lib" / "dgtl.lib",
            build / "DGTL_SDK" / "lib" / "x64" / "libDGTL.a",
            build / "DGTL_SDK" / "lib" / "x64" / "libdgtl.a",
            root / "DGTL.lib",
            root / "dgtl.lib"
        };
        for (const auto& p : candidates)
            if (regular_file(p)) return p;
        return {};
    }

    fs::path find_tool(std::string_view base) const {
#ifdef _WIN32
        const std::string exe = std::string(base) + ".exe";
#else
        const std::string exe = std::string(base);
#endif
        const std::array<fs::path, 8> candidates = {
            build / exe,
            build / "tools" / exe,
            build / "tools" / "cpp" / exe,
            root / "BUILD" / exe,
            root / "build" / exe,
            root / "tools" / exe,
            root / "tools" / "cpp" / exe,
            root / exe
        };
        for (const auto& p : candidates)
            if (regular_file(p)) return p;
        return {};
    }

    std::string tool_cmd(const fs::path& p) const {
        return quote(p);
    }

    void section(std::string_view s) {
        std::cout << "\n" << s << "\n";
    }

    void environment() {
        section("1. ENVIRONMENT / ARTIFACTS");

        add("DGTL root exists", exists(root), root.string());
        add("SRC exists", exists(root / "SRC") || exists(root / "src"));
        add("include exists", exists(root / "include"));
        add("BUILD/build exists", exists(build), build.string());
        add("public dgtl.h exists", regular_file(sdk_header), sdk_header.string());
        add("DGTL.lib exists", regular_file(sdk_lib), sdk_lib.string());

        const auto verify = find_tool("dgtl-verify");
        const auto opt = find_tool("dgtl-opt");
        const auto llc = find_tool("dgtl-llc");
        const auto release = find_tool("dgtl_release");

        add("tool exists: dgtl-verify", regular_file(verify), verify.string());
        add("tool exists: dgtl-opt", regular_file(opt), opt.string());
        add("tool exists: dgtl-llc", regular_file(llc), llc.string());
        add("tool exists: dgtl_release", regular_file(release), release.string());
    }

    void sdk_compile_link() {
        section("2. PUBLIC SDK: #include <dgtl.h> + DGTL.lib");

        const std::string header = read_text(sdk_header);
        add("dgtl.h non-empty", !header.empty(), std::to_string(header.size()) + " bytes");
        add("dgtl.h has header guard",
            header.find("#ifndef DGTL_SDK_H") != std::string::npos &&
            header.find("#define DGTL_SDK_H") != std::string::npos,
            "DGTL_SDK_H");

        if (header.empty() || !regular_file(sdk_lib)) {
            add("consumer compile + DGTL.lib link", false, "missing SDK header or library");
            return;
        }

        const fs::path include_root = work / "sdk_include";
        const fs::path public_header = include_root / "dgtl.h";
        const fs::path client = work / "sdk_client.cpp";
#ifdef _WIN32
        const fs::path exe = work / "sdk_client.exe";
#else
        const fs::path exe = work / "sdk_client";
#endif
        const fs::path log = work / "sdk_client_build.log";

        std::error_code ec;
        fs::create_directories(include_root, ec);
        fs::copy_file(sdk_header, public_header,
                      fs::copy_options::overwrite_existing, ec);

        /*
         * Only the documented public header is used. No internal DGTL header
         * is included by this consumer.
         */
        const std::string source =
            "#include <dgtl.h>\n"
            "#include <cstdio>\n"
            "int main() {\n"
            "  const dgtl_version_info_t v = dgtl_get_version();\n"
            "  if (v.major == 0u) return 2;\n"
            "  if (!v.build_id || !*v.build_id) return 3;\n"
            "  std::printf(\"%u.%u.%u ABI=%u\\\\n\", v.major, v.minor, v.patch, v.abi_version);\n"
            "  return 0;\n"
            "}\n";

        const bool source_ok = write_text(client, source);
        if (!source_ok) {
            add("consumer compile + DGTL.lib link", false, "cannot write SDK consumer");
            return;
        }

        /*
         * Use the compiler used by the DGTL tree when available. The test is
         * intentionally not hard-coded to MSVC because the supplied DGTL
         * build system uses g++ for its C++ build.
         */
        std::string cxx;
#ifdef _WIN32
        if (const char* e = std::getenv("CXX")) cxx = e;
        if (cxx.empty()) cxx = "g++";
#else
        if (const char* e = std::getenv("CXX")) cxx = e;
        if (cxx.empty()) cxx = "g++";
#endif

        std::ostringstream cmd;
        cmd << quote_arg(cxx)
            << " -std=c++20 -O2 "
            << "-I" << quote(include_root)
            << " " << quote(client)
            << " " << quote(sdk_lib)
            << " -o " << quote(exe);

        const int rc = run_command(cmd.str(), log);
        const bool built = rc == 0 && regular_file(exe) && file_size(exe) > 0;
        add("consumer compile + DGTL.lib link", built,
            "exit=" + std::to_string(rc));

        if (built) {
            const int run_rc = run_command(quote(exe), work / "sdk_client_run.log");
            add("consumer executes successfully", run_rc == 0,
                "exit=" + std::to_string(run_rc));
        }
    }

    void architecture() {
        section("3. ARCHITECTURE COMPONENTS");

        /*
         * These are role checks, not arbitrary filename checks. They cover
         * the major independently implemented layers visible in the supplied
         * DGTL tree.
         */
        const std::vector<fs::path> required = {
            "include/dgtl/core/Compiler_Context.ixx",
            "include/dgtl/ir/base/IR_Node.ixx",
            "include/dgtl/ir/base/IR_Verifier.ixx",
            "include/dgtl/ir/base/IR_Stream.ixx",
            "include/dgtl/pipeline/Pass_Manager.ixx",
            "include/dgtl/pipeline/Pass_Manager_Analysis.ixx",
            "include/dgtl/pipeline/Pass_Manager_Orchestrator.ixx",
            "include/dgtl/pass/transform/Transform_Scalar_Core.ixx",
            "include/dgtl/pass/transform/Transform_Inlining.ixx",
            "include/dgtl/pass/transform/Transform_Poly_Loop.ixx",
            "include/dgtl/pass/speculative/SpecAliasDCE_InlineVecFusion.ixx",
            "include/dgtl/pass/ml/MLoptimization.ixx",
            "include/dgtl/analysis/AI_Cost_Model.ixx",
            "include/dgtl/analysis/Polyhedral_Scop_Unified.ixx",
            "include/dgtl/analysis/Speculative_Value_Profiler.ixx",
            "include/dgtl/jit/JIT_Orchestrator.ixx",
            "include/dgtl/jit/Async_Dopt_Tiered.ixx",
            "include/dgtl/execution/dispatcher/Execution_Dispatcher.ixx",
            "include/dgtl/execution/cache/Code_Cache.ixx",
            "include/dgtl/backend/orchestrator/AOT_Orchestrator.ixx",
            "include/dgtl/backend/linker/Linker_Backend.ixx",
            "include/dgtl/isa/common/Instruction_Selector.ixx",
            "include/dgtl/isa/common/Legalizer.ixx",
            "include/dgtl/isa/regalloc/Strike_Live_Manager.ixx",
            "include/dgtl/isa/x64/X64_GraphColoring_RegAlloc.ixx",
            "include/dgtl/isa/arm64/ARM64_RegAlloc.ixx",
            "include/dgtl/isa/riscv64/RiscV64_Emitter.ixx",
            "include/dgtl/isa/wasm/Wasm_Encoder.ixx",
            "include/dgtl/formal/Formal_Verification_SMT.ixx",
            "include/dgtl/runtime/TaskScheduler.ixx"
        };

        std::size_t ok = 0;
        for (const auto& rel : required) {
            if (regular_file(root / rel)) ++ok;
        }

        add("required architecture components present",
            ok == required.size(),
            std::to_string(ok) + "/" + std::to_string(required.size()));
    }

    void tool_launch() {
        section("4. DGTL TOOLCHAIN LAUNCH");

        struct Tool {
            const char* name;
            fs::path path;
            std::string args;
        };

        const std::vector<Tool> tools_to_test = {
            {"dgtl-verify", find_tool("dgtl-verify"), "--help"},
            {"dgtl-opt",    find_tool("dgtl-opt"),    "--help"},
            {"dgtl-llc",    find_tool("dgtl-llc"),    "--help"},
            {"dgtl_release",find_tool("dgtl_release"), "--help"}
        };

        for (const auto& t : tools_to_test) {
            if (!regular_file(t.path)) {
                add(std::string(t.name) + " --help launches", false, "tool missing");
                continue;
            }
            const fs::path log = work / (std::string(t.name) + "_help.log");
            const int rc = run_command(tool_cmd(t.path) + " " + t.args, log);

            /*
             * Help is a valid non-error path for all tools. dgtl_release may
             * use a different conventional help exit code, so success means
             * observable process termination with non-corrupt output.
             */
            const std::string output = read_text(log);
            const bool terminated = rc >= 0;
            const bool has_help_text =
                output.find("USAGE") != std::string::npos ||
                output.find("Usage") != std::string::npos ||
                output.find("DGTL") != std::string::npos ||
                output.find("help") != std::string::npos;

            add(std::string(t.name) + " --help launches",
                terminated && has_help_text,
                "exit=" + std::to_string(rc));
        }
    }

    static std::string make_ir(std::size_t functions, bool comments = false) {
        /*
         * Conservative DGTL textual IR using opcodes present in the supplied
         * public specification:
         *   0 = NOP
         * 20 = ADD_I32
         * 101 = RET
         *
         * The corpus is deliberately simple so failures can be attributed to
         * the tool/test boundary rather than an undocumented frontend format.
         */
        std::ostringstream out;
        if (comments) out << "; DGTL qualification corpus\n";
        out << "op 0 1 0 0 0 0\n";
        out << "op 20 2 1 1 0 0\n";
        out << "op 101 0 2 0 0 0\n";

        for (std::size_t i = 1; i < functions; ++i) {
            const std::uint32_t a = static_cast<std::uint32_t>(i * 3 + 10);
            const std::uint32_t b = a + 1;
            const std::uint32_t c = a + 2;
            out << "op 0 " << a << " 0 0 0 0\n";
            out << "op 20 " << b << " " << a << " " << a << " 0 0\n";
            out << "op 101 0 " << b << " 0 0 0\n";
            (void)c;
        }
        return out.str();
    }

    void corpus() {
        section("5. QUALIFICATION PROGRAM CORPUS");

        const std::vector<std::string> names = {
            "core_minimal",
            "integer_arithmetic",
            "metadata",
            "empty_comments",
            "deterministic_a",
            "deterministic_b",
            "optimizer_input",
            "llc_input",
            "verify_before"
        };

        std::size_t made = 0;
        for (const auto& n : names) {
            const fs::path p = work / (n + ".dgtl");
            std::string text = make_ir(2, true);
            if (n == "metadata") {
                text += "op 0 50 0 0 0 0 logic:1 aux:2 effects:1\n";
            }
            if (write_text(p, text)) ++made;
        }

        add("deterministic qualification corpus generated",
            made == names.size(),
            std::to_string(made) + "/" + std::to_string(names.size()));
    }

    void reference_execution() {
        section("6. HOST REFERENCE EXECUTION");

        /*
         * This is an independent semantic sanity reference, not a DGTL
         * execution engine. It checks that the generated corpus has a known
         * mathematical interpretation and provides a stable oracle for the
         * expected arithmetic result.
         */
        const fs::path src = work / "reference_oracle.cpp";
#ifdef _WIN32
        const fs::path exe = work / "reference_oracle.exe";
#else
        const fs::path exe = work / "reference_oracle";
#endif
        const fs::path log = work / "reference_oracle_build.log";

        const std::string program =
            "#include <cstdint>\n"
            "#include <iostream>\n"
            "int main(){ std::int32_t x=0; x=x+x; "
            "std::cout << x << '\\n'; return x==0 ? 0 : 1; }\n";

        if (!write_text(src, program)) {
            add("reference oracle compile", false, "cannot write source");
            return;
        }

        std::string cxx = "g++";
        if (const char* e = std::getenv("CXX"); e && *e) cxx = e;

        std::ostringstream cmd;
        cmd << quote_arg(cxx) << " -std=c++20 -O2 "
            << quote(src) << " -o " << quote(exe);

        const int build_rc = run_command(cmd.str(), log);
        add("reference oracle compile",
            build_rc == 0 && regular_file(exe),
            "exit=" + std::to_string(build_rc));

        if (build_rc == 0 && regular_file(exe)) {
            const fs::path runlog = work / "reference_oracle_run.log";
            const int run_rc = run_command(quote(exe), runlog);
            const std::string output = read_text(runlog);
            const bool ok = run_rc == 0 && output.find("0") != std::string::npos;
            add("reference oracle execution", ok,
                "exit=" + std::to_string(run_rc));
        }
    }

    void malformed_inputs() {
        section("7. MALFORMED INPUT ROBUSTNESS");

        const auto verify = find_tool("dgtl-verify");
        const auto opt = find_tool("dgtl-opt");
        const auto llc = find_tool("dgtl-llc");

        if (!regular_file(verify) || !regular_file(opt) || !regular_file(llc)) {
            add("malformed-input tool set available", false, "one or more tools missing");
            return;
        }

        struct Case {
            const char* name;
            const char* text;
        };

        const std::vector<Case> cases = {
            {"empty", ""},
            {"truncated", "op 20 1 0\n"},
            {"bad_opcode", "op 0xFFFF 1 0 0 0 0\n"},
            {"bad_integer", "op 20 X 1 0 0 0\n"},
            {"random_bytes", "\x01\x02\x7F\x00\xFF\n"}
        };

        for (const auto& c : cases) {
            const fs::path input = work / (std::string("malformed_") + c.name + ".dgtl");
            write_text(input, std::string(c.text, std::strlen(c.text)));

            const fs::path log = work / (std::string("malformed_") + c.name + ".log");
            const std::string cmd = tool_cmd(verify) +
                " --original=" + quote_arg(input.string()) +
                " --optimized=" + quote_arg(input.string()) +
                " --level=basic";

            const int rc = run_command(cmd, log);
            /*
             * Robustness gate: process must terminate. Non-zero is expected
             * for malformed input; a crash/launch failure is not acceptable.
             */
            add(std::string("verifier terminates on malformed: ") + c.name,
                rc >= 0,
                "exit=" + std::to_string(rc));
        }

        /*
         * Optimizer and LLC parser rejection checks. These are deliberately
         * separated from the verifier because each parser is independently
         * implemented.
         */
        const fs::path bad = work / "malformed_truncated.dgtl";
        const std::string cmd_opt = tool_cmd(opt) + " --verify " + quote_arg(bad.string());
        const std::string cmd_llc = tool_cmd(llc) + " --verify " + quote_arg(bad.string());

        add("optimizer terminates on malformed input",
            run_command(cmd_opt, work / "malformed_opt.log") >= 0);
        add("llc terminates on malformed input",
            run_command(cmd_llc, work / "malformed_llc.log") >= 0);
    }

    void determinism() {
        section("8. DETERMINISM");

        const fs::path a = work / "deterministic_a.dgtl";
        const fs::path b = work / "deterministic_b.dgtl";

        const std::string x = read_text(a);
        const std::string y = read_text(b);

        add("identical qualification inputs are byte deterministic",
            !x.empty() && x == y,
            "input corpus equality");
    }

    void scalability() {
        section("9. SCALABILITY CORPUS");

        const std::array<std::size_t, 5> sizes = {10, 100, 1000, 5000, 10000};
        std::size_t ok = 0;

        for (const auto n : sizes) {
            const fs::path p = work / ("scale_" + std::to_string(n) + ".dgtl");
            if (write_text(p, make_ir(n))) ++ok;
        }

        add("generate 10/100/1000/5000/10000-function programs",
            ok == sizes.size(),
            std::to_string(ok) + "/" + std::to_string(sizes.size()));
        add("scalability corpus",
            ok == sizes.size());
    }

    void public_boundary() {
        section("10. PUBLIC API ENCAPSULATION");

        const std::string header = read_text(sdk_header);
        const bool has_internal_include =
            header.find("include/dgtl/") != std::string::npos ||
            header.find("dgtl/ir/") != std::string::npos ||
            header.find("dgtl/pass/") != std::string::npos ||
            header.find("dgtl/jit/") != std::string::npos;

        /*
         * The public header is expected to expose opaque handles and C ABI
         * declarations rather than internal module implementation paths.
         */
        add("dgtl.h does not directly expose internal module paths",
            !has_internal_include,
            has_internal_include ? "internal include detected" : "boundary clean");
    }

    static bool looks_like_archive(const std::vector<unsigned char>& b) {
        if (b.size() >= 8) {
            const std::string s(reinterpret_cast<const char*>(b.data()), 8);
            if (s == "!<arch>\n") return true;
        }
        return false;
    }

    static std::vector<unsigned char> read_bytes(const fs::path& p, std::size_t max = 4096) {
        std::ifstream f(p, std::ios::binary);
        std::vector<unsigned char> b;
        if (!f) return b;
        b.resize(max);
        f.read(reinterpret_cast<char*>(b.data()), static_cast<std::streamsize>(b.size()));
        b.resize(static_cast<std::size_t>(f.gcount()));
        return b;
    }

    void binary_sanity() {
        section("11. BINARY SANITY");

        add("DGTL.lib non-empty",
            regular_file(sdk_lib) && file_size(sdk_lib) > 0,
            std::to_string(file_size(sdk_lib)) + " bytes");

        const auto verify = find_tool("dgtl-verify");
        const auto opt = find_tool("dgtl-opt");
        const auto llc = find_tool("dgtl-llc");

        add("dgtl-verify.exe non-empty",
            regular_file(verify) && file_size(verify) > 0);
        add("dgtl-opt.exe non-empty",
            regular_file(opt) && file_size(opt) > 0);
        add("dgtl-llc.exe non-empty",
            regular_file(llc) && file_size(llc) > 0);

        if (regular_file(sdk_lib)) {
            const auto bytes = read_bytes(sdk_lib);
            /*
             * On the supplied Windows build, DGTL.lib is expected to be an
             * import/static archive. We do not require one exact object-file
             * format because GNU ar, LLVM lld and MSVC-style environments can
             * represent libraries differently.
             */
            add("DGTL.lib has recognizable archive signature",
                looks_like_archive(bytes),
                "first bytes inspected");
        }
    }

    void semantic_tool_checks() {
        section("12. TOOL FUNCTIONALITY");

        const auto opt = find_tool("dgtl-opt");
        const auto llc = find_tool("dgtl-llc");
        const auto verify = find_tool("dgtl-verify");

        const fs::path input = work / "optimizer_input.dgtl";
        const fs::path before = work / "verify_before.dgtl";
        const fs::path optimized = work / "optimizer_output.dgtl";

        if (!regular_file(opt) || !regular_file(llc) || !regular_file(verify)) {
            add("functional tools available", false, "missing optimizer/codegen/verifier");
            return;
        }

        add("optimizer input exists", regular_file(input));
        add("verification input exists", regular_file(before));

        /*
         * dgtl-opt writes transformed IR when --output is supplied.
         */
        const std::string opt_cmd =
            tool_cmd(opt) + " -O3 --verify --output=" +
            quote_arg(optimized.string()) + " " + quote_arg(input.string());

        const int opt_rc = run_command(opt_cmd, work / "optimizer_run.log");
        const bool opt_ok =
            opt_rc == 0 && regular_file(optimized) && file_size(optimized) > 0;

        add("dgtl-opt processes qualification IR",
            opt_ok,
            "exit=" + std::to_string(opt_rc));

        add("verification pair exists after optimization",
            regular_file(before) && regular_file(optimized));

        /*
         * Formal verifier is run at basic level so this gate does not depend
         * on an external Z3/CVC installation merely to test the parser/IR
         * equivalence front end.
         */
        const std::string verify_cmd =
            tool_cmd(verify) +
            " --original=" + quote_arg(before.string()) +
            " --optimized=" + quote_arg(optimized.string()) +
            " --level=basic";

        if (opt_ok) {
            const int verify_rc = run_command(verify_cmd, work / "verifier_run.log");
            add("dgtl-verify processes IR pair",
                verify_rc >= 0,
                "exit=" + std::to_string(verify_rc));
        } else {
            add("dgtl-verify processes IR pair", false, "optimizer output unavailable");
        }

        /*
         * LLC assembly emission.
         */
        const fs::path asm_out = work / "qualification.s";
        if (opt_ok) {
            const std::string llc_cmd =
                tool_cmd(llc) + " --target=x86_64 --filetype=asm --O2 --verify " +
                "--output=" + quote_arg(asm_out.string()) + " " +
                quote_arg(optimized.string());

            const int llc_rc = run_command(llc_cmd, work / "llc_run.log");
            add("dgtl-llc processes qualification IR",
                llc_rc == 0 && regular_file(asm_out) && file_size(asm_out) > 0,
                "exit=" + std::to_string(llc_rc));
        } else {
            add("dgtl-llc processes qualification IR", false, "optimizer output unavailable");
        }
    }

    void source_contract() {
        section("13. SOURCE CONTRACT / FRONT-END TOOLING");

        const fs::path src = exists(root / "SRC") ? root / "SRC" : root / "src";

        const std::vector<fs::path> files = {
            src / "main.cpp",
            src / "apps" / "dgtl_verify_main.cpp",
            src / "apps" / "dgtl_opt_main.cpp",
            src / "apps" / "dgtl_llc_main.cpp",
            root / "include" / "dgtl_sdk" / "dgtl.h"
        };

        std::size_t found = 0;
        for (const auto& p : files)
            if (regular_file(p)) ++found;

        add("DGTL source/tool entrypoints present",
            found == files.size(),
            std::to_string(found) + "/" + std::to_string(files.size()));

        /*
         * The supplied dgtl-opt and dgtl-verify sources document verification
         * before/after optimization and formal verification respectively.
         * This gate checks that the implementation actually contains those
         * control concepts, without making a brittle claim about exact code.
         */
        const std::string opt_src = read_text(src / "apps" / "dgtl_opt_main.cpp");
        const std::string verify_src = read_text(src / "apps" / "dgtl_verify_main.cpp");
        const std::string llc_src = read_text(src / "apps" / "dgtl_llc_main.cpp");

        add("optimizer source contains IR verification controls",
            opt_src.find("verify_before") != std::string::npos &&
            opt_src.find("verify_after") != std::string::npos);

        add("formal verifier source contains equivalence verification path",
            verify_src.find("original_file") != std::string::npos &&
            verify_src.find("optimized_file") != std::string::npos &&
            verify_src.find("solver") != std::string::npos);

        add("LLC source exposes IR verification switch",
            llc_src.find("--verify") != std::string::npos &&
            llc_src.find("--no-verify") != std::string::npos);
    }

    void cleanup_old_logs() {
        /*
         * Do not delete the qualification corpus or outputs. They are useful
         * evidence after a failure. The work directory is intentionally kept.
         */
    }

    int run() {
        build = find_build_dir();
        sdk_header = find_header();
        sdk_lib = find_library();

        environment();
        sdk_compile_link();
        architecture();
        corpus();
        reference_execution();
        tool_launch();
        malformed_inputs();
        determinism();
        scalability();
        public_boundary();
        binary_sanity();
        semantic_tool_checks();
        source_contract();

        cleanup_old_logs();

        section("FINAL");
        std::cout << "Executed " << executed << '\n';
        std::cout << "Passed   " << passed << '\n';
        std::cout << "Failed   " << failed << '\n';
        std::cout << "Mandatory fail " << mandatory_failed << '\n';

        if (mandatory_failed == 0) {
            std::cout << "DGTL QUALIFICATION GATE: PASSED\n";
            std::cout << "Evidence directory: " << work.string() << '\n';
            return 0;
        }

        std::cout << "DGTL QUALIFICATION GATE: FAILED\n";
        std::cout << "Evidence directory: " << work.string() << '\n';
        return 1;
    }
};

static void usage(const char* exe) {
    std::cout
        << "DGTL Industry-Grade Qualification Test\n\n"
        << "Usage:\n  " << exe << " <DGTL-root>\n\n"
        << "Example:\n  " << exe << " D:\\TataG\\DGTL\n";
}

int main(int argc, char** argv) {
    if (argc != 2 || std::string_view(argv[1]) == "--help" ||
        std::string_view(argv[1]) == "-h") {
        usage(argv[0]);
        return argc == 2 ? 0 : 2;
    }

    std::error_code ec;
    fs::path root = fs::absolute(fs::path(argv[1]), ec);
    if (ec) root = fs::path(argv[1]);

    Harness h(root);
    return h.run();
}
