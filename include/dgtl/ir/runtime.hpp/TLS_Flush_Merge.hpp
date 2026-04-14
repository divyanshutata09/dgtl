/*
 * DGTL-MLIR-X v1.0
 * STEP 6: TLS → IRStream FLUSH & MERGE LAYER
 * VERSION 3.3: FINAL STABLE (TLS-FIXED + ZERO-RED + V4-ALIGNED)
 */

#pragma once
#include <mutex>

#include "dgtl/core/Context.hpp"        // brings g_tls_buffer
#include "dgtl/ir/base/Global_IR.hpp"

namespace dgtl::runtime {

class TLSFlushMerge {
private:
    std::mutex merge_mutex;

public:
    TLSFlushMerge() = default;

    /**
     * @brief Flush PRIMARY buffer (normal execution)
     */
    void flush_primary(ir::IRStream& global_stream) noexcept {
        auto& tls = ::dgtl::core::g_tls_buffer;

        // 🔒 Safety: ensure valid buffers
        if (!tls.base_atom_ptr || !tls.base_core_ptr ||
            !tls.atom_ptr || !tls.core_ptr)
            return;

        const size_t used =
            static_cast<size_t>(tls.atom_ptr - tls.base_atom_ptr);

        if (used == 0) return;

        std::lock_guard<std::mutex> lock(merge_mutex);

        for (size_t i = 0; i < used; ++i) {
            global_stream.push_node(
                tls.base_atom_ptr[i],
                tls.base_core_ptr[i]
            );
        }

        // 🔁 Reset primary buffer
        tls.atom_ptr = tls.base_atom_ptr;
        tls.core_ptr = tls.base_core_ptr;
        tls.remaining_primary = ::dgtl::core::DGTLContext::PageSizeNodes;
    }

    /**
     * @brief Flush SHADOW buffer (after successful transaction)
     */
    void flush_shadow(ir::IRStream& global_stream) noexcept {
        auto& tls = ::dgtl::core::g_tls_buffer;

        // 🔒 Safety: ensure valid buffers
        if (!tls.base_shadow_atom_ptr || !tls.base_shadow_core_ptr ||
            !tls.shadow_atom_ptr || !tls.shadow_core_ptr)
            return;

        const size_t used =
            static_cast<size_t>(tls.shadow_atom_ptr - tls.base_shadow_atom_ptr);

        if (used == 0) return;

        std::lock_guard<std::mutex> lock(merge_mutex);

        for (size_t i = 0; i < used; ++i) {
            global_stream.push_node(
                tls.base_shadow_atom_ptr[i],
                tls.base_shadow_core_ptr[i]
            );
        }

        // 🔁 Reset shadow buffer
        tls.shadow_atom_ptr = tls.base_shadow_atom_ptr;
        tls.shadow_core_ptr = tls.base_shadow_core_ptr;
        tls.remaining_shadow = ::dgtl::core::DGTLContext::PageSizeNodes;
    }
};

} // namespace dgtl::runtime

