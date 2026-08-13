/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * Project:     DGTL (Deep General-purpose Transformation Layer)
 * Owner:       Divyanshu Tata
 * Repository: github.com/TataG-Systems/DGTL
 *
 * This software is the proprietary property of TataG Systems.
 * Unauthorized copying, modification, or distribution is strictly prohibited.
 */

module;

#include <cstdlib>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cmath>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <deque>
#include <functional>
#include <iostream>
#include <limits>
#include <mutex>
#include <new>
#include <numeric>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

export module dgtl.runtime_intrinsics;

import dgtl.dgtl_bulletins;

export namespace dgtl::runtime::intrinsics_detail {

inline const char* resolve_string_arg(uint64_t raw) noexcept {
    if (raw == 0u) return nullptr;
    const std::string_view by_handle = dgtl::runtime::try_lookup_string(raw);
    if (!by_handle.empty()) {
        return by_handle.data();
    }
    return reinterpret_cast<const char*>(raw);
}

inline std::mutex& channel_mutex() {
    static std::mutex m;
    return m;
}

inline std::unordered_map<uint64_t, std::deque<uint64_t>>& channel_queues() {
    static std::unordered_map<uint64_t, std::deque<uint64_t>> q;
    return q;
}

inline std::condition_variable& channel_cv() {
    static std::condition_variable cv;
    return cv;
}

inline std::array<std::atomic_flag, 256>& lock_table() {
    static std::array<std::atomic_flag, 256> table{};
    return table;
}

struct Tensor {
    std::vector<uint64_t> shape;
    std::vector<double> data;
};

[[nodiscard]] inline uint64_t shape_product(const std::vector<uint64_t>& shape) noexcept {
    uint64_t total = 1;
    for (const uint64_t dim : shape) {
        if (dim == 0 || total > (std::numeric_limits<uint64_t>::max() / dim)) {
            return 0;
        }
        total *= dim;
    }
    return total;
}

[[nodiscard]] inline std::vector<uint64_t> shape_strides(const std::vector<uint64_t>& shape) {
    std::vector<uint64_t> strides(shape.size(), 1);
    for (std::size_t i = shape.size(); i > 1; --i) {
        strides[i - 2] = strides[i - 1] * shape[i - 1];
    }
    return strides;
}

inline std::mutex& tensor_mutex() {
    static std::mutex m;
    return m;
}

inline std::unordered_map<uint64_t, Tensor>& tensor_store() {
    static std::unordered_map<uint64_t, Tensor> store;
    return store;
}

inline std::atomic<uint64_t>& next_tensor_handle() {
    static std::atomic<uint64_t> h{1};
    return h;
}

[[nodiscard]] inline uint64_t emplace_tensor(Tensor tensor) {
    if (tensor.shape.empty()) {
        tensor.shape.push_back(static_cast<uint64_t>(tensor.data.size()));
    }
    if (shape_product(tensor.shape) != tensor.data.size()) {
        return 0;
    }
    std::lock_guard<std::mutex> lock(tensor_mutex());
    const uint64_t handle = next_tensor_handle().fetch_add(1, std::memory_order_relaxed);
    tensor_store().emplace(handle, std::move(tensor));
    return handle;
}

struct Graph {
    uint64_t node_count = 0;
    std::unordered_map<uint64_t, std::vector<uint64_t>> adj;
};

inline std::mutex& graph_mutex() {
    static std::mutex m;
    return m;
}

inline std::unordered_map<uint64_t, Graph>& graph_store() {
    static std::unordered_map<uint64_t, Graph> store;
    return store;
}

inline std::atomic<uint64_t>& next_graph_handle() {
    static std::atomic<uint64_t> h{1};
    return h;
}

struct Tree {
    struct Node {
        uint64_t value = 0;
        int64_t left = -1;
        int64_t right = -1;
    };
    std::vector<Node> nodes;
    int64_t root = -1;
};

inline std::mutex& tree_mutex() {
    static std::mutex m;
    return m;
}

inline std::unordered_map<uint64_t, Tree>& tree_store() {
    static std::unordered_map<uint64_t, Tree> store;
    return store;
}

inline std::atomic<uint64_t>& next_tree_handle() {
    static std::atomic<uint64_t> h{1};
    return h;
}

struct Trie {
    struct Node {
        std::unordered_map<unsigned char, uint64_t> next;
        bool has_value = false;
        uint64_t value = 0;
    };
    std::vector<Node> nodes;
};

inline std::mutex& trie_mutex() {
    static std::mutex m;
    return m;
}

inline std::unordered_map<uint64_t, Trie>& trie_store() {
    static std::unordered_map<uint64_t, Trie> store;
    return store;
}

inline std::atomic<uint64_t>& next_trie_handle() {
    static std::atomic<uint64_t> h{1};
    return h;
}

inline std::mutex& dsa_mutex() {
    static std::mutex m;
    return m;
}

inline std::unordered_map<uint64_t, std::size_t>& dsa_alloc_align_by_addr() {
    static std::unordered_map<uint64_t, std::size_t> m;
    return m;
}

inline std::unordered_map<uint64_t, std::array<uint64_t, 3>>& tuple_store() {
    static std::unordered_map<uint64_t, std::array<uint64_t, 3>> m;
    return m;
}

inline std::mutex& tuple_mutex() {
    static std::mutex m;
    return m;
}

inline std::atomic<uint64_t>& next_tuple_handle() {
    static std::atomic<uint64_t> h{1};
    return h;
}

inline std::unordered_map<uint64_t, std::vector<double>>& vector_store() {
    static std::unordered_map<uint64_t, std::vector<double>> m;
    return m;
}

inline std::mutex& vector_mutex() {
    static std::mutex m;
    return m;
}

inline std::atomic<uint64_t>& next_vector_handle() {
    static std::atomic<uint64_t> h{1};
    return h;
}

inline std::mutex& ownership_mutex() {
    static std::mutex m;
    return m;
}

inline std::unordered_map<uint64_t, uint64_t>& ownership_owner_by_resource() {
    static std::unordered_map<uint64_t, uint64_t> m;
    return m;
}

inline std::mutex& thread_tokens_mutex() {
    static std::mutex m;
    return m;
}

inline std::unordered_map<uint64_t, std::thread>& thread_tokens() {
    static std::unordered_map<uint64_t, std::thread> m;
    return m;
}

inline std::atomic<uint64_t>& next_thread_token() {
    static std::atomic<uint64_t> h{1};
    return h;
}

[[nodiscard]] inline bool is_pow2(std::size_t x) noexcept {
    return x != 0u && (x & (x - 1u)) == 0u;
}

[[nodiscard]] inline uint16_t f32_to_f16_bits(float f) noexcept {
    uint32_t x = 0;
    std::memcpy(&x, &f, sizeof(x));
    const uint32_t sign = (x >> 16) & 0x8000u;
    int32_t exp = static_cast<int32_t>((x >> 23) & 0xFFu) - 127 + 15;
    uint32_t mant = x & 0x7FFFFFu;

    if (exp <= 0) {
        if (exp < -10) return static_cast<uint16_t>(sign);
        mant |= 0x800000u;
        const uint32_t shift = static_cast<uint32_t>(14 - exp);
        const uint16_t half = static_cast<uint16_t>(sign | (mant >> shift));
        return half;
    }
    if (exp >= 31) {
        return static_cast<uint16_t>(sign | 0x7C00u);
    }
    return static_cast<uint16_t>(sign | (static_cast<uint32_t>(exp) << 10) | (mant >> 13));
}

[[nodiscard]] inline float f16_bits_to_f32(uint16_t h) noexcept {
    const uint32_t sign = (static_cast<uint32_t>(h & 0x8000u)) << 16;
    uint32_t exp = (h >> 10) & 0x1Fu;
    uint32_t mant = h & 0x03FFu;
    uint32_t bits = 0;

    if (exp == 0) {
        if (mant == 0) {
            bits = sign;
        } else {
            exp = 1;
            while ((mant & 0x0400u) == 0u) {
                mant <<= 1u;
                --exp;
            }
            mant &= 0x03FFu;
            bits = sign | ((exp + 127u - 15u) << 23) | (mant << 13);
        }
    } else if (exp == 31u) {
        bits = sign | 0x7F800000u | (mant << 13);
    } else {
        bits = sign | ((exp + 127u - 15u) << 23) | (mant << 13);
    }

    float out = 0.0f;
    std::memcpy(&out, &bits, sizeof(out));
    return out;
}

} // namespace dgtl::runtime::intrinsics_detail

