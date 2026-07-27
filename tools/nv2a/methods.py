#!/usr/bin/env python3
"""NV2A PGRAPH method-name tables shared by the tools/nv2a decoders.

Names follow the KELVIN (NV20/NV097) numbering used by nouveau/xemu and by the
NV097_* defines in src/cxbx/src/win32/CxbxKrnl/Emu.cpp. Single methods live in
SINGLE_METHODS; register arrays (combiner stages, texture stages, vertex
arrays, ...) live in ARRAY_METHODS as (base, count, stride, name) and resolve
to "NAME[index]" (texture stages resolve to "NAME[stage]+field").
"""

from __future__ import annotations

CLASS_NAMES: dict[int, str] = {
    0x12: "BETA1",
    0x19: "CLIP",
    0x39: "M2MF",
    0x43: "ROP",
    0x44: "PATTERN",
    0x62: "SURFACES_2D",
    0x72: "BETA4",
    0x97: "KELVIN",
    0x9F: "IMAGE_BLIT",
}

BEGIN_OP_NAMES: dict[int, str] = {
    0x00: "END",
    0x01: "POINTS",
    0x02: "LINES",
    0x03: "LINE_LOOP",
    0x04: "LINE_STRIP",
    0x05: "TRIANGLES",
    0x06: "TRIANGLE_STRIP",
    0x07: "TRIANGLE_FAN",
    0x08: "QUADS",
    0x09: "QUAD_STRIP",
    0x0A: "POLYGON",
}

