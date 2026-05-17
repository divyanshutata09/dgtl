/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 */

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <regex>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace fs = std::filesystem;

namespace {

struct Options {
    fs::path root{"."};
};

[[nodiscard]] std::string now_local_string() {
    const auto now = std::chrono::system_clock::now();
    const auto tt = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &tt);
#else
    localtime_r(&tt, &tm);
#endif
    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");
    return oss.str();
}

[[nodiscard]] std::string read_all(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    if (!in.is_open()) return {};
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

bool write_all(const fs::path& p, std::string_view content) {
    fs::create_directories(p.parent_path());
    std::ofstream out(p, std::ios::binary | std::ios::trunc);
    if (!out.is_open()) return false;
    out.write(content.data(), static_cast<std::streamsize>(content.size()));
    return out.good();
}

[[nodiscard]] std::vector<std::string> regex_lines(std::string_view text, const std::regex& re, std::size_t max_count) {
    std::vector<std::string> out;
    std::istringstream iss{std::string(text)};
    std::string line;
    while (std::getline(iss, line)) {
        if (std::regex_search(line, re)) {
            const std::string t = std::regex_replace(line, std::regex("^\\s+|\\s+$"), "");
            if (!t.empty() && std::find(out.begin(), out.end(), t) == out.end()) {
                out.push_back(t);
                if (out.size() >= max_count) break;
            }
        }
    }
    return out;
}

[[nodiscard]] std::string join_lines(const std::vector<std::string>& lines) {
    std::string out;
    for (const auto& l : lines) {
        out += l;
        out.push_back('\n');
    }
    return out;
}

[[nodiscard]] bool parse_args(int argc, char** argv, Options& opts) {
    for (int i = 1; i < argc; ++i) {
        std::string_view arg(argv[i]);
        if (arg == "--root") {
            if (i + 1 >= argc) return false;
            opts.root = argv[++i];
        } else if (arg == "--help") {
            std::cout << "Usage: regen-mydocs-prelaunch [--root <repo>]\n";
            return false;
        } else {
            return false;
        }
    }
    return true;
}

[[nodiscard]] std::string infer_intent(std::string_view p) {
    const std::string s(p);
    if (std::regex_search(s, std::regex("emitter|encoder|lowering|linker|target", std::regex::icase))) {
        return "Lower IR into target-specific representation while preserving semantic correctness.";
    }
    if (std::regex_search(s, std::regex("jit|runtime|orchestrator|scheduler", std::regex::icase))) {
        return "Drive runtime execution, specialization, and backend dispatch with predictable checks.";
    }
    if (std::regex_search(s, std::regex("pass|transform|legalizer|optimizer|dce|gvn|folder", std::regex::icase))) {
        return "Apply correctness-preserving transformations to improve cost and latency.";
    }
    return "Provide reusable infrastructure used by the DGTL compilation and execution pipeline.";
}

}  // namespace