export extern "C" {

/* ─── STANDARD OUTPUT & LOGGING INTRINSICS ─── */

inline uint64_t __dgtl_rt_println_i64(int64_t val, uint64_t = 0, uint64_t = 0) noexcept {
    std::printf("%lld\n", static_cast<long long>(val));
    std::fflush(stdout);
    return 0u;
}

inline uint64_t __dgtl_rt_print_i64(int64_t val, uint64_t = 0, uint64_t = 0) noexcept {
    std::printf("%lld", static_cast<long long>(val));
    std::fflush(stdout);
    return 0u;
}

inline uint64_t __dgtl_rt_println_f64(double val, uint64_t = 0, uint64_t = 0) noexcept {
    std::printf("%g\n", val);
    std::fflush(stdout);
    return 0u;
}

inline uint64_t __dgtl_rt_print_f64(double val, uint64_t = 0, uint64_t = 0) noexcept {
    std::printf("%g", val);
    std::fflush(stdout);
    return 0u;
}

inline uint64_t __dgtl_rt_println_str(uint64_t str_raw, uint64_t = 0, uint64_t = 0) noexcept {
    const char* s = dgtl::runtime::intrinsics_detail::resolve_string_arg(str_raw);
    if (s != nullptr) {
        std::printf("%s\n", s);
    } else {
        std::printf("\n");
    }
    std::fflush(stdout);
    return 0u;
}

inline uint64_t __dgtl_rt_print_str(uint64_t str_raw, uint64_t = 0, uint64_t = 0) noexcept {
    const char* s = dgtl::runtime::intrinsics_detail::resolve_string_arg(str_raw);
    if (s != nullptr) {
        std::printf("%s", s);
    }
    std::fflush(stdout);
    return 0u;
}

inline uint64_t __dgtl_rt_println_bool(uint64_t cond, uint64_t = 0, uint64_t = 0) noexcept {
    std::printf("%s\n", (cond != 0u) ? "true" : "false");
    std::fflush(stdout);
    return 0u;
}

inline uint64_t __dgtl_rt_print_bool(uint64_t cond, uint64_t = 0, uint64_t = 0) noexcept {
    std::printf("%s", (cond != 0u) ? "true" : "false");
    std::fflush(stdout);
    return 0u;
}

/* ─── STRING INTRINSICS ─── */

inline uint64_t __dgtl_rt_string_length(uint64_t s, uint64_t = 0, uint64_t = 0) noexcept {
    const char* p = dgtl::runtime::intrinsics_detail::resolve_string_arg(s);
    return (p == nullptr) ? 0ull : static_cast<uint64_t>(std::strlen(p));
}

inline uint64_t __dgtl_rt_string_compare(uint64_t a, uint64_t b, uint64_t = 0) noexcept {
    const char* lhs = dgtl::runtime::intrinsics_detail::resolve_string_arg(a);
    const char* rhs = dgtl::runtime::intrinsics_detail::resolve_string_arg(b);
    if (lhs == nullptr || rhs == nullptr) {
        return 0;
    }
    return static_cast<uint64_t>(std::strcmp(lhs, rhs));
}

inline uint64_t __dgtl_rt_string_concat(uint64_t a, uint64_t b, uint64_t = 0) noexcept {
    const char* lhs = dgtl::runtime::intrinsics_detail::resolve_string_arg(a);
    const char* rhs = dgtl::runtime::intrinsics_detail::resolve_string_arg(b);
    if (lhs == nullptr || rhs == nullptr) {
        return 0;
    }
    thread_local std::string buffer;
    buffer.assign(lhs);
    buffer += rhs;
    return reinterpret_cast<uint64_t>(buffer.c_str());
}

inline uint64_t __dgtl_rt_string_substring(uint64_t s, uint64_t start, uint64_t len) noexcept {
    const char* src = dgtl::runtime::intrinsics_detail::resolve_string_arg(s);
    if (src == nullptr) {
        return 0;
    }
    const std::size_t src_len = std::strlen(src);
    const std::size_t begin = static_cast<std::size_t>(start);
    if (begin >= src_len) {
        return 0;
    }
    const std::size_t n = std::min(static_cast<std::size_t>(len), src_len - begin);
    thread_local std::string buffer;
    buffer.assign(src + begin, n);
    return reinterpret_cast<uint64_t>(buffer.c_str());
}

inline uint64_t __dgtl_rt_bool_select(uint64_t cond, uint64_t on_true, uint64_t on_false) noexcept {
    return (cond != 0u) ? on_true : on_false;
}

inline uint64_t __dgtl_rt_ptr_diff(uint64_t a, uint64_t b, uint64_t = 0) noexcept {
    return a - b;
}

inline uint64_t __dgtl_rt_atomic_load(uint64_t addr, uint64_t = 0, uint64_t = 0) noexcept {
    if (addr == 0u) return 0u;
    const auto* p = reinterpret_cast<const std::atomic<uint64_t>*>(addr);
    return p->load(std::memory_order_seq_cst);
}

inline uint64_t __dgtl_rt_atomic_store(uint64_t addr, uint64_t value, uint64_t = 0) noexcept {
    if (addr == 0u) return 0u;
    auto* p = reinterpret_cast<std::atomic<uint64_t>*>(addr);
    p->store(value, std::memory_order_seq_cst);
    return value;
}

inline uint64_t __dgtl_rt_atomic_fence(uint64_t = 0, uint64_t = 0, uint64_t = 0) noexcept {
    std::atomic_thread_fence(std::memory_order_seq_cst);
    return 0u;
}

inline uint64_t __dgtl_rt_acquire_fence(uint64_t = 0, uint64_t = 0, uint64_t = 0) noexcept {
    std::atomic_thread_fence(std::memory_order_acquire);
    return 0u;
}

inline uint64_t __dgtl_rt_release_fence(uint64_t = 0, uint64_t = 0, uint64_t = 0) noexcept {
    std::atomic_thread_fence(std::memory_order_release);
    return 0u;
}

inline uint64_t __dgtl_rt_memory_fence(uint64_t = 0, uint64_t = 0, uint64_t = 0) noexcept {
    std::atomic_thread_fence(std::memory_order_seq_cst);
    return 0u;
}

inline uint64_t __dgtl_rt_async_await(uint64_t token, uint64_t = 0, uint64_t = 0) noexcept {
    return token;
}

inline uint64_t __dgtl_rt_async_launch(uint64_t fn_ptr, uint64_t arg0, uint64_t arg1) noexcept {
    using Fn = uint64_t (*)(uint64_t, uint64_t, uint64_t);
    if (fn_ptr == 0u) return 0u;
    const Fn fn = reinterpret_cast<Fn>(fn_ptr);
    std::thread([fn, arg0, arg1]() {
        (void)fn(arg0, arg1, 0u);
    }).detach();
    return fn_ptr;
}

inline uint64_t __dgtl_rt_async_spawn(uint64_t fn_ptr, uint64_t arg0, uint64_t arg1) noexcept {
    return __dgtl_rt_async_launch(fn_ptr, arg0, arg1);
}

inline uint64_t __dgtl_rt_atomic_snapshot(uint64_t addr, uint64_t words, uint64_t = 0) noexcept {
    if (addr == 0u || words == 0u) return 0u;
    const auto* p = reinterpret_cast<const std::atomic<uint64_t>*>(addr);
    uint64_t hash = 1469598103934665603ull;
    for (uint64_t i = 0; i < words; ++i) {
        const uint64_t v = p[i].load(std::memory_order_seq_cst);
        hash ^= v + 0x9E3779B97F4A7C15ull + (hash << 6u) + (hash >> 2u);
    }
    return hash;
}

inline uint64_t __dgtl_rt_barrier_sync(uint64_t = 0, uint64_t = 0, uint64_t = 0) noexcept {
    std::atomic_thread_fence(std::memory_order_seq_cst);
    return 0u;
}

inline uint64_t __dgtl_rt_chan_send(uint64_t channel_id, uint64_t value, uint64_t = 0) noexcept {
    auto& m = dgtl::runtime::intrinsics_detail::channel_mutex();
    auto& q = dgtl::runtime::intrinsics_detail::channel_queues();
    {
        std::lock_guard<std::mutex> lock(m);
        q[channel_id].push_back(value);
    }
    dgtl::runtime::intrinsics_detail::channel_cv().notify_one();
    return value;
}

inline uint64_t __dgtl_rt_chan_recv(uint64_t channel_id, uint64_t = 0, uint64_t = 0) noexcept {
    auto& m = dgtl::runtime::intrinsics_detail::channel_mutex();
    auto& q = dgtl::runtime::intrinsics_detail::channel_queues();
    auto& cv = dgtl::runtime::intrinsics_detail::channel_cv();
    std::unique_lock<std::mutex> lock(m);
    cv.wait(lock, [&]() {
        auto it = q.find(channel_id);
        return it != q.end() && !it->second.empty();
    });
    auto& dq = q[channel_id];
    const uint64_t value = dq.front();
    dq.pop_front();
    return value;
}

inline uint64_t __dgtl_rt_deopt_trigger(uint64_t reason, uint64_t pc, uint64_t meta) noexcept {
    uint64_t tag = 0xD30F000000000000ull;
    tag ^= (reason & 0xFFFFull) << 32u;
    tag ^= (pc & 0xFFFFull) << 16u;
    tag ^= (meta & 0xFFFFull);
    return tag;
}

inline uint64_t __dgtl_rt_dsa_allocate(uint64_t size, uint64_t alignment, uint64_t = 0) noexcept {
    if (size == 0u) return 0u;
    std::size_t align = static_cast<std::size_t>(alignment);
    if (!dgtl::runtime::intrinsics_detail::is_pow2(align) || align < alignof(std::max_align_t)) {
        align = alignof(std::max_align_t);
    }
    const void* mem = nullptr;
    try {
        mem = ::operator new(static_cast<std::size_t>(size), std::align_val_t(align));
    } catch (...) {
        return 0u;
    }
    const uint64_t addr = reinterpret_cast<uint64_t>(mem);
    std::lock_guard<std::mutex> lock(dgtl::runtime::intrinsics_detail::dsa_mutex());
    dgtl::runtime::intrinsics_detail::dsa_alloc_align_by_addr()[addr] = align;
    return addr;
}

inline uint64_t __dgtl_rt_dsa_deallocate(uint64_t addr, uint64_t = 0, uint64_t = 0) noexcept {
    if (addr == 0u) return 0u;
    std::size_t align = alignof(std::max_align_t);
    {
        std::lock_guard<std::mutex> lock(dgtl::runtime::intrinsics_detail::dsa_mutex());
        auto& map = dgtl::runtime::intrinsics_detail::dsa_alloc_align_by_addr();
        const auto it = map.find(addr);
        if (it != map.end()) {
            align = it->second;
            map.erase(it);
        }
    }
    ::operator delete(reinterpret_cast<void*>(addr), std::align_val_t(align));
    return 1u;
}

inline uint64_t __dgtl_rt_fcvtl_f16_to_f32(uint64_t hbits, uint64_t = 0, uint64_t = 0) noexcept {
    const uint16_t h = static_cast<uint16_t>(hbits & 0xFFFFu);
    const float f = dgtl::runtime::intrinsics_detail::f16_bits_to_f32(h);
    uint32_t out = 0;
    std::memcpy(&out, &f, sizeof(out));
    return static_cast<uint64_t>(out);
}

inline uint64_t __dgtl_rt_fcvtn_f32_to_f16(uint64_t fbits, uint64_t = 0, uint64_t = 0) noexcept {
    const uint32_t in = static_cast<uint32_t>(fbits & 0xFFFFFFFFu);
    float f = 0.0f;
    std::memcpy(&f, &in, sizeof(f));
    return static_cast<uint64_t>(dgtl::runtime::intrinsics_detail::f32_to_f16_bits(f));
}

inline uint64_t __dgtl_rt_fiber_yield(uint64_t = 0, uint64_t = 0, uint64_t = 0) noexcept {
    std::this_thread::yield();
    return 0u;
}

inline uint64_t __dgtl_rt_fma_widen_f16_f32(uint64_t a_h, uint64_t b_h, uint64_t c_fbits) noexcept {
    const float a = dgtl::runtime::intrinsics_detail::f16_bits_to_f32(static_cast<uint16_t>(a_h & 0xFFFFu));
    const float b = dgtl::runtime::intrinsics_detail::f16_bits_to_f32(static_cast<uint16_t>(b_h & 0xFFFFu));
    const uint32_t c_bits = static_cast<uint32_t>(c_fbits & 0xFFFFFFFFu);
    float c = 0.0f;
    std::memcpy(&c, &c_bits, sizeof(c));
    const float out_f = std::fma(a, b, c);
    uint32_t out_bits = 0;
    std::memcpy(&out_bits, &out_f, sizeof(out_bits));
    return static_cast<uint64_t>(out_bits);
}

inline uint64_t __dgtl_rt_gather_16(uint64_t base_ptr, uint64_t indices_ptr, uint64_t count) noexcept {
    if (base_ptr == 0u || indices_ptr == 0u || count == 0u) return 0u;
    const uint16_t* base = reinterpret_cast<const uint16_t*>(base_ptr);
    const uint32_t* idx = reinterpret_cast<const uint32_t*>(indices_ptr);
    std::vector<double> gathered;
    gathered.reserve(static_cast<std::size_t>(count));
    for (uint64_t i = 0; i < count; ++i) {
        gathered.push_back(static_cast<double>(base[idx[i]]));
    }
    std::lock_guard<std::mutex> lock(dgtl::runtime::intrinsics_detail::vector_mutex());
    const uint64_t handle = dgtl::runtime::intrinsics_detail::next_vector_handle().fetch_add(1, std::memory_order_relaxed);
    dgtl::runtime::intrinsics_detail::vector_store().emplace(handle, std::move(gathered));
    return handle;
}

inline uint64_t __dgtl_rt_gpu_launch(uint64_t kernel_ptr, uint64_t arg0, uint64_t arg1) noexcept {
    using KernelFn = uint64_t (*)(uint64_t, uint64_t, uint64_t);
    if (kernel_ptr == 0u) return 0u;
    const KernelFn fn = reinterpret_cast<KernelFn>(kernel_ptr);
    return fn(arg0, arg1, 0u);
}

inline uint64_t __dgtl_rt_graph_build(uint64_t node_count, uint64_t edges_ptr, uint64_t edge_count) noexcept {
    dgtl::runtime::intrinsics_detail::Graph graph{};
    graph.node_count = node_count;

    const uint64_t* edges = reinterpret_cast<const uint64_t*>(edges_ptr);
    for (uint64_t i = 0; i < edge_count; ++i) {
        const uint64_t packed = (edges != nullptr) ? edges[i] : 0u;
        const uint64_t from = static_cast<uint32_t>(packed >> 32u);
        const uint64_t to = static_cast<uint32_t>(packed & 0xFFFFFFFFu);
        graph.adj[from].push_back(to);
    }

    std::lock_guard<std::mutex> lock(dgtl::runtime::intrinsics_detail::graph_mutex());
    const uint64_t handle = dgtl::runtime::intrinsics_detail::next_graph_handle().fetch_add(1, std::memory_order_relaxed);
    dgtl::runtime::intrinsics_detail::graph_store().emplace(handle, std::move(graph));
    return handle;
}

inline uint64_t __dgtl_rt_graph_traverse(uint64_t graph_handle, uint64_t start_node, uint64_t mode) noexcept {
    std::lock_guard<std::mutex> lock(dgtl::runtime::intrinsics_detail::graph_mutex());
    auto& store = dgtl::runtime::intrinsics_detail::graph_store();
    const auto it_graph = store.find(graph_handle);
    if (it_graph == store.end()) {
        return 0u;
    }

    const auto& graph = it_graph->second;
    std::unordered_map<uint64_t, bool> visited;
    std::vector<uint64_t> frontier;
    frontier.push_back(start_node);

    uint64_t count = 0;
    uint64_t hash = 1469598103934665603ull;

    while (!frontier.empty()) {
        const uint64_t node = (mode == 0u) ? frontier.front() : frontier.back();
        if (mode == 0u) {
            frontier.erase(frontier.begin());
        } else {
            frontier.pop_back();
        }

        if (visited[node]) {
            continue;
        }
        visited[node] = true;
        ++count;
        hash ^= node + 0x9E3779B97F4A7C15ull + (hash << 6u) + (hash >> 2u);

        const auto it_adj = graph.adj.find(node);
        if (it_adj != graph.adj.end()) {
            std::copy_if(it_adj->second.begin(), it_adj->second.end(), std::back_inserter(frontier), [&visited](uint64_t next) noexcept {
                return !visited[next];
            });
        }
    }

    return (hash & 0xFFFFFFFF00000000ull) | (count & 0xFFFFFFFFull);
}

inline uint64_t __dgtl_rt_guard_alias(uint64_t a, uint64_t b, uint64_t fail_code) noexcept {
    return (a == b) ? 1u : fail_code;
}

inline uint64_t __dgtl_rt_guard_value(uint64_t value, uint64_t expected, uint64_t fail_code) noexcept {
    return (value == expected) ? value : fail_code;
}

inline uint64_t __dgtl_rt_lea(uint64_t base, uint64_t index, uint64_t scale) noexcept {
    return base + index * scale;
}

inline uint64_t __dgtl_rt_lock_acquire(uint64_t lock_id, uint64_t = 0, uint64_t = 0) noexcept {
    auto& table = dgtl::runtime::intrinsics_detail::lock_table();
    std::atomic_flag& flag = table[static_cast<std::size_t>(lock_id & 0xFFu)];
    while (flag.test_and_set(std::memory_order_acquire)) {
        std::this_thread::yield();
    }
    return 1u;
}

inline uint64_t __dgtl_rt_lock_release(uint64_t lock_id, uint64_t = 0, uint64_t = 0) noexcept {
    auto& table = dgtl::runtime::intrinsics_detail::lock_table();
    std::atomic_flag& flag = table[static_cast<std::size_t>(lock_id & 0xFFu)];
    flag.clear(std::memory_order_release);
    return 0u;
}

inline uint64_t __dgtl_rt_ownership_acquire(uint64_t resource, uint64_t owner, uint64_t = 0) noexcept {
    std::lock_guard<std::mutex> lock(dgtl::runtime::intrinsics_detail::ownership_mutex());
    auto& map = dgtl::runtime::intrinsics_detail::ownership_owner_by_resource();
    const auto it = map.find(resource);
    if (it == map.end() || it->second == owner) {
        map[resource] = owner;
        return 1u;
    }
    return 0u;
}

inline uint64_t __dgtl_rt_ownership_release(uint64_t resource, uint64_t owner, uint64_t = 0) noexcept {
    std::lock_guard<std::mutex> lock(dgtl::runtime::intrinsics_detail::ownership_mutex());
    auto& map = dgtl::runtime::intrinsics_detail::ownership_owner_by_resource();
    const auto it = map.find(resource);
    if (it == map.end() || it->second != owner) return 0u;
    map.erase(it);
    return 1u;
}

inline uint64_t __dgtl_rt_ownership_transfer(uint64_t resource, uint64_t from_owner, uint64_t to_owner) noexcept {
    std::lock_guard<std::mutex> lock(dgtl::runtime::intrinsics_detail::ownership_mutex());
    auto& map = dgtl::runtime::intrinsics_detail::ownership_owner_by_resource();
    const auto it = map.find(resource);
    if (it == map.end() || it->second != from_owner) return 0u;
    it->second = to_owner;
    return 1u;
}

inline uint64_t __dgtl_rt_phi_multi(uint64_t values_ptr, uint64_t count, uint64_t index) noexcept {
    if (values_ptr == 0u || count == 0u || index >= count) return 0u;
    const uint64_t* values = reinterpret_cast<const uint64_t*>(values_ptr);
    return values[index];
}

inline uint64_t __dgtl_rt_phi_node(uint64_t from_a, uint64_t from_b, uint64_t pick_b) noexcept {
    return (pick_b != 0u) ? from_b : from_a;
}

inline uint64_t __dgtl_rt_prefetch(uint64_t addr, uint64_t = 0, uint64_t = 0) noexcept {
#if defined(__GNUC__) || defined(__clang__)
    if (addr != 0u) {
        __builtin_prefetch(reinterpret_cast<const void*>(addr), 0, 3);
    }
#endif
    return 0u;
}

inline uint64_t __dgtl_rt_scop_analyze(uint64_t begin, uint64_t end, uint64_t stride) noexcept {
    if (stride == 0u || end <= begin) return 0u;
    const uint64_t trip = (end - begin + stride - 1u) / stride;
    uint64_t flags = 1u;
    if (trip >= 16u) flags |= 2u;
    if ((trip & (trip - 1u)) == 0u) flags |= 4u;
    return (trip << 8u) | flags;
}

inline uint64_t __dgtl_rt_select_multi(uint64_t values_ptr, uint64_t count, uint64_t selector) noexcept {
    if (values_ptr == 0u || count == 0u) return 0u;
    const uint64_t* values = reinterpret_cast<const uint64_t*>(values_ptr);
    return values[selector % count];
}

inline uint64_t __dgtl_rt_simd_bsl_predicate(uint64_t mask, uint64_t a, uint64_t b) noexcept {
    return (a & mask) | (b & ~mask);
}

inline uint64_t __dgtl_rt_switch_dispatch(uint64_t key, uint64_t table_ptr, uint64_t entries) noexcept {
    if (table_ptr == 0u || entries == 0u) return 0u;
    const uint64_t* table = reinterpret_cast<const uint64_t*>(table_ptr);
    for (uint64_t i = 0; i < entries; ++i) {
        const uint64_t k = table[i * 2u + 0u];
        const uint64_t v = table[i * 2u + 1u];
        if (k == key) return v;
    }
    return 0u;
}

inline uint64_t __dgtl_rt_sync_barrier(uint64_t = 0, uint64_t = 0, uint64_t = 0) noexcept {
    std::atomic_thread_fence(std::memory_order_seq_cst);
    return 0u;
}

inline uint64_t __dgtl_rt_tensor_reshape(uint64_t tensor_handle, uint64_t shape_ptr, uint64_t rank) noexcept {
    if (shape_ptr == 0u || rank == 0u || rank > 8u) {
        return 0u;
    }
    const uint64_t* dims = reinterpret_cast<const uint64_t*>(shape_ptr);
    std::vector<uint64_t> shape(dims, dims + static_cast<std::size_t>(rank));

    std::lock_guard<std::mutex> lock(dgtl::runtime::intrinsics_detail::tensor_mutex());
    auto& store = dgtl::runtime::intrinsics_detail::tensor_store();
    const auto it = store.find(tensor_handle);
    if (it == store.end()) {
        return 0u;
    }

    if (dgtl::runtime::intrinsics_detail::shape_product(shape) != it->second.data.size()) {
        return 0u;
    }

    dgtl::runtime::intrinsics_detail::Tensor out = it->second;
    out.shape = std::move(shape);
    const uint64_t handle = dgtl::runtime::intrinsics_detail::next_tensor_handle().fetch_add(1, std::memory_order_relaxed);
    store.emplace(handle, std::move(out));
    return handle;
}

inline uint64_t __dgtl_rt_tensor_slice(uint64_t tensor_handle, uint64_t begin, uint64_t len) noexcept {
    std::lock_guard<std::mutex> lock(dgtl::runtime::intrinsics_detail::tensor_mutex());
    auto& store = dgtl::runtime::intrinsics_detail::tensor_store();
    const auto it = store.find(tensor_handle);
    if (it == store.end() || begin >= it->second.data.size()) {
        return 0u;
    }

    const std::size_t n = std::min<std::size_t>(static_cast<std::size_t>(len), it->second.data.size() - static_cast<std::size_t>(begin));
    dgtl::runtime::intrinsics_detail::Tensor out{};
    out.shape = {static_cast<uint64_t>(n)};
    out.data.assign(it->second.data.begin() + static_cast<std::ptrdiff_t>(begin),
                    it->second.data.begin() + static_cast<std::ptrdiff_t>(begin + n));
    const uint64_t handle = dgtl::runtime::intrinsics_detail::next_tensor_handle().fetch_add(1, std::memory_order_relaxed);
    store.emplace(handle, std::move(out));
    return handle;
}

inline uint64_t __dgtl_rt_tensor_concat(uint64_t lhs_handle, uint64_t rhs_handle, uint64_t axis) noexcept {
    std::lock_guard<std::mutex> lock(dgtl::runtime::intrinsics_detail::tensor_mutex());
    auto& store = dgtl::runtime::intrinsics_detail::tensor_store();
    const auto it_l = store.find(lhs_handle);
    const auto it_r = store.find(rhs_handle);
    if (it_l == store.end() || it_r == store.end()) {
        return 0u;
    }

    const auto& a = it_l->second;
    const auto& b = it_r->second;
    if (a.shape.size() != b.shape.size() || a.shape.empty() || axis >= a.shape.size()) {
        return 0u;
    }
    for (std::size_t i = 0; i < a.shape.size(); ++i) {
        if (i != axis && a.shape[i] != b.shape[i]) {
            return 0u;
        }
    }

    if (axis != 0u) {
        return 0u;
    }

    dgtl::runtime::intrinsics_detail::Tensor out{};
    out.shape = a.shape;
    out.shape[0] += b.shape[0];
    out.data.reserve(a.data.size() + b.data.size());
    out.data.insert(out.data.end(), a.data.begin(), a.data.end());
    out.data.insert(out.data.end(), b.data.begin(), b.data.end());

    const uint64_t handle = dgtl::runtime::intrinsics_detail::next_tensor_handle().fetch_add(1, std::memory_order_relaxed);
    store.emplace(handle, std::move(out));
    return handle;
}

inline uint64_t __dgtl_rt_tensor_broadcast(uint64_t tensor_handle, uint64_t new_extent, uint64_t = 0) noexcept {
    if (new_extent == 0u) {
        return 0u;
    }
    std::lock_guard<std::mutex> lock(dgtl::runtime::intrinsics_detail::tensor_mutex());
    auto& store = dgtl::runtime::intrinsics_detail::tensor_store();
    const auto it = store.find(tensor_handle);
    if (it == store.end() || it->second.data.empty()) {
        return 0u;
    }

    dgtl::runtime::intrinsics_detail::Tensor out{};
    out.shape = {new_extent};
    out.data.resize(static_cast<std::size_t>(new_extent));
    for (std::size_t i = 0; i < out.data.size(); ++i) {
        out.data[i] = it->second.data[i % it->second.data.size()];
    }

    const uint64_t handle = dgtl::runtime::intrinsics_detail::next_tensor_handle().fetch_add(1, std::memory_order_relaxed);
    store.emplace(handle, std::move(out));
    return handle;
}

inline uint64_t __dgtl_rt_tensor_reduce(uint64_t tensor_handle, uint64_t op, uint64_t axis) noexcept {
    std::lock_guard<std::mutex> lock(dgtl::runtime::intrinsics_detail::tensor_mutex());
    auto& store = dgtl::runtime::intrinsics_detail::tensor_store();
    const auto it = store.find(tensor_handle);
    if (it == store.end() || it->second.shape.empty()) {
        return 0u;
    }

    const auto& in = it->second;
    const std::size_t rank = in.shape.size();
    const std::size_t reduce_axis = static_cast<std::size_t>(axis % rank);
    const auto strides = dgtl::runtime::intrinsics_detail::shape_strides(in.shape);

    const uint64_t axis_dim = in.shape[reduce_axis];
    const uint64_t inner = (reduce_axis + 1 < rank) ? strides[reduce_axis] : 1u;
    const uint64_t outer = in.data.size() / (axis_dim * inner);

    dgtl::runtime::intrinsics_detail::Tensor out{};
    out.shape = in.shape;
    out.shape.erase(out.shape.begin() + static_cast<std::ptrdiff_t>(reduce_axis));
    if (out.shape.empty()) {
        out.shape.push_back(1u);
    }
    out.data.resize(static_cast<std::size_t>(outer * inner));

    for (uint64_t o = 0; o < outer; ++o) {
        for (uint64_t i = 0; i < inner; ++i) {
            double acc = 0.0;
            if (op == 1u) acc = -std::numeric_limits<double>::infinity();
            if (op == 2u) acc = std::numeric_limits<double>::infinity();
            for (uint64_t k = 0; k < axis_dim; ++k) {
                const std::size_t idx = static_cast<std::size_t>((o * axis_dim + k) * inner + i);
                const double v = in.data[idx];
                if (op == 1u) acc = std::max(acc, v);
                else if (op == 2u) acc = std::min(acc, v);
                else acc += v;
            }
            out.data[static_cast<std::size_t>(o * inner + i)] = acc;
        }
    }

    const uint64_t handle = dgtl::runtime::intrinsics_detail::next_tensor_handle().fetch_add(1, std::memory_order_relaxed);
    store.emplace(handle, std::move(out));
    return handle;
}

inline uint64_t __dgtl_rt_tensor_pool(uint64_t tensor_handle, uint64_t window, uint64_t stride) noexcept {
    if (window == 0u) return 0u;
    if (stride == 0u) stride = window;

    std::lock_guard<std::mutex> lock(dgtl::runtime::intrinsics_detail::tensor_mutex());
    auto& store = dgtl::runtime::intrinsics_detail::tensor_store();
    const auto it = store.find(tensor_handle);
    if (it == store.end() || it->second.data.size() < window) {
        return 0u;
    }

    const auto& in = it->second.data;
    const std::size_t out_n = 1u + (in.size() - static_cast<std::size_t>(window)) / static_cast<std::size_t>(stride);
    dgtl::runtime::intrinsics_detail::Tensor out{};
    out.shape = {static_cast<uint64_t>(out_n)};
    out.data.resize(out_n);

    for (std::size_t i = 0; i < out_n; ++i) {
        const std::size_t start = i * static_cast<std::size_t>(stride);
        double mx = -std::numeric_limits<double>::infinity();
        for (std::size_t k = 0; k < static_cast<std::size_t>(window); ++k) {
            mx = std::max(mx, in[start + k]);
        }
        out.data[i] = mx;
    }

    const uint64_t handle = dgtl::runtime::intrinsics_detail::next_tensor_handle().fetch_add(1, std::memory_order_relaxed);
    store.emplace(handle, std::move(out));
    return handle;
}

inline uint64_t __dgtl_rt_tensor_conv(uint64_t signal_handle, uint64_t kernel_handle, uint64_t stride) noexcept {
    if (stride == 0u) stride = 1u;
    std::lock_guard<std::mutex> lock(dgtl::runtime::intrinsics_detail::tensor_mutex());
    auto& store = dgtl::runtime::intrinsics_detail::tensor_store();
    const auto it_s = store.find(signal_handle);
    const auto it_k = store.find(kernel_handle);
    if (it_s == store.end() || it_k == store.end()) {
        return 0u;
    }

    const auto& s = it_s->second.data;
    const auto& k = it_k->second.data;
    if (s.size() < k.size() || k.empty()) {
        return 0u;
    }

    const std::size_t out_n = 1u + (s.size() - k.size()) / static_cast<std::size_t>(stride);
    dgtl::runtime::intrinsics_detail::Tensor out{};
    out.shape = {static_cast<uint64_t>(out_n)};
    out.data.resize(out_n, 0.0);

    for (std::size_t i = 0; i < out_n; ++i) {
        const std::size_t start = i * static_cast<std::size_t>(stride);
        double acc = 0.0;
        for (std::size_t j = 0; j < k.size(); ++j) {
            acc += s[start + j] * k[j];
        }
        out.data[i] = acc;
    }

    const uint64_t handle = dgtl::runtime::intrinsics_detail::next_tensor_handle().fetch_add(1, std::memory_order_relaxed);
    store.emplace(handle, std::move(out));
    return handle;
}

inline uint64_t __dgtl_rt_tensor_gemm(uint64_t a_handle, uint64_t b_handle, uint64_t flags) noexcept {
    std::lock_guard<std::mutex> lock(dgtl::runtime::intrinsics_detail::tensor_mutex());
    auto& store = dgtl::runtime::intrinsics_detail::tensor_store();
    const auto it_a = store.find(a_handle);
    const auto it_b = store.find(b_handle);
    if (it_a == store.end() || it_b == store.end()) {
        return 0u;
    }

    const auto& a = it_a->second;
    const auto& b = it_b->second;
    if (a.shape.size() != 2 || b.shape.size() != 2) {
        return 0u;
    }

    const bool trans_a = (flags & 1u) != 0u;
    const bool trans_b = (flags & 2u) != 0u;
    const std::size_t a_rows = static_cast<std::size_t>(trans_a ? a.shape[1] : a.shape[0]);
    const std::size_t a_cols = static_cast<std::size_t>(trans_a ? a.shape[0] : a.shape[1]);
    const std::size_t b_rows = static_cast<std::size_t>(trans_b ? b.shape[1] : b.shape[0]);
    const std::size_t b_cols = static_cast<std::size_t>(trans_b ? b.shape[0] : b.shape[1]);
    if (a_cols != b_rows) {
        return 0u;
    }

    auto idx_a = [&](std::size_t r, std::size_t c) {
        return trans_a ? (c * static_cast<std::size_t>(a.shape[1]) + r) : (r * static_cast<std::size_t>(a.shape[1]) + c);
    };
    auto idx_b = [&](std::size_t r, std::size_t c) {
        return trans_b ? (c * static_cast<std::size_t>(b.shape[1]) + r) : (r * static_cast<std::size_t>(b.shape[1]) + c);
    };

    dgtl::runtime::intrinsics_detail::Tensor out{};
    out.shape = {static_cast<uint64_t>(a_rows), static_cast<uint64_t>(b_cols)};
    out.data.assign(a_rows * b_cols, 0.0);
    for (std::size_t i = 0; i < a_rows; ++i) {
        for (std::size_t j = 0; j < b_cols; ++j) {
            double acc = 0.0;
            for (std::size_t k_idx = 0; k_idx < a_cols; ++k_idx) {
                acc += a.data[idx_a(i, k_idx)] * b.data[idx_b(k_idx, j)];
            }
            out.data[i * b_cols + j] = acc;
        }
    }

    const uint64_t handle = dgtl::runtime::intrinsics_detail::next_tensor_handle().fetch_add(1, std::memory_order_relaxed);
    store.emplace(handle, std::move(out));
    return handle;
}

inline uint64_t __dgtl_rt_tensor_softmax(uint64_t tensor_handle, uint64_t axis, uint64_t = 0) noexcept {
    std::lock_guard<std::mutex> lock(dgtl::runtime::intrinsics_detail::tensor_mutex());
    auto& store = dgtl::runtime::intrinsics_detail::tensor_store();
    const auto it = store.find(tensor_handle);
    if (it == store.end() || it->second.shape.empty()) {
        return 0u;
    }

    const auto& in = it->second;
    const std::size_t rank = in.shape.size();
    const std::size_t soft_axis = static_cast<std::size_t>(axis % rank);
    const auto strides = dgtl::runtime::intrinsics_detail::shape_strides(in.shape);
    const uint64_t axis_dim = in.shape[soft_axis];
    const uint64_t inner = (soft_axis + 1 < rank) ? strides[soft_axis] : 1u;
    const uint64_t outer = in.data.size() / (axis_dim * inner);

    dgtl::runtime::intrinsics_detail::Tensor out = in;
    for (uint64_t o = 0; o < outer; ++o) {
        for (uint64_t i = 0; i < inner; ++i) {
            double max_v = -std::numeric_limits<double>::infinity();
            for (uint64_t k = 0; k < axis_dim; ++k) {
                const std::size_t idx = static_cast<std::size_t>((o * axis_dim + k) * inner + i);
                max_v = std::max(max_v, in.data[idx]);
            }
            double denom = 0.0;
            for (uint64_t k = 0; k < axis_dim; ++k) {
                const std::size_t idx = static_cast<std::size_t>((o * axis_dim + k) * inner + i);
                const double e = std::exp(in.data[idx] - max_v);
                out.data[idx] = e;
                denom += e;
            }
            if (denom > 0.0) {
                for (uint64_t k = 0; k < axis_dim; ++k) {
                    const std::size_t idx = static_cast<std::size_t>((o * axis_dim + k) * inner + i);
                    out.data[idx] /= denom;
                }
            }
        }
    }

    const uint64_t handle = dgtl::runtime::intrinsics_detail::next_tensor_handle().fetch_add(1, std::memory_order_relaxed);
    store.emplace(handle, std::move(out));
    return handle;
}

inline uint64_t __dgtl_rt_tensor_transpose(uint64_t tensor_handle, uint64_t dim0, uint64_t dim1) noexcept {
    std::lock_guard<std::mutex> lock(dgtl::runtime::intrinsics_detail::tensor_mutex());
    auto& store = dgtl::runtime::intrinsics_detail::tensor_store();
    const auto it = store.find(tensor_handle);
    if (it == store.end()) {
        return 0u;
    }

    const auto& in = it->second;
    if (dim0 >= in.shape.size() || dim1 >= in.shape.size() || dim0 == dim1) {
        return 0u;
    }

    dgtl::runtime::intrinsics_detail::Tensor out{};
    out.shape = in.shape;
    std::swap(out.shape[static_cast<std::size_t>(dim0)], out.shape[static_cast<std::size_t>(dim1)]);
    out.data.assign(in.data.size(), 0.0);

    const auto in_strides = dgtl::runtime::intrinsics_detail::shape_strides(in.shape);
    const auto out_strides = dgtl::runtime::intrinsics_detail::shape_strides(out.shape);
    std::vector<uint64_t> coord(in.shape.size(), 0u);

    for (std::size_t idx = 0; idx < in.data.size(); ++idx) {
        std::size_t rem = idx;
        for (std::size_t i = 0; i < in.shape.size(); ++i) {
            coord[i] = static_cast<uint64_t>(rem / static_cast<std::size_t>(in_strides[i]));
            rem %= static_cast<std::size_t>(in_strides[i]);
        }
        std::swap(coord[static_cast<std::size_t>(dim0)], coord[static_cast<std::size_t>(dim1)]);
        std::size_t out_idx = 0;
        for (std::size_t i = 0; i < coord.size(); ++i) {
            out_idx += static_cast<std::size_t>(coord[i] * out_strides[i]);
        }
        out.data[out_idx] = in.data[idx];
        std::swap(coord[static_cast<std::size_t>(dim0)], coord[static_cast<std::size_t>(dim1)]);
    }

    const uint64_t handle = dgtl::runtime::intrinsics_detail::next_tensor_handle().fetch_add(1, std::memory_order_relaxed);
    store.emplace(handle, std::move(out));
    return handle;
}

inline uint64_t __dgtl_rt_thread_spawn(uint64_t fn_ptr, uint64_t arg0, uint64_t arg1) noexcept {
    using Fn = uint64_t (*)(uint64_t, uint64_t, uint64_t);
    if (fn_ptr == 0u) return 0u;
    const Fn fn = reinterpret_cast<Fn>(fn_ptr);
    const uint64_t token = dgtl::runtime::intrinsics_detail::next_thread_token().fetch_add(1, std::memory_order_relaxed);
    std::thread worker([fn, arg0, arg1]() {
        (void)fn(arg0, arg1, 0u);
    });
    std::lock_guard<std::mutex> lock(dgtl::runtime::intrinsics_detail::thread_tokens_mutex());
    dgtl::runtime::intrinsics_detail::thread_tokens().emplace(token, std::move(worker));
    auto it = dgtl::runtime::intrinsics_detail::thread_tokens().find(token);
    if (it != dgtl::runtime::intrinsics_detail::thread_tokens().end() && it->second.joinable()) {
        it->second.detach();
    }
    dgtl::runtime::intrinsics_detail::thread_tokens().erase(token);
    return token;
}

inline uint64_t __dgtl_rt_tree_build(uint64_t tree_handle, uint64_t value, uint64_t = 0) noexcept {
    std::lock_guard<std::mutex> lock(dgtl::runtime::intrinsics_detail::tree_mutex());
    auto& store = dgtl::runtime::intrinsics_detail::tree_store();

    if (tree_handle == 0u) {
        dgtl::runtime::intrinsics_detail::Tree tree{};
        tree.nodes.push_back({value, -1, -1});
        tree.root = 0;
        const uint64_t handle = dgtl::runtime::intrinsics_detail::next_tree_handle().fetch_add(1, std::memory_order_relaxed);
        store.emplace(handle, std::move(tree));
        return handle;
    }

    const auto it = store.find(tree_handle);
    if (it == store.end()) {
        return 0u;
    }
    auto& tree = it->second;
    if (tree.root < 0) {
        tree.nodes.push_back({value, -1, -1});
        tree.root = 0;
        return tree_handle;
    }

    int64_t cur = tree.root;
    while (true) {
        auto& n = tree.nodes[static_cast<std::size_t>(cur)];
        if (value < n.value) {
            if (n.left < 0) {
                n.left = static_cast<int64_t>(tree.nodes.size());
                tree.nodes.push_back({value, -1, -1});
                break;
            }
            cur = n.left;
        } else {
            if (n.right < 0) {
                n.right = static_cast<int64_t>(tree.nodes.size());
                tree.nodes.push_back({value, -1, -1});
                break;
            }
            cur = n.right;
        }
    }

    return tree_handle;
}

inline uint64_t __dgtl_rt_tree_traverse(uint64_t tree_handle, uint64_t order, uint64_t = 0) noexcept {
    std::lock_guard<std::mutex> lock(dgtl::runtime::intrinsics_detail::tree_mutex());
    auto& store = dgtl::runtime::intrinsics_detail::tree_store();
    const auto it = store.find(tree_handle);
    if (it == store.end() || it->second.root < 0) {
        return 0u;
    }

    const auto& tree = it->second;
    uint64_t hash = 1469598103934665603ull;
    uint64_t count = 0u;

    std::function<void(int64_t)> walk = [&](int64_t idx) {
        if (idx < 0) return;
        const auto& n = tree.nodes[static_cast<std::size_t>(idx)];
        if (order == 1u) {
            hash ^= n.value + 0x9E3779B97F4A7C15ull + (hash << 6u) + (hash >> 2u);
            ++count;
        }
        walk(n.left);
        if (order == 0u || order > 2u) {
            hash ^= n.value + 0x9E3779B97F4A7C15ull + (hash << 6u) + (hash >> 2u);
            ++count;
        }
        walk(n.right);
        if (order == 2u) {
            hash ^= n.value + 0x9E3779B97F4A7C15ull + (hash << 6u) + (hash >> 2u);
            ++count;
        }
    };

    walk(tree.root);
    return (hash & 0xFFFFFFFF00000000ull) | (count & 0xFFFFFFFFull);
}

inline uint64_t __dgtl_rt_trie_build(uint64_t = 0, uint64_t = 0, uint64_t = 0) noexcept {
    dgtl::runtime::intrinsics_detail::Trie trie{};
    trie.nodes.emplace_back();
    std::lock_guard<std::mutex> lock(dgtl::runtime::intrinsics_detail::trie_mutex());
    const uint64_t handle = dgtl::runtime::intrinsics_detail::next_trie_handle().fetch_add(1, std::memory_order_relaxed);
    dgtl::runtime::intrinsics_detail::trie_store().emplace(handle, std::move(trie));
    return handle;
}

inline uint64_t __dgtl_rt_trie_insert(uint64_t trie_handle, uint64_t key_raw, uint64_t value) noexcept {
    const char* key = dgtl::runtime::intrinsics_detail::resolve_string_arg(key_raw);
    if (key == nullptr) {
        return 0u;
    }

    std::lock_guard<std::mutex> lock(dgtl::runtime::intrinsics_detail::trie_mutex());
    auto& store = dgtl::runtime::intrinsics_detail::trie_store();
    const auto it = store.find(trie_handle);
    if (it == store.end()) {
        return 0u;
    }

    auto& trie = it->second;
    uint64_t node = 0u;
    for (const unsigned char ch : std::string_view(key)) {
        auto it_next = trie.nodes[static_cast<std::size_t>(node)].next.find(ch);
        if (it_next == trie.nodes[static_cast<std::size_t>(node)].next.end()) {
            const uint64_t new_idx = static_cast<uint64_t>(trie.nodes.size());
            trie.nodes.emplace_back();
            trie.nodes[static_cast<std::size_t>(node)].next.emplace(ch, new_idx);
            node = new_idx;
        } else {
            node = it_next->second;
        }
    }

    auto& terminal = trie.nodes[static_cast<std::size_t>(node)];
    terminal.has_value = true;
    terminal.value = value;
    return 1u;
}

inline uint64_t __dgtl_rt_trie_lookup(uint64_t trie_handle, uint64_t key_raw, uint64_t fallback) noexcept {
    const char* key = dgtl::runtime::intrinsics_detail::resolve_string_arg(key_raw);
    if (key == nullptr) {
        return fallback;
    }

    std::lock_guard<std::mutex> lock(dgtl::runtime::intrinsics_detail::trie_mutex());
    auto& store = dgtl::runtime::intrinsics_detail::trie_store();
    const auto it = store.find(trie_handle);
    if (it == store.end()) {
        return fallback;
    }

    const auto& trie = it->second;
    uint64_t node = 0u;
    for (const unsigned char ch : std::string_view(key)) {
        const auto it_next = trie.nodes[static_cast<std::size_t>(node)].next.find(ch);
        if (it_next == trie.nodes[static_cast<std::size_t>(node)].next.end()) {
            return fallback;
        }
        node = it_next->second;
    }

    const auto& terminal = trie.nodes[static_cast<std::size_t>(node)];
    return terminal.has_value ? terminal.value : fallback;
}

inline uint64_t __dgtl_rt_tuple_pack(uint64_t a, uint64_t b, uint64_t c) noexcept {
    const uint64_t handle = dgtl::runtime::intrinsics_detail::next_tuple_handle().fetch_add(1, std::memory_order_relaxed);
    std::lock_guard<std::mutex> lock(dgtl::runtime::intrinsics_detail::tuple_mutex());
    dgtl::runtime::intrinsics_detail::tuple_store()[handle] = {a, b, c};
    return handle;
}

inline uint64_t __dgtl_rt_tuple_unpack(uint64_t tuple_handle, uint64_t index, uint64_t fallback) noexcept {
    std::lock_guard<std::mutex> lock(dgtl::runtime::intrinsics_detail::tuple_mutex());
    const auto& store = dgtl::runtime::intrinsics_detail::tuple_store();
    const auto it = store.find(tuple_handle);
    if (it == store.end() || index > 2u) return fallback;
    return it->second[static_cast<std::size_t>(index)];
}

inline uint64_t __dgtl_rt_vec_load(uint64_t data_ptr, uint64_t count, uint64_t = 0) noexcept {
    if (data_ptr == 0u || count == 0u) return 0u;
    const double* p = reinterpret_cast<const double*>(data_ptr);
    std::vector<double> vec(p, p + static_cast<std::ptrdiff_t>(count));
    const uint64_t handle = dgtl::runtime::intrinsics_detail::next_vector_handle().fetch_add(1, std::memory_order_relaxed);
    std::lock_guard<std::mutex> lock(dgtl::runtime::intrinsics_detail::vector_mutex());
    dgtl::runtime::intrinsics_detail::vector_store().emplace(handle, std::move(vec));
    return handle;
}

inline uint64_t __dgtl_rt_vec_reduce_add(uint64_t vec_handle, uint64_t op, uint64_t = 0) noexcept {
    std::lock_guard<std::mutex> lock(dgtl::runtime::intrinsics_detail::vector_mutex());
    const auto& store = dgtl::runtime::intrinsics_detail::vector_store();
    const auto it = store.find(vec_handle);
    if (it == store.end() || it->second.empty()) return 0u;
    const auto& v = it->second;
    double acc = 0.0;
    if (op == 1u) acc = -std::numeric_limits<double>::infinity();
    if (op == 2u) acc = std::numeric_limits<double>::infinity();
    for (double x : v) {
        if (op == 1u) acc = std::max(acc, x);
        else if (op == 2u) acc = std::min(acc, x);
        else acc += x;
    }
    uint64_t out = 0;
    std::memcpy(&out, &acc, sizeof(out));
    return out;
}

inline uint64_t __dgtl_rt_vec_shuffle(uint64_t vec_handle, uint64_t mode, uint64_t param) noexcept {
    std::lock_guard<std::mutex> lock(dgtl::runtime::intrinsics_detail::vector_mutex());
    auto& store = dgtl::runtime::intrinsics_detail::vector_store();
    const auto it = store.find(vec_handle);
    if (it == store.end()) return 0u;
    std::vector<double> out = it->second;
    if (out.empty()) return 0u;

    if (mode == 0u) {
        std::reverse(out.begin(), out.end());
    } else if (mode == 1u) {
        const std::size_t r = static_cast<std::size_t>(param % out.size());
        std::rotate(out.begin(), out.begin() + static_cast<std::ptrdiff_t>(r), out.end());
    } else {
        for (std::size_t i = 1; i < out.size(); i += 2) {
            std::swap(out[i - 1], out[i]);
        }
    }

    const uint64_t handle = dgtl::runtime::intrinsics_detail::next_vector_handle().fetch_add(1, std::memory_order_relaxed);
    store.emplace(handle, std::move(out));
    return handle;
}

inline uint64_t __dgtl_rt_vec_store(uint64_t vec_handle, uint64_t out_ptr, uint64_t max_count) noexcept {
    if (out_ptr == 0u || max_count == 0u) return 0u;
    std::lock_guard<std::mutex> lock(dgtl::runtime::intrinsics_detail::vector_mutex());
    const auto& store = dgtl::runtime::intrinsics_detail::vector_store();
    const auto it = store.find(vec_handle);
    if (it == store.end()) return 0u;
    double* out = reinterpret_cast<double*>(out_ptr);
    const std::size_t n = std::min<std::size_t>(static_cast<std::size_t>(max_count), it->second.size());
    std::memcpy(out, it->second.data(), n * sizeof(double));
    return static_cast<uint64_t>(n);
}

inline uint64_t __dgtl_rt_select(uint64_t cond, uint64_t when_true, uint64_t when_false) noexcept {
    return cond ? when_true : when_false;
}

inline uint64_t __dgtl_rt_gep(uint64_t base_ptr, uint64_t elem_size, uint64_t index) noexcept {
    if (base_ptr == 0u || elem_size == 0u) return 0u;
    return base_ptr + (elem_size * index);
}

inline uint64_t __dgtl_rt_ptr_to_int(uint64_t ptr, uint64_t = 0, uint64_t = 0) noexcept {
    return ptr;
}

inline uint64_t __dgtl_rt_int_to_ptr(uint64_t value, uint64_t = 0, uint64_t = 0) noexcept {
    return value;
}

inline uint64_t __dgtl_rt_prefetch_t0(uint64_t addr, uint64_t = 0, uint64_t = 0) noexcept {
#if defined(__GNUC__) || defined(__clang__)
    if (addr != 0u) {
        __builtin_prefetch(reinterpret_cast<const void*>(addr), 0, 3);
    }
#endif
    return 0u;
}

inline uint64_t __dgtl_rt_clflush(uint64_t = 0, uint64_t = 0, uint64_t = 0) noexcept {
    return 0u;
}

inline uint64_t __dgtl_rt_read_tsc(uint64_t = 0, uint64_t = 0, uint64_t = 0) noexcept {
    using clock = std::chrono::steady_clock;
    const auto now = clock::now().time_since_epoch();
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(now).count());
}

