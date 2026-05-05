bool build_release_emit_ir(dgtl::ir::IRStream& stream) {
    stream.reset();
    stream.begin_transaction();
    stream.push_node(dgtl::ir::OpCode::LOAD_IMM, dgtl::ir::Flags::IS_CONST, 1, 0, 21, dgtl::ir::INVALID_SLOT, 0);
    stream.push_node(dgtl::ir::OpCode::LOAD_IMM, dgtl::ir::Flags::IS_CONST, 2, 0, 21, dgtl::ir::INVALID_SLOT, 0);
    stream.push_node(dgtl::ir::OpCode::ADD_I32, dgtl::ir::Flags::NONE, 3, 1, 2, dgtl::ir::INVALID_SLOT, 0);
    stream.push_node(dgtl::ir::OpCode::ADD_I32, dgtl::ir::Flags::NONE, 4, 3, 3, dgtl::ir::INVALID_SLOT, 0);
    stream.push_node(dgtl::ir::OpCode::RET, dgtl::ir::Flags::IS_RETURN, 0, 4, dgtl::ir::INVALID_SLOT, dgtl::ir::INVALID_SLOT, 0);
    stream.commit_transaction();
    return stream.size() != 0;
}

bool build_backend_sample_ir(dgtl::ir::IRStream& stream) {
    dgtl::pipeline::probes::build_release_int_probe(stream);
    return stream.size() == 5;
}

bool build_fusion_ir(dgtl::ir::IRStream& stream) {
    stream.reset();
    stream.begin_transaction();
    stream.push_node(dgtl::ir::OpCode::LOAD_IMM, dgtl::ir::Flags::IS_CONST, 10, 0, 2, dgtl::ir::INVALID_SLOT, 0);
    stream.push_node(dgtl::ir::OpCode::LOAD_IMM, dgtl::ir::Flags::IS_CONST, 11, 0, 3, dgtl::ir::INVALID_SLOT, 0);
    stream.push_node(dgtl::ir::OpCode::LOAD_IMM, dgtl::ir::Flags::IS_CONST, 12, 0, 4, dgtl::ir::INVALID_SLOT, 0);
    stream.push_node(dgtl::ir::OpCode::MUL_F32, dgtl::ir::Flags::NONE, 20, 10, 11, dgtl::ir::INVALID_SLOT, 0);
    stream.push_node(dgtl::ir::OpCode::ADD_F32, dgtl::ir::Flags::NONE, 21, 20, 12, dgtl::ir::INVALID_SLOT, 0);
    stream.commit_transaction();
    return stream.size() == 5;
}

bool build_simd_ir(dgtl::ir::IRStream& stream) {
    stream.reset();
    stream.begin_transaction();
    for (uint32_t i = 0; i < 8; ++i) {
        stream.push_node(dgtl::ir::OpCode::LOAD_IMM,
                         dgtl::ir::Flags::IS_CONST,
                         i + 1,
                         0,
                         i + 1,
                         dgtl::ir::INVALID_SLOT,
                         0);
    }
    for (uint32_t i = 0; i < 4; ++i) {
        const uint32_t lhs = (i * 2) + 1;
        const uint32_t rhs = lhs + 1;
        stream.push_node(dgtl::ir::OpCode::ADD_I32, dgtl::ir::Flags::NONE, 100 + i, lhs, rhs, dgtl::ir::INVALID_SLOT, 0);
    }
    stream.commit_transaction();
    return stream.size() == 12;
}

bool build_pressure_ir(dgtl::ir::IRStream& stream, uint32_t vars) {
    if (vars < 2) return false;
    stream.reset();
    stream.begin_transaction();

    for (uint32_t i = 0; i < vars; ++i) {
        stream.push_node(dgtl::ir::OpCode::LOAD_IMM,
                         dgtl::ir::Flags::IS_CONST,
                         i + 1,
                         0,
                         i + 1,
                         dgtl::ir::INVALID_SLOT,
                         0);
    }

    uint32_t accum = 1;
    uint32_t next_result = vars + 1;
    for (uint32_t i = 2; i <= vars; ++i) {
        stream.push_node(dgtl::ir::OpCode::ADD_I32,
                         dgtl::ir::Flags::NONE,
                         next_result,
                         accum,
                         i,
                         dgtl::ir::INVALID_SLOT,
                         0);
        accum = next_result;
        ++next_result;
    }

    stream.push_node(dgtl::ir::OpCode::RET, dgtl::ir::Flags::IS_RETURN, 0, accum, dgtl::ir::INVALID_SLOT, dgtl::ir::INVALID_SLOT, 0);
    stream.commit_transaction();
    return stream.size() != 0;
}

bool build_predication_ir(dgtl::ir::IRStream& stream) {
    dgtl::pipeline::probes::build_masked_predication_probe(stream);
    return stream.size() == 4;
}

