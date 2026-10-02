
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

#include <cstdint>
#include <cstddef>

export module dgtl.runtime.gpu;

export namespace dgtl::gpu {

// =========================================================================
// 1. HARDWARE-ALIGNED GPU DRAW INSTANCES (CACHE LINE OPTIMIZED)
// =========================================================================

// Visual render flags for QuadInstance
enum class QuadFlags : uint32_t {
    SolidColor       = 0x00,
    LinearGradient   = 0x01,
    RadialGradient   = 0x02,
    TextureSampled   = 0x04,
    BoxShadow        = 0x08,
    BorderOnly       = 0x10,
    ClipEnabled      = 0x20
};

// Dual cacheline 128-byte alignment: exactly 88 bytes payload + 40 bytes padding = 128 bytes
struct alignas(64) QuadInstance {
    float x, y, w, h;                     // 16 bytes (offset 0..16)
    float r, g, b, a;                     // 16 bytes (offset 16..32)
    float secondary_r, secondary_g;       // 8 bytes  (offset 32..40)
    float secondary_b, secondary_a;       // 8 bytes  (offset 40..48)
    float radius_tl, radius_tr;           // 8 bytes  (offset 48..56)
    float radius_br, radius_bl;           // 8 bytes  (offset 56..64)
    float border_width;                   // 4 bytes  (offset 64..68)
    float shadow_blur;                    // 4 bytes  (offset 68..72)
    float shadow_spread;                  // 4 bytes  (offset 72..76)
    uint32_t flags;                       // 4 bytes  (offset 76..80)
    uint32_t texture_slot;                // 4 bytes  (offset 80..84)
    uint32_t clip_rect_id;                // 4 bytes  (offset 84..88)
    float _padding[10];                   // 40 bytes (offset 88..128)
};
static_assert(sizeof(QuadInstance) == 128, "QuadInstance must be exactly 128 bytes (2x 64-byte cachelines)");

// Exact 64-byte alignment: 15 floats/uints (60 bytes) + 1 float padding (4 bytes) = 64 bytes
struct alignas(64) GlyphGPUInstance {
    float screen_x, screen_y;             // 8 bytes  (offset 0..8)
    float screen_w, screen_h;             // 8 bytes  (offset 8..16)
    float uv_x, uv_y;                     // 8 bytes  (offset 16..24)
    float uv_w, uv_h;                     // 8 bytes  (offset 24..32)
    float color_r, color_g;               // 8 bytes  (offset 32..40)
    float color_b, color_a;               // 8 bytes  (offset 40..48)
    float font_size;                      // 4 bytes  (offset 48..52)
    float px_range;                       // 4 bytes  (offset 52..56)
    uint32_t atlas_texture_slot;          // 4 bytes  (offset 56..60)
    uint32_t clip_rect_id;                // 4 bytes  (offset 60..64)
};
static_assert(sizeof(GlyphGPUInstance) == 64, "GlyphGPUInstance must be exactly 64 bytes");

// Exact 64-byte alignment: 13 floats/uints (52 bytes) + 3 floats padding (12 bytes) = 64 bytes
struct alignas(64) VectorPathInstance {
    float p0_x, p0_y;                     // 8 bytes  (offset 0..8)
    float p1_x, p1_y;                     // 8 bytes  (offset 8..16)
    float p2_x, p2_y;                     // 8 bytes  (offset 16..24)
    float stroke_width;                   // 4 bytes  (offset 24..28)
    float r, g, b, a;                     // 16 bytes (offset 28..44)
    uint32_t path_type;                   // 4 bytes  (offset 44..48)
    uint32_t clip_rect_id;                // 4 bytes  (offset 48..52)
    float _padding[3];                    // 12 bytes (offset 52..64)
};
static_assert(sizeof(VectorPathInstance) == 64, "VectorPathInstance must be exactly 64 bytes");

// Hardware Scissor Rectangle
struct alignas(16) ClipRect {
    float x, y, w, h;
};

// =========================================================================
// 2. INPUT, PLATFORM & IME COMPOSITION (DESKTOP & MOBILE COMPLETE)
// =========================================================================

enum class EventType : uint32_t {
    None           = 0x0000,
    MouseDown      = 0x0001,
    MouseUp        = 0x0002,
    MouseMove      = 0x0004,
    Click          = 0x0008,
    KeyDown        = 0x0010,
    KeyUp          = 0x0020,
    Scroll         = 0x0040,
    TouchStart     = 0x0080,
    TouchMove      = 0x0100,
    TouchEnd       = 0x0200,
    TouchCancel    = 0x0400,
    Focus          = 0x0800,
    Blur           = 0x1000,
    DpiChanged     = 0x2000,
    ImeComposition = 0x4000,
    ImeCommit      = 0x8000
};

// Ordered by alignment: 8-byte members first, followed by 4-byte and 1-byte fields (64 bytes total)
struct alignas(64) RawInputEvent {
    uint64_t timestamp_us;                // 8 bytes  (offset 0..8)
    uint32_t event_type;                  // 4 bytes  (offset 8..12)
    float pointer_x, pointer_y;           // 8 bytes  (offset 12..20)
    float delta_x, delta_y;               // 8 bytes  (offset 20..28)
    uint32_t key_code;                    // 4 bytes  (offset 28..32)
    uint32_t modifiers;                   // 4 bytes  (offset 32..36)
    uint32_t touch_id;                    // 4 bytes  (offset 36..40)
    float dpi_scale;                      // 4 bytes  (offset 40..44)
    uint32_t target_node;                 // 4 bytes  (offset 44..48)
    uint8_t is_handled;                   // 1 byte   (offset 48..49)
    char ime_text[15];                    // 15 bytes (offset 49..64)
};
static_assert(sizeof(RawInputEvent) == 64, "RawInputEvent must be exactly 64 bytes");

class GpuContext;

} // namespace dgtl::gpu