inline uint64_t __dgtl_rt_spin_acquire(uint64_t lock_id, uint64_t max_spins = 0, uint64_t = 0) noexcept {
    auto& table = dgtl::runtime::intrinsics_detail::lock_table();
    std::atomic_flag& flag = table[static_cast<std::size_t>(lock_id & 0xFFu)];
    const uint64_t spins = (max_spins == 0u) ? 1024u : max_spins;
    for (uint64_t i = 0; i < spins; ++i) {
        if (!flag.test_and_set(std::memory_order_acquire)) {
            return 1u;
        }
    }
    return 0u;
}

inline uint64_t __dgtl_rt_ring_push_free(uint64_t ring_id, uint64_t value, uint64_t capacity) noexcept {
    if (capacity == 0u) return 0u;
    std::lock_guard<std::mutex> lock(dgtl::runtime::intrinsics_detail::channel_mutex());
    auto& q = dgtl::runtime::intrinsics_detail::channel_queues()[ring_id];
    if (q.size() >= static_cast<std::size_t>(capacity)) return 0u;
    q.push_back(value);
    dgtl::runtime::intrinsics_detail::channel_cv().notify_one();
    return 1u;
}

inline uint64_t __dgtl_rt_wasm_invoke(uint64_t fn_ptr, uint64_t arg0, uint64_t arg1) noexcept {
    using Fn = uint64_t (*)(uint64_t, uint64_t, uint64_t);
    if (fn_ptr == 0u) return 0u;
    const Fn fn = reinterpret_cast<Fn>(fn_ptr);
    return fn(arg0, arg1, 0u);
}

