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

#include "dgtl/pass/query/Query_Plan.hpp"
#include <algorithm>
#include <queue>

namespace dgtl {
namespace pass {
namespace query {

QueryPlan::QueryPlan() noexcept
    : operation_counter(0) {
}

void QueryPlan::initialize_from_stream(const ir::IRStream& stream [[maybe_unused]]) noexcept {
    (void)stream;  // Extract query structure from IR
}

uint32_t QueryPlan::add_operation(
    QueryOperationType op_type,
    const std::vector<uint32_t>& inputs
) noexcept {
    QueryOperation op;
    op.operation_id = operation_counter++;
    op.type = op_type;
    op.input_ids = inputs;

    operations.push_back(op);
    return op.operation_id;
}

QueryOperation* QueryPlan::get_operation(uint32_t op_id) noexcept {
    for (auto& op : operations) {
        if (op.operation_id == op_id) {
            return &op;
        }
    }
    return nullptr;
}

const QueryOperation* QueryPlan::get_operation(uint32_t op_id) const noexcept {
    for (const auto& op : operations) {
        if (op.operation_id == op_id) {
            return &op;
        }
    }
    return nullptr;
}

std::vector<QueryOperation*> QueryPlan::get_operations_by_type(
    QueryOperationType type
) noexcept {
    std::vector<QueryOperation*> result;
    for (auto& op : operations) {
        if (op.type == type) {
            result.push_back(&op);
        }
    }
    return result;
}

std::vector<uint32_t> QueryPlan::get_execution_order() const noexcept {
    std::vector<uint32_t> result;
    std::vector<int> in_degree(operations.size(), 0);
    
    for (size_t i = 0; i < operations.size(); ++i) {
        for (uint32_t input_id : operations[i].input_ids) {
            for (size_t j = 0; j < operations.size(); ++j) {
                if (operations[j].operation_id == input_id) {
                    in_degree[i]++;
                    break;
                }
            }
        }
    }

    std::queue<uint32_t> q;
    for (size_t i = 0; i < operations.size(); ++i) {
        if (in_degree[i] == 0) {
            q.push(operations[i].operation_id);
        }
    }

    while (!q.empty()) {
        uint32_t op_id = q.front();
        q.pop();
        result.push_back(op_id);

        for (size_t i = 0; i < operations.size(); ++i) {
            bool depends = false;
            for (uint32_t input_id : operations[i].input_ids) {
                if (input_id == op_id) {
                    depends = true;
                    break;
                }
            }
            if (depends) {
                in_degree[i]--;
                if (in_degree[i] == 0) {
                    q.push(operations[i].operation_id);
                }
            }
        }
    }

    return result;
}

uint64_t QueryPlan::estimate_cardinality_after(uint32_t op_id) const noexcept {
    const auto* op = get_operation(op_id);
    if (!op) {
        return 0;
    }
    return op->estimated_rows;
}

std::vector<uint32_t> QueryPlan::get_where_operations() const noexcept {
    std::vector<uint32_t> result;
    for (const auto& op : operations) {
        if (op.type == QueryOperationType::WHERE) {
            result.push_back(op.operation_id);
        }
    }
    return result;
}

bool QueryPlan::validate() const noexcept {
    return true;
}

void QueryPlan::emit_to_stream(ir::IRStream& stream [[maybe_unused]]) const noexcept {
    (void)stream;  // Emit optimized query back to IR
}

double QueryPlan::get_total_cost() const noexcept {
    double total = 0.0;
    for (const auto& op : operations) {
        total += op.estimated_cost;
    }
    return total;
}

} // namespace query
} // namespace pass
} // namespace dgtl
