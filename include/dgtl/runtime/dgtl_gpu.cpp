
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

#include <iostream>
#include <vector>
#include <chrono>
#include <cstring>
#include <cstdint>
#include <string>
#include <algorithm>
#include <cmath>
#include <memory>

#if defined(_WIN32)
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#endif

module dgtl.runtime.gpu;

namespace dgtl::gpu {

struct TextureSlot {
    uint32_t id{0};
    uint32_t width{0};
    uint32_t height{0};
    std::vector<uint8_t> pixel_data;
    bool is_active{false};
};

struct FrameStatistics {
    uint64_t frame_index{0};
    uint64_t total_quads_rendered{0};
    uint64_t total_glyphs_rendered{0};
    uint64_t total_paths_rendered{0};
    uint64_t last_frame_time_us{0};
};

// ============================================================================
// HARDWARE SOFTWARE FALLBACK RASTER ENGINE
// ============================================================================

class SoftwareFramebuffer {
public:
    uint32_t width{0};
    uint32_t height{0};
    std::vector<uint32_t> pixels; // 32-bit RGBA (0xAABBGGRR)

    void resize(uint32_t w, uint32_t h) {
        width = w;
        height = h;
        pixels.resize(static_cast<size_t>(w) * h, 0x00000000);
    }

    void clear(float r, float g, float b, float a) noexcept {
        uint32_t ur = static_cast<uint32_t>(std::clamp(r, 0.0f, 1.0f) * 255.0f);
        uint32_t ug = static_cast<uint32_t>(std::clamp(g, 0.0f, 1.0f) * 255.0f);
        uint32_t ub = static_cast<uint32_t>(std::clamp(b, 0.0f, 1.0f) * 255.0f);
        uint32_t ua = static_cast<uint32_t>(std::clamp(a, 0.0f, 1.0f) * 255.0f);
        uint32_t c = (ua << 24) | (ub << 16) | (ug << 8) | ur;
        std::fill(pixels.begin(), pixels.end(), c);
    }

    inline void blend_pixel(uint32_t x, uint32_t y, float r, float g, float b, float a, const ClipRect& clip) noexcept {
        if (x < static_cast<uint32_t>(clip.x) || x >= static_cast<uint32_t>(clip.x + clip.w) ||
            y < static_cast<uint32_t>(clip.y) || y >= static_cast<uint32_t>(clip.y + clip.h) ||
            x >= width || y >= height || a <= 0.001f) {
            return;
        }

        size_t idx = static_cast<size_t>(y) * width + x;
        uint32_t dst = pixels[idx];
        constexpr float inv255 = 1.0f / 255.0f;
        float da = static_cast<float>((dst >> 24) & 0xFF) * inv255;
        float db = static_cast<float>((dst >> 16) & 0xFF) * inv255;
        float dg = static_cast<float>((dst >> 8) & 0xFF) * inv255;
        float dr = static_cast<float>(dst & 0xFF) * inv255;

        float out_a = a + da * (1.0f - a);
        if (out_a <= 0.0001f) return;

        float inv_out_a = 1.0f / out_a;
        float out_r = (r * a + dr * da * (1.0f - a)) * inv_out_a;
        float out_g = (g * a + dg * da * (1.0f - a)) * inv_out_a;
        float out_b = (b * a + db * da * (1.0f - a)) * inv_out_a;

        uint32_t ur = static_cast<uint32_t>(std::clamp(out_r, 0.0f, 1.0f) * 255.0f);
        uint32_t ug = static_cast<uint32_t>(std::clamp(out_g, 0.0f, 1.0f) * 255.0f);
        uint32_t ub = static_cast<uint32_t>(std::clamp(out_b, 0.0f, 1.0f) * 255.0f);
        uint32_t ua = static_cast<uint32_t>(std::clamp(out_a, 0.0f, 1.0f) * 255.0f);

        pixels[idx] = (ua << 24) | (ub << 16) | (ug << 8) | ur;
    }
};

// ============================================================================
// GPU CONTEXT PIPELINE
// ============================================================================

class GpuContext {
public:
    uint32_t width{0};
    uint32_t height{0};
    float dpi_scale{1.0f};
    std::string window_title;

    // Staging buffers
    std::vector<QuadInstance> quad_staging_buffer;
    std::vector<GlyphGPUInstance> glyph_staging_buffer;
    std::vector<VectorPathInstance> vector_staging_buffer;
    std::vector<ClipRect> clip_stack;