inline uint64_t __dgtl_rt_fiber_yield_loop(uint64_t iter, uint64_t mask, uint64_t = 0) noexcept {
    if ((iter & mask) == 0u) {
        std::this_thread::yield();
    }
    return iter + 1u;
}

inline uint64_t __dgtl_rt_suspend_ctx(uint64_t ctx, uint64_t reason, uint64_t = 0) noexcept {
    return (ctx ^ (reason * 0x9E3779B97F4A7C15ull));
}

inline uint64_t __dgtl_rt_resume_ctx(uint64_t token, uint64_t = 0, uint64_t = 0) noexcept {
    return token;
}

inline uint64_t __dgtl_rt_neon_vadd_q(uint64_t vec_a, uint64_t vec_b, uint64_t = 0) noexcept {
    std::lock_guard<std::mutex> lock(dgtl::runtime::intrinsics_detail::vector_mutex());
    const auto& store = dgtl::runtime::intrinsics_detail::vector_store();
    const auto it_a = store.find(vec_a);
    const auto it_b = store.find(vec_b);
    if (it_a == store.end() || it_b == store.end()) return 0u;
    const std::size_t n = std::min(it_a->second.size(), it_b->second.size());
    if (n == 0u) return 0u;
    std::vector<double> out(n, 0.0);
    for (std::size_t i = 0; i < n; ++i) out[i] = it_a->second[i] + it_b->second[i];
    const uint64_t handle = dgtl::runtime::intrinsics_detail::next_vector_handle().fetch_add(1, std::memory_order_relaxed);
    dgtl::runtime::intrinsics_detail::vector_store().emplace(handle, std::move(out));
    return handle;
}