# Single KELVIN methods (method offset -> name).
SINGLE_METHODS: dict[int, str] = {
    0x0000: "SET_OBJECT",
    0x0100: "NO_OPERATION",
    0x0104: "NOTIFY",
    0x0110: "WAIT_FOR_IDLE",
    0x0120: "SET_FLIP_READ",
    0x0124: "SET_FLIP_WRITE",
    0x0128: "SET_FLIP_MODULO",
    0x012C: "FLIP_INCREMENT_WRITE",
    0x0130: "FLIP_STALL",
    0x0180: "SET_CONTEXT_DMA_NOTIFIES",
    0x0184: "SET_CONTEXT_DMA_A",
    0x0188: "SET_CONTEXT_DMA_B",
    0x0190: "SET_CONTEXT_DMA_STATE",
    0x0194: "SET_CONTEXT_DMA_COLOR",
    0x0198: "SET_CONTEXT_DMA_ZETA",
    0x019C: "SET_CONTEXT_DMA_VERTEX_A",
    0x01A0: "SET_CONTEXT_DMA_VERTEX_B",
    0x01A4: "SET_CONTEXT_DMA_SEMAPHORE",
    0x01A8: "SET_CONTEXT_DMA_REPORT",
    0x0200: "SET_SURFACE_CLIP_HORIZONTAL",
    0x0204: "SET_SURFACE_CLIP_VERTICAL",
    0x0208: "SET_SURFACE_FORMAT",
    0x020C: "SET_SURFACE_PITCH",
    0x0210: "SET_SURFACE_COLOR_OFFSET",
    0x0214: "SET_SURFACE_ZETA_OFFSET",
    0x0288: "SET_COMBINER_SPECULAR_FOG_CW0",
    0x028C: "SET_COMBINER_SPECULAR_FOG_CW1",
    0x02A8: "SET_FOG_COLOR",
    0x0300: "SET_ALPHA_TEST_ENABLE",
    0x0304: "SET_BLEND_ENABLE",
    0x0308: "SET_CULL_FACE_ENABLE",
    0x030C: "SET_DEPTH_TEST_ENABLE",
    0x0310: "SET_DITHER_ENABLE",
    0x0314: "SET_LIGHTING_ENABLE",
    0x0318: "SET_POINT_PARAMS_ENABLE",
    0x031C: "SET_POINT_SMOOTH_ENABLE",
    0x0320: "SET_LINE_SMOOTH_ENABLE",
    0x0324: "SET_POLY_SMOOTH_ENABLE",
    0x0328: "SET_SKIN_MODE",
    0x032C: "SET_STENCIL_TEST_ENABLE",
    0x0330: "SET_POLY_OFFSET_POINT_ENABLE",
    0x0334: "SET_POLY_OFFSET_LINE_ENABLE",
    0x0338: "SET_POLY_OFFSET_FILL_ENABLE",
    0x033C: "SET_ALPHA_FUNC",
    0x0340: "SET_ALPHA_REF",
    0x0344: "SET_BLEND_FUNC_SFACTOR",
    0x0348: "SET_BLEND_FUNC_DFACTOR",
    0x034C: "SET_BLEND_COLOR",
    0x0350: "SET_BLEND_EQUATION",
    0x0354: "SET_DEPTH_FUNC",
    0x0358: "SET_COLOR_MASK",
    0x035C: "SET_DEPTH_MASK",
    0x0360: "SET_STENCIL_MASK",
    0x0364: "SET_STENCIL_FUNC",
    0x0368: "SET_STENCIL_FUNC_REF",
    0x036C: "SET_STENCIL_FUNC_MASK",
    0x0370: "SET_STENCIL_OP_FAIL",
    0x0374: "SET_STENCIL_OP_ZFAIL",
    0x0378: "SET_STENCIL_OP_ZPASS",
    0x037C: "SET_SHADE_MODE",
    0x0380: "SET_LINE_WIDTH",
    0x0384: "SET_POLYGON_OFFSET_SCALE_FACTOR",
    0x0388: "SET_POLYGON_OFFSET_BIAS",
    0x038C: "SET_FRONT_POLYGON_MODE",
    0x0390: "SET_BACK_POLYGON_MODE",
    0x03A0: "SET_CLIP_MIN",
    0x03A4: "SET_CLIP_MAX",
    0x03B0: "SET_CULL_FACE",
    0x03B4: "SET_FRONT_FACE",
    0x03B8: "SET_NORMALIZATION_ENABLE",
    0x09F8: "SET_SWATH_WIDTH",
    0x09FC: "SET_FLAT_SHADE_OP",
    0x0A1C: "SET_SHADER_CLIP_PLANE_MODE",
    0x0A28: "SET_EYE_POSITION",
    0x1D60: "SET_SHADER_OTHER_STAGE_INPUT",
    0x1D64: "SET_TRANSFORM_DATA",
    0x1D6C: "SET_SEMAPHORE_OFFSET",
    0x1D70: "BACK_END_WRITE_SEMAPHORE_RELEASE",
    0x1D78: "SET_DEPTH_CLAMP_CONTROL",
    0x1D7C: "SET_ANTI_ALIASING_CONTROL",
    0x1D80: "SET_SURFACE_COMPRESS_ZBUFFER_EN",
    0x1D84: "SET_ZSTENCIL_CLEAR_VALUE_LEGACY",
    0x1D8C: "SET_ZSTENCIL_CLEAR_VALUE",
    0x1D90: "SET_COLOR_CLEAR_VALUE",
    0x1D94: "CLEAR_SURFACE",
    0x1D98: "SET_CLEAR_RECT_HORIZONTAL",
    0x1D9C: "SET_CLEAR_RECT_VERTICAL",
    0x1DA0: "SET_CLIP_RECT_HORIZONTAL",
    0x1DA4: "SET_CLIP_RECT_VERTICAL",
    0x1E60: "SET_COMBINER_CONTROL",
    0x1E68: "SET_SHADOW_ZSLOPE_THRESHOLD",
    0x1E6C: "SET_SHADOW_DEPTH_FUNC",
    0x1E70: "SET_SHADER_STAGE_PROGRAM",
    0x1E74: "SET_DOT_RGBMAPPING",
    0x1E78: "SET_SHADER_OTHER_STAGE_INPUT_2",
    0x1E7C: "SET_TRANSFORM_TIMEOUT",
    0x1E94: "SET_TRANSFORM_EXECUTION_MODE",
    0x1E98: "SET_TRANSFORM_PROGRAM_CXT_WRITE_EN",
    0x1E9C: "SET_TRANSFORM_PROGRAM_LOAD",
    0x1EA0: "SET_TRANSFORM_PROGRAM_START",
    0x1EA4: "SET_TRANSFORM_CONSTANT_LOAD",
    0x17FC: "SET_BEGIN_END",
    0x1800: "ARRAY_ELEMENT16",
    0x1808: "ARRAY_ELEMENT32",
    0x1810: "DRAW_ARRAYS",
    0x1818: "INLINE_ARRAY",
    0x181C: "SET_VERTEX_DATA_BASE_OFFSET",
    0x1820: "SET_INDEX_BASE_OFFSET",
}