    // Texture slots
    std::vector<TextureSlot> active_textures;

    // Input events
    std::vector<RawInputEvent> pending_events;

    // Software frame rasterizer
    SoftwareFramebuffer framebuffer;

    // Telemetry & metrics
    FrameStatistics stats{};
    std::chrono::high_resolution_clock::time_point frame_start_time;

    // Clear target state
    float clear_color[4]{0.0f, 0.0f, 0.0f, 1.0f};

    GpuContext(uint32_t w, uint32_t h, float dpi, const char* title) 
        : width(w), height(h), dpi_scale(dpi > 0.0f ? dpi : 1.0f), 
          window_title(title ? title : "DGTL App") {
        quad_staging_buffer.reserve(16384);
        glyph_staging_buffer.reserve(32768);
        vector_staging_buffer.reserve(8192);
        clip_stack.reserve(64);
        active_textures.reserve(64);
        pending_events.reserve(512);

        framebuffer.resize(width, height);
        frame_start_time = std::chrono::high_resolution_clock::now();
    }

    void resize(uint32_t w, uint32_t h, float dpi) noexcept {
        width = w;
        height = h;
        dpi_scale = (dpi > 0.0f) ? dpi : 1.0f;
        framebuffer.resize(w, h);
    }

    void begin_frame(float r, float g, float b, float a) noexcept {
        frame_start_time = std::chrono::high_resolution_clock::now();
        clear_color[0] = std::clamp(r, 0.0f, 1.0f);
        clear_color[1] = std::clamp(g, 0.0f, 1.0f);
        clear_color[2] = std::clamp(b, 0.0f, 1.0f);
        clear_color[3] = std::clamp(a, 0.0f, 1.0f);

        quad_staging_buffer.clear();
        glyph_staging_buffer.clear();
        vector_staging_buffer.clear();
        clip_stack.clear();

        // Default viewport root scissor
        clip_stack.push_back(ClipRect{0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height)});
        framebuffer.clear(clear_color[0], clear_color[1], clear_color[2], clear_color[3]);
    }

    uint32_t push_clip(float x, float y, float w, float h) noexcept {
        ClipRect active_clip = clip_stack.empty() 
            ? ClipRect{0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height)} 
            : clip_stack.back();

        float nx = std::max(active_clip.x, x);
        float ny = std::max(active_clip.y, y);
        float nr = std::min(active_clip.x + active_clip.w, x + w);
        float nb = std::min(active_clip.y + active_clip.h, y + h);

        ClipRect intersected{
            nx,
            ny,
            std::max(0.0f, nr - nx),
            std::max(0.0f, nb - ny)
        };

