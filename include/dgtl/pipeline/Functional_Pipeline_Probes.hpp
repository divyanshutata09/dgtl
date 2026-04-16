#pragma once

#include <cstddef>

#include "dgtl/core/Verified_Formal.hpp"
#include "dgtl/ir/base/Global_IR.hpp"
#include "dgtl/pass/speculative/Temporal_Vector_Fusion.hpp"
#include "dgtl/pass/transform/Instruction_Fusion_Synth.hpp"
#include "dgtl/pass/transform/Masked_Predication_Lowering.hpp"

namespace dgtl::pipeline::probes {

inline void run_common_optimizations(dgtl::ir::IRStream& stream) {
    stream.perform_gvn();
    stream.perform_dead_code_elimination();
}

inline bool stream_has_opcode(const dgtl::ir::IRStream& stream, dgtl::ir::OpCode opcode) {
    for (size_t i = 0; i < stream.size(); ++i) {
        if (stream.get_node(i).opcode == opcode) {
            return true;
        }
    }
    return false;
}

inline size_t count_opcode(const dgtl::ir::IRStream& stream, dgtl::ir::OpCode opcode) {
    size_t count = 0;
    for (size_t i = 0; i < stream.size(); ++i) {
        if (stream.get_node(i).opcode == opcode) {
            ++count;
        }
    }
    return count;
}

inline void build_release_int_probe(dgtl::ir::IRStream& stream) {
    stream.reset();
    stream.begin_transaction();
    stream.push_node(dgtl::ir::OpCode::ADD_I32, dgtl::ir::Flags::NONE, 20, 10, 11, dgtl::ir::INVALID_SLOT, 0);
    stream.push_node(dgtl::ir::OpCode::ADD_I32, dgtl::ir::Flags::NONE, 21, 20, 12, dgtl::ir::INVALID_SLOT, 0);
    stream.push_node(dgtl::ir::OpCode::ADD_I32, dgtl::ir::Flags::NONE, 22, 20, 13, dgtl::ir::INVALID_SLOT, 0);
    stream.push_node(dgtl::ir::OpCode::MUL_I32, dgtl::ir::Flags::NONE, 23, 21, 22, dgtl::ir::INVALID_SLOT, 0);
    stream.push_node(dgtl::ir::OpCode::RET, dgtl::ir::Flags::IS_RETURN, 0, 23, dgtl::ir::INVALID_SLOT, dgtl::ir::INVALID_SLOT, 0);
    stream.commit_transaction();
}

inline void build_release_float_probe(dgtl::ir::IRStream& stream) {
    stream.reset();
    stream.begin_transaction();
    stream.push_node(dgtl::ir::OpCode::MUL_F32, dgtl::ir::Flags::NONE, 20, 10, 11, dgtl::ir::INVALID_SLOT, 0);
    stream.push_node(dgtl::ir::OpCode::ADD_F32, dgtl::ir::Flags::NONE, 21, 20, 12, dgtl::ir::INVALID_SLOT, 0);
    stream.push_node(dgtl::ir::OpCode::RET, dgtl::ir::Flags::IS_RETURN, 0, 21, dgtl::ir::INVALID_SLOT, dgtl::ir::INVALID_SLOT, 0);
    stream.commit_transaction();
}

inline void build_fma_probe(dgtl::ir::IRStream& stream,
                            uint32_t dst = 22,
                            uint32_t op_a = 10,
                            uint32_t op_b = 11,
                            uint32_t op_c = 12) {
    stream.reset();
    stream.begin_transaction();
    stream.push_node(dgtl::ir::OpCode::FMA_F32,
                     dgtl::ir::Flags::IS_MACRO_FUSED,
                     dst,
                     op_a,
                     op_b,
                     op_c,
                     0);
    stream.push_node(dgtl::ir::OpCode::RET,
                     dgtl::ir::Flags::IS_RETURN,
                     0,
                     dst,
                     dgtl::ir::INVALID_SLOT,
                     dgtl::ir::INVALID_SLOT,
                     0);
    stream.commit_transaction();
}

inline void build_mul_add_fusion_pairs_probe(dgtl::ir::IRStream& stream,
                                             size_t pair_count,
                                             uint32_t mul_a = 10,
                                             uint32_t mul_b = 11,
                                             uint32_t add_rhs = 12) {
    stream.reset();
    stream.begin_transaction();
    for (size_t i = 0; i < pair_count; ++i) {
        const uint32_t mul_result = static_cast<uint32_t>(i * 2);
        const uint32_t add_result = static_cast<uint32_t>(i * 2 + 1);
        stream.push_node(dgtl::ir::OpCode::MUL_F32,
                         dgtl::ir::Flags::NONE,
                         mul_result,
                         mul_a,
                         mul_b,
                         dgtl::ir::INVALID_SLOT,
                         0);
        stream.push_node(dgtl::ir::OpCode::ADD_F32,
                         dgtl::ir::Flags::NONE,
                         add_result,
                         mul_result,
                         add_rhs,
                         dgtl::ir::INVALID_SLOT,
                         0);
    }
    stream.commit_transaction();
}

inline void build_add_chain_probe(dgtl::ir::IRStream& stream,
                                  uint32_t first_result,
                                  uint32_t first_input,
                                  uint32_t addend,
                                  size_t length) {
    stream.reset();
    stream.begin_transaction();
    for (size_t i = 0; i < length; ++i) {
        const uint32_t result_id = first_result + static_cast<uint32_t>(i);
        const uint32_t op_a = (i == 0) ? first_input : (result_id - 1);
        stream.push_node(dgtl::ir::OpCode::ADD_I32,
                         dgtl::ir::Flags::NONE,
                         result_id,
                         op_a,
                         addend,
                         dgtl::ir::INVALID_SLOT,
                         0);
    }
    stream.commit_transaction();
}

inline void apply_float_fusion_probe(dgtl::ir::IRStream& stream) {
    dgtl::core::DGTLFormalVerifier verifier;
    dgtl::pass::DGTLInstructionFusionSynth fusion(verifier);
    fusion.execute(stream);
}

inline void build_fcvtl_probe(dgtl::ir::IRStream& stream, uint32_t dst = 21, uint32_t src = 10) {
    stream.reset();
    stream.begin_transaction();
    stream.push_node(dgtl::ir::OpCode::FCVTL_F16_TO_F32,
                     dgtl::ir::Flags::IS_SIMD_BURST,
                     dst,
                     src,
                     dgtl::ir::INVALID_SLOT,
                     dgtl::ir::INVALID_SLOT,
                     0);
    stream.push_node(dgtl::ir::OpCode::RET,
                     dgtl::ir::Flags::IS_RETURN,
                     0,
                     dst,
                     dgtl::ir::INVALID_SLOT,
                     dgtl::ir::INVALID_SLOT,
                     0);
    stream.commit_transaction();
}

inline void build_fcvtn_probe(dgtl::ir::IRStream& stream, uint32_t dst = 22, uint32_t src = 11) {
    stream.reset();
    stream.begin_transaction();
    stream.push_node(dgtl::ir::OpCode::FCVTN_F32_TO_F16,
                     dgtl::ir::Flags::IS_SIMD_BURST,
                     dst,
                     src,
                     dgtl::ir::INVALID_SLOT,
                     dgtl::ir::INVALID_SLOT,
                     0);
    stream.push_node(dgtl::ir::OpCode::RET,
                     dgtl::ir::Flags::IS_RETURN,
                     0,
                     dst,
                     dgtl::ir::INVALID_SLOT,
                     dgtl::ir::INVALID_SLOT,
                     0);
    stream.commit_transaction();
}

inline void build_temporal_fusion_probe(dgtl::ir::IRStream& stream) {
    stream.reset();
    stream.begin_transaction();
    stream.push_node(dgtl::ir::OpCode::FCVTL_F16_TO_F32, dgtl::ir::Flags::IS_SIMD_BURST, 30, 10, dgtl::ir::INVALID_SLOT, dgtl::ir::INVALID_SLOT, 0);
    stream.push_node(dgtl::ir::OpCode::FMA_F32, dgtl::ir::Flags::NONE, 31, 30, 11, 12, 0);
    stream.push_node(dgtl::ir::OpCode::RET, dgtl::ir::Flags::IS_RETURN, 0, 31, dgtl::ir::INVALID_SLOT, dgtl::ir::INVALID_SLOT, 0);
    stream.commit_transaction();
}

inline void build_masked_predication_probe(dgtl::ir::IRStream& stream) {
    stream.reset();
    stream.begin_transaction();
    stream.push_node(dgtl::ir::OpCode::BRANCH_COND, dgtl::ir::Flags::NONE, dgtl::ir::INVALID_SLOT, 7, 0, 0, 0);
    stream.push_node(dgtl::ir::OpCode::ADD_I32, dgtl::ir::Flags::NONE, 40, 1, 2, dgtl::ir::INVALID_SLOT, 0);
    stream.push_node(dgtl::ir::OpCode::MUL_I32, dgtl::ir::Flags::NONE, 40, 3, 4, dgtl::ir::INVALID_SLOT, 0);
    stream.push_node(dgtl::ir::OpCode::RET, dgtl::ir::Flags::IS_RETURN, 0, 40, dgtl::ir::INVALID_SLOT, dgtl::ir::INVALID_SLOT, 0);
    stream.commit_transaction();
}

inline void apply_temporal_fusion(dgtl::ir::IRStream& stream) {
    dgtl::pass::DGTLTemporalVectorFusion temporal;
    temporal.execute(stream);
}

inline void apply_masked_predication(dgtl::ir::IRStream& stream) {
    dgtl::pass::DGTLMaskedPredicationLowering predication;
    predication.execute(stream);
}

} // namespace dgtl::pipeline::probes