// Bench-specific variants: same IR as the smoke builders but include a RET so
// the full chain stays live through DCE and emission does not fail.
bool build_fusion_bench_ir(dgtl::ir::IRStream& stream) {
    stream.reset();
    stream.begin_transaction();
    stream.push_node(dgtl::ir::OpCode::LOAD_IMM, dgtl::ir::Flags::IS_CONST, 10, 0, 2, dgtl::ir::INVALID_SLOT, 0);
    stream.push_node(dgtl::ir::OpCode::LOAD_IMM, dgtl::ir::Flags::IS_CONST, 11, 0, 3, dgtl::ir::INVALID_SLOT, 0);
    stream.push_node(dgtl::ir::OpCode::LOAD_IMM, dgtl::ir::Flags::IS_CONST, 12, 0, 4, dgtl::ir::INVALID_SLOT, 0);
    stream.push_node(dgtl::ir::OpCode::MUL_F32,  dgtl::ir::Flags::NONE, 20, 10, 11, dgtl::ir::INVALID_SLOT, 0);
    stream.push_node(dgtl::ir::OpCode::ADD_F32,  dgtl::ir::Flags::NONE, 21, 20, 12, dgtl::ir::INVALID_SLOT, 0);
    stream.push_node(dgtl::ir::OpCode::RET, dgtl::ir::Flags::IS_RETURN, 0, 21, dgtl::ir::INVALID_SLOT, dgtl::ir::INVALID_SLOT, 0);
    stream.commit_transaction();
    return stream.size() == 6;
}

bool build_simd_bench_ir(dgtl::ir::IRStream& stream) {
    stream.reset();
    stream.begin_transaction();
    for (uint32_t i = 0; i < 8; ++i) {
        stream.push_node(dgtl::ir::OpCode::LOAD_IMM,
                         dgtl::ir::Flags::IS_CONST,
                         i + 1,
                         0,
                         i + 1,
                         dgtl::ir::INVALID_SLOT,
                         0);
    }
    for (uint32_t i = 0; i < 4; ++i) {
        const uint32_t lhs = (i * 2) + 1;
        const uint32_t rhs = lhs + 1;
        stream.push_node(dgtl::ir::OpCode::ADD_I32, dgtl::ir::Flags::NONE, 100 + i, lhs, rhs, dgtl::ir::INVALID_SLOT, 0);
    }
    // RET references the last accumulated result (id=103) to keep the chain live.
    stream.push_node(dgtl::ir::OpCode::RET, dgtl::ir::Flags::IS_RETURN, 0, 103, dgtl::ir::INVALID_SLOT, dgtl::ir::INVALID_SLOT, 0);
    stream.commit_transaction();
    return stream.size() == 13;
}

std::string_view profile_name(BenchmarkProfile profile) {
    switch (profile) {
        case BenchmarkProfile::fold:     return "fold";
        case BenchmarkProfile::fusion:   return "fusion";
        case BenchmarkProfile::simd:     return "simd";
        case BenchmarkProfile::pressure: return "pressure";
        case BenchmarkProfile::predication: return "predication";
    }
    return "unknown";
}

bool build_ir_for_profile(dgtl::ir::IRStream& stream, BenchmarkProfile profile, uint32_t vars) {
    switch (profile) {
        case BenchmarkProfile::fold:     return build_release_emit_ir(stream);
        case BenchmarkProfile::fusion:   return build_fusion_bench_ir(stream);
        case BenchmarkProfile::simd:     return build_simd_bench_ir(stream);
        case BenchmarkProfile::pressure: return build_pressure_ir(stream, vars);
        case BenchmarkProfile::predication: return build_predication_ir(stream);
    }
    return false;
}

void run_core_optimizations(dgtl::ir::IRStream& stream) {
    stream.perform_gvn();
    dgtl::pass::DGTLTemporalVectorFusion temporal_fusion;
    temporal_fusion.execute(stream);
    dgtl::pass::DGTLMaskedPredicationLowering masked_predication;
    masked_predication.execute(stream);
    stream.perform_dead_code_elimination();
}

std::optional<dgtl::ir::IRNode> find_node_by_result(const dgtl::ir::IRStream& stream, uint32_t result_id) {
    for (size_t i = 0; i < stream.size(); ++i) {
        const auto node = stream.get_node(i);
        if (node.result_id == result_id) {
            return node;
        }
    }
    return std::nullopt;
}

[[maybe_unused]] void append_check(std::vector<SmokeCheck>& checks, std::string name, bool passed, std::string detail) {
    checks.push_back({std::move(name), passed, std::move(detail)});
}

dgtl::pass::TargetArch target_arch_for_backend(BackendKind backend) {
    switch (backend) {
        case BackendKind::arm64:
        case BackendKind::arm32:
        case BackendKind::macho:
            return dgtl::pass::TargetArch::ARM_NEON;
        case BackendKind::wasm:
            return dgtl::pass::TargetArch::WASM_SIMD128;
        case BackendKind::riscv64:
        case BackendKind::cuda:
        case BackendKind::rocm:
        case BackendKind::elfobj:
        case BackendKind::staticlib:
        case BackendKind::coffobj:
        case BackendKind::cofflib:
        case BackendKind::x64:
        case BackendKind::nasm:
        default:
            switch (dgtl::core::preferred_simd_tier()) {
                case dgtl::core::SimdTier::AVX512:
                    return dgtl::pass::TargetArch::X86_AVX512;
                case dgtl::core::SimdTier::AVX2:
                    return dgtl::pass::TargetArch::X86_AVX2;
                case dgtl::core::SimdTier::Scalar:
                default:
                    return dgtl::pass::TargetArch::GENERIC;
            }
    }
}

uint32_t expected_simd_opcode(BackendKind backend) {
    switch (backend) {
        case BackendKind::arm64:
        case BackendKind::arm32:
        case BackendKind::macho:
            return 0x304;
        case BackendKind::wasm:
            return 0x404;
        case BackendKind::riscv64:
        case BackendKind::cuda:
        case BackendKind::rocm:
        case BackendKind::elfobj:
        case BackendKind::staticlib:
        case BackendKind::coffobj:
        case BackendKind::cofflib:
        case BackendKind::x64:
        case BackendKind::nasm:
        default:
            return 0x204;
    }
}