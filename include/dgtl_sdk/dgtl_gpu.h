/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * Project:     DGTL (Deep General-purpose Transformation Layer)
 * Owner:       Divyanshu Tata
 * Repository:  github.com/TataG-Systems/DGTL
 * Component:   Public GPU & Platform SDK Interface
 *
 * This software is the proprietary property of TataG Systems.
 * Unauthorized copying, modification, or distribution is strictly prohibited.
 */

#ifndef DGTL_GPU_H
#define DGTL_GPU_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ─── GPU QUAD RENDER FLAGS ─── */
typedef enum dgtl_quad_flags {
    DGTL_QUAD_SOLID_COLOR     = 0x00,
    DGTL_QUAD_LINEAR_GRADIENT = 0x01,
    DGTL_QUAD_RADIAL_GRADIENT = 0x02,
    DGTL_QUAD_TEXTURE_SAMPLED = 0x04,
    DGTL_QUAD_BOX_SHADOW      = 0x08,
    DGTL_QUAD_BORDER_ONLY     = 0x10,
    DGTL_QUAD_CLIP_ENABLED    = 0x20
} dgtl_quad_flags_t;

/* ─── PLATFORM INPUT EVENT TYPES ─── */
typedef enum dgtl_event_type {
    DGTL_EVENT_NONE            = 0x0000,
    DGTL_EVENT_MOUSE_DOWN      = 0x0001,
    DGTL_EVENT_MOUSE_UP        = 0x0002,
    DGTL_EVENT_MOUSE_MOVE      = 0x0004,
    DGTL_EVENT_CLICK           = 0x0008,
    DGTL_EVENT_KEY_DOWN        = 0x0010,
    DGTL_EVENT_KEY_UP          = 0x0020,
    DGTL_EVENT_SCROLL          = 0x0040,
    DGTL_EVENT_TOUCH_START     = 0x0080,
    DGTL_EVENT_TOUCH_MOVE      = 0x0100,
    DGTL_EVENT_TOUCH_END       = 0x0200,
    DGTL_EVENT_TOUCH_CANCEL    = 0x0400,
    DGTL_EVENT_FOCUS           = 0x0800,
    DGTL_EVENT_BLUR            = 0x1000,
    DGTL_EVENT_DPI_CHANGED     = 0x2000,
    DGTL_EVENT_IME_COMPOSITION = 0x4000,
    DGTL_EVENT_IME_COMMIT      = 0x8000
} dgtl_event_type_t;

/* ─── EXACT 128-BYTE DUAL CACHELINE QUAD INSTANCE ─── */
typedef struct dgtl_quad_instance {
    float x, y, w, h;
    float r, g, b, a;
    float secondary_r, secondary_g;
    float secondary_b, secondary_a;
    float radius_tl, radius_tr;
    float radius_br, radius_bl;
    float border_width;
    float shadow_blur;
    float shadow_spread;
    uint32_t flags;
    uint32_t texture_slot;
    uint32_t clip_rect_id;
    float _padding[10];
} dgtl_quad_instance_t;

/* ─── EXACT 64-BYTE SINGLE CACHELINE GLYPH GPU INSTANCE ─── */
typedef struct dgtl_glyph_gpu_instance {
    float screen_x, screen_y;
    float screen_w, screen_h;
    float uv_x, uv_y;
    float uv_w, uv_h;
    float color_r, color_g;
    float color_b, color_a;
    float font_size;
    float px_range;
    uint32_t atlas_texture_slot;
    uint32_t clip_rect_id;
} dgtl_glyph_gpu_instance_t;

/* ─── EXACT 64-BYTE SINGLE CACHELINE VECTOR PATH INSTANCE ─── */
typedef struct dgtl_vector_path_instance {
    float p0_x, p0_y;
    float p1_x, p1_y;
    float p2_x, p2_y;
    float stroke_width;
    float r, g, b, a;
    uint32_t path_type;
    uint32_t clip_rect_id;
    float _padding[3];
} dgtl_vector_path_instance_t;

/* ─── EXACT 64-BYTE SINGLE CACHELINE RAW INPUT EVENT ─── */
typedef struct dgtl_raw_input_event {
    uint64_t timestamp_us;
    uint32_t event_type;
    float pointer_x, pointer_y;
    float delta_x, delta_y;
    uint32_t key_code;
    uint32_t modifiers;
    uint32_t touch_id;
    float dpi_scale;
    uint32_t target_node;
    uint8_t is_handled;
    char ime_text[15];
} dgtl_raw_input_event_t;

/* ─── EXPORTED C-ABI PIPELINE & PLATFORM FUNCTIONS ─── */
void*    dgtl_gpu_init_pipeline(uint32_t width, uint32_t height, float dpi_scale, const char* window_title);
void     dgtl_gpu_resize(void* ctx_ptr, uint32_t width, uint32_t height, float dpi_scale);
void     dgtl_gpu_destroy_pipeline(void* ctx_ptr);

void     dgtl_gpu_begin_frame(void* ctx_ptr, float clear_r, float clear_g, float clear_b, float clear_a);
void     dgtl_gpu_end_frame(void* ctx_ptr);

uint32_t dgtl_gpu_push_clip_rect(void* ctx_ptr, float x, float y, float w, float h);
void     dgtl_gpu_pop_clip_rect(void* ctx_ptr);

void     dgtl_gpu_render_quad_instances(void* ctx_ptr, const dgtl_quad_instance_t* instances, uint32_t count);
void     dgtl_gpu_render_glyph_instances(void* ctx_ptr, uint32_t atlas_slot, const dgtl_glyph_gpu_instance_t* instances, uint32_t count);
void     dgtl_gpu_render_vector_paths(void* ctx_ptr, const dgtl_vector_path_instance_t* instances, uint32_t count);

uint32_t dgtl_gpu_upload_texture(void* ctx_ptr, uint32_t width, uint32_t height, const uint8_t* rgba_data);
void     dgtl_gpu_destroy_texture(void* ctx_ptr, uint32_t texture_slot);

bool     dgtl_platform_poll_events(void* ctx_ptr, dgtl_raw_input_event_t* out_event);
uint64_t dgtl_platform_get_time_us(void);
void     dgtl_platform_set_cursor(void* ctx_ptr, uint32_t cursor_type);

#ifdef __cplusplus
}
#endif

#endif /* DGTL_GPU_H */
