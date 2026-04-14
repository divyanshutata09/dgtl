#include "dgtl/all.hpp"

#include <cassert>
#include <cstdint>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

/**
 * DGTL-MLIR-X: PRODUCTION GRADE ARCHITECTURAL VALIDATOR
 * Target: ARM64 Backend (AArch64)
 * Metric: Better-than-LLVM Efficiency and Traceability (project-local heuristic)
 */

struct BackendReport {
    std::string assembly_output;
    uint64_t logic_hash = 0;
    int instruction_count = 0;
    bool has_stack_spills = false;
};

struct TestNode {
    uint32_t reg = 0;
    bool is_const = false;
    uint32_t const_value = 0;
};

class TestGraph {
public:
    TestGraph(dgtl::core::DGTL_Arena& arena, dgtl::core::SymbolTable& symbols)
        : stream_(arena, 1024), symbols_(symbols) {}

    TestNode* imm(uint32_t value) {
        nodes_.push_back(TestNode{next_reg_++, true, value});
        return &nodes_.back();
    }

    TestNode* add(TestNode* a, TestNode* b) {
        assert(a != nullptr && b != nullptr);
        nodes_.push_back(TestNode{next_reg_++, false, 0});
        ops_.push_back(Op{OpKind::Add, a->reg, b->reg, nodes_.back().reg});
        return &nodes_.back();
    }

    void set_return(TestNode* n) {
        assert(n != nullptr);
        return_reg_ = n->reg;
    }

    dgtl::ir::IRStream& stream() { return stream_; }
    dgtl::core::SymbolTable& symbols() { return symbols_; }
    uint32_t return_reg() const { return return_reg_; }
    const std::vector<TestNode>& nodes() const { return nodes_; }

private:
    enum class OpKind : uint8_t { Add };

    struct Op {
        OpKind kind;
        uint32_t a;
        uint32_t b;
        uint32_t out;
    };

    dgtl::ir::IRStream stream_;
    dgtl::core::SymbolTable& symbols_;
    uint32_t next_reg_ = 1;
    uint32_t return_reg_ = 0;
    std::vector<TestNode> nodes_;
    std::vector<Op> ops_;

    friend class ValidatorContext;
};

class ValidatorContext {
public:
    ValidatorContext() : arena_(1, 64), symbols_(arena_) {}

    std::unique_ptr<TestGraph> create_graph(const std::string&) {
        return std::make_unique<TestGraph>(arena_, symbols_);
    }

    BackendReport lower_and_analyze(TestGraph& graph) {
        emit_graph_to_ir(graph);
        auto& stream = graph.stream();

        stream.perform_gvn();
        stream.perform_dead_code_elimination();

        dgtl::isa::ARM64Emitter emitter(stream, graph.symbols());
        dgtl::isa::ARM64Emitter::Config cfg;
        cfg.verbose_asm = true;

        std::ostringstream oss;
        emitter.emit_assembly(oss, cfg);

        BackendReport report;
        report.assembly_output = oss.str();
        report.logic_hash = stream.generate_logic_hash();
        report.instruction_count = count_instruction_lines(report.assembly_output);
        report.has_stack_spills = detect_stack_spills(report.assembly_output);
        return report;
    }

private:
    dgtl::core::DGTL_Arena arena_;
    dgtl::core::SymbolTable symbols_;

    static int count_instruction_lines(const std::string& asm_text) {
        std::istringstream iss(asm_text);
        std::string line;
        int count = 0;
        while (std::getline(iss, line)) {
            if (line.empty()) continue;

            const auto first = line.find_first_not_of(" \t");
            if (first == std::string::npos) continue;

            const char ch = line[first];
            if (ch == '/' || ch == '.') continue;
            if (line[first] == '_') continue; // labels like _DGTL_entry:
            if (line.find(':') != std::string::npos && line.find("//") == std::string::npos) continue;

            ++count;
        }
        return count;
    }

    static bool detect_stack_spills(const std::string& asm_text) {
        return asm_text.find("[sp") != std::string::npos ||
               asm_text.find(", sp") != std::string::npos ||
               asm_text.find(" sp,") != std::string::npos;
    }