inline uint64_t __dgtl_rt_matmul_bf16(uint64_t a_handle, uint64_t b_handle, uint64_t flags) noexcept {
    return __dgtl_rt_tensor_gemm(a_handle, b_handle, flags);
}

inline uint64_t __dgtl_rt_matmul_fp8(uint64_t a_handle, uint64_t b_handle, uint64_t flags) noexcept {
    return __dgtl_rt_tensor_gemm(a_handle, b_handle, flags);
}

inline uint64_t __dgtl_rt_sdpa_f16(uint64_t q_handle, uint64_t k_handle, uint64_t v_handle) noexcept {
    const uint64_t qk = __dgtl_rt_tensor_gemm(q_handle, k_handle, 0u);
    if (qk == 0u) return 0u;
    return __dgtl_rt_tensor_gemm(qk, v_handle, 0u);
}

inline uint64_t __dgtl_rt_softmax_fast(uint64_t tensor_handle, uint64_t axis, uint64_t flags) noexcept {
    (void)flags;
    return __dgtl_rt_tensor_softmax(tensor_handle, axis, 0u);
}

inline uint64_t __dgtl_rt_layer_norm(uint64_t tensor_handle, uint64_t epsilon_bits, uint64_t = 0) noexcept {
    double eps = 1e-5;
    std::memcpy(&eps, &epsilon_bits, std::min(sizeof(eps), sizeof(epsilon_bits)));
    std::lock_guard<std::mutex> lock(dgtl::runtime::intrinsics_detail::tensor_mutex());
    auto& store = dgtl::runtime::intrinsics_detail::tensor_store();
    const auto it = store.find(tensor_handle);
    if (it == store.end() || it->second.data.empty()) return 0u;
    dgtl::runtime::intrinsics_detail::Tensor out = it->second;
    const double mean = std::accumulate(out.data.begin(), out.data.end(), 0.0) / static_cast<double>(out.data.size());
    double var = 0.0;
    for (double v : out.data) {
        const double d = v - mean;
        var += d * d;
    }
    var /= static_cast<double>(out.data.size());
    const double inv = 1.0 / std::sqrt(var + std::max(eps, 1e-12));
    std::transform(out.data.begin(), out.data.end(), out.data.begin(), [mean, inv](double v) noexcept {
        return (v - mean) * inv;
    });
    const uint64_t handle = dgtl::runtime::intrinsics_detail::next_tensor_handle().fetch_add(1, std::memory_order_relaxed);
    store.emplace(handle, std::move(out));
    return handle;
}

