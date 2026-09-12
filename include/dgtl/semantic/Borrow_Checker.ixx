/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * Project:     DGTL (Deep General-purpose Transformation Layer)
 * Owner:       Divyanshu Tata
 * Repository:  github.com/TataG-Systems/DGTL
 *
 * Component:   Linear Ownership & Borrow Verifier (Rust-Grade)
 * Standards:   C++26 / UCRT64 / Zero-Overhead Memory Safety
 */

module;

#include <cstdint>
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>

export module dgtl.ir_core.borrow_checker;

import dgtl.ir_enums;
import dgtl.ir_node;
import dgtl.ir_core.stream;
import dgtl.ir_core.semantic;

export namespace dgtl::semantic {

struct OwnershipError {
    size_t node_index = 0;
    uint32_t handle_slot = dgtl::ir::INVALID_SLOT;
    std::string message;
};

/**
 * @class BorrowChecker
 * @brief Enforces linear/affine move semantics and lifetime invariants.
 */
class BorrowChecker {
public:
    enum class State : uint8_t {
        Uninitialized = 0,
        Live,
        Moved,
        Released,
        BorrowedMut
    };

    BorrowChecker(SymbolTable& sym_table) noexcept : symbol_table_(sym_table) {}

    [[nodiscard]] bool verify(const dgtl::ir::IRStream& stream, std::vector<OwnershipError>& errors) const {
        errors.clear();
        if (stream.is_null() || stream.size() == 0) return true;

        std::unordered_map<uint32_t, State> states;
        states.reserve(128);

        for (size_t i = 0; i < stream.size(); ++i) {
            const auto node = stream.get_node(i);
            using Op = dgtl::ir::OpCode;

            switch (node.opcode) {
                case Op::OWNERSHIP_ACQUIRE:
                case Op::DSA_ALLOCATE: {
                    if (node.result_id != dgtl::ir::INVALID_SLOT) {
                        states[node.result_id] = State::Live;
                    }
                    break;
                }

                case Op::OWNERSHIP_RELEASE:
                case Op::DSA_DEALLOCATE: {
                    const uint32_t h = node.op_a;
                    auto it = states.find(h);
                    if (it == states.end() || it->second != State::Live) {
                        errors.push_back(OwnershipError{
                            .node_index = i,
                            .handle_slot = h,
                            .message = "Attempted release of non-live or already moved handle"
                        });
                    } else {
                        it->second = State::Released;
                    }
                    break;
                }

                case Op::OWNERSHIP_TRANSFER: {
                    const uint32_t from = node.op_a;
                    auto it = states.find(from);
                    if (it == states.end() || it->second != State::Live) {
                        errors.push_back(OwnershipError{
                            .node_index = i,
                            .handle_slot = from,
                            .message = "Use-after-move violation: handle is not active"
                        });
                    } else {
                        it->second = State::Moved;
                        if (node.result_id != dgtl::ir::INVALID_SLOT) {
                            states[node.result_id] = State::Live;
                        }
                    }
                    break;
                }

                case Op::LOAD:
                case Op::STORE: {
                    const uint32_t ptr = node.op_a;
                    auto it = states.find(ptr);
                    if (it != states.end() && it->second == State::Moved) {
                        errors.push_back(OwnershipError{
                            .node_index = i,
                            .handle_slot = ptr,
                            .message = "Dereference after ownership move violation"
                        });
                    }
                    break;
                }

                default:
                    break;
            }
        }

        // Check for leaks at stream termination
        for (const auto& [handle, state] : states) {
            if (state == State::Live || state == State::BorrowedMut) {
                errors.push_back(OwnershipError{
                    .node_index = stream.size() - 1,
                    .handle_slot = handle,
                    .message = "Linear ownership resource leak: resource was never released"
                });
            }
        }

        return errors.empty();
    }

private:
    SymbolTable& symbol_table_;
};

} // namespace dgtl::semantic