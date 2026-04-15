/* * DGTL Backend vNext - Strike Runner
 * Executable-memory backed injection and controlled execution bridge.
 */

#pragma once
#include <cstdint>
#include <cstddef>
#include <vector>
#include "dgtl/core/Arena.hpp"

namespace dgtl::exec {

enum class StrikeStatus : uint32_t {
    Ok = 0,
    InvalidInput = 1,
    NoExecutePermission = 10,
    NullEntryPoint = 11,
    InjectFailed = 12,
};

/**
 * @struct StrikeMetrics
 * @brief Lightweight metrics container for a single generated strike.
 */
struct StrikeMetrics {
    uint64_t executed_instructions = 0;
    uint32_t status_code = 0;
};

/**
 * @class StrikeRunner
 * @brief Runtime bridge for executable-memory injection and optional strike execution.
 */
class StrikeRunner {
private:
    uint8_t* exec_region_ = nullptr;
    size_t exec_size_ = 0;
    uintptr_t exec_addr_ = 0;

    void release_exec_region() noexcept;

public:
    StrikeRunner() = default;
    ~StrikeRunner();

    StrikeRunner(const StrikeRunner&) = delete;
    StrikeRunner& operator=(const StrikeRunner&) = delete;

    StrikeRunner(StrikeRunner&& other) noexcept;
    StrikeRunner& operator=(StrikeRunner&& other) noexcept;

    // Returns injected byte count on success, 0 on failure.
    size_t inject_code(dgtl::core::DGTL_Arena& arena, const std::vector<uint8_t>& machine_code);

    // Returns last injected executable address, or 0 if none.
    uintptr_t last_injected_address() const noexcept;

    StrikeMetrics execute(dgtl::core::DGTL_Arena& arena, size_t injected_size, void* payload);
};

} // namespace dgtl::exec