inline uint64_t __dgtl_rt_rope_embed(uint64_t tensor_handle, uint64_t rotate_by, uint64_t = 0) noexcept {
    std::lock_guard<std::mutex> lock(dgtl::runtime::intrinsics_detail::tensor_mutex());
    auto& store = dgtl::runtime::intrinsics_detail::tensor_store();
    const auto it = store.find(tensor_handle);
    if (it == store.end() || it->second.data.empty()) return 0u;
    dgtl::runtime::intrinsics_detail::Tensor out = it->second;
    const std::size_t shift = static_cast<std::size_t>(rotate_by % out.data.size());
    std::rotate(out.data.begin(), out.data.begin() + static_cast<std::ptrdiff_t>(shift), out.data.end());
    const uint64_t handle = dgtl::runtime::intrinsics_detail::next_tensor_handle().fetch_add(1, std::memory_order_relaxed);
    store.emplace(handle, std::move(out));
    return handle;
}

inline uint64_t __dgtl_rt_fast_inv_sqrt(uint64_t value_bits, uint64_t = 0, uint64_t = 0) noexcept {
    float x = 0.0f;
    uint32_t in = static_cast<uint32_t>(value_bits & 0xFFFFFFFFu);
    std::memcpy(&x, &in, sizeof(x));
    if (x <= 0.0f) return 0u;
    const float y = 1.0f / std::sqrt(x);
    uint32_t out = 0u;
    std::memcpy(&out, &y, sizeof(out));
    return static_cast<uint64_t>(out);
}

