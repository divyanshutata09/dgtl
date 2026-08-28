/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * Project:     DGTL (Deep General-purpose Transformation Layer)
 * Owner:       Divyanshu Tata
 * Repository:  github.com/TataG-Systems/DGTL
 *
 * This software is the proprietary property of TataG Systems.
 * Unauthorized copying, modification, or distribution is strictly prohibited.
 */

module;

#include <cstdlib>

// Global Module Fragment: Capture core platform headers and filesystem components
#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#elif defined(__linux__)
#include <sched.h>
#include <unistd.h>
#endif

export module dgtl.numa;

export namespace dgtl::core {

enum class NUMAPolicy : std::uint8_t {
    None = 0,
    PreferLocal = 1,
    StrictLocal = 2
};

enum class NUMAAvailability : std::uint8_t {
    Unknown = 0,
    Unavailable = 1,
    Available = 2
};

struct NUMANodeInfo {
    std::uint16_t id = 0u;
    bool online = false;
    std::uint64_t processor_mask_low64 = 0ull;
};

struct NUMATopology {
    NUMAAvailability availability = NUMAAvailability::Unknown;
    std::uint16_t node_count = 0u;
    std::uint16_t max_node_id = 0u;
    std::vector<NUMANodeInfo> nodes;

    [[nodiscard]] bool available() const noexcept {
        return availability == NUMAAvailability::Available && node_count > 0u;
    }

    [[nodiscard]] bool multi_node() const noexcept {
        return node_count > 1u;
    }

    [[nodiscard]] size_t get_node_count() const noexcept {
        return nodes.size();
    }

    [[nodiscard]] const NUMANodeInfo* get_node(uint16_t id) const noexcept {
        const auto it = std::find_if(nodes.begin(), nodes.end(), [id](const NUMANodeInfo& node) noexcept {
            return node.id == id;
        });
        return it != nodes.end() ? &(*it) : nullptr;
    }
};

[[nodiscard]] inline NUMATopology probe_numa_topology() noexcept {
    NUMATopology topo{};

#if defined(_WIN32)
    ULONG highest_node = 0u;
    if (!GetNumaHighestNodeNumber(&highest_node)) {
        topo.availability = NUMAAvailability::Unavailable;
        return topo;
    }

    topo.max_node_id = static_cast<std::uint16_t>(highest_node);
    topo.nodes.reserve(static_cast<std::size_t>(highest_node) + 1u);

    for (USHORT nid = 0u; nid <= static_cast<USHORT>(highest_node); ++nid) {
        NUMANodeInfo node{};
        node.id = nid;

        GROUP_AFFINITY affinity{};
        if (GetNumaNodeProcessorMaskEx(nid, &affinity) != 0) {
            node.online = affinity.Mask != 0;
            node.processor_mask_low64 = static_cast<std::uint64_t>(affinity.Mask);
        }
        topo.nodes.push_back(node);
    }

    for (const auto& n : topo.nodes) {
        if (n.online) {
            ++topo.node_count;
        }
    }

    topo.availability = topo.node_count > 0u ? NUMAAvailability::Available : NUMAAvailability::Unavailable;

#elif defined(__linux__)
    const std::filesystem::path nodes_root{"/sys/devices/system/node"};
    if (!std::filesystem::exists(nodes_root)) {
        topo.availability = NUMAAvailability::Unavailable;
        return topo;
    }

    topo.nodes.reserve(8u);

    for (const auto& entry : std::filesystem::directory_iterator(nodes_root)) {
        if (!entry.is_directory()) {
            continue;
        }

        // Standardize path extraction across wide/narrow multi-OS implementations
        const std::string filename = entry.path().filename().string();
        const std::string_view name{filename};

        if (!name.starts_with("node") || name.size() <= 4u) {
            continue;
        }

        const bool digits_only = std::all_of(name.begin() + 4, name.end(), [](char c) {
            return c >= '0' && c <= '9';
        });
        if (!digits_only) {
            continue;
        }

        const unsigned long parsed = std::strtoul(name.data() + 4u, nullptr, 10);
        if (parsed > 65535ul) {
            continue;
        }
        const std::uint16_t node_id = static_cast<std::uint16_t>(parsed);
        NUMANodeInfo node{};
        node.id = node_id;
        node.online = true;
        topo.nodes.push_back(node);
        ++topo.node_count;
        if (node_id > topo.max_node_id) {
            topo.max_node_id = node_id;
        }
    }

    topo.availability = topo.node_count > 0u ? NUMAAvailability::Available : NUMAAvailability::Unavailable;
#else
    topo.availability = NUMAAvailability::Unavailable;
#endif

    return topo;
}

class NUMAController {
public:
    NUMAController() = default;

    [[nodiscard]] static constexpr NUMAPolicy default_policy() noexcept {
        return NUMAPolicy::None;
    }

    [[nodiscard]] static const NUMATopology& topology() noexcept {
        static const NUMATopology s_topo = probe_numa_topology();
        return s_topo;
    }

    [[nodiscard]] static bool available() noexcept {
        return topology().available();
    }

    void set_policy(NUMAPolicy policy) noexcept {
        policy_.store(policy, std::memory_order_relaxed);
    }

    [[nodiscard]] NUMAPolicy policy() const noexcept {
        return policy_.load(std::memory_order_relaxed);
    }

    [[nodiscard]] bool strict_local_required() const noexcept {
        return policy() == NUMAPolicy::StrictLocal;
    }

    void set_preferred_node(std::uint16_t node) noexcept {
        preferred_node_.store(node, std::memory_order_relaxed);
    }

    [[nodiscard]] std::uint16_t preferred_node() const noexcept {
        return preferred_node_.load(std::memory_order_relaxed);
    }

    [[nodiscard]] bool bind_current_thread_to_node(std::uint16_t node) const noexcept {
        const NUMATopology topo = topology();
        if (!topo.available() || node > topo.max_node_id) [[unlikely]] {
            return false;
        }

#if defined(_WIN32)
        GROUP_AFFINITY affinity{};
        if (GetNumaNodeProcessorMaskEx(static_cast<USHORT>(node), &affinity) == 0 || affinity.Mask == 0) [[unlikely]] {
            return false;
        }
        return SetThreadGroupAffinity(GetCurrentThread(), &affinity, nullptr) != 0;
#elif defined(__linux__)
        (void)node;
        return false;
#else
        (void)node;
        return false;
#endif
    }

    [[nodiscard]] bool apply_policy() const noexcept {
        if (policy() == NUMAPolicy::None) [[likely]] {
            return true;
        }
        return bind_current_thread_to_node(preferred_node());
    }

private:
    std::atomic<NUMAPolicy> policy_{default_policy()};
    std::atomic<std::uint16_t> preferred_node_{0u};
};

} // namespace dgtl::core