int main(int argc, char** argv) {
    Options opts;
    if (!parse_args(argc, argv, opts)) {
        std::cerr << "Invalid arguments. Use --help.\n";
        return 1;
    }

    const fs::path root = fs::weakly_canonical(opts.root);
    const fs::path include_root = root / "include/dgtl";
    const fs::path my_docs = root / "My_Docs";

    const std::vector<std::string> top_folders = {"analysis", "core", "graph", "ir", "isa", "jit", "pass", "pipeline"};

    std::size_t generated = 0;
    for (const auto& top : top_folders) {
        const fs::path src_top = include_root / top;
        if (!fs::exists(src_top)) continue;

        for (auto it = fs::recursive_directory_iterator(src_top), end = fs::recursive_directory_iterator(); it != end; ++it) {
            if (!it->is_regular_file()) continue;
            if (it->path().extension() != ".hpp") continue;

            const fs::path header = it->path();
            const fs::path rel = fs::relative(header, src_top);
            const fs::path out_path = my_docs / top / rel.parent_path() / (rel.filename().string() + ".md");

            const std::string content = read_all(header);
            const auto includes = regex_lines(content, std::regex(R"(^\s*#include\s+[<"].+[>"])", std::regex::icase), 200);
            const auto classes = regex_lines(content, std::regex(R"(^\s*class\s+[A-Za-z_][A-Za-z0-9_]*)", std::regex::icase), 120);
            const auto structs = regex_lines(content, std::regex(R"(^\s*struct\s+[A-Za-z_][A-Za-z0-9_]*)", std::regex::icase), 120);
            const auto funcs = regex_lines(content, std::regex(R"(^\s*(?:\[\[nodiscard\]\]\s*)?(?:inline\s+)?(?:static\s+)?(?:virtual\s+)?[A-Za-z_][A-Za-z0-9_:<>,\s*&]*\s+[A-Za-z_][A-Za-z0-9_]*\s*\([^;]*\)\s*(?:const)?\s*(?:noexcept)?\s*(?:override)?\s*(?:=\s*0)?\s*[{;]$)", std::regex::icase), 300);

            std::ostringstream doc;
            doc << "# " << header.filename().string() << "\n\n";
            doc << "Source Header: include/dgtl/" << top << "/" << rel.generic_string() << "\n";
            doc << "Last Synced: " << now_local_string() << "\n\n";
            doc << "## Vision\n";
            doc << "This component exists to make the compiler/runtime pipeline explicit, safe, and predictable.\n\n";
            doc << "## Why This File Exists\n";
            doc << "- " << infer_intent(header.generic_string()) << "\n";
            doc << "- Keep responsibilities localized so surrounding modules can compose safely.\n\n";

            doc << "## Include Dependencies\n";
            if (includes.empty()) doc << "- none\n";
            else for (const auto& l : includes) doc << "- " << l << "\n";
            doc << "\n";

            doc << "## Class and Struct Catalog\n";
            if (classes.empty() && structs.empty()) {
                doc << "- none detected\n\n";
            } else {
                for (const auto& c : classes) doc << "- " << c << "\n";
                for (const auto& s : structs) doc << "- " << s << "\n";
                doc << "\n";
            }

            doc << "## Function Catalog\n";
            if (funcs.empty()) doc << "- none detected\n\n";
            else {
                for (const auto& f : funcs) doc << "- " << f << "\n";
                doc << "\n";
            }

            doc << "## Control Flow Graph and Map\n";
            doc << "```mermaid\n";
            doc << "flowchart TD\n";
            doc << "A[Inputs] --> B[Validate]\n";
            doc << "B --> C[Analyze]\n";
            doc << "C --> D[Transform]\n";
            doc << "D --> E[Finalize]\n";
            doc << "```\n";

            if (!write_all(out_path, doc.str())) {
                std::cerr << "Failed writing: " << out_path.string() << "\n";
                return 1;
            }
            ++generated;
        }
    }

    // Generate command docs index similar to the PowerShell utility.
    const fs::path cmd_out = my_docs / "commands";
    fs::create_directories(cmd_out);

    {
        const fs::path fast_md = cmd_out / "fast_regression.ps1.md";
        const fs::path source = root / "docs/My_Docs/commands/fast_regression.ps1";
        const std::string content = read_all(source);
        const auto snippets = regex_lines(content, std::regex(R"(^\s*Write-Host|^\s*if\s*\(|^\s*&\s+powershell|^\s*for(each)?\s*\(|^\s*\$)", std::regex::icase), 120);

        std::ostringstream md;
        md << "# fast_regression.ps1\n\n";
        md << "Source Script: docs/My_Docs/commands/fast_regression.ps1\n";
        md << "Last Synced: " << now_local_string() << "\n\n";
        md << "## Vision\nProvide deterministic prelaunch automation with clear pass/fail behavior.\n\n";
        md << "## Key Logic Snippets\n";
        if (snippets.empty()) md << "- Source script snippets unavailable.\n";
        else for (const auto& s : snippets) md << "- " << s << "\n";
        write_all(fast_md, md.str());
    }

    {
        const fs::path back_md = cmd_out / "backend_conformance_check.ps1.md";
        const fs::path source = root / "docs/My_Docs/commands/backend_conformance_check.ps1";
        const std::string content = read_all(source);
        const auto snippets = regex_lines(content, std::regex(R"(^\s*Write-Host|^\s*if\s*\(|^\s*&\s+powershell|^\s*for(each)?\s*\(|^\s*\$)", std::regex::icase), 120);

        std::ostringstream md;
        md << "# backend_conformance_check.ps1\n\n";
        md << "Source Script: docs/My_Docs/commands/backend_conformance_check.ps1\n";
        md << "Last Synced: " << now_local_string() << "\n\n";
        md << "## Vision\nValidate emitter outputs against deterministic backend expectations.\n\n";
        md << "## Key Logic Snippets\n";
        if (snippets.empty()) md << "- Source script snippets unavailable.\n";
        else for (const auto& s : snippets) md << "- " << s << "\n";
        write_all(back_md, md.str());
    }

    {
        std::ostringstream idx;
        idx << "# commands_index.md\n\n";
        idx << "Source Folder: My_Docs/commands\n";
        idx << "Last Synced: " << now_local_string() << "\n\n";
        idx << "## Scripts\n";
        idx << "- fast_regression.ps1.md\n";
        idx << "- backend_conformance_check.ps1.md\n";
        write_all(cmd_out / "commands_index.md", idx.str());
    }

    std::cout << "Enhanced markdown regenerated: " << generated << "\n";
    return 0;
}