inline uint64_t __dgtl_rt_vec3_cross(uint64_t a_handle, uint64_t b_handle, uint64_t = 0) noexcept {
    std::lock_guard<std::mutex> lock(dgtl::runtime::intrinsics_detail::vector_mutex());
    auto& store = dgtl::runtime::intrinsics_detail::vector_store();
    const auto it_a = store.find(a_handle);
    const auto it_b = store.find(b_handle);
    if (it_a == store.end() || it_b == store.end()) return 0u;
    if (it_a->second.size() < 3u || it_b->second.size() < 3u) return 0u;
    std::vector<double> out(3u, 0.0);
    out[0] = it_a->second[1] * it_b->second[2] - it_a->second[2] * it_b->second[1];
    out[1] = it_a->second[2] * it_b->second[0] - it_a->second[0] * it_b->second[2];
    out[2] = it_a->second[0] * it_b->second[1] - it_a->second[1] * it_b->second[0];
    const uint64_t handle = dgtl::runtime::intrinsics_detail::next_vector_handle().fetch_add(1, std::memory_order_relaxed);
    store.emplace(handle, std::move(out));
    return handle;
}

inline uint64_t __dgtl_rt_vec4_dot(uint64_t a_handle, uint64_t b_handle, uint64_t = 0) noexcept {
    std::lock_guard<std::mutex> lock(dgtl::runtime::intrinsics_detail::vector_mutex());
    const auto& store = dgtl::runtime::intrinsics_detail::vector_store();
    const auto it_a = store.find(a_handle);
    const auto it_b = store.find(b_handle);
    if (it_a == store.end() || it_b == store.end()) return 0u;
    if (it_a->second.size() < 4u || it_b->second.size() < 4u) return 0u;
    double dot = 0.0;
    for (std::size_t i = 0; i < 4u; ++i) dot += it_a->second[i] * it_b->second[i];
    uint64_t out = 0u;
    std::memcpy(&out, &dot, sizeof(out));
    return out;
}