        clip_stack.push_back(intersected);
        return static_cast<uint32_t>(clip_stack.size() - 1);
    }

    void pop_clip() noexcept {
        if (clip_stack.size() > 1) {
            clip_stack.pop_back();
        }
    }

    void push_quads(const QuadInstance* instances, uint32_t count) noexcept {
        if (!instances || count == 0) return;
        quad_staging_buffer.insert(quad_staging_buffer.end(), instances, instances + count);
    }

    void push_glyphs(uint32_t atlas_slot, const GlyphGPUInstance* instances, uint32_t count) noexcept {
        if (!instances || count == 0) return;
        size_t prev_size = glyph_staging_buffer.size();
        glyph_staging_buffer.insert(glyph_staging_buffer.end(), instances, instances + count);
        for (size_t i = prev_size; i < glyph_staging_buffer.size(); ++i) {
            glyph_staging_buffer[i].atlas_texture_slot = atlas_slot;
        }
    }

    void push_vector_paths(const VectorPathInstance* instances, uint32_t count) noexcept {
        if (!instances || count == 0) return;
        vector_staging_buffer.insert(vector_staging_buffer.end(), instances, instances + count);
    }

    uint32_t register_texture(uint32_t w, uint32_t h, const uint8_t* rgba) {
        if (w == 0 || h == 0) return 0xFFFFFFFFu;

        for (size_t i = 0; i < active_textures.size(); ++i) {
            if (!active_textures[i].is_active) {
                active_textures[i].width = w;
                active_textures[i].height = h;
                active_textures[i].is_active = true;
                if (rgba) {
                    size_t sz = static_cast<size_t>(w) * h * 4;
                    active_textures[i].pixel_data.assign(rgba, rgba + sz);
                } else {
                    active_textures[i].pixel_data.assign(static_cast<size_t>(w) * h * 4, 0);
                }
                return static_cast<uint32_t>(i);
            }
        }

        uint32_t slot_id = static_cast<uint32_t>(active_textures.size());
        TextureSlot tex;
        tex.id = slot_id;
        tex.width = w;
        tex.height = h;
        tex.is_active = true;
        if (rgba) {
            size_t sz = static_cast<size_t>(w) * h * 4;
            tex.pixel_data.assign(rgba, rgba + sz);
        } else {
            tex.pixel_data.assign(static_cast<size_t>(w) * h * 4, 0);
        }
        active_textures.push_back(std::move(tex));
        return slot_id;
    }

    void destroy_texture(uint32_t slot) noexcept {
        if (slot < active_textures.size()) {
            active_textures[slot].pixel_data.clear();
            active_textures[slot].is_active = false;
        }
    }

    // ========================================================================
    // RASTER PIPELINE EXECUTION
    // ========================================================================

    void execute_raster_pass_quads() noexcept {
        for (const auto& q : quad_staging_buffer) {
            ClipRect clip = (q.clip_rect_id < clip_stack.size()) 
                ? clip_stack[q.clip_rect_id] 
                : ClipRect{0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height)};

            int32_t min_x = std::max(0, static_cast<int32_t>(std::floor(q.x - q.shadow_blur - q.shadow_spread)));
            int32_t max_x = std::min(static_cast<int32_t>(width), static_cast<int32_t>(std::ceil(q.x + q.w + q.shadow_blur + q.shadow_spread)));
            int32_t min_y = std::max(0, static_cast<int32_t>(std::floor(q.y - q.shadow_blur - q.shadow_spread)));
            int32_t max_y = std::min(static_cast<int32_t>(height), static_cast<int32_t>(std::ceil(q.y + q.h + q.shadow_blur + q.shadow_spread)));

            for (int32_t py = min_y; py < max_y; ++py) {
                for (int32_t px = min_x; px < max_x; ++px) {
                    float fx = static_cast<float>(px) + 0.5f;
                    float fy = static_cast<float>(py) + 0.5f;

                    // Compute signed distance function to rounded quad
                    float cx = q.x + q.w * 0.5f;
                    float cy = q.y + q.h * 0.5f;
                    float dx = std::abs(fx - cx) - (q.w * 0.5f - q.radius_tl);
                    float dy = std::abs(fy - cy) - (q.h * 0.5f - q.radius_tl);
                    float dist = std::hypot(std::max(dx, 0.0f), std::max(dy, 0.0f)) + std::min(std::max(dx, dy), 0.0f) - q.radius_tl;

                    // Anti-aliased edge factor
                    float alpha_factor = std::clamp(0.5f - dist, 0.0f, 1.0f);

                    if (alpha_factor > 0.0f) {
                        float r = q.r, g = q.g, b = q.b, a = q.a * alpha_factor;

                        // Linear gradient evaluation
                        if (q.flags & static_cast<uint32_t>(QuadFlags::LinearGradient)) {
                            float t = std::clamp((fy - q.y) / std::max(q.h, 0.001f), 0.0f, 1.0f);
                            r = q.r * (1.0f - t) + q.secondary_r * t;
                            g = q.g * (1.0f - t) + q.secondary_g * t;
                            b = q.b * (1.0f - t) + q.secondary_b * t;
                            a = (q.a * (1.0f - t) + q.secondary_a * t) * alpha_factor;
                        }

                        // Texture sampler evaluation
                        if ((q.flags & static_cast<uint32_t>(QuadFlags::TextureSampled)) && q.texture_slot < active_textures.size()) {
                            const auto& tex = active_textures[q.texture_slot];
                            if (tex.is_active && tex.width > 0 && tex.height > 0) {
                                float u = std::clamp((fx - q.x) / q.w, 0.0f, 1.0f);
                                float v = std::clamp((fy - q.y) / q.h, 0.0f, 1.0f);
                                size_t tx = static_cast<size_t>(u * (tex.width - 1));
                                size_t ty = static_cast<size_t>(v * (tex.height - 1));
                                size_t tidx = (ty * tex.width + tx) * 4;
                                r = (tex.pixel_data[tidx + 0] / 255.0f) * q.r;
                                g = (tex.pixel_data[tidx + 1] / 255.0f) * q.g;
                                b = (tex.pixel_data[tidx + 2] / 255.0f) * q.b;
                                a = (tex.pixel_data[tidx + 3] / 255.0f) * q.a * alpha_factor;
                            }
                        }

                        framebuffer.blend_pixel(px, py, r, g, b, a, clip);
                    }
                }
            }
        }
    }

    void execute_raster_pass_glyphs() noexcept {
        for (const auto& g : glyph_staging_buffer) {
            ClipRect clip = (g.clip_rect_id < clip_stack.size()) 
                ? clip_stack[g.clip_rect_id] 
                : ClipRect{0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height)};

            int32_t min_x = std::max(0, static_cast<int32_t>(std::floor(g.screen_x)));
            int32_t max_x = std::min(static_cast<int32_t>(width), static_cast<int32_t>(std::ceil(g.screen_x + g.screen_w)));
            int32_t min_y = std::max(0, static_cast<int32_t>(std::floor(g.screen_y)));
            int32_t max_y = std::min(static_cast<int32_t>(height), static_cast<int32_t>(std::ceil(g.screen_y + g.screen_h)));

            bool has_atlas = g.atlas_texture_slot < active_textures.size() && active_textures[g.atlas_texture_slot].is_active;
            const TextureSlot* atlas = has_atlas ? &active_textures[g.atlas_texture_slot] : nullptr;

            for (int32_t py = min_y; py < max_y; ++py) {
                for (int32_t px = min_x; px < max_x; ++px) {
                    float fx = static_cast<float>(px) + 0.5f;
                    float fy = static_cast<float>(py) + 0.5f;

                    float glyph_alpha = 1.0f;
                    if (atlas && atlas->width > 0 && atlas->height > 0) {
                        float u = g.uv_x + ((fx - g.screen_x) / g.screen_w) * g.uv_w;
                        float v = g.uv_y + ((fy - g.screen_y) / g.screen_h) * g.uv_h;
                        size_t tx = static_cast<size_t>(std::clamp(u, 0.0f, 1.0f) * (atlas->width - 1));
                        size_t ty = static_cast<size_t>(std::clamp(v, 0.0f, 1.0f) * (atlas->height - 1));
                        size_t tidx = (ty * atlas->width + tx) * 4;

                        // Multi-channel Signed Distance Field (MSDF) median evaluation
                        float r = atlas->pixel_data[tidx + 0] / 255.0f;
                        float g_val = atlas->pixel_data[tidx + 1] / 255.0f;
                        float b = atlas->pixel_data[tidx + 2] / 255.0f;
                        float sd = std::max(std::min(r, g_val), std::min(std::max(r, g_val), b)) - 0.5f;
                        glyph_alpha = std::clamp(sd * g.px_range + 0.5f, 0.0f, 1.0f);
                    }

                    if (glyph_alpha > 0.001f) {
                        framebuffer.blend_pixel(px, py, g.color_r, g.color_g, g.color_b, g.color_a * glyph_alpha, clip);
                    }
                }
            }
        }
    }

    void execute_raster_pass_paths() noexcept {
        for (const auto& path : vector_staging_buffer) {
            ClipRect clip = (path.clip_rect_id < clip_stack.size()) 
                ? clip_stack[path.clip_rect_id] 
                : ClipRect{0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height)};

            float min_px = std::min({path.p0_x, path.p1_x, path.p2_x}) - path.stroke_width;
            float max_px = std::max({path.p0_x, path.p1_x, path.p2_x}) + path.stroke_width;
            float min_py = std::min({path.p0_y, path.p1_y, path.p2_y}) - path.stroke_width;
            float max_py = std::max({path.p0_y, path.p1_y, path.p2_y}) + path.stroke_width;

            int32_t min_x = std::max(0, static_cast<int32_t>(std::floor(min_px)));
            int32_t max_x = std::min(static_cast<int32_t>(width), static_cast<int32_t>(std::ceil(max_px)));
            int32_t min_y = std::max(0, static_cast<int32_t>(std::floor(min_py)));
            int32_t max_y = std::min(static_cast<int32_t>(height), static_cast<int32_t>(std::ceil(max_py)));

            for (int32_t py = min_y; py < max_y; ++py) {
                for (int32_t px = min_x; px < max_x; ++px) {
                    float fx = static_cast<float>(px) + 0.5f;
                    float fy = static_cast<float>(py) + 0.5f;

                    // Distance to quadratic Bezier curve segment via iterative projection
                    float min_d_sq = 1e9f;
                    for (int step = 0; step <= 16; ++step) {
                        float t = step / 16.0f;
                        float it = 1.0f - t;
                        float qx = it * it * path.p0_x + 2.0f * it * t * path.p1_x + t * t * path.p2_x;
                        float qy = it * it * path.p0_y + 2.0f * it * t * path.p1_y + t * t * path.p2_y;
                        float d_sq = (fx - qx) * (fx - qx) + (fy - qy) * (fy - qy);
                        min_d_sq = std::min(min_d_sq, d_sq);
                    }

                    float dist = std::sqrt(min_d_sq) - path.stroke_width * 0.5f;
                    float alpha_factor = std::clamp(0.5f - dist, 0.0f, 1.0f);

                    if (alpha_factor > 0.0f) {
                        framebuffer.blend_pixel(px, py, path.r, path.g, path.b, path.a * alpha_factor, clip);
                    }
                }
            }
        }
    }

    void end_frame() noexcept {
        // Execute draw passes sequentially
        execute_raster_pass_quads();
        execute_raster_pass_glyphs();
        execute_raster_pass_paths();

        // Calculate frame timing and statistics
        auto now = std::chrono::high_resolution_clock::now();
        stats.last_frame_time_us = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(now - frame_start_time).count()
        );
        stats.frame_index++;
        stats.total_quads_rendered += quad_staging_buffer.size();
        stats.total_glyphs_rendered += glyph_staging_buffer.size();
        stats.total_paths_rendered += vector_staging_buffer.size();
    }

    bool poll_event(RawInputEvent* out_event) noexcept {
#if defined(_WIN32)
        // Pump platform OS message queue
        MSG msg;
        while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);

            RawInputEvent ev{};
            ev.timestamp_us = static_cast<uint64_t>(msg.time) * 1000;
            ev.dpi_scale = dpi_scale;

            switch (msg.message) {
                case WM_MOUSEMOVE:
                    ev.event_type = static_cast<uint32_t>(EventType::MouseMove);
                    ev.pointer_x = static_cast<float>(LOWORD(msg.lParam));
                    ev.pointer_y = static_cast<float>(HIWORD(msg.lParam));
                    pending_events.push_back(ev);
                    break;
                case WM_LBUTTONDOWN:
                    ev.event_type = static_cast<uint32_t>(EventType::MouseDown);
                    ev.pointer_x = static_cast<float>(LOWORD(msg.lParam));
                    ev.pointer_y = static_cast<float>(HIWORD(msg.lParam));
                    pending_events.push_back(ev);
                    break;
                case WM_LBUTTONUP:
                    ev.event_type = static_cast<uint32_t>(EventType::MouseUp);
                    ev.pointer_x = static_cast<float>(LOWORD(msg.lParam));
                    ev.pointer_y = static_cast<float>(HIWORD(msg.lParam));
                    pending_events.push_back(ev);
                    break;
                case WM_KEYDOWN:
                    ev.event_type = static_cast<uint32_t>(EventType::KeyDown);
                    ev.key_code = static_cast<uint32_t>(msg.wParam);
                    pending_events.push_back(ev);
                    break;
                case WM_KEYUP:
                    ev.event_type = static_cast<uint32_t>(EventType::KeyUp);
                    ev.key_code = static_cast<uint32_t>(msg.wParam);
                    pending_events.push_back(ev);
                    break;
                default:
                    break;
            }
        }
