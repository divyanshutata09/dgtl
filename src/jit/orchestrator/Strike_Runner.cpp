#include "dgtl/jit/orchestrator/Strike_Runner.hpp"

#include <cstring>
#include <cstdlib>
#include <mutex>

#if defined(_WIN32) || defined(_WIN64)
#include <windows.h>
#else
#include <sys/mman.h>
#include <unistd.h>
#endif

namespace {

std::mutex g_strike_mutex;
uint8_t* g_exec_region = nullptr;
size_t g_exec_size = 0;
uintptr_t g_exec_addr = 0;

void release_exec_region() {
    if (!g_exec_region) {
        return;
    }

#if defined(_WIN32) || defined(_WIN64)
    VirtualFree(g_exec_region, 0, MEM_RELEASE);
#else
    munmap(g_exec_region, g_exec_size);
#endif

    g_exec_region = nullptr;
    g_exec_size = 0;
    g_exec_addr = 0;
}

} // namespace

namespace dgtl::exec {

size_t StrikeRunner::inject_code(dgtl::core::DGTL_Arena& arena, const std::vector<uint8_t>& machine_code) {
    (void)arena;

    std::lock_guard<std::mutex> lock(g_strike_mutex);
    release_exec_region();

    if (machine_code.empty()) {
        return 0;
    }

    const size_t alloc_size = (machine_code.size() + 63ULL) & ~63ULL;

#if defined(_WIN32) || defined(_WIN64)
    g_exec_region = static_cast<uint8_t*>(
        VirtualAlloc(nullptr, alloc_size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE)
    );
    if (!g_exec_region) {
        return 0;
    }

    std::memcpy(g_exec_region, machine_code.data(), machine_code.size());

    DWORD old_protect = 0;
    if (!VirtualProtect(g_exec_region, alloc_size, PAGE_EXECUTE_READ, &old_protect)) {
        release_exec_region();
        return 0;
    }
#else
    g_exec_region = static_cast<uint8_t*>(
        mmap(nullptr, alloc_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0)
    );
    if (!g_exec_region || g_exec_region == MAP_FAILED) {
        g_exec_region = nullptr;
        return 0;
    }

    std::memcpy(g_exec_region, machine_code.data(), machine_code.size());

    if (mprotect(g_exec_region, alloc_size, PROT_READ | PROT_EXEC) != 0) {
        release_exec_region();
        return 0;
    }
#endif

    g_exec_size = alloc_size;
    g_exec_addr = reinterpret_cast<uintptr_t>(g_exec_region);
    return machine_code.size();
}

uintptr_t StrikeRunner::last_injected_address() noexcept {
    std::lock_guard<std::mutex> lock(g_strike_mutex);
    return g_exec_addr;
}

StrikeMetrics StrikeRunner::execute(dgtl::core::DGTL_Arena& arena, size_t injected_size, void* payload) {
    (void)arena;

    std::lock_guard<std::mutex> lock(g_strike_mutex);

    if (!g_exec_region || injected_size == 0) {
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
    const auto fn = reinterpret_cast<Entry>(g_exec_region);
    if (!fn) {
        metrics.status_code = static_cast<uint32_t>(StrikeStatus::NullEntryPoint);
        return metrics;
    }

    (void)fn(payload);
    metrics.status_code = static_cast<uint32_t>(StrikeStatus::Ok);
    return metrics;
}

} // namespace dgtl::exec
