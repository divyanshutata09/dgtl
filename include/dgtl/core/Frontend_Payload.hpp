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

#pragma once

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "dgtl/core/FrontendHelpers.hpp"

namespace dgtl::core {

struct FrontendPayloadModule {
    std::vector<FrontendLoweringFunction> functions;
};

inline std::string frontend_payload_escape(std::string_view input) {
    static constexpr char kHex[] = "0123456789ABCDEF";
    std::string out;
    out.reserve(input.size());
    for (const unsigned char c : input) {
        const bool keep =
            (c >= 'a' && c <= 'z') ||
            (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') ||
            c == '_' || c == '-' || c == '.' || c == ':';
        if (keep) {
            out.push_back(static_cast<char>(c));
            continue;
        }

        out.push_back('%');
        out.push_back(kHex[(c >> 4) & 0x0F]);
        out.push_back(kHex[c & 0x0F]);
    }
    return out;
}

inline bool frontend_payload_unescape(std::string_view input, std::string& out) {
    out.clear();
    out.reserve(input.size());

    auto hex_value = [](char c) -> int {
        if (c >= '0' && c <= '9') {
            return c - '0';
        }
        if (c >= 'A' && c <= 'F') {
            return c - 'A' + 10;
        }
        if (c >= 'a' && c <= 'f') {
            return c - 'a' + 10;
        }
        return -1;
    };

    for (size_t i = 0; i < input.size(); ++i) {
        if (input[i] != '%') {
            out.push_back(input[i]);
            continue;
        }

        if (i + 2 >= input.size()) {
            return false;
        }

        const int hi = hex_value(input[i + 1]);
        const int lo = hex_value(input[i + 2]);
        if (hi < 0 || lo < 0) {
            return false;
        }

        out.push_back(static_cast<char>((hi << 4) | lo));
        i += 2;
    }

    return true;
}

inline const char* frontend_instruction_kind_name(FrontendInstructionKind kind) {
    switch (kind) {
    case FrontendInstructionKind::const_i64: return "const_i64";
    case FrontendInstructionKind::const_str: return "const_str";
    case FrontendInstructionKind::add_i32: return "add_i32";
    case FrontendInstructionKind::sub_i32: return "sub_i32";
    case FrontendInstructionKind::mul_i32: return "mul_i32";
    case FrontendInstructionKind::div_i32: return "div_i32";
    case FrontendInstructionKind::mod_i32: return "mod_i32";
    case FrontendInstructionKind::cmp_eq: return "cmp_eq";
    case FrontendInstructionKind::cmp_ne: return "cmp_ne";
    case FrontendInstructionKind::cmp_lt: return "cmp_lt";
    case FrontendInstructionKind::cmp_le: return "cmp_le";
    case FrontendInstructionKind::cmp_gt: return "cmp_gt";
    case FrontendInstructionKind::cmp_ge: return "cmp_ge";
    case FrontendInstructionKind::call: return "call";
    case FrontendInstructionKind::ret: return "ret";
    case FrontendInstructionKind::label: return "label";
    case FrontendInstructionKind::jmp: return "jmp";
    case FrontendInstructionKind::br_if: return "br_if";
    case FrontendInstructionKind::and_i32: return "and_i32";
    case FrontendInstructionKind::or_i32:  return "or_i32";
    case FrontendInstructionKind::xor_i32: return "xor_i32";
    case FrontendInstructionKind::shl_i32: return "shl_i32";
    case FrontendInstructionKind::shr_i32: return "shr_i32";
    default: return "const_i64";
    }
}

inline bool parse_frontend_instruction_kind(std::string_view text, FrontendInstructionKind& out) {
    if (text == "const_i64") {
        out = FrontendInstructionKind::const_i64;
        return true;
    }
    if (text == "const_str") {
        out = FrontendInstructionKind::const_str;
        return true;
    }
    if (text == "add_i32") {
        out = FrontendInstructionKind::add_i32;
        return true;
    }
    if (text == "sub_i32") {
        out = FrontendInstructionKind::sub_i32;
        return true;
    }
    if (text == "mul_i32") {
        out = FrontendInstructionKind::mul_i32;
        return true;
    }
    if (text == "call") {
        out = FrontendInstructionKind::call;
        return true;
    }
    if (text == "ret") {
        out = FrontendInstructionKind::ret;
        return true;
    }
    if (text == "div_i32") {
        out = FrontendInstructionKind::div_i32;
        return true;
    }
    if (text == "mod_i32") {
        out = FrontendInstructionKind::mod_i32;
        return true;
    }
    if (text == "cmp_eq") {
        out = FrontendInstructionKind::cmp_eq;
        return true;
    }
    if (text == "cmp_ne") {
        out = FrontendInstructionKind::cmp_ne;
        return true;
    }
    if (text == "cmp_lt") {
        out = FrontendInstructionKind::cmp_lt;
        return true;
    }
    if (text == "cmp_le") {
        out = FrontendInstructionKind::cmp_le;
        return true;
    }
    if (text == "cmp_gt") {
        out = FrontendInstructionKind::cmp_gt;
        return true;
    }
    if (text == "cmp_ge") {
        out = FrontendInstructionKind::cmp_ge;
        return true;
    }
    if (text == "label") {
        out = FrontendInstructionKind::label;
        return true;
    }
    if (text == "jmp") {
        out = FrontendInstructionKind::jmp;
        return true;
    }
    if (text == "br_if") {
        out = FrontendInstructionKind::br_if;
        return true;
    }
    if (text == "and_i32") {
        out = FrontendInstructionKind::and_i32;
        return true;
    }
    if (text == "or_i32") {
        out = FrontendInstructionKind::or_i32;
        return true;
    }
    if (text == "xor_i32") {
        out = FrontendInstructionKind::xor_i32;
        return true;
    }
    if (text == "shl_i32") {
        out = FrontendInstructionKind::shl_i32;
        return true;
    }
    if (text == "shr_i32") {
        out = FrontendInstructionKind::shr_i32;
        return true;
    }
    return false;
    }

inline bool write_frontend_payload_file(const FrontendPayloadModule& payload,
                                        std::string_view file_path,
                                        std::string* error = nullptr) {
    const std::filesystem::path path = std::filesystem::absolute(std::filesystem::path(file_path)).lexically_normal();
    if (path.has_parent_path()) {
        std::error_code ec;
        std::filesystem::create_directories(path.parent_path(), ec);
        if (ec) {
            if (error) {
                *error = "failed to create payload directory";
            }
            return false;
        }
    }

    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        if (error) {
            *error = "failed to open payload file for writing";
        }
        return false;
    }

    out << "DGTL_FRONTEND_PAYLOAD_V1\n";

    for (const auto& fn : payload.functions) {
        out << "function " << frontend_payload_escape(fn.name)
            << " " << fn.return_type_handle
            << " " << static_cast<unsigned int>(fn.return_abi_class)
            << "\n";

        for (const auto& param : fn.parameters) {
            out << "param " << param.value_id
                << " " << frontend_payload_escape(param.name)
                << " " << param.type_handle
                << " " << static_cast<unsigned int>(param.abi_class)
                << "\n";
        }

        for (const auto& ins : fn.instructions) {
            out << "ins " << frontend_instruction_kind_name(ins.kind) << " ";
            if (ins.kind == FrontendInstructionKind::const_i64) {
                out << ins.result_value_id << " " << ins.type_handle << " " << ins.immediate;
            } else if (ins.kind == FrontendInstructionKind::const_str) {
                out << ins.result_value_id << " " << ins.type_handle
                    << " " << frontend_payload_escape(ins.callee_name);
            } else if (ins.kind == FrontendInstructionKind::add_i32 ||
                       ins.kind == FrontendInstructionKind::sub_i32 ||
                       ins.kind == FrontendInstructionKind::mul_i32 ||
                       ins.kind == FrontendInstructionKind::div_i32 ||
                       ins.kind == FrontendInstructionKind::mod_i32 ||
                       ins.kind == FrontendInstructionKind::cmp_eq ||
                       ins.kind == FrontendInstructionKind::cmp_ne ||
                       ins.kind == FrontendInstructionKind::cmp_lt ||
                       ins.kind == FrontendInstructionKind::cmp_le ||
                       ins.kind == FrontendInstructionKind::cmp_gt ||
                       ins.kind == FrontendInstructionKind::cmp_ge ||
                       ins.kind == FrontendInstructionKind::and_i32 ||
                       ins.kind == FrontendInstructionKind::or_i32  ||
                       ins.kind == FrontendInstructionKind::xor_i32 ||
                       ins.kind == FrontendInstructionKind::shl_i32 ||
                       ins.kind == FrontendInstructionKind::shr_i32) {
                out << ins.result_value_id << " " << ins.type_handle
                    << " " << ins.lhs_value_id << " " << ins.rhs_value_id;
            } else if (ins.kind == FrontendInstructionKind::call) {
                out << ins.result_value_id << " " << ins.type_handle
                    << " " << frontend_payload_escape(ins.callee_name)
                    << " " << ins.arg_value_ids.size();
                for (const auto arg : ins.arg_value_ids) {
                    out << " " << arg;
                }
            } else if (ins.kind == FrontendInstructionKind::ret) {
                out << ins.ret_value_id;
            } else if (ins.kind == FrontendInstructionKind::label) {
                out << ins.immediate;
            } else if (ins.kind == FrontendInstructionKind::jmp) {
                out << ins.immediate;
            } else if (ins.kind == FrontendInstructionKind::br_if) {
                out << ins.lhs_value_id << " " << ins.immediate;
            }
            out << "\n";
        }

        out << "endfunction\n";
    }

    if (!out.good()) {
        if (error) {
            *error = "failed while writing payload file";
        }
        return false;
    }

    return true;
}

inline bool read_frontend_payload_file(std::string_view file_path,
                                       FrontendPayloadModule& out_payload,
                                       std::string* error = nullptr) {
    out_payload = FrontendPayloadModule{};

    std::ifstream in(std::filesystem::absolute(std::filesystem::path(file_path)).lexically_normal(), std::ios::binary);
    if (!in) {
        if (error) {
            *error = "failed to open payload file";
        }
        return false;
    }

    std::string line;
    if (!std::getline(in, line) || line != "DGTL_FRONTEND_PAYLOAD_V1") {
        if (error) {
            *error = "invalid payload header";
        }
        return false;
    }

    FrontendLoweringFunction* current = nullptr;
    while (std::getline(in, line)) {
        if (line.empty()) {
            continue;
        }

        std::istringstream iss(line);
        std::string tag;
        iss >> tag;

        if (tag == "function") {
            std::string encoded_name;
            uint64_t return_type = 0;
            unsigned int return_abi = 0;
            if (!(iss >> encoded_name >> return_type >> return_abi)) {
                if (error) {
                    *error = "invalid function line in payload";
                }
                return false;
            }

            FrontendLoweringFunction fn;
            if (!frontend_payload_unescape(encoded_name, fn.name)) {
                if (error) {
                    *error = "invalid escaped function name in payload";
                }
                return false;
            }
            fn.return_type_handle = return_type;
            fn.return_abi_class = static_cast<DGTLABIValueClass>(return_abi);
            out_payload.functions.push_back(std::move(fn));
            current = &out_payload.functions.back();
            continue;
        }

        if (tag == "param") {
            if (!current) {
                if (error) {
                    *error = "param found before function";
                }
                return false;
            }

            FrontendLoweringParameter param;
            std::string encoded_name;
            unsigned int abi_class = 0;
            if (!(iss >> param.value_id >> encoded_name >> param.type_handle >> abi_class)) {
                if (error) {
                    *error = "invalid param line in payload";
                }
                return false;
            }
            if (!frontend_payload_unescape(encoded_name, param.name)) {
                if (error) {
                    *error = "invalid escaped param name in payload";
                }
                return false;
            }
            param.abi_class = static_cast<DGTLABIValueClass>(abi_class);
            current->parameters.push_back(std::move(param));
            continue;
        }

        if (tag == "ins") {
            if (!current) {
                if (error) {
                    *error = "ins found before function";
                }
                return false;
            }

            std::string kind_token;
            if (!(iss >> kind_token)) {
                if (error) {
                    *error = "instruction kind missing";
                }
                return false;
            }

            FrontendLoweringInstruction ins;
            if (!parse_frontend_instruction_kind(kind_token, ins.kind)) {
                if (error) {
                    *error = "unknown instruction kind in payload: " + kind_token;
                }
                return false;
            }

            if (ins.kind == FrontendInstructionKind::const_i64) {
                uint32_t result = 0;
                FrontendTypeHandle type_handle = 0;
                std::string imm_str;
                if (!(iss >> result >> type_handle >> imm_str)) {
                    if (error) {
                        *error = "invalid const_i64 instruction line";
                    }
                    return false;
                }
                ins.result_value_id = result;
                ins.type_handle = type_handle;
                
                // Parse immediate as signed int64_t, then reinterpret bits as uint64_t
                // This correctly handles negative values from string interning and memory addresses
                try {
                    int64_t signed_imm = std::stoll(imm_str, nullptr, 0);
                    ins.immediate = static_cast<uint64_t>(signed_imm);
                } catch (...) {
                    if (error) {
                        *error = "invalid const_i64 immediate value: " + imm_str;
                    }
                    return false;
                }
            } else if (ins.kind == FrontendInstructionKind::const_str) {
                std::string encoded_literal;
                if (!(iss >> ins.result_value_id >> ins.type_handle >> encoded_literal)) {
                    if (error) {
                        *error = "invalid const_str instruction line";
                    }
                    return false;
                }
                if (!frontend_payload_unescape(encoded_literal, ins.callee_name)) {
                    if (error) {
                        *error = "invalid escaped literal in const_str instruction";
                    }
                    return false;
                }
            } else if (ins.kind == FrontendInstructionKind::add_i32 ||
                       ins.kind == FrontendInstructionKind::sub_i32 ||
                       ins.kind == FrontendInstructionKind::mul_i32 ||
                       ins.kind == FrontendInstructionKind::div_i32 ||
                       ins.kind == FrontendInstructionKind::mod_i32 ||
                       ins.kind == FrontendInstructionKind::cmp_eq ||
                       ins.kind == FrontendInstructionKind::cmp_ne ||
                       ins.kind == FrontendInstructionKind::cmp_lt ||
                       ins.kind == FrontendInstructionKind::cmp_le ||
                       ins.kind == FrontendInstructionKind::cmp_gt ||
                       ins.kind == FrontendInstructionKind::cmp_ge ||
                       ins.kind == FrontendInstructionKind::and_i32 ||
                       ins.kind == FrontendInstructionKind::or_i32  ||
                       ins.kind == FrontendInstructionKind::xor_i32 ||
                       ins.kind == FrontendInstructionKind::shl_i32 ||
                       ins.kind == FrontendInstructionKind::shr_i32) {
                if (!(iss >> ins.result_value_id >> ins.type_handle >> ins.lhs_value_id >> ins.rhs_value_id)) {
                    if (error) {
                        *error = "invalid binary instruction line";
                    }
                    return false;
                }
            } else if (ins.kind == FrontendInstructionKind::label) {
                if (!(iss >> ins.immediate)) {
                    if (error) { *error = "invalid label instruction line"; }
                    return false;
                }
            } else if (ins.kind == FrontendInstructionKind::jmp) {
                if (!(iss >> ins.immediate)) {
                    if (error) { *error = "invalid jmp instruction line"; }
                    return false;
                }
            } else if (ins.kind == FrontendInstructionKind::br_if) {
                if (!(iss >> ins.lhs_value_id >> ins.immediate)) {
                    if (error) { *error = "invalid br_if instruction line"; }
                    return false;
                }
            } else if (ins.kind == FrontendInstructionKind::call) {
                std::string encoded_callee;
                size_t arg_count = 0;
                if (!(iss >> ins.result_value_id >> ins.type_handle >> encoded_callee >> arg_count)) {
                    if (error) {
                        *error = "invalid call instruction line";
                    }
                    return false;
                }
                if (!frontend_payload_unescape(encoded_callee, ins.callee_name)) {
                    if (error) {
                        *error = "invalid escaped callee in call instruction";
                    }
                    return false;
                }
                for (size_t i = 0; i < arg_count; ++i) {
                    uint32_t arg = dgtl::ir::INVALID_SLOT;
                    if (!(iss >> arg)) {
                        if (error) {
                            *error = "call instruction missing argument";
                        }
                        return false;
                    }
                    ins.arg_value_ids.push_back(arg);
                }
            } else if (ins.kind == FrontendInstructionKind::ret) {
                if (!(iss >> ins.ret_value_id)) {
                    if (error) {
                        *error = "invalid ret instruction line";
                    }
                    return false;
                }
            }

            current->instructions.push_back(std::move(ins));
            continue;
        }

        if (tag == "endfunction") {
            current = nullptr;
            continue;
        }

        if (error) {
            *error = "unknown payload tag: " + tag;
        }
        return false;
    }

    return true;
}

} // namespace dgtl::core