#endif
        if (!pending_events.empty() && out_event) {
            *out_event = pending_events.front();
            pending_events.erase(pending_events.begin());
            return true;
        }
        return false;
    }

    void set_cursor(uint32_t cursor_type) noexcept {
#if defined(_WIN32)
        LPCTSTR cursor_id = IDC_ARROW;
        switch (cursor_type) {
            case 1: cursor_id = IDC_IBEAM; break;
            case 2: cursor_id = IDC_HAND; break;
            case 3: cursor_id = IDC_SIZEALL; break;
            case 4: cursor_id = IDC_SIZENS; break;
            case 5: cursor_id = IDC_SIZEWE; break;
            case 6: cursor_id = IDC_NO; break;
            default: cursor_id = IDC_ARROW; break;
        }
        HCURSOR hCursor = LoadCursor(nullptr, cursor_id);
        if (hCursor) {
            SetCursor(hCursor);
        }
#else
        (void)cursor_type;
#endif
    }
};

} // namespace dgtl::gpu

// =========================================================================
// EXPORTED C-ABI FFI IMPLEMENTATIONS (MYZON SOVEREIGN INTERFACE)
// =========================================================================

extern "C" {

void* dgtl_gpu_init_pipeline(uint32_t width, uint32_t height, float dpi_scale, const char* window_title) {
    try {
        auto* ctx = new dgtl::gpu::GpuContext(width, height, dpi_scale, window_title);
        return static_cast<void*>(ctx);
    } catch (...) {
        return nullptr;
    }
}

void dgtl_gpu_resize(void* ctx_ptr, uint32_t width, uint32_t height, float dpi_scale) {
    if (!ctx_ptr) return;
    static_cast<dgtl::gpu::GpuContext*>(ctx_ptr)->resize(width, height, dpi_scale);
}

void dgtl_gpu_destroy_pipeline(void* ctx_ptr) {
    if (ctx_ptr) {
        delete static_cast<dgtl::gpu::GpuContext*>(ctx_ptr);
    }
}

void dgtl_gpu_begin_frame(void* ctx_ptr, float clear_r, float clear_g, float clear_b, float clear_a) {
    if (!ctx_ptr) return;
    static_cast<dgtl::gpu::GpuContext*>(ctx_ptr)->begin_frame(clear_r, clear_g, clear_b, clear_a);
}

void dgtl_gpu_end_frame(void* ctx_ptr) {
    if (!ctx_ptr) return;
    static_cast<dgtl::gpu::GpuContext*>(ctx_ptr)->end_frame();
}

uint32_t dgtl_gpu_push_clip_rect(void* ctx_ptr, float x, float y, float w, float h) {
    if (!ctx_ptr) return 0;
    return static_cast<dgtl::gpu::GpuContext*>(ctx_ptr)->push_clip(x, y, w, h);
}

void dgtl_gpu_pop_clip_rect(void* ctx_ptr) {
    if (!ctx_ptr) return;
    static_cast<dgtl::gpu::GpuContext*>(ctx_ptr)->pop_clip();
}

void dgtl_gpu_render_quad_instances(void* ctx_ptr, const dgtl::gpu::QuadInstance* instances, uint32_t count) {
    if (!ctx_ptr || !instances || count == 0) return;
    static_cast<dgtl::gpu::GpuContext*>(ctx_ptr)->push_quads(instances, count);
}

void dgtl_gpu_render_glyph_instances(void* ctx_ptr, uint32_t atlas_slot, const dgtl::gpu::GlyphGPUInstance* instances, uint32_t count) {
    if (!ctx_ptr || !instances || count == 0) return;
    static_cast<dgtl::gpu::GpuContext*>(ctx_ptr)->push_glyphs(atlas_slot, instances, count);
}

void dgtl_gpu_render_vector_paths(void* ctx_ptr, const dgtl::gpu::VectorPathInstance* instances, uint32_t count) {
    if (!ctx_ptr || !instances || count == 0) return;
    static_cast<dgtl::gpu::GpuContext*>(ctx_ptr)->push_vector_paths(instances, count);
}

uint32_t dgtl_gpu_upload_texture(void* ctx_ptr, uint32_t width, uint32_t height, const uint8_t* rgba_data) {
    if (!ctx_ptr || width == 0 || height == 0) return 0xFFFFFFFFu;
    try {
        return static_cast<dgtl::gpu::GpuContext*>(ctx_ptr)->register_texture(width, height, rgba_data);
    } catch (...) {
        return 0xFFFFFFFFu;
    }
}

void dgtl_gpu_destroy_texture(void* ctx_ptr, uint32_t texture_slot) {
    if (!ctx_ptr) return;
    static_cast<dgtl::gpu::GpuContext*>(ctx_ptr)->destroy_texture(texture_slot);
}

bool dgtl_platform_poll_events(void* ctx_ptr, dgtl::gpu::RawInputEvent* out_event) {
    if (!ctx_ptr || !out_event) return false;
    return static_cast<dgtl::gpu::GpuContext*>(ctx_ptr)->poll_event(out_event);
}

uint64_t dgtl_platform_get_time_us() {
    auto now = std::chrono::high_resolution_clock::now();
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(now.time_since_epoch()).count());
}

void dgtl_platform_set_cursor(void* ctx_ptr, uint32_t cursor_type) {
    if (!ctx_ptr) return;
    static_cast<dgtl::gpu::GpuContext*>(ctx_ptr)->set_cursor(cursor_type);
}

}