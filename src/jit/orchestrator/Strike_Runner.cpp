#include "dgtl/jit/orchestrator/Strike_Runner.hpp"

#include <cstring>
#include <cstdlib>

#if defined(_WIN32) || defined(_WIN64)
#include <windows.h>
#else
#include <sys/mman.h>
#include <unistd.h>
#endif

namespace dgtl::exec {

void StrikeRunner::release_exec_region() noexcept {
    if (!exec_region_) {
        return;
    }

#if defined(_WIN32) || defined(_WIN64)
    VirtualFree(exec_region_, 0, MEM_RELEASE);
#else
    munmap(exec_region_, exec_size_);
#endif

    exec_region_ = nullptr;
    exec_size_ = 0;
    exec_addr_ = 0;
}

StrikeRunner::~StrikeRunner() {
    release_exec_region();
}

StrikeRunner::StrikeRunner(StrikeRunner&& other) noexcept
    : exec_region_(other.exec_region_),
      exec_size_(other.exec_size_),
      exec_addr_(other.exec_addr_) {
    other.exec_region_ = nullptr;
    other.exec_size_ = 0;
    other.exec_addr_ = 0;
}

StrikeRunner& StrikeRunner::operator=(StrikeRunner&& other) noexcept {
    if (this == &other) {
        return *this;
    }

    release_exec_region();
    exec_region_ = other.exec_region_;
    exec_size_ = other.exec_size_;
    exec_addr_ = other.exec_addr_;

    other.exec_region_ = nullptr;
    other.exec_size_ = 0;
    other.exec_addr_ = 0;
    return *this;
}

size_t StrikeRunner::inject_code(dgtl::core::DGTL_Arena& arena, const std::vector<uint8_t>& machine_code) {
    (void)arena;
    release_exec_region();

    if (machine_code.empty()) {
        return 0;
    }

    const size_t alloc_size = (machine_code.size() + 63ULL) & ~63ULL;

#if defined(_WIN32) || defined(_WIN64)
    exec_region_ = static_cast<uint8_t*>(
        VirtualAlloc(nullptr, alloc_size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE)
    );
    if (!exec_region_) {
        return 0;
    }

    std::memcpy(exec_region_, machine_code.data(), machine_code.size());

    DWORD old_protect = 0;
    if (!VirtualProtect(exec_region_, alloc_size, PAGE_EXECUTE_READ, &old_protect)) {
        release_exec_region();
        return 0;
    }
#else
    exec_region_ = static_cast<uint8_t*>(
        mmap(nullptr, alloc_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0)
    );
    if (!exec_region_ || exec_region_ == MAP_FAILED) {
        exec_region_ = nullptr;
        return 0;
    }

    std::memcpy(exec_region_, machine_code.data(), machine_code.size());

    if (mprotect(exec_region_, alloc_size, PROT_READ | PROT_EXEC) != 0) {
        release_exec_region();
        return 0;
    }

    __builtin___clear_cache(reinterpret_cast<char*>(exec_region_),
                            reinterpret_cast<char*>(exec_region_ + alloc_size));
#endif

    exec_size_ = alloc_size;
    exec_addr_ = reinterpret_cast<uintptr_t>(exec_region_);
    return machine_code.size();
}

uintptr_t StrikeRunner::last_injected_address() const noexcept {
    return exec_addr_;
}

StrikeMetrics StrikeRunner::execute(dgtl::core::DGTL_Arena& arena, size_t injected_size, void* payload) {
    (void)arena;

    if (!exec_region_ || injected_size == 0) {
        return StrikeMetrics{0, static_cast<uint32_t>(StrikeStatus::InvalidInput)};
    }

    StrikeMetrics metrics{};
    metrics.executed_instructions = injected_size;

    // Safety gate: execution is opt-in via environment variable.
    // Set DGTL_STRIKE_EXECUTE=1 to enable native jump execution.
    const char* env = std::getenv("DGTL_STRIKE_EXECUTE");
    if (!env || std::strcmp(env, "1") != 0) {
        metrics.status_code = static_cast<uint32_t>(StrikeStatus::NoExecutePermission);
        return metrics;
    }

    using Entry = uint64_t (*)(void*);
    const auto fn = reinterpret_cast<Entry>(exec_region_);
    if (!fn) {
        metrics.status_code = static_cast<uint32_t>(StrikeStatus::NullEntryPoint);
        return metrics;
    }

    (void)fn(payload);
    metrics.status_code = static_cast<uint32_t>(StrikeStatus::Ok);
    return metrics;
}

} // namespace dgtl::exec
