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

/*
 * DGTL (Deep General-purpose Transformation Layer) version 1.0.0
 * STEP 6: TLS ÃƒÂ¢Ã¢â‚¬Â Ã¢â‚¬â„¢ IRStream FLUSH & MERGE LAYER
 * VERSION 3.3: FINAL STABLE (TLS-FIXED + ZERO-RED + V4-ALIGNED)
 */

#pragma once
#include <algorithm>
#include <cstddef>
#include <mutex>

#include "dgtl/core/Context.hpp"        // brings g_tls_buffer
#include "dgtl/ir/Global_IR.hpp"

namespace dgtl::runtime {

class TLSFlushMerge {
private:
    std::mutex merge_mutex;

    [[nodiscard]] static size_t pending_primary_nodes(const ::dgtl::core::ThreadLocalBuffer& tls) noexcept {
        if (!tls.base_atom_ptr || !tls.base_core_ptr || !tls.atom_ptr || !tls.core_ptr) {
            return 0;
        }
        if (tls.atom_ptr < tls.base_atom_ptr || tls.core_ptr < tls.base_core_ptr) {
            return 0;
        }

        const size_t atom_used = static_cast<size_t>(tls.atom_ptr - tls.base_atom_ptr);
        const size_t core_used = static_cast<size_t>(tls.core_ptr - tls.base_core_ptr);
        return std::min(atom_used, core_used);
    }

    [[nodiscard]] static size_t pending_shadow_nodes(const ::dgtl::core::ThreadLocalBuffer& tls) noexcept {
        if (!tls.base_shadow_atom_ptr || !tls.base_shadow_core_ptr || !tls.shadow_atom_ptr || !tls.shadow_core_ptr) {
            return 0;
        }
        if (tls.shadow_atom_ptr < tls.base_shadow_atom_ptr || tls.shadow_core_ptr < tls.base_shadow_core_ptr) {
            return 0;
        }

        const size_t atom_used = static_cast<size_t>(tls.shadow_atom_ptr - tls.base_shadow_atom_ptr);
        const size_t core_used = static_cast<size_t>(tls.shadow_core_ptr - tls.base_shadow_core_ptr);
        return std::min(atom_used, core_used);
    }

    static void reset_primary(::dgtl::core::ThreadLocalBuffer& tls) noexcept {
        tls.atom_ptr = tls.base_atom_ptr;
        tls.core_ptr = tls.base_core_ptr;
        tls.remaining_primary = ::dgtl::core::DGTLContext::PageSizeNodes;
    }

    static void reset_shadow(::dgtl::core::ThreadLocalBuffer& tls) noexcept {
        tls.shadow_atom_ptr = tls.base_shadow_atom_ptr;
        tls.shadow_core_ptr = tls.base_shadow_core_ptr;
        tls.remaining_shadow = ::dgtl::core::DGTLContext::PageSizeNodes;
    }

public:
    TLSFlushMerge() = default;
    TLSFlushMerge(const TLSFlushMerge&) = delete;
    TLSFlushMerge& operator=(const TLSFlushMerge&) = delete;

    /**
     * @brief Flush PRIMARY buffer (normal execution)
     */
    void flush_primary(ir::IRStream& global_stream) noexcept {
        auto& tls = ::dgtl::core::g_tls_buffer;

        // ÃƒÂ°Ã…Â¸Ã¢â‚¬ÂÃ¢â‚¬â„¢ Safety: ensure valid buffers
        if (!tls.base_atom_ptr || !tls.base_core_ptr ||
            !tls.atom_ptr || !tls.core_ptr) {
            return;
        }

        const size_t used = pending_primary_nodes(tls);

        if (used == 0) {
            reset_primary(tls);
            return;
        }

        std::lock_guard<std::mutex> lock(merge_mutex);

        for (size_t i = 0; i < used; ++i) {
            global_stream.push_node(
                tls.base_atom_ptr[i],
                tls.base_core_ptr[i]
            );
        }

        // ÃƒÂ°Ã…Â¸Ã¢â‚¬ÂÃ‚Â Reset primary buffer
        reset_primary(tls);
    }

    /**
     * @brief Flush SHADOW buffer (after successful transaction)
     */
    void flush_shadow(ir::IRStream& global_stream) noexcept {
        auto& tls = ::dgtl::core::g_tls_buffer;

        // ÃƒÂ°Ã…Â¸Ã¢â‚¬ÂÃ¢â‚¬â„¢ Safety: ensure valid buffers
        if (!tls.base_shadow_atom_ptr || !tls.base_shadow_core_ptr ||
            !tls.shadow_atom_ptr || !tls.shadow_core_ptr) {
            return;
        }

        const size_t used = pending_shadow_nodes(tls);

        if (used == 0) {
            reset_shadow(tls);
            return;
        }

        std::lock_guard<std::mutex> lock(merge_mutex);

        for (size_t i = 0; i < used; ++i) {
            global_stream.push_node(
                tls.base_shadow_atom_ptr[i],
                tls.base_shadow_core_ptr[i]
            );
        }

        // ÃƒÂ°Ã…Â¸Ã¢â‚¬ÂÃ‚Â Reset shadow buffer
        reset_shadow(tls);
    }
};

} // namespace dgtl::runtime