    static void emit_graph_to_ir(TestGraph& graph) {
        auto& stream = graph.stream();
        stream.reset();
        stream.begin_transaction();

        for (const auto& n : graph.nodes_) {
            if (!n.is_const) continue;
            stream.push_node(dgtl::ir::OpCode::LOAD_IMM,
                             dgtl::ir::Flags::IS_CONST,
                             n.reg,
                             0,
                             n.const_value,
                             0,
                             0);
        }

        for (const auto& op : graph.ops_) {
            if (op.kind == TestGraph::OpKind::Add) {
                stream.push_node(dgtl::ir::OpCode::ADD_I32,
                                 dgtl::ir::Flags::NONE,
                                 op.out,
                                 op.a,
                                 op.b,
                                 0,
                                 0);
            }
        }

        stream.push_node(dgtl::ir::OpCode::RET,
                         dgtl::ir::Flags::IS_RETURN,
                         0,
                         graph.return_reg(),
                         0,
                         0,
                         0);
        stream.commit_transaction();
    }
};

// TEST 1: CONSTANT FOLDING SUPREMACY
void test_arithmetic_collapsing(ValidatorContext& ctx) {
    std::cout << "[TEST 1] Starting Constant Folding Torture..." << std::endl;

    auto graph = ctx.create_graph("fold_torture");
    auto n1 = graph->imm(21);
    auto n2 = graph->imm(21);
    auto sum1 = graph->add(n1, n2);
    auto sum2 = graph->add(sum1, sum1);
    graph->set_return(sum2);

    const auto report = ctx.lower_and_analyze(*graph);

    if (report.instruction_count <= 2) {
        std::cout << "  >> PASS: Constant folding collapsed to optimal instruction." << std::endl;
    } else {
        std::cout << "  >> FAIL: Redundant instructions detected (Count: "
                  << report.instruction_count << ")" << std::endl;
    }
}

// TEST 2: REGISTER PRESSURE and ZERO-SPILL
void test_register_pressure(ValidatorContext& ctx) {
    std::cout << "[TEST 2] Starting 25-Variable Register Pressure Test..." << std::endl;

    auto graph = ctx.create_graph("pressure_test");
    std::vector<TestNode*> nodes;
    nodes.reserve(25);

    for (int i = 0; i < 25; ++i) {
        nodes.push_back(graph->imm(static_cast<uint32_t>(i + 1)));
    }

    auto final_sum = nodes[0];
    for (size_t i = 1; i < nodes.size(); ++i) {
        final_sum = graph->add(final_sum, nodes[i]);
    }
    graph->set_return(final_sum);

    const auto report = ctx.lower_and_analyze(*graph);

    if (!report.has_stack_spills) {
        std::cout << "  >> PASS: Zero-Spill verified. All 25 variables stayed in registers." << std::endl;
    } else {
        std::cout << "  >> FAIL: Stack spill detected. Optimizer is sub-optimal." << std::endl;
    }
}

// TEST 3: TR_HASH and LOGICHASH INTEGRITY
void test_traceability_integrity(ValidatorContext& ctx) {
    std::cout << "[TEST 3] Starting Traceability Audit..." << std::endl;

    auto graph = ctx.create_graph("audit_test");
    graph->set_return(graph->add(graph->imm(100), graph->imm(200)));

    const auto report1 = ctx.lower_and_analyze(*graph);
    const auto report2 = ctx.lower_and_analyze(*graph);

    if (report1.logic_hash == report2.logic_hash) {
        std::cout << "  >> PASS: LogicHash is deterministic (Value: 0x"
                  << std::hex << report1.logic_hash << std::dec << ")" << std::endl;
    } else {
        std::cout << "  >> FAIL: LogicHash drift detected." << std::endl;
    }

    if (report1.assembly_output.find("TR_HASH:") != std::string::npos) {
        std::cout << "  >> PASS: TR_HASH telemetry present in backend stream." << std::endl;
    } else {
        std::cout << "  >> FAIL: Telemetry missing. Backend is black-box." << std::endl;
    }
}

void run_production_arch_validator() {
    std::cout << "--- DGTL-MLIR-X PRODUCTION SMOKE PIPELINE ---" << std::endl;

    ValidatorContext ctx;

    try {
        test_arithmetic_collapsing(ctx);
        std::cout << "--------------------------------------------" << std::endl;
        test_register_pressure(ctx);
        std::cout << "--------------------------------------------" << std::endl;
        test_traceability_integrity(ctx);

        std::cout << "\n[DGTL] ALL PRODUCTION TESTS COMPLETED." << std::endl;
        std::cout << "[DGTL] VERDICT: CHECK OUTPUT ABOVE FOR PASS/FAIL DETAILS." << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "[DGTL] CRITICAL FAILURE: " << e.what() << std::endl;
    }
}

int main() {
    run_production_arch_validator();
    return 0;
}