# Register arrays: (base, count, stride, name). A method inside
# [base, base + count * stride) resolves to name[(method - base) // stride]
# with "+0xNN" appended for a nonzero offset within the element.
ARRAY_METHODS: list[tuple[int, int, int, str]] = [
    (0x0260, 8, 4, "SET_COMBINER_ALPHA_ICW"),
    (0x0294, 4, 4, "SET_EYE_VECTOR"),
    (0x0440, 16, 4, "SET_PROJECTION_MATRIX"),
    (0x0480, 32, 4, "SET_MODEL_VIEW_MATRIX"),
    (0x0580, 32, 4, "SET_INVERSE_MODEL_VIEW_MATRIX"),
    (0x0680, 16, 4, "SET_COMPOSITE_MATRIX"),
    (0x06C0, 16, 4, "SET_TEXTURE_MATRIX0"),
    (0x0700, 16, 4, "SET_TEXTURE_MATRIX1"),
    (0x0740, 16, 4, "SET_TEXTURE_MATRIX2"),
    (0x0780, 16, 4, "SET_TEXTURE_MATRIX3"),
    (0x0A20, 4, 4, "SET_VIEWPORT_OFFSET"),
    (0x0A60, 8, 4, "SET_COMBINER_FACTOR0"),
    (0x0A80, 8, 4, "SET_COMBINER_FACTOR1"),
    (0x0AA0, 8, 4, "SET_COMBINER_ALPHA_OCW"),
    (0x0AC0, 8, 4, "SET_COMBINER_COLOR_ICW"),
    (0x0AE0, 4, 4, "SET_COLOR_KEY_COLOR"),
    (0x0AF0, 4, 4, "SET_VIEWPORT_SCALE"),
    (0x0B00, 32, 4, "SET_TRANSFORM_PROGRAM"),
    (0x0B80, 32, 4, "SET_TRANSFORM_CONSTANT"),
    (0x1500, 4, 4, "SET_VERTEX3F"),
    (0x1518, 4, 4, "SET_VERTEX4F"),
    (0x1720, 16, 4, "SET_VERTEX_DATA_ARRAY_OFFSET"),
    (0x1760, 16, 4, "SET_VERTEX_DATA_ARRAY_FORMAT"),
    (0x1880, 16, 8, "SET_VERTEX_DATA2F_M"),
    (0x1900, 16, 4, "SET_VERTEX_DATA2S"),
    (0x1940, 16, 4, "SET_VERTEX_DATA4UB"),
    (0x1980, 16, 8, "SET_VERTEX_DATA4S_M"),
    (0x1A00, 16, 16, "SET_VERTEX_DATA4F_M"),
    (0x1E20, 4, 4, "SET_BACK_SPECULAR_PARAMS"),
    (0x1E40, 8, 4, "SET_COMBINER_COLOR_OCW"),
    (0x1E80, 4, 4, "SET_TRANSFORM_BRANCH_BITS"),
]

# Texture stages: 4 stages of stride 0x40 starting at 0x1B00; per-stage fields.
TEXTURE_BASE = 0x1B00
TEXTURE_STAGE_COUNT = 4
TEXTURE_STAGE_STRIDE = 0x40
TEXTURE_FIELDS: dict[int, str] = {
    0x00: "SET_TEXTURE_OFFSET",
    0x04: "SET_TEXTURE_FORMAT",
    0x08: "SET_TEXTURE_ADDRESS",
    0x0C: "SET_TEXTURE_CONTROL0",
    0x10: "SET_TEXTURE_CONTROL1",
    0x14: "SET_TEXTURE_FILTER",
    0x1C: "SET_TEXTURE_IMAGE_RECT",
    0x20: "SET_TEXTURE_PALETTE",
    0x24: "SET_TEXTURE_BORDER_COLOR",
}


def kelvin_method_name(method: int) -> str:
    """Best-effort name for one KELVIN method offset ("0x1B44" -> stage name)."""
    if method in SINGLE_METHODS:
        return SINGLE_METHODS[method]

    texture_end = TEXTURE_BASE + TEXTURE_STAGE_COUNT * TEXTURE_STAGE_STRIDE
    if TEXTURE_BASE <= method < texture_end:
        stage = (method - TEXTURE_BASE) // TEXTURE_STAGE_STRIDE
        field = (method - TEXTURE_BASE) % TEXTURE_STAGE_STRIDE
        if field in TEXTURE_FIELDS:
            return f"{TEXTURE_FIELDS[field]}[{stage}]"
        return f"SET_TEXTURE_?[{stage}]+0x{field:02X}"

    for base, count, stride, name in ARRAY_METHODS:
        if base <= method < base + count * stride:
            index = (method - base) // stride
            rest = (method - base) % stride
            if rest == 0:
                return f"{name}[{index}]"
            return f"{name}[{index}]+0x{rest:X}"

    return f"0x{method:04X}"


def method_name(class_id: int, method: int) -> str:
    """Name a (class, method) pair; non-KELVIN classes get hex methods."""
    if class_id & 0xFF == 0x97:
        return kelvin_method_name(method)
    return f"0x{method:04X}"


def class_name(class_id: int) -> str:
    return CLASS_NAMES.get(class_id & 0xFF, f"0x{class_id:02X}")
