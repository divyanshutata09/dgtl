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

#include "dgtl/ir/dialect/IR_Dialect_Interface.hpp"
#include "dgtl/ir/base/IR_Stream.hpp"

namespace dgtl::dialect {

bool LanguageDialectRegistry::register_frontend_dialect(
    ir::IRStream& stream, 
    ir::FrontendLanguage lang
) noexcept {
    try {
        stream.set_frontend_language(lang);
        return true;
    } catch (...) {
        return false;
    }
}

bool LanguageDialectRegistry::validate_dialect_ops(const ir::IRStream& stream) noexcept {
    auto lang = stream.get_frontend_language();
    
    switch (lang) {
        case ir::FrontendLanguage::MYZON:
            return validate_myzon_ops(stream);
        case ir::FrontendLanguage::CPP:
            return validate_cpp_ops(stream);
        case ir::FrontendLanguage::RUST:
            return validate_rust_ops(stream);
        default:
            return true; // Neutral acceptance for unknown languages
    }
}

bool LanguageDialectRegistry::validate_myzon_ops(const ir::IRStream& stream) noexcept {
    if (stream.is_null() || stream.size() == 0) return false;
    int ownership_depth = 0;
    for (size_t i = 0; i < stream.size(); ++i) {
        auto node = stream.get_node(i);
        auto op = node.opcode;
        if (op == ir::OpCode::OWNERSHIP_ACQUIRE) {
            if (ownership_depth > 0 || node.result_id == 0) return false;
            ownership_depth++;
        } else if (op == ir::OpCode::OWNERSHIP_RELEASE) {
            if (ownership_depth <= 0) return false;
            ownership_depth--;
        } else if (op == ir::OpCode::OWNERSHIP_TRANSFER) {
            if (ownership_depth <= 0) return false;
        }
        if (ir::IRTraits::is_concurrency_op(op)) {
            if (ir::IRTraits::must_synchronize(op) && node.op_a == 0) return false;
        }
        if (ir::IRTraits::is_dsa_op(op)) {
            if (node.result_id == 0) return false;
        }
    }
    return ownership_depth == 0;
}

bool LanguageDialectRegistry::validate_cpp_ops(const ir::IRStream& stream) noexcept {
    if (stream.is_null() || stream.size() == 0) return false;
    for (size_t i = 0; i < stream.size(); ++i) {
        auto node = stream.get_node(i);
        auto op = node.opcode;
        if (ir::IRTraits::can_fold_constants(op)) {
            if ((node.flags & ir::Flags::IS_CONST) == 0) {
                // Mark for specialization
            }
        }
        if ((node.flags & ir::Flags::IS_OPTIMIZED) && ir::IRTraits::is_single_def_use(op)) {
            // Good inlining candidate
        }
        if (op == ir::OpCode::DIV_I32 || op == ir::OpCode::DIV_I64) {
            if (node.op_b == 0) return false;
        }
    }
    return true;
}

bool LanguageDialectRegistry::validate_rust_ops(const ir::IRStream& stream) noexcept {
    if (stream.is_null() || stream.size() == 0) return false;
    std::unordered_map<uint32_t, size_t> mutable_borrows;
    std::unordered_map<uint32_t, size_t> immutable_borrows;
    for (size_t i = 0; i < stream.size(); ++i) {
        auto node = stream.get_node(i);
        auto op = node.opcode;
        if (op == ir::OpCode::OWNERSHIP_ACQUIRE) {
            bool is_mutable = (node.flags & ir::ExtendedFlags::IS_LINEAR) != 0;
            if (is_mutable) {
                if (mutable_borrows.count(node.op_a) || immutable_borrows.count(node.op_a)) {
                    return false;
                }
                mutable_borrows[node.op_a] = i;
            } else {
                if (mutable_borrows.count(node.op_a)) {
                    return false;
                }
                immutable_borrows[node.op_a]++;
            }
        } else if (op == ir::OpCode::OWNERSHIP_RELEASE) {
            bool is_mutable = mutable_borrows.count(node.op_a) > 0;
            if (is_mutable) {
                mutable_borrows.erase(node.op_a);
            } else {
                auto it = immutable_borrows.find(node.op_a);
                if (it != immutable_borrows.end()) {
                    it->second--;
                    if (it->second == 0) immutable_borrows.erase(it);
                }
            }
        }
        if (op == ir::OpCode::DSA_DEALLOCATE) {
            if ((node.flags & ir::Flags::IS_DEAD) != 0) return false;
        }
    }
    return mutable_borrows.empty() && immutable_borrows.empty();
}

} // namespace dgtl::dialect