inline uint64_t __dgtl_rt_mat4_mul(uint64_t a_handle, uint64_t b_handle, uint64_t = 0) noexcept {
    std::lock_guard<std::mutex> lock(dgtl::runtime::intrinsics_detail::vector_mutex());
    auto& store = dgtl::runtime::intrinsics_detail::vector_store();
    const auto it_a = store.find(a_handle);
    const auto it_b = store.find(b_handle);
    if (it_a == store.end() || it_b == store.end()) return 0u;
    if (it_a->second.size() < 16u || it_b->second.size() < 16u) return 0u;
    std::vector<double> out(16u, 0.0);
    for (std::size_t r = 0; r < 4u; ++r) {
        for (std::size_t c = 0; c < 4u; ++c) {
            for (std::size_t k = 0; k < 4u; ++k) {
                out[r * 4u + c] += it_a->second[r * 4u + k] * it_b->second[k * 4u + c];
            }
        }
    }
    const uint64_t handle = dgtl::runtime::intrinsics_detail::next_vector_handle().fetch_add(1, std::memory_order_relaxed);
    store.emplace(handle, std::move(out));
    return handle;
}

inline uint64_t __dgtl_rt_quat_slerp(uint64_t a_handle, uint64_t b_handle, uint64_t t_bits) noexcept {
    double t = 0.0;
    std::memcpy(&t, &t_bits, std::min(sizeof(t), sizeof(t_bits)));
    t = std::clamp(t, 0.0, 1.0);
    std::lock_guard<std::mutex> lock(dgtl::runtime::intrinsics_detail::vector_mutex());
    auto& store = dgtl::runtime::intrinsics_detail::vector_store();
    const auto it_a = store.find(a_handle);
    const auto it_b = store.find(b_handle);
    if (it_a == store.end() || it_b == store.end()) return 0u;
    if (it_a->second.size() < 4u || it_b->second.size() < 4u) return 0u;
    std::vector<double> out(4u, 0.0);
    for (std::size_t i = 0; i < 4u; ++i) {
        out[i] = it_a->second[i] * (1.0 - t) + it_b->second[i] * t;
    }
    const uint64_t handle = dgtl::runtime::intrinsics_detail::next_vector_handle().fetch_add(1, std::memory_order_relaxed);
    store.emplace(handle, std::move(out));
    return handle;
}

inline uint64_t __dgtl_rt_ray_aabb_isect(uint64_t ray_ptr, uint64_t box_ptr, uint64_t = 0) noexcept {
    if (ray_ptr == 0u || box_ptr == 0u) return 0u;
    const double* ray = reinterpret_cast<const double*>(ray_ptr);
    const double* box = reinterpret_cast<const double*>(box_ptr);
    const double ox = ray[0], oy = ray[1], oz = ray[2];
    const double dx = ray[3], dy = ray[4], dz = ray[5];
    const double minx = box[0], miny = box[1], minz = box[2];
    const double maxx = box[3], maxy = box[4], maxz = box[5];
    double tmin = 0.0;
    double tmax = std::numeric_limits<double>::infinity();
    auto slab = [&](double o, double d, double mn, double mx) {
        if (std::abs(d) < 1e-12) {
            return (o >= mn && o <= mx);
        }
        double t0 = (mn - o) / d;
        double t1 = (mx - o) / d;
        if (t0 > t1) std::swap(t0, t1);
        tmin = std::max(tmin, t0);
        tmax = std::min(tmax, t1);
        return tmin <= tmax;
    };
    if (!slab(ox, dx, minx, maxx)) return 0u;
    if (!slab(oy, dy, miny, maxy)) return 0u;
    if (!slab(oz, dz, minz, maxz)) return 0u;
    return 1u;
}

} // extern "C"

export namespace dgtl::runtime {

} // namespace dgtl::runtime