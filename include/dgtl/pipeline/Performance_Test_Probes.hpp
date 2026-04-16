#pragma once

#include <immintrin.h>

#include "dgtl/ir/base/Global_IR.hpp"
#include "dgtl/pipeline/Pass_Manager.hpp"

namespace dgtl::pipeline::probes {

class SIMDIntMulFoldingPass final : public dgtl::pipeline::DGTLOptimizationPass {
public:
    void execute(dgtl::ir::IRStream& stream) override {
        size_t i = 0;
        for (; i + 8 <= stream.size(); i += 8) {
            __m256i a_vals = _mm256_set_epi32(
                stream.get_node_readonly(i + 7).op_a,
                stream.get_node_readonly(i + 6).op_a,
                stream.get_node_readonly(i + 5).op_a,
                stream.get_node_readonly(i + 4).op_a,
                stream.get_node_readonly(i + 3).op_a,
                stream.get_node_readonly(i + 2).op_a,
                stream.get_node_readonly(i + 1).op_a,
                stream.get_node_readonly(i).op_a);

            __m256i b_vals = _mm256_set_epi32(
                stream.get_node_readonly(i + 7).op_b,
                stream.get_node_readonly(i + 6).op_b,
                stream.get_node_readonly(i + 5).op_b,
                stream.get_node_readonly(i + 4).op_b,
                stream.get_node_readonly(i + 3).op_b,
                stream.get_node_readonly(i + 2).op_b,
                stream.get_node_readonly(i + 1).op_b,
                stream.get_node_readonly(i).op_b);

            const __m256i res = _mm256_mullo_epi32(a_vals, b_vals);

            stream.get_node_ref(i).op_a = _mm256_extract_epi32(res, 0);
            stream.get_node_ref(i + 1).op_a = _mm256_extract_epi32(res, 1);
            stream.get_node_ref(i + 2).op_a = _mm256_extract_epi32(res, 2);
            stream.get_node_ref(i + 3).op_a = _mm256_extract_epi32(res, 3);
            stream.get_node_ref(i + 4).op_a = _mm256_extract_epi32(res, 4);
            stream.get_node_ref(i + 5).op_a = _mm256_extract_epi32(res, 5);
            stream.get_node_ref(i + 6).op_a = _mm256_extract_epi32(res, 6);
            stream.get_node_ref(i + 7).op_a = _mm256_extract_epi32(res, 7);
        }

        for (; i < stream.size(); ++i) {
            auto& node = stream.get_node_ref(i);
            node.op_a *= node.op_b;
        }
    }

    [[nodiscard]] const char* get_pass_name() const noexcept override {
        return "SIMDIntMulFoldingPass";
    }
};

} // namespace dgtl::pipeline::probes