// =========================================================================
// 3. EXPORTED C-ABI PIPELINE & LIFECYCLE SYMBOLS (MYZON FFI LINKAGE)
// =========================================================================

extern "C" {
    // Pipeline Lifecycle & Configuration
    export void* dgtl_gpu_init_pipeline(uint32_t width, uint32_t height, float dpi_scale, const char* window_title);
    export void dgtl_gpu_resize(void* ctx_ptr, uint32_t width, uint32_t height, float dpi_scale);
    export void dgtl_gpu_destroy_pipeline(void* ctx_ptr);

    // Frame Lifecycle
    export void dgtl_gpu_begin_frame(void* ctx_ptr, float clear_r, float clear_g, float clear_b, float clear_a);
    export void dgtl_gpu_end_frame(void* ctx_ptr);

    // Hardware Clip / Scissor Operations
    export uint32_t dgtl_gpu_push_clip_rect(void* ctx_ptr, float x, float y, float w, float h);
    export void dgtl_gpu_pop_clip_rect(void* ctx_ptr);

    // Geometry & Texture Batch Submissions
    export void dgtl_gpu_render_quads(void* ctx_ptr, const dgtl::gpu::QuadInstance* instances, uint32_t count);
    export void dgtl_gpu_render_glyphs(void* ctx_ptr, uint32_t atlas_slot, const dgtl::gpu::GlyphGPUInstance* instances, uint32_t count);
    export void dgtl_gpu_render_vector_paths(void* ctx_ptr, const dgtl::gpu::VectorPathInstance* instances, uint32_t count);

    // Texture Asset Uploads
    export uint32_t dgtl_gpu_upload_texture(void* ctx_ptr, uint32_t width, uint32_t height, const uint8_t* rgba_data);
    export void dgtl_gpu_destroy_texture(void* ctx_ptr, uint32_t texture_slot);

    // Platform Input & Timer Queries
    export bool dgtl_platform_poll_event(void* ctx_ptr, dgtl::gpu::RawInputEvent* out_event);
    export uint64_t dgtl_platform_time_us(void);
    export void dgtl_platform_set_cursor(void* ctx_ptr, uint32_t cursor_type);
}